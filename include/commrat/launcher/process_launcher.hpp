#pragma once
/**
 * @file process_launcher.hpp
 * @brief Process-based CommRaT application launcher.
 *
 * Reads an AppDescription JSON, discovers module binaries from cmake-generated
 * *.module.json descriptor files, and spawns each module as a separate child
 * process with a generated ModuleConfig JSON.
 *
 * Descriptor files are auto-discovered from the same directory as the launcher
 * binary (dirname(argv[0])). cmake's commrat_module() generates them at build
 * time alongside each module binary.
 *
 * Usage:
 * @code
 * #include <commrat/launcher/process_launcher.hpp>
 *
 * int main(int argc, char** argv) {
 *     return commrat::ProcessLauncher::main(argc, argv);
 * }
 * @endcode
 *
 * CLI:
 *   ./my_launcher  app.json
 *   ./my_launcher  app.json  --duration-ms 5000   (exit after 5s, for CTest)
 */

#include <commrat/launcher/module_description.hpp>
#include <commrat/meta/descriptor.hpp>
#include <commrat/module/module_config.hpp>
#include <rfl/json.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

// POSIX process management
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace commrat {

namespace detail {
    inline std::atomic<bool> g_process_launcher_shutdown{false};
    inline void process_launcher_signal_handler(int) {
        g_process_launcher_shutdown.store(true);
    }
}

// ---------------------------------------------------------------------------
// ProcessLauncher
// ---------------------------------------------------------------------------

class ProcessLauncher {
public:
    // ------------------------------------------------------------------
    // Load
    // ------------------------------------------------------------------

    /**
     * Load AppDescription and discover module descriptors.
     *
     * @param app_desc_path   Path to the AppDescription JSON file.
     * @param descriptor_dirs Directories to scan for *.module.json files
     *                        and companion binaries.
     *                        Defaults to dirname(argv[0]) when using main().
     */
    void load(const std::string& app_desc_path,
              const std::vector<std::string>& descriptor_dirs) {
        app_desc_path_ = std::filesystem::absolute(app_desc_path).string();
        description_ = load_app_description(app_desc_path);
        // Seed with caller-supplied dirs, then extend with any dirs listed in
        // the AppDescription itself.
        for (const auto& dir : descriptor_dirs)
            scan_directory_for_descriptors(dir);
        if (description_.descriptor_dirs.has_value()) {
            for (const auto& dir : *description_.descriptor_dirs)
                scan_directory_for_descriptors(dir);
        }
    }

    // ------------------------------------------------------------------
    // Start / stop
    // ------------------------------------------------------------------

    /**
     * Spawn all module processes.
     *
     * Modules with no inputs are started first (producers), followed by
     * modules with inputs (consumers). A 200 ms stagger is inserted between
     * each spawn so that TiMS mailboxes are registered before consumers
     * attempt to subscribe.
     */
    void start() {
        launcher_pid_ = ::getpid();

        validate_routing();

        // Producers first, consumers second
        std::vector<const ModuleDescription*> ordered;
        for (const auto& m : description_.modules) {
            if (m.inputs.empty()) ordered.insert(ordered.begin(), &m);
            else ordered.push_back(&m);
        }

        for (const auto* mod : ordered) {
            spawn_module(*mod);
            ::usleep(200'000);  // 200 ms stagger
        }

        // Spawn companions (e.g. web dashboards) after all modules are up
        if (description_.companions.has_value()) {
            ::usleep(200'000);  // brief extra pause before non-RT companions
            for (const auto& comp : *description_.companions)
                spawn_companion(comp);
        }
    }

    /**
     * Send SIGTERM to all child processes and wait for them to exit.
     * Falls back to SIGKILL after 3 seconds. Cleans up temp config files.
     */
    void stop() {
        for (auto& child : children_) {
            if (child.pid > 0) ::kill(child.pid, SIGTERM);
        }

        // Wait up to 3 s
        for (int poll = 0; poll < 30; ++poll) {
            ::usleep(100'000);
            bool all_done = true;
            for (auto& child : children_) {
                if (child.pid <= 0) continue;
                int status{};
                if (::waitpid(child.pid, &status, WNOHANG) == child.pid)
                    child.pid = -1;
                else
                    all_done = false;
            }
            if (all_done) break;
        }

        // SIGKILL stragglers
        for (auto& child : children_) {
            if (child.pid > 0) {
                ::kill(child.pid, SIGKILL);
                ::waitpid(child.pid, nullptr, 0);
            }
        }

        cleanup_temp_files();
        children_.clear();
    }

    // ------------------------------------------------------------------
    // Convenience main()
    // ------------------------------------------------------------------

    /**
     * Drop-in main() for a process launcher binary.
     *
     * Descriptor files are auto-discovered from dirname(argv[0]).
     * Additional descriptor directories can be specified via --descriptor-dir.
     *
     * @param argc  Argument count.
     * @param argv  argv[1] = AppDescription JSON path.
     *              Optional: --duration-ms N       (exit after N ms, for CTest)
     *              Optional: --descriptor-dir DIR  (repeat for multiple dirs)
     */
    static int main(int argc, char** argv) {
        if (argc < 2) {
            std::cerr << "Usage: " << argv[0]
                      << " <app.json> [--duration-ms N] [--descriptor-dir DIR]...\n";
            return 1;
        }

        uint32_t duration_ms = 0;
        // Auto-discover descriptors from the directory containing this binary
        std::filesystem::path exe = std::filesystem::canonical(argv[0]);
        std::vector<std::string> descriptor_dirs{exe.parent_path().string()};

        for (int i = 2; i < argc; ++i) {
            std::string_view arg(argv[i]);
            if (arg == "--duration-ms" && i + 1 < argc)
                duration_ms = static_cast<uint32_t>(std::stoul(argv[++i]));
            else if (arg == "--descriptor-dir" && i + 1 < argc)
                descriptor_dirs.emplace_back(argv[++i]);
        }

        detail::g_process_launcher_shutdown.store(false);
        std::signal(SIGINT,  detail::process_launcher_signal_handler);
        std::signal(SIGTERM, detail::process_launcher_signal_handler);

        try {
            ProcessLauncher launcher;
            launcher.load(argv[1], descriptor_dirs);
            launcher.start();

            std::cout << "[Launcher] " << launcher.description_.app_name
                      << " running";
            if (duration_ms > 0)
                std::cout << " for " << duration_ms << " ms";
            std::cout << " (Ctrl+C to stop)\n";

            uint32_t elapsed_ms = 0;
            while (!detail::g_process_launcher_shutdown.load()) {
                ::usleep(100'000);
                elapsed_ms += 100;

                // Detect unexpected child exits
                for (auto& child : launcher.children_) {
                    if (child.pid <= 0) continue;
                    int status{};
                    if (::waitpid(child.pid, &status, WNOHANG) == child.pid) {
                        std::cerr << "[Launcher] Module '" << child.name
                                  << "' (pid " << child.pid
                                  << ") exited with status "
                                  << WEXITSTATUS(status) << "\n";
                        child.pid = -1;
                    }
                }

                if (duration_ms > 0 && elapsed_ms >= duration_ms) break;
            }

            std::cout << "[Launcher] Stopping...\n";
            launcher.stop();
            std::cout << "[Launcher] Done.\n";
            return 0;

        } catch (const std::exception& e) {
            std::cerr << "[Launcher] Error: " << e.what() << "\n";
            return 1;
        }
    }

private:
    struct ChildInfo { pid_t pid; std::string name; };

    AppDescription                              description_;
    std::string                                 app_desc_path_; // absolute path passed to companions
    std::unordered_map<std::string,
                       ModuleDescriptor>        descriptors_;   // module_class → descriptor
    std::vector<std::string>                    scanned_dirs_;  // for companion binary lookup
    std::vector<ChildInfo>                      children_;
    std::vector<std::string>                    temp_files_;
    pid_t                                       launcher_pid_{0};

    // ------------------------------------------------------------------
    // Process management
    // ------------------------------------------------------------------

    void spawn_module(const ModuleDescription& mod) {
        const ModuleDescriptor& desc = find_descriptor(mod.module_class);

        // Write temp ModuleConfig JSON for this instance
        std::string config_path = write_temp_config(mod);

        pid_t pid = ::fork();
        if (pid < 0)
            throw std::runtime_error("fork() failed for module '" + mod.name + "'");

        if (pid == 0) {
            // Child process
            const char* args[] = {desc.binary.c_str(), config_path.c_str(), nullptr};
            ::execv(desc.binary.c_str(), const_cast<char* const*>(args));
            // execv only returns on error
            std::cerr << "[Child] execv failed for: " << desc.binary << "\n";
            ::_exit(1);
        }

        std::cout << "[Launcher] Started '" << mod.name << "'"
                  << " (pid=" << pid << " binary="
                  << std::filesystem::path(desc.binary).filename().string() << ")\n";
        children_.push_back({pid, mod.name});
    }

    void spawn_companion(const CompanionDescription& comp) {
        // Resolve binary: absolute path if it contains '/', otherwise search
        // the scanned descriptor directories (same dirs as modules).
        std::string binary = resolve_companion_binary(comp.binary);
        if (binary.empty()) {
            std::cerr << "[Launcher] WARNING: companion binary '" << comp.binary
                      << "' not found — skipping\n";
            return;
        }

        // Build argv: binary [app_desc_path] + extra args
        // The AppDescription path is injected as the first positional argument
        // so companions can read addresses and config from the same JSON.
        std::vector<const char*> argv_vec;
        argv_vec.push_back(binary.c_str());
        if (!app_desc_path_.empty())
            argv_vec.push_back(app_desc_path_.c_str());
        for (const auto& a : comp.args) argv_vec.push_back(a.c_str());
        argv_vec.push_back(nullptr);

        pid_t pid = ::fork();
        if (pid < 0)
            throw std::runtime_error("fork() failed for companion '" + comp.name + "'");

        if (pid == 0) {
            ::execv(binary.c_str(), const_cast<char* const*>(argv_vec.data()));
            std::cerr << "[Child] execv failed for companion: " << binary << "\n";
            ::_exit(1);
        }

        std::cout << "[Launcher] Started companion '" << comp.name << "'"
                  << " (pid=" << pid << " binary="
                  << std::filesystem::path(binary).filename().string() << ")\n";
        children_.push_back({pid, comp.name});
    }

    // ------------------------------------------------------------------
    // ModuleDescription → temp ModuleConfig JSON
    // ------------------------------------------------------------------

    std::string write_temp_config(const ModuleDescription& mod) {
        ModuleConfig cfg = to_config(mod);
        std::string json = rfl::json::write(cfg);

        std::string path = "/tmp/commrat_" + std::to_string(::getpid())
                         + "_" + mod.name + ".json";
        std::ofstream f(path);
        if (!f)
            throw std::runtime_error("Cannot write temp config: " + path);
        f << json;
        temp_files_.push_back(path);
        return path;
    }

    static ModuleConfig to_config(const ModuleDescription& desc) {
        ModuleConfig cfg;
        cfg.name = desc.name;

        if (desc.period_ms.has_value())
            cfg.period = std::chrono::milliseconds(*desc.period_ms);

        // Outputs
        if (desc.outputs.empty()) {
            cfg.outputs = NoOutputConfig{};
        } else if (desc.outputs.size() == 1) {
            const auto& o = desc.outputs[0];
            cfg.outputs = SimpleOutputConfig{.system_id   = o.system_id,
                                             .instance_id = o.instance_id};
        } else {
            MultiOutputConfig multi;
            for (const auto& o : desc.outputs)
                multi.addresses.push_back({.system_id   = o.system_id,
                                           .instance_id = o.instance_id});
            cfg.outputs = multi;
        }

        // Build a flat source list: continuous inputs first, then synced.
        // Supports both old format (synced: bool in flat inputs[]) and new
        // format (separate synced_inputs[] array).
        struct Source { uint8_t sys; uint8_t inst; bool is_primary; };
        std::vector<Source> all_sources;
        if (!desc.synced_inputs.has_value() || desc.synced_inputs->empty()) {
            for (const auto& i : desc.inputs)
                all_sources.push_back({i.source_system_id, i.source_instance_id, !i.synced.value()});
        } else {
            for (const auto& i : desc.inputs)
                all_sources.push_back({i.source_system_id, i.source_instance_id, true});
            for (const auto& i : *desc.synced_inputs)
                all_sources.push_back({i.source_system_id, i.source_instance_id, false});
        }

        if (all_sources.empty()) {
            cfg.inputs = NoInputConfig{};
        } else if (all_sources.size() == 1 && all_sources[0].is_primary) {
            cfg.inputs = SingleInputConfig{.source_system_id   = all_sources[0].sys,
                                           .source_instance_id = all_sources[0].inst};
        } else {
            MultiInputConfig multi;
            for (const auto& s : all_sources)
                multi.sources.push_back({.system_id   = s.sys,
                                         .instance_id = s.inst,
                                         .is_primary  = s.is_primary});
            cfg.inputs = multi;
        }

        if (desc.params.has_value())
            cfg.params = *desc.params;

        return cfg;
    }

    void cleanup_temp_files() {
        for (const auto& p : temp_files_) ::unlink(p.c_str());
        temp_files_.clear();
    }

    // ------------------------------------------------------------------
    // Routing validation
    // ------------------------------------------------------------------

    // Find the module in app.json that owns a given output address.
    const ModuleDescription* find_module_by_output_addr(uint8_t sys, uint8_t inst) const {
        for (const auto& mod : description_.modules)
            for (const auto& out : mod.outputs)
                if (out.system_id == sys && out.instance_id == inst)
                    return &mod;
        return nullptr;
    }

    // Find the output index within a module that matches a given address.
    static size_t find_output_index(const ModuleDescription& mod, uint8_t sys, uint8_t inst) {
        for (size_t i = 0; i < mod.outputs.size(); ++i)
            if (mod.outputs[i].system_id == sys && mod.outputs[i].instance_id == inst)
                return i;
        return mod.outputs.size(); // not found
    }

    // Check one connection: expected type (from consumer descriptor) vs actual type (from producer descriptor).
    void check_connection(const std::string& consumer_name,
                          const std::string& role,    // "inputs" or "synced_inputs"
                          size_t idx,
                          const std::string& expected_type,
                          uint8_t src_sys, uint8_t src_inst) const {
        const auto* src_mod = find_module_by_output_addr(src_sys, src_inst);
        if (!src_mod) {
            std::cerr << "[Launcher] WARNING: " << consumer_name << "." << role
                      << "[" << idx << "] source address ("
                      << static_cast<int>(src_sys) << ":" << static_cast<int>(src_inst)
                      << ") not found in app.json\n";
            return;
        }
        const auto* src_desc = [&]() -> const ModuleDescriptor* {
            auto it = descriptors_.find(src_mod->module_class);
            return it != descriptors_.end() ? &it->second : nullptr;
        }();
        if (!src_desc || !src_desc->outputs) return; // no schema yet

        size_t out_idx = find_output_index(*src_mod, src_sys, src_inst);
        if (out_idx >= src_desc->outputs->size()) return;

        const std::string& actual_type = (*src_desc->outputs)[out_idx];
        if (actual_type != expected_type) {
            throw std::runtime_error(
                "[Launcher] Routing mismatch: " + consumer_name + "." + role +
                "[" + std::to_string(idx) + "] expects '" + expected_type +
                "' but source module '" + src_mod->name +
                "' (" + std::to_string(src_sys) + ":" + std::to_string(src_inst) +
                ") outputs '" + actual_type + "'");
        }
    }

    void validate_routing() const {
        for (const auto& mod : description_.modules) {
            auto it = descriptors_.find(mod.module_class);
            if (it == descriptors_.end()) continue;
            const auto& desc = it->second;

            // Validate continuous inputs against producer descriptors.
            if (desc.inputs) {
                const auto& flat = desc.inputs.value();
                // Use synced_inputs array if present, otherwise split flat inputs by synced flag.
                std::vector<InputDescription> continuous;
                if (mod.synced_inputs.has_value() && !mod.synced_inputs->empty()) {
                    continuous = mod.inputs;
                } else {
                    for (const auto& i : mod.inputs)
                        if (!i.synced.value()) continuous.push_back(i);
                }
                for (size_t i = 0; i < continuous.size() && i < flat.size(); ++i)
                    check_connection(mod.name, "inputs", i, flat[i],
                                     continuous[i].source_system_id,
                                     continuous[i].source_instance_id);
            }

            // Validate synced inputs.
            if (desc.synced_inputs) {
                const auto& flat = desc.synced_inputs.value();
                const auto synced = (mod.synced_inputs.has_value() && !mod.synced_inputs->empty())
                    ? *mod.synced_inputs
                    : [&]() {
                        std::vector<InputDescription> s;
                        for (const auto& i : mod.inputs) if (i.synced.value()) s.push_back(i);
                        return s;
                    }();
                for (size_t i = 0; i < synced.size() && i < flat.size(); ++i)
                    check_connection(mod.name, "synced_inputs", i, flat[i],
                                     synced[i].source_system_id,
                                     synced[i].source_instance_id);
            }
        }
    }

    // ------------------------------------------------------------------
    // Descriptor discovery
    // ------------------------------------------------------------------

    void scan_directory_for_descriptors(const std::string& dir) {
        if (!std::filesystem::is_directory(dir)) return;
        scanned_dirs_.push_back(dir);  // remember for companion lookup
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.path().extension() != ".json") continue;
            const std::string& p = entry.path().string();
            // Only load files ending in .module.json
            if (p.size() < 12) continue;
            if (p.substr(p.size() - 12) != ".module.json") continue;
            try_load_descriptor(p);
        }
    }

    void try_load_descriptor(const std::string& path) {
        auto result = rfl::json::load<ModuleDescriptor>(path);
        if (!result) return;  // Not a valid descriptor — skip silently
        ModuleDescriptor d = std::move(result.value());
        descriptors_[d.module_class] = std::move(d);
    }

    const ModuleDescriptor& find_descriptor(const std::string& module_class) const {
        auto it = descriptors_.find(module_class);
        if (it == descriptors_.end()) {
            std::string msg = "[Launcher] No descriptor found for module_class '"
                            + module_class + "'. Discovered classes:";
            for (const auto& [k, _] : descriptors_) msg += " " + k;
            throw std::runtime_error(msg);
        }
        return it->second;
    }

    // Resolve a companion binary name to an absolute path.
    // If the name already contains '/', treat as a path (verify existence).
    // Otherwise search scanned descriptor directories for an executable.
    std::string resolve_companion_binary(const std::string& name) const {
        if (name.find('/') != std::string::npos) {
            return std::filesystem::exists(name) ? name : std::string{};
        }
        for (const auto& dir : scanned_dirs_) {
            std::filesystem::path candidate = std::filesystem::path(dir) / name;
            if (std::filesystem::exists(candidate))
                return candidate.string();
        }
        return {};
    }

    // ------------------------------------------------------------------
    // AppDescription loading (JSON)
    // ------------------------------------------------------------------

    static AppDescription load_app_description(const std::string& path) {
        auto result = rfl::json::load<AppDescription>(path);
        if (!result)
            throw std::runtime_error("Failed to load AppDescription from '" + path
                                     + "': " + result.error().what());
        return std::move(result.value());
    }
};

} // namespace commrat

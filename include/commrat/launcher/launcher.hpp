#pragma once
/**
 * @file launcher.hpp
 * @brief Config-driven CommRaT application launcher.
 *
 * Reads an AppDescription from a YAML/JSON/TOML file, instantiates the
 * registered module classes, and starts them.
 *
 * Usage:
 * @code
 * // commrat.hpp forward-declares Launcher so MyApp::Launcher is valid.
 * // Include this header to use it:
 * #include <commrat/launcher/launcher.hpp>
 *
 * int main(int argc, char** argv) {
 *     return MyApp::Launcher::run(argc, argv, [](auto& launcher) {
 *         launcher.register_module<IMUModule>("IMUModule");
 *         launcher.register_module<SensorFusionModule>("SensorFusion");
 *     });
 * }
 * @endcode
 *
 * Config file (YAML example):
 * @code{.yaml}
 * app_name: MySensorSystem
 * modules:
 *   - name: IMU_1
 *     module_class: IMUModule
 *     outputs:
 *       - system_id: 10
 *         instance_id: 1
 *     period_ms: 10
 *
 *   - name: Fusion_1
 *     module_class: SensorFusion
 *     outputs:
 *       - system_id: 100
 *         instance_id: 1
 *     inputs:
 *       - source_system_id: 10
 *         source_instance_id: 1
 *       - source_system_id: 20
 *         source_instance_id: 1
 *         synced: true
 * @endcode
 */

#include <commrat/launcher/module_description.hpp>
#include <commrat/module/module_config.hpp>

#include <rfl/json.hpp>
#include <rfl/yaml.hpp>

#include <atomic>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace commrat {

// ---------------------------------------------------------------------------
// ModuleBase — type-erased handle for start/stop
// ---------------------------------------------------------------------------

/**
 * Non-template base for any Module2 instance held by the launcher.
 * Module2 already provides start()/stop() — we just need a pointer.
 */
struct ModuleBase {
    virtual ~ModuleBase() = default;
    virtual void start() = 0;
    virtual void stop()  = 0;
};

// Thin wrapper that delegates to a concrete Module2 subclass.
template<typename T>
struct ModuleHolder : ModuleBase {
    explicit ModuleHolder(std::unique_ptr<T> m) : module_(std::move(m)) {}
    void start() override { module_->start(); }
    void stop()  override { module_->stop();  }
    std::unique_ptr<T> module_;
};

// ---------------------------------------------------------------------------
// Launcher<App>
// ---------------------------------------------------------------------------

template<typename App>
class Launcher {
public:
    using Factory = std::function<std::unique_ptr<ModuleBase>(const ModuleDescription&)>;

    // ------------------------------------------------------------------
    // Module registration
    // ------------------------------------------------------------------

    /**
     * Register a module class by name.
     *
     * The factory receives the ModuleDescription so the module constructor
     * can read custom fields from it (e.g. via module_class for disambiguation).
     * The simplest convention: pass a ModuleConfig derived from the description.
     *
     * @tparam T  Concrete Module2 subclass. Must be constructible from ModuleConfig.
     */
    template<typename T>
    void register_module(std::string name) {
        factories_[std::move(name)] = [this](const ModuleDescription& desc) {
            return std::make_unique<ModuleHolder<T>>(
                std::make_unique<T>(to_config(desc)));
        };
    }

    /**
     * Register with a custom factory (for modules with non-standard constructors).
     */
    void register_module(std::string name, Factory factory) {
        factories_[std::move(name)] = std::move(factory);
    }

    // ------------------------------------------------------------------
    // Start / stop
    // ------------------------------------------------------------------

    /**
     * Load AppDescription from file and start all modules.
     * File format is auto-detected from the extension (.json, .yml/.yaml, .toml).
     */
    void start(const std::string& description_file) {
        AppDescription desc = load_description(description_file);
        app_name_ = desc.app_name;

        for (const auto& mod_desc : desc.modules) {
            auto it = factories_.find(mod_desc.module_class);
            if (it == factories_.end()) {
                throw std::runtime_error(
                    "Launcher: unknown module_class '" + mod_desc.module_class +
                    "' — did you call register_module<T>(\"" + mod_desc.module_class + "\")?");
            }
            auto holder = it->second(mod_desc);
            holder->start();
            instances_.push_back(std::move(holder));
        }
    }

    void stop() {
        for (auto& inst : instances_) inst->stop();
        instances_.clear();
    }

    // ------------------------------------------------------------------
    // Convenience main()
    // ------------------------------------------------------------------

    /**
     * Minimal main() wrapper.
     * Reads description file from argv[1], calls register_fn to register modules,
     * starts, waits for SIGINT/SIGTERM, stops.
     *
     * Usage:
     *   int main(int argc, char** argv) {
     *       return commrat::Launcher<MyApp>::run(argc, argv, [](auto& l) {
     *           l.register_module<IMUModule>("IMUModule");
     *       });
     *   }
     */
    static int run(int argc, char** argv,
                   std::function<void(Launcher&)> register_fn) {
        if (argc < 2) {
            std::cerr << "Usage: " << argv[0] << " <description.yml|json|toml>\n";
            return 1;
        }

        // Install signal handler
        static std::atomic<bool> shutdown{false};
        std::signal(SIGINT,  [](int) { shutdown.store(true); });
        std::signal(SIGTERM, [](int) { shutdown.store(true); });

        try {
            Launcher launcher;
            register_fn(launcher);
            launcher.start(argv[1]);

            std::cout << "Launcher: " << launcher.app_name_ << " running. "
                      << "Press Ctrl+C to stop.\n";

            while (!shutdown.load()) {
                corerat::Time::sleep(corerat::Milliseconds(100));
            }

            std::cout << "Launcher: shutting down...\n";
            launcher.stop();
            return 0;
        } catch (const std::exception& e) {
            std::cerr << "Launcher error: " << e.what() << "\n";
            return 1;
        }
    }

private:
    // ------------------------------------------------------------------
    // ModuleDescription → ModuleConfig translation
    // ------------------------------------------------------------------

    ModuleConfig to_config(const ModuleDescription& desc) const {
        ModuleConfig cfg;
        cfg.name = desc.name;

        // Period
        if (desc.period_ms.has_value()) {
            cfg.period = std::chrono::milliseconds(*desc.period_ms);
        }

        // Outputs
        if (desc.outputs.empty()) {
            cfg.outputs = NoOutputConfig{};
        } else if (desc.outputs.size() == 1) {
            const auto& o = desc.outputs[0];
            cfg.outputs = SimpleOutputConfig{
                .system_id   = o.system_id,
                .instance_id = o.instance_id,
            };
        } else {
            MultiOutputConfig multi;
            for (const auto& o : desc.outputs) {
                multi.addresses.push_back({
                    .system_id   = o.system_id,
                    .instance_id = o.instance_id,
                });
            }
            cfg.outputs = multi;
        }

        // Inputs
        if (desc.inputs.empty()) {
            cfg.inputs = NoInputConfig{};
        } else if (desc.inputs.size() == 1 && !desc.inputs[0].synced) {
            const auto& i = desc.inputs[0];
            cfg.inputs = SingleInputConfig{
                .source_system_id   = i.source_system_id,
                .source_instance_id = i.source_instance_id,
            };
        } else {
            MultiInputConfig multi;
            for (const auto& i : desc.inputs) {
                multi.sources.push_back({
                    .system_id   = i.source_system_id,
                    .instance_id = i.source_instance_id,
                });
            }
            cfg.inputs = multi;
        }

        return cfg;
    }

    // ------------------------------------------------------------------
    // File loading
    // ------------------------------------------------------------------

    static AppDescription load_description(const std::string& path) {
        std::ifstream f(path);
        if (!f) throw std::runtime_error("Cannot open: " + path);
        std::string text((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());

        std::filesystem::path p(path);
        std::string ext = p.extension().string();
        // Normalise: ".yml" → ".yaml"
        if (ext == ".yml") ext = ".yaml";

        if (ext == ".json") {
            auto result = rfl::json::read<AppDescription>(text);
            if (!result) throw std::runtime_error("JSON parse error: " + result.error().what());
            return std::move(result.value());
        }
        if (ext == ".yaml") {
            auto result = rfl::yaml::read<AppDescription>(text);
            if (!result) throw std::runtime_error("YAML parse error: " + result.error().what());
            return std::move(result.value());
        }
        throw std::runtime_error("Unsupported description format: " + ext +
                                  " (supported: .json, .yml, .yaml)");
    }

    std::unordered_map<std::string, Factory> factories_;
    std::vector<std::unique_ptr<ModuleBase>> instances_;
    std::string app_name_;
};

} // namespace commrat

#pragma once

#include <commrat/commrat.hpp>
#include <commrat/module/module_config.hpp>
#include <commrat/platform/threading.hpp>
#include <rfl.hpp>
#include <rfl/json.hpp>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <atomic>
#include <string>

namespace commrat {

// Global flag for signal handling
inline std::atomic<bool> g_shutdown_requested{false};

/**
 * @brief Signal handler for graceful shutdown
 * 
 * Handles SIGINT and SIGTERM by setting shutdown flag.
 */
inline void signal_handler(int signal) {
    g_shutdown_requested.store(true);
    std::cout << "\nReceived signal " << signal << ", shutting down...\n";
}

/**
 * @brief Main entry point for standalone module binaries
 * 
 * Provides complete lifecycle management:
 * - Signal handler installation (SIGINT, SIGTERM)
 * - Module instantiation with configuration
 * - Module start and execution
 * - Graceful shutdown on signal
 * - Exception handling with proper exit codes
 * 
 * Real-time safe after module start. Uses blocking sleep (0% CPU idle).
 * 
 * @tparam ModuleType Module class (must inherit from Module<...>)
 * @tparam Registry Message registry (CommRaT<...>)
 * @param config Module configuration
 * @return Exit code (0=success, 1=error, 130=SIGINT)
 * 
 * @note This function does NOT duplicate Module lifecycle - it just wraps it
 *       with process-level concerns (signals, CLI, config loading).
 * 
 * Example:
 * @code
 * ModuleConfig config{
 *     .name = "SensorModule",
 *     .system_id = 10,
 *     .instance_id = 1,
 *     .period = Milliseconds(100)
 * };
 * return module_main<SensorModule, MyApp>(config);
 * @endcode
 */
template<typename ModuleType, typename Registry>
int module_main(const ModuleConfig& config) {
    try {
        // Install signal handlers
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);
        
        // Create module instance
        std::cout << "Starting " << config.name << " (system_id=" 
                  << static_cast<int>(config.system_id()) << ", instance_id=" 
                  << static_cast<int>(config.instance_id()) << ")\n";
        
        ModuleType module(config);
        
        // Start module (begins subscription protocol, starts threads)
        module.start();
        std::cout << config.name << " running (press Ctrl+C to stop)...\n";
        
        // Wait for shutdown signal (0% CPU - blocking sleep)
        while (!g_shutdown_requested.load()) {
            Time::sleep(Milliseconds(100));
        }
        
        // Stop module (graceful shutdown of threads, unsubscribe)
        std::cout << "Stopping " << config.name << "...\n";
        module.stop();
        
        std::cout << config.name << " stopped successfully\n";
        return g_shutdown_requested.load() ? 130 : 0;  // 130 = SIGINT convention
        
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "ERROR: Unknown exception\n";
        return 1;
    }
}

/**
 * @brief Main entry point with argc/argv handling
 * 
 * Overload that handles configuration from command line:
 * 1. No arguments: Use compile-time defaults
 * 2. Single file argument: Load config from JSON/YAML/BSON/TOML (auto-detect by extension)
 * 3. CLI flags: Parse via rfl::parse_args with auto-generated --help
 * 
 * @tparam ModuleType Module class (must inherit from Module<...>)
 * @tparam Registry Message registry (CommRaT<...>)
 * @param argc Argument count
 * @param argv Argument vector
 * @return Exit code (0=success, 1=error, 130=SIGINT)
 */
template<typename ModuleType, typename Registry>
int module_main(int argc, char** argv) {
    try {
        ModuleConfig config;
        
        if (argc != 2) {
            std::cerr << "ERROR: Configuration file required\n";
            std::cerr << "Usage: " << argv[0] << " <config.json>\n";
            return 1;
        }
        
        std::string filename = argv[1];
        if (!filename.ends_with(".json")) {
            std::cerr << "ERROR: Only JSON config files supported (got: " << filename << ")\n";
            std::cerr << "Usage: " << argv[0] << " <config.json>\n";
            return 1;
        }
        
        config = rfl::json::load<ModuleConfig>(filename).value();
        
        // Call the main template function
        return module_main<ModuleType, Registry>(config);
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }
}

} // namespace commrat

/**
 * @brief Macro for creating standalone module binaries
 * 
 * Generates main() that calls module_main<ModuleType, Registry>(argc, argv).
 * 
 * Supports three configuration modes:
 * 1. File config: ./module config.json
 * 2. CLI flags: ./module --system-id 10 --instance-id 1 --period 100000000
 * 3. Help: ./module --help
 * 
 * @param ModuleType Module class to instantiate
 * @param Registry Message registry (CommRaT<...>)
 * 
 * Example:
 * @code
 * class SensorModule : public MyApp::Module<Output<Data>, PeriodicInput> {
 *     Data process() override { return Data{...}; }
 * };
 * 
 * COMMRAT_MODULE_MAIN(SensorModule, MyApp)
 * @endcode
 */
#define COMMRAT_MODULE_MAIN(ModuleType, Registry) \
    int main(int argc, char** argv) { \
        return commrat::module_main<ModuleType, Registry>(argc, argv); \
    }

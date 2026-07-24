/**
 * @file commrat_main.cpp
 * @brief commrat CLI — launch CommRaT applications from config files.
 *
 * Usage:
 *   commrat start <app.json> [--descriptor-dir DIR]...
 *
 * Subcommands:
 *   start   Load an AppDescription JSON and launch all modules listed in it.
 *           Module binaries are discovered via *.module.json descriptors found
 *           in the directories listed under descriptor_dirs in the JSON,
 *           in the directory containing the app.json, and in /etc/commrat/.
 *
 * Config files are conventionally placed in /etc/commrat/:
 *   commrat start /etc/commrat/my_app.json
 *
 * For development builds, put descriptor_dirs in the JSON:
 *   {
 *     "app_name": "my_app",
 *     "descriptor_dirs": ["/home/user/src/MyApp/build/default/examples"],
 *     "modules": [...]
 *   }
 */

#include <commrat/launcher/process_launcher.hpp>

#include <cstring>
#include <iostream>

int main(int argc, char** argv) {
    if (argc >= 2 && std::strcmp(argv[1], "start") == 0) {
        // Forward remaining args to ProcessLauncher::main, shifting "start" out.
        // ProcessLauncher::main expects: argv[0]=binary argv[1]=app.json [opts...]
        // We have:                       argv[0]=commrat argv[1]=start argv[2]=app.json [opts...]
        // Build a shifted argv.
        std::vector<const char*> new_argv;
        new_argv.push_back(argv[0]);           // keep binary name for error messages
        for (int i = 2; i < argc; ++i)
            new_argv.push_back(argv[i]);
        new_argv.push_back(nullptr);

        return commrat::ProcessLauncher::main(
            static_cast<int>(new_argv.size() - 1),
            const_cast<char**>(new_argv.data()));
    }

    std::cerr << "CommRaT launcher\n"
              << "\n"
              << "Usage:\n"
              << "  commrat start <app.json> [--descriptor-dir DIR]...\n"
              << "\n"
              << "Config files are conventionally placed in /etc/commrat/.\n";
    return 1;
}

#include <commrat/launcher/process_launcher.hpp>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory()
        : path_(std::filesystem::temp_directory_path()
                / ("commrat-launcher-validation-" + std::to_string(::getpid()))) {
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

void write_file(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream stream(path);
    assert(stream);
    stream << contents;
}

bool validation_fails(const std::filesystem::path& app_path,
                      const std::filesystem::path& descriptor_dir,
                      const std::string& expected_message) {
    try {
        commrat::ProcessLauncher launcher;
        launcher.load(app_path.string(), {descriptor_dir.string()});
        launcher.validate();
    } catch (const std::runtime_error& error) {
        return std::string(error.what()).find(expected_message) != std::string::npos;
    }
    return false;
}

void validate(const std::filesystem::path& app_path,
              const std::filesystem::path& descriptor_dir) {
    commrat::ProcessLauncher launcher;
    launcher.load(app_path.string(), {descriptor_dir.string()});
    launcher.validate();
}

} // namespace

int main() {
    TemporaryDirectory temporary;

    write_file(temporary.path() / "Consumer.module.json", R"({
        "module_class": "Consumer",
        "binary": "/bin/true",
        "outputs": [],
        "inputs": ["Payload"],
        "synced_inputs": ["Clock"],
        "remotes": []
    })");
    write_file(temporary.path() / "Producer.module.json", R"({
        "module_class": "Producer",
        "binary": "/bin/true",
        "outputs": ["Payload"],
        "inputs": [],
        "synced_inputs": [],
        "remotes": []
    })");

    write_file(temporary.path() / "missing-required-input.json", R"({
        "app_name": "MissingRequiredInput",
        "modules": [{
            "name": "Consumer_1",
            "module_class": "Consumer",
            "module_address": {"system_id": 20, "instance_id": 1},
            "outputs": [],
            "inputs": []
        }]
    })");
    assert(validation_fails(temporary.path() / "missing-required-input.json",
                            temporary.path(), "Input count mismatch"));

    write_file(temporary.path() / "missing-source.json", R"({
        "app_name": "MissingSource",
        "modules": [{
            "name": "Consumer_1",
            "module_class": "Consumer",
            "module_address": {"system_id": 20, "instance_id": 1},
            "outputs": [],
            "inputs": [{"source_system_id": 10, "source_instance_id": 1}]
        }]
    })");
    assert(validation_fails(temporary.path() / "missing-source.json",
                            temporary.path(), "Missing route"));

    write_file(temporary.path() / "optional-synced-input.json", R"({
        "app_name": "OptionalSyncedInput",
        "modules": [
            {
                "name": "Producer_1",
                "module_class": "Producer",
                "outputs": [{"system_id": 10, "instance_id": 1}],
                "inputs": []
            },
            {
                "name": "Consumer_1",
                "module_class": "Consumer",
                "module_address": {"system_id": 20, "instance_id": 1},
                "outputs": [],
                "inputs": [{"source_system_id": 10, "source_instance_id": 1}]
            }
        ]
    })");
    validate(temporary.path() / "optional-synced-input.json", temporary.path());

    write_file(temporary.path() / "duplicate-output.json", R"({
        "app_name": "DuplicateOutput",
        "modules": [
            {
                "name": "Producer_1",
                "module_class": "Producer",
                "outputs": [{"system_id": 10, "instance_id": 1}],
                "inputs": []
            },
            {
                "name": "Producer_2",
                "module_class": "Producer",
                "outputs": [{"system_id": 10, "instance_id": 1}],
                "inputs": []
            }
        ]
    })");
    assert(validation_fails(temporary.path() / "duplicate-output.json",
                            temporary.path(), "Duplicate output address"));

    write_file(temporary.path() / "missing-descriptor.json", R"({
        "app_name": "MissingDescriptor",
        "modules": [{
            "name": "Unknown_1",
            "module_class": "Unknown",
            "module_address": {"system_id": 30, "instance_id": 1},
            "outputs": [],
            "inputs": []
        }]
    })");
    assert(validation_fails(temporary.path() / "missing-descriptor.json",
                            temporary.path(), "No descriptor found"));

    bool validator_called = false;
    commrat::ProcessLauncher launcher;
    launcher.load(
        (temporary.path() / "optional-synced-input.json").string(),
        {temporary.path().string()});
    launcher.set_validator(
        [&](const commrat::AppDescription& description,
            const commrat::ProcessLauncher::DescriptorMap& descriptors) {
            validator_called = description.app_name == "OptionalSyncedInput"
                && descriptors.size() == 2;
        });
    launcher.validate();
    assert(validator_called);

    return 0;
}
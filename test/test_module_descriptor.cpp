#include <commrat/meta/descriptor.hpp>
#include <rfl/json.hpp>

#include <cassert>
#include <iostream>
#include <string>

struct ExpectedParams {
    float gain;
    int sample_count;
};

int main(int argc, char** argv) {
    assert(argc == 2);

    auto parsed = rfl::json::load<commrat::ModuleDescriptor>(argv[1]);
    assert(parsed.has_value());
    const auto& descriptor = parsed.value();

    assert(descriptor.module_class == "descriptor_test::DescriptorModule");
    assert(!descriptor.binary.empty());
    assert(descriptor.outputs.has_value());
    assert(descriptor.outputs->size() == 1);
    assert(descriptor.outputs->front() == "descriptor_test::OutputData");
    assert(descriptor.inputs.has_value() && descriptor.inputs->empty());
    assert(descriptor.synced_inputs.has_value() && descriptor.synced_inputs->empty());
    assert(descriptor.execution_mode == "timer");
    assert(descriptor.default_period_ms == 25);

    assert(descriptor.cmd_messages.has_value());
    assert(descriptor.cmd_messages->size() == 1);
    const auto& commands = descriptor.cmd_messages->front();
    assert(commands.output_index == 0);
    assert(commands.types.size() == 1);
    assert(commands.types.front() == "descriptor_test::ResetCommand");

    assert(descriptor.params_defaults.has_value());
    auto params = rfl::json::read<ExpectedParams>(
        rfl::json::write(*descriptor.params_defaults));
    assert(params.has_value());
    assert(params->gain == 1.5F);
    assert(params->sample_count == 8);

    std::cout << "PASS: complete module descriptor\n";
    return 0;
}
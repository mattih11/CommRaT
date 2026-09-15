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
    assert(descriptor.remotes.has_value() && descriptor.remotes->empty());
    assert(descriptor.execution_mode == "timer");
    assert(descriptor.default_period_ms == 25);

    assert(descriptor.cmd_messages.has_value());
    assert(descriptor.cmd_messages->size() == 1);
    const auto& commands = descriptor.cmd_messages->front();
    assert(commands.output_index == 0);
    assert(commands.types.size() == 1);
    assert(commands.types.front() == "descriptor_test::ResetCommand");

    assert(descriptor.command_endpoints.has_value());
    assert(descriptor.command_endpoints->size() == 1);
    const auto& endpoint = descriptor.command_endpoints->front();
    assert(endpoint.output_index == 0);
    assert(endpoint.output_type == "descriptor_test::OutputData");
    assert(endpoint.output_message_id != 0);
    assert(endpoint.address_type_id == (endpoint.output_message_id & 0xFF));
    assert(endpoint.mailbox_index == 0);
    assert(endpoint.commands.size() == 1);
    assert(endpoint.commands.front().request_type == "descriptor_test::ResetCommand");
    assert(endpoint.commands.front().request_id != 0);
    assert(endpoint.commands.front().reply_type == "descriptor_test::ResetCommand::Reply");
    assert(endpoint.commands.front().reply_id != 0);

    assert(descriptor.lifecycle_endpoint.has_value());
    assert(descriptor.lifecycle_endpoint->anchor_output_index == 0);
    assert(descriptor.lifecycle_endpoint->anchor_message_id == endpoint.output_message_id);
    assert(descriptor.lifecycle_endpoint->address_type_id == endpoint.address_type_id);
    assert(descriptor.lifecycle_endpoint->mailbox_index == 0xFF);

    assert(descriptor.params_defaults.has_value());
    auto params = rfl::json::read<ExpectedParams>(
        rfl::json::write(*descriptor.params_defaults));
    assert(params.has_value());
    assert(params->gain == 1.5F);
    assert(params->sample_count == 8);

    std::cout << "PASS: complete module descriptor\n";
    return 0;
}
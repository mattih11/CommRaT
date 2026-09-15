/**
 * @file test_command_rpc.cpp
 * @brief Tests typed Module2 command RPC helpers.
 */

#include <commrat/commrat.hpp>
#include <cassert>
#include <cmath>
#include <iostream>
#include <optional>

using namespace commrat;

struct SensorData {
    float value{0.0f};
};

struct ControllerData {
    bool command_ok{false};
};

struct CalibrateCmd {
    float offset{0.0f};

    struct Reply {
        bool success{false};
        float previous_offset{0.0f};
    };
};

using SensorWithCommands = DataWithCommands<SensorData, CalibrateCmd>;
using CommandRpcApp = CommRaT<SensorWithCommands, Message::Data<ControllerData>>;

class CommandableSensor : public CommandRpcApp::Module2<Output<SensorData>, Period<Milliseconds(50)>> {
public:
    explicit CommandableSensor(const ModuleConfig& config)
        : CommandRpcApp::Module2<Output<SensorData>, Period<Milliseconds(50)>>(config) {
        const bool registered = this->template register_command_handler<
            0,
            CalibrateCmd,
            &CommandableSensor::handle_calibrate
        >(*this);
        assert(registered);
    }

    [[nodiscard]] float offset() const { return calibration_offset_; }

protected:
    void process(SensorData& output) override {
        output.value = 20.0f + calibration_offset_;
    }

private:
    void handle_calibrate(const CalibrateCmd& cmd, CalibrateCmd::Reply& reply) {
        reply.previous_offset = calibration_offset_;
        calibration_offset_ = cmd.offset;
        reply.success = true;
    }

    float calibration_offset_{0.0f};
};

class CommandController : public CommandRpcApp::Module2<
    Output<ControllerData>,
    Remote<SensorData>,
    Period<Milliseconds(50)>> {
public:
    explicit CommandController(const ModuleConfig& config)
        : CommandRpcApp::Module2<
              Output<ControllerData>,
              Remote<SensorData>,
              Period<Milliseconds(50)>>(config) {}

    std::optional<TimsMessage<CalibrateCmd::Reply>> calibrate(float offset) {
        return this->template remote<SensorData>().template send_command<CalibrateCmd>(
            CalibrateCmd{.offset = offset},
            Milliseconds(500)
        );
    }

protected:
    void process(ControllerData& output) override {
        output.command_ok = true;
    }
};

class InputCommandController : public CommandRpcApp::Module2<Output<ControllerData>, Input<SensorData>> {
public:
    explicit InputCommandController(const ModuleConfig& config)
        : CommandRpcApp::Module2<Output<ControllerData>, Input<SensorData>>(config) {}

    std::optional<TimsMessage<CalibrateCmd::Reply>> calibrate_input(float offset) {
        return this->template input<SensorData>().template send_command<CalibrateCmd>(
            CalibrateCmd{.offset = offset},
            Milliseconds(500)
        );
    }

protected:
    void process(const SensorData& input, ControllerData& output) override {
        output.command_ok = input.value > 0.0f;
    }
};

int main() {
    ModuleConfig sensor_config{
        .name = "CommandableSensor",
        .outputs = SimpleOutputConfig{.system_id = 10, .instance_id = 1},
        .inputs = NoInputConfig{},
        .period = std::chrono::milliseconds(50),
        .params = std::nullopt
    };

    ModuleConfig controller_config{
        .name = "CommandController",
        .outputs = SimpleOutputConfig{.system_id = 20, .instance_id = 1},
        .inputs = NoInputConfig{},
        .remotes = {{.system_id = 10, .instance_id = 1}},
        .period = std::chrono::milliseconds(50),
        .params = std::nullopt
    };

    ModuleConfig input_controller_config{
        .name = "InputCommandController",
        .outputs = SimpleOutputConfig{.system_id = 30, .instance_id = 1},
        .inputs = SingleInputConfig{.source_system_id = 10, .source_instance_id = 1},
        .period = std::chrono::milliseconds(50),
        .params = std::nullopt
    };

    CommandableSensor sensor(sensor_config);
    CommandController controller(controller_config);
    InputCommandController input_controller(input_controller_config);

    sensor.start();
    controller.start();
    input_controller.start();
    Time::sleep(Milliseconds(100));

    auto first_reply = controller.calibrate(1.25f);
    assert(first_reply.has_value());
    assert(first_reply->payload.success);
    assert(std::fabs(first_reply->payload.previous_offset - 0.0f) < 0.001f);

    auto second_reply = controller.calibrate(2.5f);
    assert(second_reply.has_value());
    assert(second_reply->payload.success);
    assert(std::fabs(second_reply->payload.previous_offset - 1.25f) < 0.001f);
    assert(std::fabs(sensor.offset() - 2.5f) < 0.001f);

    auto input_reply = input_controller.calibrate_input(3.75f);
    assert(input_reply.has_value());
    assert(input_reply->payload.success);
    assert(std::fabs(input_reply->payload.previous_offset - 2.5f) < 0.001f);
    assert(std::fabs(sensor.offset() - 3.75f) < 0.001f);

    input_controller.stop();
    controller.stop();
    sensor.stop();

    std::cout << "PASS: typed command RPC\n";
    return 0;
}

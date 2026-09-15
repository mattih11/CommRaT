#include <commrat/commrat.hpp>

#include <atomic>
#include <cassert>
#include <iostream>
#include <optional>

using namespace commrat;

struct ControllerOutput {
    bool running{false};
};

using LifecycleApp = CommRaT<Message::Data<ControllerOutput>>;

class LifecycleTargetModule : public LifecycleApp::Module2<Period<Milliseconds(5)>> {
public:
    explicit LifecycleTargetModule(const ModuleConfig& config)
        : LifecycleApp::Module2<Period<Milliseconds(5)>>(config) {}

    [[nodiscard]] uint32_t process_count() const {
        return process_count_.load(std::memory_order_acquire);
    }

    [[nodiscard]] uint32_t enable_count() const {
        return enable_count_.load(std::memory_order_acquire);
    }

    [[nodiscard]] uint32_t disable_count() const {
        return disable_count_.load(std::memory_order_acquire);
    }

    [[nodiscard]] uint32_t stop_count() const {
        return stop_count_.load(std::memory_order_acquire);
    }

protected:
    void process() override {
        process_count_.fetch_add(1, std::memory_order_release);
    }

    LifecycleResult on_enable() override {
        enable_count_.fetch_add(1, std::memory_order_release);
        return LifecycleResult::Success;
    }

    void on_disable() override {
        disable_count_.fetch_add(1, std::memory_order_release);
    }

    void on_stop() override {
        stop_count_.fetch_add(1, std::memory_order_release);
    }

private:
    std::atomic<uint32_t> process_count_{0};
    std::atomic<uint32_t> enable_count_{0};
    std::atomic<uint32_t> disable_count_{0};
    std::atomic<uint32_t> stop_count_{0};
};

class LifecycleController : public LifecycleApp::Module2<
    Output<ControllerOutput>,
    Period<Milliseconds(20)>> {
public:
    explicit LifecycleController(const ModuleConfig& config)
        : LifecycleApp::Module2<Output<ControllerOutput>, Period<Milliseconds(20)>>(config) {}

protected:
    void process(ControllerOutput& output) override {
        output.running = true;
    }
};

int main() {
    ModuleConfig target_config{
        .name = "LifecycleTarget",
        .outputs = NoOutputConfig{.system_id = 10, .instance_id = 1},
        .inputs = NoInputConfig{},
        .period = std::chrono::milliseconds(5),
        .params = std::nullopt
    };
    ModuleConfig controller_config{
        .name = "LifecycleController",
        .outputs = SimpleOutputConfig{.system_id = 20, .instance_id = 1},
        .inputs = NoInputConfig{},
        .period = std::chrono::milliseconds(20),
        .params = std::nullopt
    };

    LifecycleTargetModule target(target_config);
    LifecycleController controller(controller_config);
    target.start();
    controller.start();
    Time::sleep(Milliseconds(30));

    assert(target.enable_count() == 1);
    assert(target.process_count() > 0);

    auto initial_status = controller.get_lifecycle_status(10, 1, Milliseconds(500));
    assert(initial_status.has_value());
    assert(initial_status->payload.state == static_cast<uint8_t>(LifecycleState::Enabled));
    assert(initial_status->payload.target == static_cast<uint8_t>(LifecycleTarget::On));

    auto off_reply = controller.lifecycle_off(10, 1, Milliseconds(500));
    assert(off_reply.has_value());
    assert(off_reply->payload.result == static_cast<uint8_t>(LifecycleResult::Success));
    assert(off_reply->payload.state == static_cast<uint8_t>(LifecycleState::Disabled));
    assert(target.disable_count() == 1);

    const uint32_t stopped_count = target.process_count();
    Time::sleep(Milliseconds(30));
    assert(target.process_count() == stopped_count);

    auto disabled_status = controller.get_lifecycle_status(10, 1, Milliseconds(500));
    assert(disabled_status.has_value());
    assert(disabled_status->payload.state == static_cast<uint8_t>(LifecycleState::Disabled));

    auto repeated_off = controller.lifecycle_off(10, 1, Milliseconds(500));
    assert(repeated_off.has_value());
    assert(repeated_off->payload.result ==
           static_cast<uint8_t>(LifecycleResult::AlreadyInState));

    auto on_reply = controller.lifecycle_on(10, 1, Milliseconds(500));
    assert(on_reply.has_value());
    assert(on_reply->payload.result == static_cast<uint8_t>(LifecycleResult::Success));
    assert(on_reply->payload.state == static_cast<uint8_t>(LifecycleState::Enabled));
    assert(target.enable_count() == 2);

    Time::sleep(Milliseconds(30));
    assert(target.process_count() > stopped_count);

    auto repeated_on = controller.lifecycle_on(10, 1, Milliseconds(500));
    assert(repeated_on.has_value());
    assert(repeated_on->payload.result ==
           static_cast<uint8_t>(LifecycleResult::AlreadyInState));

    auto controller_off = controller.lifecycle_off<ControllerOutput>(
        20, 1, Milliseconds(500));
    assert(controller_off.has_value());
    assert(controller_off->payload.state ==
           static_cast<uint8_t>(LifecycleState::Disabled));

    auto controller_on = controller.lifecycle_on<ControllerOutput>(
        20, 1, Milliseconds(500));
    assert(controller_on.has_value());
    assert(controller_on->payload.state ==
           static_cast<uint8_t>(LifecycleState::Enabled));

    auto missing_status = controller.get_lifecycle_status<void>(
        99, 99, Milliseconds(20));
    assert(!missing_status.has_value());

    controller.stop();
    target.stop();
    target.stop();
    assert(target.stop_count() == 1);

    std::cout << "PASS: module lifecycle commands\n";
    return 0;
}
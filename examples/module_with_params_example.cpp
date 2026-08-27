/**
 * @file module_with_params_example.cpp
 * @brief Demonstrates the CommRaT parameter system.
 *
 * Params<SensorParams> in the IOSpec causes Module2 to:
 *   - Store params_ of type SensorParams automatically
 *   - Load it from ModuleConfig::params on construction
 *   - Handle GetParamsCmd / SetParamsCmd on the CMD mailbox automatically
 *
 * The user only overrides on_params_changed() for runtime notification.
 * params_ is directly accessible in process() — no overhead.
 */

#include <commrat/commrat.hpp>
#include <commrat/module2.hpp>
#include <commrat/module_main.hpp>
#include <sertial/containers/fixed_string.hpp>
#include <sertial/containers/reflectors.hpp>

using namespace commrat;

struct SensorReading {
    float    value{0.0f};
    uint32_t tick{0};
};

using ExampleApp = CommRaT<Message::Data<SensorReading>>;

// Params struct: rfl-serializable via SeRTial reflectors (fixed_string + fixed_vector).
struct SensorParams {
    float gain{1.0f};
    int   filter_window{5};
    sertial::fixed_string<64> device{"/dev/i2c-1"};
};

// Params<SensorParams> wires params_ storage, loading, and command handling automatically.
class SensorModule : public ExampleApp::Module2<Output<SensorReading>,
                                                Period<Milliseconds(100)>,
                                                Params<SensorParams>> {
    using Base = ExampleApp::Module2<Output<SensorReading>,
                                     Period<Milliseconds(100)>,
                                     Params<SensorParams>>;
public:
    explicit SensorModule(const ModuleConfig& config) : Base(config) {
        RTLOG_INFO(logger_) << "SensorModule: gain=" << params_.gain
                            << " device=" << params_.device.c_str();
    }

protected:
    void process(SensorReading& out) override {
        out.value = static_cast<float>(tick_) * params_.gain;
        out.tick  = tick_++;
    }

    void on_params_changed() override {
        RTLOG_INFO(logger_) << "Params updated: gain=" << params_.gain
                            << " window=" << params_.filter_window;
    }

private:
    uint32_t tick_{0};
};

COMMRAT_MODULE_MAIN(SensorModule)


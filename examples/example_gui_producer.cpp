/**
 * @file example_gui_producer.cpp
 * @brief Multi-output CommRaT producer for RaTGUI live display.
 *
 * Publishes StatusData, CounterData, TemperatureData and PoseData at 10 Hz.
 * Designed to be used alongside the ratgui web dashboard.
 *
 * Run standalone:
 *   ./example_gui_producer  <config.json>
 *
 * Or start together with ratgui via the process launcher:
 *   ./ratgui_example_launcher  <app.json>
 */

#include <commrat/examples/common_messages.hpp>
#include <commrat/module_main.hpp>
#include <cmath>

using namespace example_messages;

class ExampleGUIProducerModule : public ExampleApp::Module2<
    commrat::Output<StatusData>,
    commrat::Output<CounterData>,
    commrat::Output<TemperatureData>,
    commrat::Output<PoseData>,
    commrat::Period<100>  // 10 Hz
> {
    using Base = ExampleApp::Module2<
        commrat::Output<StatusData>,
        commrat::Output<CounterData>,
        commrat::Output<TemperatureData>,
        commrat::Output<PoseData>,
        commrat::Period<100>>;
public:
    using Base::Base;

protected:
    void process(StatusData& status, CounterData& counter,
                 TemperatureData& temp, PoseData& pose) override {
        float t = counter_ * 0.1f;  // seconds at 10 Hz

        status = StatusData{
            .counter      = counter_,
            .cpu_load     = 20.0f + 10.0f * std::sin(t * 0.5f),
            .memory_usage = 40.0f +  5.0f * std::cos(t * 0.3f)
        };

        counter = CounterData{ .value = counter_ };

        temp = TemperatureData{
            .sensor_id     = 42,
            .temperature_c = 20.0f + 5.0f * std::sin(t),
            .confidence    = 0.95f
        };

        pose = PoseData{
            .x     =  2.0f * std::cos(t * 0.2f),
            .y     =  2.0f * std::sin(t * 0.2f),
            .theta = t * 0.2f,
            .vx    = -0.4f * std::sin(t * 0.2f),
            .vy    =  0.4f * std::cos(t * 0.2f),
            .omega = 0.2f
        };

        ++counter_;
    }

private:
    uint32_t counter_{0};
};

COMMRAT_MODULE_MAIN(ExampleGUIProducerModule)

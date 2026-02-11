#pragma once

#include "commrat/module/base/module_base_selector.hpp"

namespace commrat {

/**
 * @brief Modern C++20 real-time messaging module
 * 
 * Provides type-safe, compile-time message passing with automatic
 * subscription management and timestamp propagation.
 * 
 * @tparam Registry MessageRegistry with all application message types
 * @tparam OutputSpec Output<T>, Outputs<T,U,V>, or raw type T
 * @tparam InputSpec Input<T>, Inputs<T,U,V>, PeriodicInput, or LoopInput
 * @tparam CommandTypes Optional variadic command types this module handles
 * 
 * ## Processing Signatures (Override in derived class)
 * 
 * **Periodic/Loop single output**:
 * ```cpp
 * void process(OutputData& output) override;
 * ```
 * 
 * **Continuous single input/output**:
 * ```cpp
 * void process(const InputData& input, OutputData& output) override;
 * ```
 * 
 * **Multi-output periodic/loop**:
 * ```cpp
 * void process(OutputData1& out1, OutputData2& out2, ...) override;
 * ```
 * 
 * **Multi-input single output**:
 * ```cpp
 * void process(const Input1&, const Input2&, OutputData& output) override;
 * ```
 * 
 * **Multi-input multi-output**:
 * ```cpp
 * void process(const Input1&, const Input2&, Output1& out1, Output2& out2) override;
 * ```
 * 
 * ## Lifecycle Hooks (Optional)
 * 
 * ```cpp
 * void on_init() override;    // Before start()
 * void on_start() override;   // After threads launched
 * void on_stop() override;    // Before cleanup
 * void on_cleanup() override; // Final cleanup
 * ```
 * 
 * ## Example Usage
 * 
 * ```cpp
 * using MyApp = CommRaT<
 *     Message::Data<TemperatureData>,
 *     Message::Data<PressureData>
 * >;
 * 
 * class SensorModule : public MyApp::Module<Output<TemperatureData>, PeriodicInput> {
 * protected:
 *     void process(TemperatureData& output) override {
 *         output.temperature_c = read_sensor();
 *     }
 * };
 * ```
 * 
 * ## Architecture (Phase 7.1)
 * 
 * This is a thin wrapper over specialized ModuleBase implementations.
 * Currently PeriodicInput/LoopInput + single output is supported.
 * Other 5 modes will be added in Phase 7.2.
 */
template<typename Registry,
         typename OutputSpec,
         typename InputSpec,
         typename... CommandTypes>
class ModuleV2 : public SelectModuleBase<Registry, OutputSpec, InputSpec, CommandTypes...> {
public:
    using Base = SelectModuleBase<Registry, OutputSpec, InputSpec, CommandTypes...>;
    
    // Validate that we got a valid base (not void)
    static_assert(!std::is_void_v<Base>, 
        "Unsupported I/O combination. Currently only NoInputSingleOutputBase implemented "
        "(PeriodicInput/LoopInput + single output).");
    
    // Explicit constructor forwarding
    explicit ModuleV2(const ModuleConfig& config) : Base(config) {}
    
    // Delete copy/move
    ModuleV2(const ModuleV2&) = delete;
    ModuleV2& operator=(const ModuleV2&) = delete;
    ModuleV2(ModuleV2&&) = delete;
    ModuleV2& operator=(ModuleV2&&) = delete;
    
    // Re-export user-facing type aliases
    using OutputData = typename Base::OutputData;
    using InputData = typename Base::InputData;
    
    // Re-export configuration flags
    static constexpr bool has_periodic_input = Base::has_periodic_input;
    static constexpr bool has_continuous_input = Base::has_continuous_input;
    static constexpr bool has_loop_input = Base::has_loop_input;
    static constexpr bool has_multi_input = Base::has_multi_input;
    static constexpr bool has_multi_output = Base::has_multi_output;
};

} // namespace commrat

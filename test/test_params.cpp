/**
 * @file test_params.cpp
 * @brief Tests for the CommRaT parameter system.
 *
 * Tests:
 * 1. params_from_config returns defaults when config has no params
 * 2. params_from_config deserializes from ModuleConfig::params
 * 3. GetParamsCmd / SetParamsCmd round-trip via Module2 virtual interface
 */

#include <commrat/module/params.hpp>
#include <commrat/messaging/system/param_messages.hpp>
#include <sertial/containers/fixed_string.hpp>
#include <sertial/containers/reflectors.hpp>
#include <rfl/json.hpp>

#include <cassert>
#include <iostream>
#include <string>

using namespace commrat;

// ============================================================================
// Test helpers
// ============================================================================

struct TestParams {
    float gain{1.0f};
    int   window{5};
    sertial::fixed_string<64> device{"/dev/i2c-1"};  // RT-safe, rfl via SeRTial reflectors
};

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) void test_##name()
#define RUN(name) do { \
    ++tests_run; \
    try { test_##name(); ++tests_passed; std::cout << "PASS " #name "\n"; } \
    catch (const std::exception& e) { std::cout << "FAIL " #name ": " << e.what() << "\n"; } \
} while(0)
#define ASSERT(cond) do { if (!(cond)) throw std::runtime_error("assertion failed: " #cond); } while(0)

// ============================================================================
// Tests
// ============================================================================

TEST(params_defaults_when_no_config) {
    ModuleConfig cfg;
    cfg.name = "test";
    auto p = params_from_config<TestParams>(cfg);
    ASSERT(p.gain == 1.0f);
    ASSERT(p.window == 5);
    ASSERT(std::string(p.device.c_str()) == "/dev/i2c-1");
}

TEST(params_loaded_from_config) {
    ModuleConfig cfg;
    cfg.name = "test";
    // Inject params as rfl::Generic by parsing a JSON blob
    auto g = rfl::json::read<rfl::Generic>(
        R"({"gain": 3.14, "window": 10, "device": "/dev/i2c-2"})");
    ASSERT(g.has_value());
    cfg.params = g.value();

    auto p = params_from_config<TestParams>(cfg);
    ASSERT(p.gain > 3.0f && p.gain < 3.2f);
    ASSERT(p.window == 10);
    ASSERT(std::string(p.device.c_str()) == "/dev/i2c-2");
}

TEST(params_partial_override_uses_defaults) {
    ModuleConfig cfg;
    cfg.name = "test";
    // Only override gain — window and device should keep defaults
    auto g = rfl::json::read<rfl::Generic>(R"({"gain": 5.0})");
    ASSERT(g.has_value());
    cfg.params = g.value();

    auto p = params_from_config<TestParams>(cfg);
    ASSERT(p.gain == 5.0f);
    // rfl fills missing fields with defaults from the struct
    ASSERT(p.window == 5);
}

TEST(params_bad_json_returns_defaults) {
    ModuleConfig cfg;
    cfg.name = "test";
    // Syntactically valid JSON but wrong type for gain — rfl should fall back.
    // With DefaultIfMissing, rfl may still fail on type mismatch → defaults.
    auto g = rfl::json::read<rfl::Generic>(R"({"gain": "not_a_float"})");
    ASSERT(g.has_value());
    cfg.params = g.value();

    auto p = params_from_config<TestParams>(cfg);
    // gain type mismatch (string vs float) → parsing fails → defaults used
    ASSERT(p.gain == 1.0f);
}

TEST(get_params_reply_payload_size) {
    // Ensure the payload fits comfortably within a 512-char fixed_string
    TestParams p;
    p.gain = 2.5f;
    p.window = 8;
    p.device = sertial::fixed_string<64>("/dev/i2c-3");
    std::string json = rfl::json::write(p);
    ASSERT(json.size() < 512);
    std::cout << "  params JSON (" << json.size() << " bytes): " << json << "\n";
}

TEST(set_params_cmd_round_trip) {
    // Simulate what Module2 does when it receives a SetParamsCmd:
    // deserialize from the fixed_string payload field.
    TestParams original;
    original.gain = 7.0f;
    original.window = 3;

    std::string json = rfl::json::write(original);
    ASSERT(json.size() < 512);

    // What Module2 does in visit_param_commands for SetParamsPayload:
    std::string received(json.c_str());  // simulate fixed_string.data()
    auto result = rfl::json::read<TestParams>(received);
    ASSERT(result.has_value());
    ASSERT(result.value().gain == 7.0f);
    ASSERT(result.value().window == 3);
}

// ============================================================================
// main
// ============================================================================

int main() {
    RUN(params_defaults_when_no_config);
    RUN(params_loaded_from_config);
    RUN(params_partial_override_uses_defaults);
    RUN(params_bad_json_returns_defaults);
    RUN(get_params_reply_payload_size);
    RUN(set_params_cmd_round_trip);

    std::cout << "\n" << tests_passed << "/" << tests_run << " passed\n";
    return (tests_passed == tests_run) ? 0 : 1;
}

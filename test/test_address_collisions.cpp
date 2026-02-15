/**
 * @file test_address_collisions.cpp
 * @brief Test address collision detection and prevention
 * 
 * Validates that:
 * 1. Same [type][sys][inst] causes collision (should fail)
 * 2. Different type_id with same [sys][inst] - no collision
 * 3. Multi-output modules handle collisions correctly
 * 4. Multi-output same type with same address - collision
 * 5. Multi-output same type with different address - no collision
 * 6. Multi-output different types - no collision even with same [sys][inst]
 */

#include <commrat/commrat.hpp>
#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>

using namespace commrat;
using namespace std::chrono_literals;

// ============================================================================
// Message Types (Different type IDs for collision testing)
// ============================================================================

struct SensorA {
    uint32_t value;
    float temperature;
};

struct SensorB {
    uint32_t value;
    float pressure;
};

struct SensorC {
    uint32_t value;
    float humidity;
};

// Registry with different message types
using TestRegistry = CommRaT<
    Message::Data<SensorA>,
    Message::Data<SensorB>,
    Message::Data<SensorC>
>;

// ============================================================================
// Test Modules
// ============================================================================

// Simple producer module
template<typename OutputType>
class ProducerModule : public TestRegistry::Module<Output<OutputType>, PeriodicInput> {
public:
    using Base = TestRegistry::Module<Output<OutputType>, PeriodicInput>;
    using Base::Base;  // Inherit constructors
    
protected:
    void process(OutputType& output) override {
        output.value = counter_++;
    }

private:
    uint32_t counter_{0};
};

using ProducerA = ProducerModule<SensorA>;
using ProducerB = ProducerModule<SensorB>;
using ProducerC = ProducerModule<SensorC>;

// Multi-output producer
class MultiProducer : public TestRegistry::Module<
    Outputs<SensorA, SensorB>,
    PeriodicInput
> {
public:
    using Base = TestRegistry::Module<Outputs<SensorA, SensorB>, PeriodicInput>;
    using Base::Base;  // Inherit constructors
    
protected:
    void process(SensorA& a, SensorB& b) override {
        a.value = counter_;
        b.value = counter_;
        counter_++;
    }

private:
    uint32_t counter_{0};
};

// Consumer modules (Input<T> with Output<T> for pass-through)
template<typename InputType>
class ConsumerModule : public TestRegistry::Module<Output<InputType>, Input<InputType>> {
public:
    using Base = TestRegistry::Module<Output<InputType>, Input<InputType>>;
    using Base::Base;
    
    std::atomic<uint32_t> received_count_{0};
    std::atomic<uint32_t> last_value_{0};
    
protected:
    void process(const InputType& input, InputType& output) override {
        received_count_++;
        last_value_ = input.value;
        output = input;  // Pass-through
    }
};

using ConsumerA = ConsumerModule<SensorA>;
using ConsumerB = ConsumerModule<SensorB>;
using ConsumerC = ConsumerModule<SensorC>;

// ============================================================================
// Test Helpers
// ============================================================================

template<typename ModuleType>
bool try_create_module(const ModuleConfig& config, const std::string& test_name) {
    try {
        std::cout << "\n[" << test_name << "] Attempting to create module: " << config.name 
                  << " (sys=" << static_cast<int>(config.system_id()) 
                  << ", inst=" << static_cast<int>(config.instance_id()) << ")\n";
        
        // Create module with config
        ModuleType module(config);
        
        std::cout << "  [OK] Module created successfully\n";
        
        // Start the module - this creates mailboxes and starts threads
        module.start();
        std::cout << "  [OK] Module started successfully\n";
        
        std::this_thread::sleep_for(100ms);
        module.stop();
        
        return true;
        
    } catch (const std::exception& e) {
        std::cout << "  [ERROR] Exception caught: " << e.what() << "\n";
        return false;
    }
}

// ============================================================================
// Test Cases
// ============================================================================

bool test_1_identical_type_system_instance() {
    std::cout << "\n========================================\n";
    std::cout << "Test 1: Identical [type][sys][inst] - SHOULD COLLIDE\n";
    std::cout << "  Both modules should try to create same mailbox IDs\n";
    std::cout << "  Also verify consumer subscription and data flow\n";
    std::cout << "========================================\n";
    
    ModuleConfig producer_config{
        .name = "ProducerA_1",
        .outputs = SimpleOutputConfig{.system_id = 10, .instance_id = 1},
        .inputs = NoInputConfig{},
        .period = 100ms,
        .max_subscribers = 4
    };
    
    ModuleConfig consumer_config{
        .name = "ConsumerA",
        .outputs = SimpleOutputConfig{.system_id = 10, .instance_id = 2},
        .inputs = SingleInputConfig{.source_system_id = 10, .source_instance_id = 1},
        .max_subscribers = 1
    };
    
    ModuleConfig collision_config{
        .name = "ProducerA_2_COLLISION",
        .outputs = SimpleOutputConfig{.system_id = 10, .instance_id = 1},  // SAME!
        .inputs = NoInputConfig{},
        .period = 100ms,
        .max_subscribers = 4
    };
    
    try {
        std::cout << "[Test1] Creating producer with [type=SensorA][sys=10][inst=1]\n";
        ProducerA producer(producer_config);
        producer.start();
        std::cout << "  [OK] Producer started\n";
        
        std::this_thread::sleep_for(100ms);
        
        std::cout << "[Test1] Creating consumer subscribing to producer\n";
        ConsumerA consumer(consumer_config);
        consumer.start();
        std::cout << "  [OK] Consumer started and subscribed\n";
        
        std::this_thread::sleep_for(500ms);
        
        uint32_t received = consumer.received_count_.load();
        std::cout << "  [INFO] Consumer received " << received << " messages\n";
        
        if (received == 0) {
            std::cout << "  [WARN] No data received - subscription may have failed\n";
        }
        
        // Now try to create second producer with SAME address
        std::cout << "\n[Test1] Creating SECOND producer with SAME [type][sys][inst]\n";
        std::cout << "        Should FAIL - mailbox IDs already exist!\n";
        ProducerA producer2(collision_config);
        producer2.start();  // Should throw - mailbox already exists!
        
        // If we get here, collision was NOT detected
        producer.stop();
        producer2.stop();
        consumer.stop();
        std::cout << "\nFAIL: Second module started (TiMS should prevent duplicate mailboxes)\n";
        return false;
        
    } catch (const std::exception& e) {
        std::cout << "  [OK] Exception caught: " << e.what() << "\n";
        std::cout << "\nPASS: Test 1 - TiMS correctly prevented mailbox collision\n";
        std::cout << "      Same [type][sys][inst] generated same mailbox IDs - collision detected!\n";
        return true;
    }
}

bool test_2_different_type_same_system_instance() {
    std::cout << "\n========================================\n";
    std::cout << "Test 2: Different type, same [sys][inst] - NO COLLISION\n";
    std::cout << "  Verify consumers can subscribe to different types\n";
    std::cout << "========================================\n";
    
    ModuleConfig producerA_config{
        .name = "ProducerA",
        .outputs = SimpleOutputConfig{.system_id = 20, .instance_id = 1},
        .inputs = NoInputConfig{},
        .period = 100ms,
        .max_subscribers = 4
    };
    
    ModuleConfig producerB_config{
        .name = "ProducerB",
        .outputs = SimpleOutputConfig{.system_id = 20, .instance_id = 1},  // SAME sys/inst, DIFFERENT type
        .inputs = NoInputConfig{},
        .period = 100ms,
        .max_subscribers = 4
    };
    
    ModuleConfig consumerA_config{
        .name = "ConsumerA",
        .outputs = SimpleOutputConfig{.system_id = 20, .instance_id = 2},
        .inputs = SingleInputConfig{.source_system_id = 20, .source_instance_id = 1},
        .max_subscribers = 1
    };
    
    ModuleConfig consumerB_config{
        .name = "ConsumerB",
        .outputs = SimpleOutputConfig{.system_id = 20, .instance_id = 3},
        .inputs = SingleInputConfig{.source_system_id = 20, .source_instance_id = 1},
        .max_subscribers = 1
    };
    
    try {
        std::cout << "[Test2] Creating ProducerA (SensorA, 20,1)\n";
        ProducerA producerA(producerA_config);
        producerA.start();
        std::cout << "  [OK] ProducerA started\n";
        
        std::cout << "[Test2] Creating ProducerB (SensorB, 20,1) - different type\n";
        ProducerB producerB(producerB_config);
        producerB.start();
        std::cout << "  [OK] ProducerB started (no collision!)\n";
        
        std::this_thread::sleep_for(100ms);
        
        std::cout << "[Test2] Creating ConsumerA (subscribes to SensorA)\n";
        ConsumerA consumerA(consumerA_config);
        consumerA.start();
        std::cout << "  [OK] ConsumerA subscribed\n";
        
        std::cout << "[Test2] Creating ConsumerB (subscribes to SensorB)\n";
        ConsumerB consumerB(consumerB_config);
        consumerB.start();
        std::cout << "  [OK] ConsumerB subscribed\n";
        
        std::this_thread::sleep_for(500ms);
        
        uint32_t countA = consumerA.received_count_.load();
        uint32_t countB = consumerB.received_count_.load();
        
        std::cout << "  [INFO] ConsumerA received " << countA << " messages\n";
        std::cout << "  [INFO] ConsumerB received " << countB << " messages\n";
        
        producerA.stop();
        producerB.stop();
        consumerA.stop();
        consumerB.stop();
        
        if (countA > 0 && countB > 0) {
            std::cout << "\nPASS: Test 2 - Different types, no collision, subscriptions work\n";
            return true;
        } else {
            std::cout << "\nFAIL: Test 2 - Modules created but no data flow (ConsumerA: " 
                      << countA << ", ConsumerB: " << countB << ")\n";
            return false;
        }
        
    } catch (const std::exception& e) {
        std::cout << "  [ERROR] Exception: " << e.what() << "\n";
        std::cout << "\nFAIL: Modules with different types should not collide\n";
        return false;
    }
}

bool test_3_same_type_different_instance() {
    std::cout << "\n========================================\n";
    std::cout << "Test 3: Same type, same sys, different inst - NO COLLISION\n";
    std::cout << "  Verify consumers can subscribe to different instances\n";
    std::cout << "========================================\n";
    
    ModuleConfig producer1_config{
        .name = "ProducerA_inst1",
        .outputs = SimpleOutputConfig{.system_id = 30, .instance_id = 1},
        .inputs = NoInputConfig{},
        .period = 100ms,
        .max_subscribers = 4
    };
    
    ModuleConfig producer2_config{
        .name = "ProducerA_inst2",
        .outputs = SimpleOutputConfig{.system_id = 30, .instance_id = 2},  // DIFFERENT instance
        .inputs = NoInputConfig{},
        .period = 100ms,
        .max_subscribers = 4
    };
    
    ModuleConfig consumer1_config{
        .name = "ConsumerA1",
        .outputs = SimpleOutputConfig{.system_id = 30, .instance_id = 3},
        .inputs = SingleInputConfig{.source_system_id = 30, .source_instance_id = 1},
        .max_subscribers = 1
    };
    
    ModuleConfig consumer2_config{
        .name = "ConsumerA2",
        .outputs = SimpleOutputConfig{.system_id = 30, .instance_id = 4},
        .inputs = SingleInputConfig{.source_system_id = 30, .source_instance_id = 2},
        .max_subscribers = 1
    };
    
    try {
        std::cout << "[Test3] Creating Producer instance 1\n";
        ProducerA producer1(producer1_config);
        producer1.start();
        std::cout << "  [OK] Producer1 started\n";
        
        std::cout << "[Test3] Creating Producer instance 2\n";
        ProducerA producer2(producer2_config);
        producer2.start();
        std::cout << "  [OK] Producer2 started (no collision!)\n";
        
        std::this_thread::sleep_for(100ms);
        
        std::cout << "[Test3] Creating Consumer1 (subscribes to instance 1)\n";
        ConsumerA consumer1(consumer1_config);
        consumer1.start();
        std::cout << "  [OK] Consumer1 subscribed\n";
        
        std::cout << "[Test3] Creating Consumer2 (subscribes to instance 2)\n";
        ConsumerA consumer2(consumer2_config);
        consumer2.start();
        std::cout << "  [OK] Consumer2 subscribed\n";
        
        std::this_thread::sleep_for(500ms);
        
        uint32_t count1 = consumer1.received_count_.load();
        uint32_t count2 = consumer2.received_count_.load();
        
        std::cout << "  [INFO] Consumer1 received " << count1 << " messages\n";
        std::cout << "  [INFO] Consumer2 received " << count2 << " messages\n";
        
        producer1.stop();
        producer2.stop();
        consumer1.stop();
        consumer2.stop();
        
        if (count1 > 0 && count2 > 0) {
            std::cout << "\nPASS: Test 3 - Different instances, no collision, subscriptions work\n";
            return true;
        } else {
            std::cout << "\nFAIL: Test 3 - Modules created but no data flow (Consumer1: " 
                      << count1 << ", Consumer2: " << count2 << ")\n";
            return false;
        }
        
    } catch (const std::exception& e) {
        std::cout << "  [ERROR] Exception: " << e.what() << "\n";
        std::cout << "\nFAIL: Modules with different instances should not collide\n";
        return false;
    }
}

bool test_4_multi_output_same_type_same_address() {
    std::cout << "\n========================================\n";
    std::cout << "Test 4: Multi-output same types, same address - SHOULD COLLIDE\n";
    std::cout << "========================================\n";
    
    ModuleConfig config1{
        .name = "MultiProducer_1",
        .outputs = SimpleOutputConfig{.system_id = 40, .instance_id = 1},
        .inputs = NoInputConfig{},
        .period = 100ms
    };
    
    ModuleConfig config2{
        .name = "MultiProducer_2_COLLISION",
        .outputs = SimpleOutputConfig{.system_id = 40, .instance_id = 1},
        .inputs = NoInputConfig{},
        .period = 100ms
    };
    
    try {
        std::cout << "[Test4] Creating first multi-output module\n";
        MultiProducer producer1(config1);
        producer1.start();
        std::cout << "  [OK] First module started\n";
        
        std::this_thread::sleep_for(200ms);
        
        std::cout << "\n[Test4] Creating SECOND multi-output with SAME address\n";
        MultiProducer producer2(config2);
        producer2.start();  // Should throw
        
        producer1.stop();
        producer2.stop();
        std::cout << "FAIL: Second multi-output should have collided\n";
        return false;
        
    } catch (const std::exception& e) {
        std::cout << "  [OK] Exception caught: " << e.what() << "\n";
        std::cout << "\nPASS: Test 4 - Multi-output collision detected\n";
        return true;
    }
}

bool test_5_multi_output_same_type_different_address() {
    std::cout << "\n========================================\n";
    std::cout << "Test 5: Multi-output same types, different address - NO COLLISION\n";
    std::cout << "========================================\n";
    
    ModuleConfig config1{
        .name = "MultiProducer_A",
        .outputs = SimpleOutputConfig{.system_id = 50, .instance_id = 1},
        .inputs = NoInputConfig{},
        .period = 100ms
    };
    
    ModuleConfig config2{
        .name = "MultiProducer_B",
        .outputs = SimpleOutputConfig{.system_id = 50, .instance_id = 2},  // DIFFERENT instance
        .inputs = NoInputConfig{},
        .period = 100ms
    };
    
    ModuleConfig consumer1_config{
        .name = "ConsumerA1",
        .outputs = SimpleOutputConfig{.system_id = 50, .instance_id = 3},
        .inputs = SingleInputConfig{.source_system_id = 50, .source_instance_id = 1},
        .max_subscribers = 1
    };
    
    ModuleConfig consumer2_config{
        .name = "ConsumerA2",
        .outputs = SimpleOutputConfig{.system_id = 50, .instance_id = 4},
        .inputs = SingleInputConfig{.source_system_id = 50, .source_instance_id = 2},
        .max_subscribers = 1
    };
    
    try {
        std::cout << "[Test5] Creating MultiProducer A\n";
        MultiProducer multi1(config1);
        multi1.start();
        std::cout << "  [OK] MultiProducer A started\n";
        
        std::cout << "[Test5] Creating MultiProducer B\n";
        MultiProducer multi2(config2);
        multi2.start();
        std::cout << "  [OK] MultiProducer B started (no collision!)\n";
        
        std::this_thread::sleep_for(100ms);
        
        std::cout << "[Test5] Creating Consumer1 (subscribes to Multi A's SensorA)\n";
        ConsumerA consumer1(consumer1_config);
        consumer1.start();
        std::cout << "  [OK] Consumer1 subscribed\n";
        
        std::cout << "[Test5] Creating Consumer2 (subscribes to Multi B's SensorA)\n";
        ConsumerA consumer2(consumer2_config);
        consumer2.start();
        std::cout << "  [OK] Consumer2 subscribed\n";
        
        std::this_thread::sleep_for(500ms);
        
        uint32_t count1 = consumer1.received_count_.load();
        uint32_t count2 = consumer2.received_count_.load();
        
        std::cout << "  [INFO] Consumer1 received " << count1 << " messages\n";
        std::cout << "  [INFO] Consumer2 received " << count2 << " messages\n";
        
        multi1.stop();
        multi2.stop();
        consumer1.stop();
        consumer2.stop();
        
        if (count1 > 0 && count2 > 0) {
            std::cout << "\nPASS: Test 5 - Multi-output different addresses, no collision, subscriptions work\n";
            return true;
        } else {
            std::cout << "\nFAIL: Test 5 - Modules created but no data flow (Consumer1: " 
                      << count1 << ", Consumer2: " << count2 << ")\n";
            return false;
        }
        
    } catch (const std::exception& e) {
        std::cout << "  [ERROR] Exception: " << e.what() << "\n";
        std::cout << "\nFAIL: Multi-output modules with different addresses should not collide\n";
        return false;
    }
}

bool test_6_multi_output_partial_collision() {
    std::cout << "\n========================================\n";
    std::cout << "Test 6: Multi-output partial collision with external module\n";
    std::cout << "========================================\n";
    
    ModuleConfig multi_config{
        .name = "MultiProducer",
        .outputs = SimpleOutputConfig{.system_id = 60, .instance_id = 1},
        .inputs = NoInputConfig{},
        .period = 100ms
    };
    
    // Single-output SensorA with SAME address as multi's first output
    ModuleConfig single_config{
        .name = "ProducerA_COLLISION",
        .outputs = SimpleOutputConfig{.system_id = 60, .instance_id = 1},
        .inputs = NoInputConfig{},
        .period = 100ms
    };
    
    try {
        std::cout << "[Test6] Creating multi-output module (SensorA + SensorB)\n";
        MultiProducer multi(multi_config);
        multi.start();
        std::cout << "  [OK] Multi-output started\n";
        
        std::this_thread::sleep_for(200ms);
        
        // ProducerA should collide with MultiProducer's SensorA output
        std::cout << "\n[Test6] Creating ProducerA with SAME [sys][inst]\n";
        std::cout << "        Should collide with multi-output's SensorA mailboxes\n";
        ProducerA single(single_config);
        single.start();  // Should throw
        
        multi.stop();
        single.stop();
        std::cout << "FAIL: ProducerA should collide with MultiProducer's first output\n";
        return false;
        
    } catch (const std::exception& e) {
        std::cout << "  [OK] Exception caught: " << e.what() << "\n";
        std::cout << "\nPASS: Test 6 - Partial collision with multi-output detected\n";
        return true;
    }
}

bool test_7_multi_output_different_types_same_sysid() {
    std::cout << "\n========================================\n";
    std::cout << "Test 7: External modules of different multi-output types - NO COLLISION\n";
    std::cout << "  Verify consumers can subscribe to different types at same [sys][inst]\n";
    std::cout << "========================================\n";
    
    // Multi-output: Outputs<SensorA, SensorB> at [70][1]
    ModuleConfig multi_config{
        .name = "MultiProducer",
        .outputs = SimpleOutputConfig{.system_id = 70, .instance_id = 1},
        .inputs = NoInputConfig{},
        .period = 100ms,
        .max_subscribers = 4
    };
    
    // SensorC at same [sys][inst] but different type
    ModuleConfig sensorc_config{
        .name = "ProducerC",
        .outputs = SimpleOutputConfig{.system_id = 70, .instance_id = 1},  // SAME sys/inst as multi
        .inputs = NoInputConfig{},
        .period = 100ms,
        .max_subscribers = 4
    };
    
    ModuleConfig consumerA_config{
        .name = "ConsumerA",
        .outputs = SimpleOutputConfig{.system_id = 70, .instance_id = 2},
        .inputs = SingleInputConfig{.source_system_id = 70, .source_instance_id = 1},
        .max_subscribers = 1
    };
    
    ModuleConfig consumerC_config{
        .name = "ConsumerC",
        .outputs = SimpleOutputConfig{.system_id = 70, .instance_id = 3},
        .inputs = SingleInputConfig{.source_system_id = 70, .source_instance_id = 1},
        .max_subscribers = 1
    };
    
    try {
        std::cout << "[Test7] Creating MultiProducer (SensorA + SensorB)\n";
        MultiProducer multi(multi_config);
        multi.start();
        std::cout << "  [OK] MultiProducer started\n";
        
        std::cout << "[Test7] Creating ProducerC (SensorC at same [sys][inst])\n";
        ProducerC producerC(sensorc_config);
        producerC.start();
        std::cout << "  [OK] ProducerC started (no collision - different type!)\n";
        
        std::this_thread::sleep_for(100ms);
        
        std::cout << "[Test7] Creating ConsumerA (subscribes to SensorA)\n";
        ConsumerA consumerA(consumerA_config);
        consumerA.start();
        std::cout << "  [OK] ConsumerA subscribed\n";
        
        std::cout << "[Test7] Creating ConsumerC (subscribes to SensorC)\n";
        ConsumerC consumerC(consumerC_config);
        consumerC.start();
        std::cout << "  [OK] ConsumerC subscribed\n";
        
        std::this_thread::sleep_for(500ms);
        
        uint32_t countA = consumerA.received_count_.load();
        uint32_t countC = consumerC.received_count_.load();
        
        std::cout << "  [INFO] ConsumerA received " << countA << " messages\n";
        std::cout << "  [INFO] ConsumerC received " << countC << " messages\n";
        
        multi.stop();
        producerC.stop();
        consumerA.stop();
        consumerC.stop();
        
        if (countA > 0 && countC > 0) {
            std::cout << "\nPASS: Test 7 - Different types at same [sys][inst], no collision, subscriptions work\n";
            return true;
        } else {
            std::cout << "\nFAIL: Test 7 - Modules created but no data flow (ConsumerA: " 
                      << countA << ", ConsumerC: " << countC << ")\n";
            return false;
        }
        
    } catch (const std::exception& e) {
        std::cout << "  [ERROR] Exception: " << e.what() << "\n";
        std::cout << "\nFAIL: Different types should not collide even with same [sys][inst]\n";
        return false;
    }
}

bool test_8_stress_test_many_instances() {
    std::cout << "\n========================================\n";
    std::cout << "Test 8: Stress test - 10 instances of same type\n";
    std::cout << "  Verify consumer can subscribe to one of many instances\n";
    std::cout << "========================================\n";
    
    std::vector<std::unique_ptr<ProducerA>> producers;
    
    for (int i = 0; i < 10; i++) {
        try {
            ModuleConfig config{
                .name = "Producer_" + std::to_string(i),
                .outputs = SimpleOutputConfig{.system_id = 80, .instance_id = static_cast<uint8_t>(i)},
                .inputs = NoInputConfig{},
                .period = 100ms,
                .max_subscribers = 4
            };
            
            auto producer = std::make_unique<ProducerA>(config);
            producer->start();
            producers.push_back(std::move(producer));
            
        } catch (const std::exception& e) {
            std::cout << "FAIL: Failed to create instance " << i << ": " << e.what() << "\n";
            return false;
        }
    }
    
    std::cout << "  [OK] Created 10 producer instances successfully\n";
    
    // Create consumer subscribing to instance 5 (use different system_id to avoid collision)
    ModuleConfig consumer_config{
        .name = "Consumer",
        .outputs = SimpleOutputConfig{.system_id = 81, .instance_id = 1},  // Different system_id!
        .inputs = SingleInputConfig{.source_system_id = 80, .source_instance_id = 5},
        .max_subscribers = 1
    };
    
    try {
        std::cout << "[Test8] Creating consumer subscribing to instance 5\n";
        ConsumerA consumer(consumer_config);
        consumer.start();
        std::cout << "  [OK] Consumer subscribed\n";
        
        std::this_thread::sleep_for(500ms);
        
        uint32_t count = consumer.received_count_.load();
        std::cout << "  [INFO] Consumer received " << count << " messages\n";
        
        consumer.stop();
        
        // Stop all producers
        for (auto& p : producers) {
            p->stop();
        }
        
        if (count > 0) {
            std::cout << "\nPASS: Test 8 - 10 instances no collisions, subscription works\n";
            return true;
        } else {
            std::cout << "\nFAIL: Test 8 - Producers created but no data flow (received: " << count << ")\n";
            return false;
        }
        
    } catch (const std::exception& e) {
        std::cout << "  [ERROR] Consumer exception: " << e.what() << "\n";
        // Stop all producers
        for (auto& p : producers) {
            p->stop();
        }
        return false;
    }
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  CommRaT Address Collision Detection Test Suite           ║\n";
    std::cout << "║  RACK-Style Addressing: [type:8][sys:8][inst:8][mbx:8]    ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n";
    
    int passed = 0;
    int total = 8;
    
    // Run all tests with delays between them
    if (test_1_identical_type_system_instance()) passed++;
    std::this_thread::sleep_for(500ms);
    
    if (test_2_different_type_same_system_instance()) passed++;
    std::this_thread::sleep_for(500ms);
    
    if (test_3_same_type_different_instance()) passed++;
    std::this_thread::sleep_for(500ms);
    
    if (test_4_multi_output_same_type_same_address()) passed++;
    std::this_thread::sleep_for(500ms);
    
    if (test_5_multi_output_same_type_different_address()) passed++;
    std::this_thread::sleep_for(500ms);
    
    if (test_6_multi_output_partial_collision()) passed++;
    std::this_thread::sleep_for(500ms);
    
    if (test_7_multi_output_different_types_same_sysid()) passed++;
    std::this_thread::sleep_for(500ms);
    
    if (test_8_stress_test_many_instances()) passed++;
    
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  Test Results                                              ║\n";
    std::cout << "╠════════════════════════════════════════════════════════════╣\n";
    std::cout << "║  Passed: " << passed << "/" << total << "                                                 ║\n";
    std::cout << "║                                                            ║\n";
    
    if (passed == total) {
        std::cout << "║  [PASS] ALL TESTS PASSED                                   ║\n";
        std::cout << "║                                                            ║\n";
        std::cout << "║  RACK-Style Addressing Validation Complete:                ║\n";
        std::cout << "║  * Same [type][sys][inst] -> Same mailbox ID               ║\n";
        std::cout << "║  * TiMS detects and prevents collisions                    ║\n";
        std::cout << "║  * Different types don't collide                           ║\n";
        std::cout << "║  * Different instances don't collide                       ║\n";
        std::cout << "║  * Multi-output addressing works correctly                 ║\n";
        std::cout << "║  * Consumer subscription and data flow verified            ║\n";
        std::cout << "║  * Format: [type:8][sys:8][inst:8][mbx:8]                 ║\n";
        std::cout << "╚════════════════════════════════════════════════════════════╝\n";
        return 0;
    } else {
        std::cout << "║                                                            ║\n";
        std::cout << "║  [FAIL] SOME TESTS FAILED                                  ║\n";
        std::cout << "║                                                            ║\n";
        std::cout << "║  Check output above for details                            ║\n";
        std::cout << "╚════════════════════════════════════════════════════════════╝\n";
        return 1;
    }
}

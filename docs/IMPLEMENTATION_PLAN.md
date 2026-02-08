# Documentation Implementation Plan

**Date**: February 8, 2026  
**Status**: Ready to Execute  
**Reference**: DOCUMENTATION_STRATEGY.md

## Immediate Actions (Today)

### Step 1: Reorganize Existing Documentation

#### Move docs/work to docs/internal
```bash
mkdir -p docs/internal/design_decisions
mkdir -p docs/internal/phase_history
mkdir -p docs/internal/refactoring

# Move design documents
mv docs/work/RACK_ANALYSIS.md docs/internal/design_decisions/
mv docs/work/IO_SYNC_STRATEGY.md docs/internal/design_decisions/
mv docs/work/SERTIAL_RINGBUFFER_REQUEST.md docs/internal/design_decisions/

# Move phase history
mv docs/work/ARCHITECTURE_ANALYSIS.md docs/internal/phase_history/
mv docs/work/FIXES_APPLIED.md docs/internal/phase_history/

# Move refactoring docs
mv docs/work/ARCHITECTURE_REFACTOR_PLAN.md docs/internal/refactoring/

# Remove empty work directory
rmdir docs/work
```

#### Create README for internal docs
File: `docs/internal/README.md`
Purpose: Explain that internal docs are for developers/contributors

### Step 2: Create User Documentation Structure

```bash
mkdir -p docs/examples/{01_hello_commrat,02_periodic_producer,03_continuous_consumer,04_multi_output,05_multi_input_fusion,06_command_handling,07_timestamp_metadata}
```

### Step 3: Create Placeholder Files

Create empty/skeleton files for immediate structure:
- `docs/USER_GUIDE.md`
- `docs/API_REFERENCE.md`
- `docs/ARCHITECTURE.md`
- `docs/EXAMPLES.md`
- `docs/MIGRATION_GUIDE.md`
- `docs/FAQ.md`
- `docs/CONTRIBUTING.md`
- `docs/examples/README.md`

## Priority 1: User Guide (Next 2 Days)

### USER_GUIDE.md Outline

```markdown
# CommRaT User Guide

## Table of Contents
1. Introduction
   - What is CommRaT?
   - Key Features
   - When to Use CommRaT

2. Core Concepts
   - Messages
   - Modules
   - Mailboxes
   - Message Registry
   - Application Template

3. Your First Module
   - Periodic Producer
   - Simple Consumer
   - Message Definitions

4. Module Types
   - Periodic Input (timer-based)
   - Loop Input (maximum throughput)
   - Continuous Input (message-driven)
   - Multi-Input (synchronized fusion)

5. Input/Output Specifications
   - Single Output: Output<T>
   - Multiple Outputs: Outputs<T1, T2, T3>
   - Single Input: Input<T>
   - Multiple Inputs: Inputs<T1, T2, T3>
   - Primary Input Designation

6. Message Flow
   - Publishing Messages
   - Subscription Protocol
   - Message Delivery Guarantees
   - Thread Safety

7. Multi-Input Synchronization
   - getData Mechanism
   - Timestamp Alignment
   - Tolerance Configuration
   - Primary vs Secondary Inputs

8. Timestamp Management
   - Single Source of Truth (TimsHeader only)
   - Automatic Timestamp Assignment
   - Accessing Input Metadata
   - Freshness Tracking

9. Command Handling
   - Defining Commands
   - on_command() Overrides
   - Command Mailbox Addressing

10. Configuration
    - ModuleConfig Structure
    - System/Instance IDs
    - Period and Timing
    - Source Configuration
    - Sync Tolerance

11. Best Practices
    - Real-Time Constraints
    - Memory Management
    - Threading Model
    - Error Handling
    - Testing Strategies

12. Troubleshooting
    - Common Errors
    - Debugging Techniques
    - Performance Issues
    - Integration Problems
```

### Content Writing Approach

For each section:
1. **Concept**: Clear explanation (2-3 paragraphs)
2. **Example**: Working code snippet
3. **Details**: API specifics, parameters
4. **Common Pitfalls**: What to avoid
5. **Related Topics**: Links to other sections

## Priority 2: Examples (Next 3 Days)

### Example 1: Hello CommRaT (Simplest Possible)

**Goal**: Get user from zero to running code in 5 minutes

```cpp
// hello_commrat.cpp - Simplest CommRaT example

#include <commrat/commrat.hpp>
#include <iostream>

// 1. Define your message
struct HelloMessage {
    uint64_t timestamp;
    int counter;
};

// 2. Create your application
using HelloApp = commrat::CommRaT<
    commrat::Message::Data<HelloMessage>
>;

// 3. Create a producer module
class HelloProducer : public HelloApp::Module<
    commrat::Output<HelloMessage>,
    commrat::PeriodicInput
> {
protected:
    HelloMessage process() override {
        return HelloMessage{
            .timestamp = 0,  // Auto-assigned by framework
            .counter = counter_++
        };
    }
    
private:
    int counter_ = 0;
};

// 4. Create a consumer module
class HelloConsumer : public HelloApp::Module<
    commrat::Output<HelloMessage>,  // Dummy output (required)
    commrat::Input<HelloMessage>
> {
protected:
    HelloMessage process_continuous(const HelloMessage& input) override {
        std::cout << "Received: counter=" << input.counter 
                  << ", timestamp=" << get_input_timestamp<0>() << "\n";
        return input;  // Pass-through
    }
};

int main() {
    // Initialize TiMS
    commrat::initialize_tims();
    
    // Configure producer (publishes every 100ms)
    commrat::ModuleConfig producer_config{
        .name = "HelloProducer",
        .system_id = 10,
        .instance_id = 1,
        .period = commrat::Milliseconds(100)
    };
    
    // Configure consumer
    commrat::ModuleConfig consumer_config{
        .name = "HelloConsumer",
        .system_id = 20,
        .instance_id = 1,
        .source_system_id = 10,
        .source_instance_id = 1
    };
    
    // Create and start modules
    HelloProducer producer(producer_config);
    HelloConsumer consumer(consumer_config);
    
    producer.start();
    consumer.start();
    
    // Run for 5 seconds
    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    // Cleanup
    producer.stop();
    consumer.stop();
    
    std::cout << "Hello CommRaT example complete!\n";
    return 0;
}
```

README.md for this example explains:
- What each section does
- How to compile and run
- Expected output
- What to try next

### Example 2-7: Similar Structure

Each example builds on previous concepts:
- **02**: Periodic with real sensor simulation
- **03**: Processing pipeline (producer → filter → consumer)
- **04**: Multi-output demonstration
- **05**: 3-input sensor fusion (IMU+GPS+Lidar)
- **06**: Command handling pattern
- **07**: Timestamp metadata access

## Priority 3: API Reference (Next 2 Days)

### API_REFERENCE.md Structure

```markdown
# CommRaT API Reference

## Overview
High-level API surface with links to detailed Doxygen docs.

## Core Templates

### CommRaT<MessageDefs...>
Application template combining registry and module/mailbox.

**Synopsis**:
```cpp
using MyApp = commrat::CommRaT<
    commrat::Message::Data<DataType1>,
    commrat::Message::Data<DataType2>,
    commrat::Message::Command<CmdType>
>;
```

**Provides**:
- `MyApp::Module<OutputSpec, InputSpec, ...Commands>`
- `MyApp::Mailbox<T>`
- `MyApp::serialize(msg)` / `deserialize<T>(data)`
- `MyApp::get_message_id<T>()`

[Full documentation: include/commrat/commrat.hpp]

### Module<OutputSpec, InputSpec, ...Commands>
[Details...]

## I/O Specifications
[Details...]

## Message Definitions
[Details...]

## Metadata Accessors
[Details...]

## Configuration
[Details...]

## Platform Abstractions
[Details...]
```

## Priority 4: Architecture Document (Next 1 Day)

### ARCHITECTURE.md for Users

User-friendly explanation:
- Why 3 mailboxes? (separate concerns)
- Why blocking receives? (0% CPU when idle)
- How does subscription work? (WORK mailbox protocol)
- How does multi-input sync work? (getData with timestamps)
- What's the threading model? (3 threads per module)
- Where do timestamps come from? (TimsHeader, single source)

Diagrams (ASCII art):
- Module structure
- Message flow
- Subscription handshake
- Multi-input synchronization

## Priority 5: Internal Documentation (Next 2 Days)

### docs/internal/README.md

```markdown
# Internal Documentation

This directory contains design documents, architecture decisions, and 
development history for CommRaT contributors and maintainers.

**For users**: See docs/USER_GUIDE.md and docs/ARCHITECTURE.md instead.

## Contents

### design_decisions/
Explains why specific technical choices were made:
- RACK-style getData mechanism
- Three-mailbox addressing scheme
- Timestamp management strategy
- SeRTial integration approach

### phase_history/
Development timeline and evolution:
- Phase 5: I/O specifications and modularization
- Phase 6: Multi-input synchronization
- Bug fixes and lessons learned

### refactoring/
Major refactoring documentation:
- Registry module extraction (1,952 → 1,003 lines)
- Template metaprogramming patterns
- Helper base class strategies

## Contributing

See docs/CONTRIBUTING.md for development guidelines.
```

### Rename and Update Internal Docs

- `RACK_ANALYSIS.md` → Add intro explaining RACK system context
- `IO_SYNC_STRATEGY.md` → Update with Phase 6.10 completion status
- `ARCHITECTURE_REFACTOR_PLAN.md` → Mark completed sections
- `FIXES_APPLIED.md` → Convert to bug fix log format

## Execution Timeline

### Day 1 (Today)
- [x] Create DOCUMENTATION_STRATEGY.md
- [x] Create IMPLEMENTATION_PLAN.md
- [x] Reorganize existing docs (move work → internal)
- [x] Create directory structure
- [x] Create placeholder files
- [ ] **Add Doxygen to documentation strategy**

### Day 2
- [ ] Write USER_GUIDE.md sections 1-4
- [ ] Create Example 01 (Hello CommRaT)
- [ ] Update main README.md with new structure
- [ ] **Create initial Doxyfile configuration**

### Day 3
- [ ] Write USER_GUIDE.md sections 5-8
- [ ] Create Examples 02-03
- [ ] Write docs/examples/README.md

### Day 4
- [ ] Write USER_GUIDE.md sections 9-12
- [ ] Create Examples 04-05
- [ ] Write API_REFERENCE.md
- [ ] **Configure Doxygen (Doxyfile)**
- [ ] **Generate initial API docs (`make docs`)**

### Day 5
- [ ] Create Examples 06-07
- [ ] Write ARCHITECTURE.md
- [ ] Write EXAMPLES.md index
- [ ] **Review and update Doxygen comments in headers**
- [ ] **Integrate Doxygen links into API_REFERENCE.md**

### Day 6
- [ ] Organize docs/internal/
- [ ] Write internal README.md
- [ ] Update internal docs with intros
- [ ] **Add Doxygen build target to CMakeLists.txt**

### Day 7
- [ ] Write MIGRATION_GUIDE.md
- [ ] Write CONTRIBUTING.md
- [ ] Write FAQ.md
- [ ] **Create docs/api/README.md (how to generate)**

### Day 8
- [ ] Review all documentation
- [ ] Update cross-references
- [ ] Test examples
- [ ] Final polish
- [ ] **Verify Doxygen output quality**
- [ ] **Update .gitignore for docs/api/html/**

## Success Metrics

After implementation:
- [ ] New user can run Hello CommRaT example in < 10 minutes
- [ ] USER_GUIDE.md answers top 10 questions
- [ ] All 7 examples compile and run correctly
- [ ] API_REFERENCE.md covers all public APIs
- [ ] Internal docs explain design decisions
- [ ] Zero emoji usage throughout
- [ ] All cross-references valid
- [ ] **Doxygen generates clean HTML without warnings**
- [ ] **API reference links to detailed Doxygen pages**
- [ ] **`make docs` command generates complete API documentation**

## Notes

- Keep language simple and direct
- Every concept needs a code example
- Test all examples before documenting
- Get user feedback early and often
- Professional tone throughout (no emojis)

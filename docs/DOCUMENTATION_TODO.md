# Documentation TODO

**Date**: February 12, 2026  
**Status**: In Progress  
**Related**: See [ROADMAP.md](ROADMAP.md) for feature development

This document tracks documentation tasks that need to be completed. For feature development ideas, see ROADMAP.md.

## Documentation Structure

**Current Status**:
- [x] Internal documentation organized (docs/internal/)
- [x] docs/work/ for active development planning (now tracked in git)
- [x] USER_GUIDE.md created (Sections 1-8 complete)
- [x] KNOWN_ISSUES.md for runtime issues and feature gaps
- [x] ROADMAP.md for feature planning with open architectural questions
- [x] Planning documents created for 7 long-term features
- [ ] Complete remaining documentation (see below)

## Needed Documentation

### High Priority

**Runnable Examples** (examples/ and docs/examples/)

**Existing Examples** (examples/):
- [x] continuous_input_example.cpp - Producer-consumer pattern (PeriodicInput → Input)
- [x] clean_interface_example.cpp - Simple API showcase
- [x] command_example.cpp - Command dispatch patterns
- [x] loop_mode_example.cpp - Maximum throughput (LoopInput)
- [x] multi_output_runtime.cpp - Single module, multiple output types
- [x] multi_output_sensor_fusion.cpp - Complete sensor fusion system
- [x] examples/README.md - Overview of all examples

**Existing Documentation Examples** (docs/examples/):
- [x] 01_hello_commrat/ - Simplest possible system with full explanation
- [x] 02_multi_output/ - Multi-output with detailed comments
- [x] 03_multi_input_fusion/ - Multi-input time synchronization
- [x] docs/examples/README.md - Index of documentation examples

**Still Needed**:
- [ ] Example: timestamp_metadata - Using metadata accessors (get_input_metadata, get_input_timestamp)
- [ ] Example: parameter_config - Loading and using module parameters
- [ ] Example: lifecycle_control - Remote start/stop/reset (once lifecycle system implemented)

### Medium Priority

**API Reference** (docs/API_REFERENCE.md)
- CommRaT<> application template
- Module base class and process signatures
- I/O specifications (Input, Output, Inputs, Outputs)
- Metadata accessors
- Configuration structures
- Cross-references to detailed Doxygen docs

**Architecture Guide** (docs/ARCHITECTURE.md)
- MailboxSet architecture (per-output mailbox sets)
- Why 4 mailbox offsets (CMD, WORK, PUBLISH, DATA)
- Subscription protocol flow
- Multi-input synchronization mechanism
- Threading model (command_loop + work_loop + data_thread)
- Message flow diagrams

### Lower Priority

**FAQ** (docs/FAQ.md)
- Common questions and answers
- Troubleshooting tips
- When to use which module type

**Contributing Guide** (docs/CONTRIBUTING.md)
- Development setup
- Code style guidelines
- Testing requirements
- Documentation expectations
- Pull request process

## Implementation Checklist

### Examples (High Priority)
**Completed**:
- [x] examples/continuous_input_example.cpp
- [x] examples/clean_interface_example.cpp
- [x] examples/command_example.cpp
- [x] examples/loop_mode_example.cpp
- [x] examples/multi_output_runtime.cpp
- [x] examples/multi_output_sensor_fusion.cpp
- [x] examples/README.md
- [x] docs/examples/01_hello_commrat/
- [x] docs/examples/02_multi_output/
- [x] docs/examples/03_multi_input_fusion/
- [x] docs/examples/README.md
- [x] CMakeLists.txt integration for all examples
- [x] CTest integration (all 9 examples validated)

**Still Needed**:
- [ ] Create example for timestamp metadata accessors
- [ ] Create example for parameter configuration (once parameter system exists)
- [ ] Create example for lifecycle control (once lifecycle system exists)

### Core Documentation (Medium Priority)
- [ ] Complete USER_GUIDE.md sections 9-12 (Command Handling, Configuration, Best Practices, Troubleshooting)
- [ ] Write API_REFERENCE.md
- [ ] Write ARCHITECTURE.md
- [ ] Configure Doxygen (Doxyfile)
- [ ] Generate API docs (`make docs`)
- [ ] Add Doxygen build target to CMakeLists.txt
- [ ] Review and update Doxygen comments in headers
- [ ] Integrate Doxygen links into API_REFERENCE.md

### Supporting Documentation (Lower Priority)
- [ ] Write FAQ.md
- [ ] Write CONTRIBUTING.md
- [ ] Review all cross-references
- [ ] Final polish and consistency check
- [ ] Create docs/api/README.md (how to generate)
- [ ] Update .gitignore for docs/api/html/
- [ ] Organize docs/internal/
- [ ] Write internal README.md
- [ ] Update internal docs with intros
- [ ] **Add Doxygen build target to CMakeLists.txt**
- [ ] Write CONTRIBUTING.md
- [ ] Write FAQ.md
- [ ] **Create docs/api/README.md (how to generate)**
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
## Success Criteria

Documentation is complete when:
- [ ] New user can run Hello CommRaT example in < 10 minutes
- [ ] USER_GUIDE.md provides comprehensive framework understanding
- [ ] All 7 examples compile and run correctly
- [ ] API_REFERENCE.md covers all public APIs with Doxygen links
- [ ] ARCHITECTURE.md explains design decisions clearly
- [ ] All cross-references are valid
- [ ] Doxygen generates clean HTML without warnings
- [ ] `make docs` command generates complete API documentation

## Success Metrics

Documentation is complete when:
- [x] Examples demonstrate all major features (6 examples + 3 documentation examples)
- [x] Examples integrated into test suite (9 example tests passing)
- [x] examples/README.md provides overview and build instructions
- [ ] New user can run Hello CommRaT example in < 10 minutes
- [ ] USER_GUIDE.md provides comprehensive framework understanding (sections 9-12 complete)
- [ ] API_REFERENCE.md covers all public APIs with Doxygen links
- [ ] ARCHITECTURE.md explains design decisions clearly
- [ ] All cross-references are valid
- [ ] Doxygen generates clean HTML without warnings
- [ ] `make docs` command generates complete API documentation
- [x] Planning documents exist for major future features
- [x] KNOWN_ISSUES.md tracks limitations and feature gaps
- [x] ROADMAP.md includes open architectural questions

## Documentation Guidelines

- **Clarity**: Simple, direct language - avoid jargon
- **Examples**: Every concept needs working code
- **Accuracy**: Test all examples before documenting
- **Professionalism**: No emojis in documentation (except .github/copilot-instructions.md)
- **Completeness**: Cover common use cases and edge cases
## Next Priorities

Based on current state, recommended next steps:

1. **Complete USER_GUIDE.md** (High Priority)
   - Write sections 9-12 (Command Handling, Configuration, Best Practices, Troubleshooting)
   - Provides comprehensive framework documentation
   - Examples already exist to reference

2. **API Reference & Architecture** (Medium Priority)
   - Write API_REFERENCE.md with cross-references
   - Write ARCHITECTURE.md explaining design decisions
   - Configure Doxygen and integrate with documentation

3. **Introspection Implementation** (Foundational Feature)
   - Smallest new feature from ROADMAP.md
   - Required by logger and composer systems
   - See docs/work/INTROSPECTION_INTEGRATION_PLAN.md

**Note**: Examples are largely complete - 6 working examples in examples/ plus 3 detailed documentation examples in docs/examples/, all validated in CTest.
   - Required by logger and composer systems
   - See docs/work/INTROSPECTION_INTEGRATION_PLAN.md
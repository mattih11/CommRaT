# Documentation TODO

**Date**: February 11, 2026  
**Status**: In Progress  
**Related**: See [ROADMAP.md](ROADMAP.md) for feature development

This document tracks documentation tasks that need to be completed. For feature development ideas, see ROADMAP.md.

## Documentation Structure

**Current Status**:
- [x] Internal documentation organized (docs/internal/)
- [x] docs/work/ for active development planning
- [x] USER_GUIDE.md created (Sections 1-8 complete)
- [x] KNOWN_ISSUES.md for runtime issues
- [x] ROADMAP.md for feature planning
- [ ] Complete remaining documentation (see below)

## Needed Documentation

### High Priority

**Runnable Examples** (examples/)
- hello_commrat: Simplest possible producer-consumer
- periodic_producer: Timer-based data generation
- continuous_consumer: Message-driven processing
- multi_output: Multiple message types from one module
- multi_input_fusion: Synchronized sensor fusion (IMU+GPS)
- command_handling: Command dispatch patterns
- timestamp_metadata: Using metadata accessors

**Examples README** (docs/examples/README.md)
- Overview of all examples
- How to build and run
- What each example demonstrates

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

**Migration Guide** (docs/MIGRATION_GUIDE.md)
- Breaking changes between versions
- How to update existing code
- Deprecation notices

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
- [ ] Create examples/hello_commrat/
- [ ] Create examples/periodic_producer/
- [ ] Create examples/continuous_consumer/
- [ ] Create examples/multi_output/
- [ ] Create examples/multi_input_fusion/
- [ ] Create examples/command_handling/
- [ ] Create examples/timestamp_metadata/
- [ ] Write docs/examples/README.md
- [ ] Add CMakeLists.txt for all examples
- [ ] Test all examples build and run

### Core Documentation (Medium Priority)
- [ ] Complete USER_GUIDE.md remaining sections
- [ ] Write API_REFERENCE.md
- [ ] Write ARCHITECTURE.md
- [ ] Configure Doxygen
- [ ] Generate API docs (`make docs`)
- [ ] Add Doxygen build target to CMakeLists.txt

### Supporting Documentation (Lower Priority)
- [ ] Write MIGRATION_GUIDE.md
- [ ] Write FAQ.md
- [ ] Write CONTRIBUTING.md
- [ ] Review all cross-references
- [ ] Final polish and consistency check
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

## Documentation Guidelines

- **Clarity**: Simple, direct language - avoid jargon
- **Examples**: Every concept needs working code
- **Accuracy**: Test all examples before documenting
- **Professionalism**: No emojis in documentation
- **Completeness**: Cover common use cases and edge cases
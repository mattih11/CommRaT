# CommRaT Documentation Strategy

**Date**: February 8, 2026  
**Status**: Planning Phase  
**Current State**: Phase 6.10 Complete (Multi-input with timestamp metadata)

## Overview

This document outlines the documentation strategy for CommRaT after Phase 6.10 completion. The goal is to create clear, accessible, and maintainable documentation that serves both new users and experienced developers.

## Documentation Principles

1. **User-First**: Documentation should match user mental models, not internal implementation
2. **Progressive Disclosure**: Start simple, reveal complexity only when needed
3. **Example-Driven**: Every concept illustrated with working code
4. **No Emojis**: Professional appearance, accessibility-first
5. **Single Source of Truth**: Each concept documented once, linked elsewhere
6. **Up-to-Date**: Documentation updated with code changes

## Current Issues

### Problems with Existing Docs
- `docs/work/` contains internal design documents mixed with user docs
- Historical analysis documents (`RACK_ANALYSIS.md`, `IO_SYNC_STRATEGY.md`) valuable but misplaced
- No clear entry point for new users beyond basic README
- Examples exist but lack comprehensive documentation
- API reference scattered across header comments
- No architecture overview accessible to users

### What Works Well
- `.github/copilot-instructions.md` is comprehensive (for AI, not users)
- Header file Doxygen comments are detailed
- Examples demonstrate real use cases
- `GETTING_STARTED.md` provides basic orientation

## Proposed Documentation Structure

```
docs/
├── README.md                          # Overview, quick links, status
├── GETTING_STARTED.md                 # Installation, first program, basic concepts
├── USER_GUIDE.md                      # NEW: Comprehensive user guide
├── API_REFERENCE.md                   # NEW: API overview with links to headers
├── EXAMPLES.md                        # NEW: Annotated example index
├── ARCHITECTURE.md                    # NEW: High-level architecture for users
├── MIGRATION_GUIDE.md                 # NEW: Upgrading between versions
├── FAQ.md                             # NEW: Common questions and solutions
├── CONTRIBUTING.md                    # NEW: Development guidelines
│
├── examples/                          # Example programs with documentation
│   ├── 01_hello_commrat/             # Simplest possible example
│   ├── 02_periodic_producer/         # Periodic message generation
│   ├── 03_continuous_consumer/       # Message processing pipeline
│   ├── 04_multi_output/              # Multiple output streams
│   ├── 05_multi_input_fusion/        # Sensor fusion (3+ inputs)
│   ├── 06_command_handling/          # Command pattern
│   ├── 07_timestamp_metadata/        # Accessing input metadata
│   └── README.md                      # Example overview
│
├── internal/                          # NEW: Internal design documents
│   ├── README.md                      # Purpose and index
│   ├── phase_history/                 # Historical development
│   │   ├── PHASE_5_MODULARIZATION.md
│   │   ├── PHASE_6_MULTI_INPUT.md
│   │   └── ARCHITECTURE_EVOLUTION.md
│   ├── design_decisions/              # Why we made specific choices
│   │   ├── RACK_STYLE_GETDATA.md
│   │   ├── THREE_MAILBOX_DESIGN.md
│   │   ├── TIMESTAMP_MANAGEMENT.md
│   │   └── SERTIAL_INTEGRATION.md
│   └── refactoring/                   # Refactoring documentation
│       └── REGISTRY_MODULE_EXTRACTION.md
│
└── archive/                           # Historical documents (reference only)
    └── [existing archived docs]
```

## Documentation Content Plan

### 1. User-Facing Documentation (Priority 1)

#### USER_GUIDE.md
Comprehensive guide covering:
- **Core Concepts**: Messages, Modules, Mailboxes, Registries
- **Module Types**: Periodic, Loop, Continuous, Multi-input
- **Message Flow**: Publishing, subscribing, synchronization
- **Multi-Input**: getData synchronization, timestamp alignment
- **Metadata Access**: Input timestamps, freshness flags
- **Configuration**: ModuleConfig options, addressing
- **Best Practices**: Real-time constraints, threading model
- **Troubleshooting**: Common errors and solutions

#### API_REFERENCE.md
High-level API overview:
- `CommRaT<...>` application template
- `Module<OutputSpec, InputSpec, ...Commands>` base class
- I/O specifications: `Output<T>`, `Outputs<Ts...>`, `Input<T>`, `Inputs<Ts...>`
- Metadata accessors: `get_input_metadata<Index>()`, `get_input_timestamp<Type>()`
- Message definitions: `Message::Data<T>`, `Message::Command<T>`
- Links to detailed Doxygen docs in headers

#### EXAMPLES.md
Annotated example catalog:
- What each example demonstrates
- Key concepts illustrated
- Code highlights with explanations
- Expected output
- Common variations

#### ARCHITECTURE.md
User-friendly architecture overview:
- 3-Mailbox system (CMD/WORK/DATA)
- Threading model (command/work/data threads)
- Message ID calculation (compile-time)
- Subscription protocol flow
- Multi-input synchronization mechanism
- Timestamp management (single source of truth)

### 2. Example Programs (Priority 1)

Each example should have:
- `README.md` - What it demonstrates, how to run
- Well-commented source code
- Expected output documented
- Variations/exercises for learning

**Proposed Examples**:
1. **Hello CommRaT**: Simplest periodic publisher + subscriber
2. **Sensor Pipeline**: Producer → Filter → Consumer chain
3. **Multi-Output**: One module publishing multiple types
4. **Sensor Fusion**: 3-input synchronized processing (IMU+GPS+Lidar)
5. **Command Control**: Module responding to commands
6. **Metadata Usage**: Accessing timestamps, freshness checks
7. **Loop Mode**: Maximum throughput demonstration

### 3. Internal Documentation (Priority 2)

Move design documents to `docs/internal/`:
- `RACK_ANALYSIS.md` → `design_decisions/RACK_STYLE_GETDATA.md`
- `IO_SYNC_STRATEGY.md` → `design_decisions/MULTI_INPUT_SYNCHRONIZATION.md`
- `ARCHITECTURE_REFACTOR_PLAN.md` → `refactoring/REGISTRY_MODULE_EXTRACTION.md`
- `FIXES_APPLIED.md` → `phase_history/BUG_FIXES_LOG.md`

Add new internal docs:
- `THREE_MAILBOX_DESIGN.md` - Why separate mailboxes
- `TIMESTAMP_MANAGEMENT.md` - Single source of truth rationale
- `PHASE_6_MULTI_INPUT.md` - Multi-input development journey

### 4. Migration and Maintenance (Priority 3)

#### MIGRATION_GUIDE.md
- Phase 4 → Phase 5 (I/O specifications)
- Phase 5 → Phase 6 (multi-input)
- Breaking changes and migration paths
- Deprecation notices

#### CONTRIBUTING.md
- Code style (from copilot-instructions.md)
- Real-time constraints
- Threading abstractions (must use CommRaT types)
- Template metaprogramming patterns
- Testing requirements
- Documentation requirements

## Implementation Plan

### Week 1: Foundation
- [ ] Create documentation structure (directories)
- [ ] Move existing docs to appropriate locations
- [ ] Write DOCUMENTATION_STRATEGY.md (this document)
- [ ] Update main README.md with new structure

### Week 2: User Documentation
- [ ] Write USER_GUIDE.md (comprehensive)
- [ ] Write API_REFERENCE.md
- [ ] Write ARCHITECTURE.md (user-friendly)
- [ ] Create EXAMPLES.md index

### Week 3: Examples
- [ ] Create example directory structure
- [ ] Document existing examples with README.md
- [ ] Create 01_hello_commrat (new simple example)
- [ ] Create 07_timestamp_metadata (new metadata example)
- [ ] Update all example comments and documentation

### Week 4: Internal & Maintenance
- [ ] Reorganize docs/work → docs/internal
- [ ] Write internal design decision docs
- [ ] Write MIGRATION_GUIDE.md
- [ ] Write CONTRIBUTING.md
- [ ] Write FAQ.md

### Week 5: Polish
- [ ] Review all documentation for consistency
- [ ] Update all cross-references
- [ ] Generate API docs with Doxygen
- [ ] User testing with documentation
- [ ] Final review and publish

## Success Criteria

Documentation is successful when:
1. New user can build first module in < 30 minutes
2. API reference answers common questions without reading source
3. Examples cover 90% of use cases
4. Internal docs explain design decisions to future developers
5. Zero emoji usage (professional appearance)
6. All docs tested against actual code (no outdated info)

## Maintenance

- Update documentation in same PR as code changes
- Quarterly review of examples for accuracy
- User feedback tracked in GitHub issues
- Versioned documentation for major releases

## Doxygen Integration

### Approach: Hybrid Documentation

**Hand-Written** (docs/):
- User guides and tutorials
- Architecture overviews
- Example walkthroughs
- Getting started guides

**Doxygen Generated** (from source comments):
- Detailed API reference
- Class/function documentation
- Template parameter descriptions
- Code cross-references

### Doxygen Configuration

Create `Doxyfile` in project root with:
- `OUTPUT_DIRECTORY = docs/api/`
- `GENERATE_HTML = YES`
- `GENERATE_LATEX = NO`
- `EXTRACT_ALL = NO` (only documented entities)
- `EXTRACT_PRIVATE = NO` (public API only)
- `JAVADOC_AUTOBRIEF = YES` (first line is brief)
- `INLINE_INHERITED_MEMBERS = YES`
- `ENABLE_PREPROCESSING = YES` (handle templates)
- `MACRO_EXPANSION = YES`
- `PREDEFINED = DOXYGEN_SHOULD_SKIP_THIS` (skip implementation details)

### Documentation Comments

Already exist in headers, ensure consistency:
```cpp
/**
 * @brief Short description (one line)
 * 
 * Detailed description with usage examples.
 * Can span multiple paragraphs.
 * 
 * @tparam T Template parameter description
 * @param param Parameter description
 * @return Return value description
 * 
 * @note Important notes
 * @warning Warnings about usage
 * @see Related functions
 * 
 * @example
 * @code
 * // Usage example
 * MyClass obj;
 * obj.method();
 * @endcode
 */
```

### Integration with API_REFERENCE.md

API_REFERENCE.md becomes the **entry point**:
- High-level overview of APIs
- Conceptual organization
- Links to detailed Doxygen pages
- Quick reference tables

Example structure:
```markdown
## Module Class

The Module base class provides the framework for message processing.

**Quick Reference**:
- `Module<OutputSpec, InputSpec, ...Commands>` - Template signature
- `start()` - Begin module execution
- `stop()` - Graceful shutdown
- `process()` - Override for periodic/loop modules
- `process_continuous()` - Override for continuous input

[Detailed API Documentation →](api/html/classcommrat_1_1Module.html)
```

### Build Integration

Add Doxygen generation to CMakeLists.txt:
```cmake
# Find Doxygen
find_package(Doxygen OPTIONAL_COMPONENTS dot)

if(DOXYGEN_FOUND)
    # Configure Doxyfile
    set(DOXYGEN_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/docs/api)
    set(DOXYGEN_INPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
    
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in
                   ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile @ONLY)
    
    # Add target
    add_custom_target(docs
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )
    
    message(STATUS "Doxygen found. Run 'make docs' to generate documentation.")
else()
    message(STATUS "Doxygen not found. API documentation will not be generated.")
endif()
```

### .gitignore Updates

Ignore generated documentation:
```
# Doxygen output
docs/api/html/
docs/api/latex/
docs/api/xml/
```

Keep `docs/api/.gitkeep` or a README explaining how to generate.

## Open Questions

1. ~~Should we generate API docs with Doxygen or write by hand?~~
   - **RESOLVED**: Hybrid approach - Doxygen for detailed API, hand-written overviews
2. Where should tutorial-style guides live?
   - Recommendation: examples/ with extensive README.md files
3. Do we need video tutorials?
   - Recommendation: Start with text, add video later if needed
4. Should internal docs be public?
   - Recommendation: Yes, in docs/internal/ - transparency helps contributors
5. Should Doxygen output be committed to git?
   - Recommendation: No, generate on-demand. Provide instructions in docs/api/README.md

## References

- Current README.md: Overview and quick start
- GETTING_STARTED.md: Installation and first program
- Examples directory: Working code demonstrations

# Documentation TODO

**Date**: February 12, 2026  
**Status**: In Progress

## Completed

- External examples (examples/ directory): 6 working examples, all in CTest
- Documentation examples (docs/examples/): 3 annotated walkthroughs, all in CTest
- examples/README.md, docs/examples/README.md
- KNOWN_ISSUES.md, ROADMAP.md, USER_GUIDE.md (sections 1-8)
- Planning documents for major future features (docs/work/)

## Needed

### High Priority

#### Examples
- `timestamp_metadata` - using `get_input_timestamp`, `has_new_data`, metadata accessors
- `parameter_config` - loading and using module parameters (once parameter system exists)
- `lifecycle_control` - remote start/stop/reset (once lifecycle system implemented)

#### Core Documentation
- Complete USER_GUIDE.md sections 9-12 (Command Handling, Configuration, Best Practices, Troubleshooting)
- Write API_REFERENCE.md covering all public CommRaT APIs
- Write ARCHITECTURE.md explaining 3-mailbox design, threading model, subscription protocol

### Medium Priority
- Configure Doxygen (Doxyfile already exists) and add `make docs` target to CMakeLists.txt
- Review and update Doxygen comments in public headers
- Update .gitignore for docs/api/html/

### Lower Priority
- Write FAQ.md
- Write CONTRIBUTING.md (setup, code style, testing, PR process)
- Review all cross-references for accuracy

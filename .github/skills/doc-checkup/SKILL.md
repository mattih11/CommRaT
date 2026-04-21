---
name: doc-checkup
description: "Documentation checkup before committing. Use when: about to commit, after implementing a feature, after changing headers or CMakeLists, after updating APIs or build options. Reviews all .md files against recent code changes and verifies copilot-instructions.md, ARCHITECTURE.md, API_REFERENCE.md, GETTING_STARTED.md, ROADMAP.md, README.md are consistent with the current codebase."
argument-hint: "optional: specific area to check (e.g. 'platform', 'ci', 'api')"
---

# Documentation Checkup

Pre-commit review of all project documentation against recent code changes.

## When to Use

- Before making a `git commit` (especially after feature work)
- After changing any public API, header, or CMakeLists.txt
- After adding or removing files from the project structure
- After completing a milestone or resolving a known issue

## Procedure

### 1. Identify What Changed

Run `git diff --name-only HEAD` (or against a specific range) to list changed files.
Group by category:
- **Headers** (`include/**/*.hpp`) — check API_REFERENCE.md and copilot-instructions.md
- **CMakeLists.txt** — check GETTING_STARTED.md, README.md, ARCHITECTURE.md, copilot-instructions.md
- **CI / scripts** — check ROADMAP.md, ARCHITECTURE.md, copilot-instructions.md (Completed/In Progress)
- **New files added** — check directory structure in copilot-instructions.md
- **Known issues resolved** — check KNOWN_ISSUES.md status fields
- **Features completed** — check ROADMAP.md and copilot-instructions.md Completed Features list

### 2. Check Each Doc File

For each of the following, verify it reflects the current state of the code:

| File | What to Verify |
|------|----------------|
| `.github/copilot-instructions.md` | Status line, Completed/In Progress lists, Directory Structure, Platform Abstraction section |
| `docs/ARCHITECTURE.md` | Platform table, file structure diagram, threading model |
| `docs/API_REFERENCE.md` | Platform Selection section, any changed class/function signatures |
| `docs/GETTING_STARTED.md` | Build commands, prerequisites, code examples |
| `docs/ROADMAP.md` | Move completed items, update In Progress, add new Planned items |
| `docs/KNOWN_ISSUES.md` | Mark resolved issues, add newly discovered ones |
| `docs/work/EVL_API_REFERENCE.md` | CMake build integration section (uses `COMMRAT_PLATFORM=EVL`, not manual defines) |
| `docs/work/PLATFORM_ABSTRACTION_LAYER.md` | Implementation status, phase completion |
| `docs/README.md` | Platform abstraction one-liner, feature list |
| `README.md` | Quick start build commands, feature bullets |
| `examples/README.md` | Build commands, list of examples |

### 3. Apply Fixes

Edit only the files where content is stale or incorrect. Do not rewrite sections that are already accurate.

Rules:
- No emojis anywhere (use plain text: VALID/WARNING/ERROR/NOTE)
- No phase references ("Phase 6.x") — describe features directly
- CMake option is `COMMRAT_PLATFORM=STD/EVL`, not raw `COMMRAT_PLATFORM_STD/EVL`
- Dependencies (RACK, SeRTial, reflectcpp, libevl) are system-installed, not submodules
- Directory structure in copilot-instructions.md must match actual repo layout
- EVL backend: always note `#error` stub status until implemented

### 4. Verify Build Still Works

```bash
cmake -B build 2>&1 | grep -E "error|warning|platform"
cmake --build build --parallel $(nproc) 2>&1 | tail -5
```

### 5. Commit in Two Stages (if needed)

If both code and docs changed, use separate commits:
```
feat/fix: <what changed in code>
docs: update <which docs> for <what changed>
```

If only docs changed:
```
docs: <summary of what was updated and why>
```

## Quick Checklist

Before committing, verify:
- [ ] `copilot-instructions.md` Current Status line is accurate
- [ ] Completed Features list includes everything that is done
- [ ] In Progress list reflects only what is actually in progress
- [ ] Directory Structure matches actual repo (no phantom tims/ or SeRTial/ dirs)
- [ ] Build commands in all docs use `-B build` style (not `cd build && cmake ..`)
- [ ] `COMMRAT_PLATFORM` cmake variable used, not bare compile definitions
- [ ] KNOWN_ISSUES.md: resolved items marked, new items added
- [ ] ROADMAP.md: last updated date is current

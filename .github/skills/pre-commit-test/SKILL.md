---
name: pre-commit-test
description: "Pre-commit testing before committing. Use when: about to commit, after editing source files, after changing CMakeLists.txt, after editing CI workflow files, after changing Doxyfile or headers. Runs only the tests relevant to what changed: build+ctest for source/headers, cmake reconfigure for CMakeLists, YAML validation for CI workflows, doxygen check for doc-related changes, shellcheck for CI scripts."
argument-hint: "optional: specific area to test (e.g. 'build', 'doxygen', 'ci')"
---

# Pre-Commit Testing

Run only the tests that are relevant to what changed. Never run everything blindly.

## Step 1: Identify Changed Files

```bash
git diff --name-only          # unstaged
git diff --cached --name-only # staged
git diff --name-only HEAD     # all since last commit
```

Map each changed file to a test category using the table below.

## Test Category Map

| Changed files | Test category |
|---|---|
| `src/*.cpp`, `include/**/*.hpp` | **build** + **ctest** |
| `CMakeLists.txt`, `examples/CMakeLists.txt`, `test/CMakeLists.txt` | **cmake-reconfigure** + **build** + **ctest** |
| `include/commrat/platform/**` | **build** + **ctest** + **evl-compile** |
| `.github/workflows/*.yml` | **yaml-lint** |
| `scripts/ci/*.sh` | **shell-syntax** |
| `Doxyfile`, `include/**/*.hpp`, `docs/**/*.md` | **doxygen** |
| `cmake/*.cmake.in` | **cmake-reconfigure** + **build** |

If no files match any category, no tests are needed.

## Test Procedures

### build + ctest (source or header changes)

**Prerequisite**: `corerat-router-tcp` must be running before ctest. Tests that open
TiMS mailboxes will abort or fail immediately if the router is not up.

```bash
# Start the router if not already running (in a separate terminal or background)
tims_router_tcp &
TIMS_PID=$!

cd /home/muddy/src/CommRaT
cmake --build build --parallel $(nproc) 2>&1 | tail -5
ctest --test-dir build --output-on-failure 2>&1 | tail -20

# Stop router if we started it
kill $TIMS_PID 2>/dev/null || true
```

To check if it is already running:
```bash
pgrep -x corerat-router-tcp && echo "running" || echo "not running"
```

Pass criteria: zero build errors, all tests pass (or known-failing tests still fail as expected).

### cmake-reconfigure (CMakeLists.txt changes)

**Prerequisite**: `corerat-router-tcp` must be running (see build + ctest above).

```bash
cd /home/muddy/src/CommRaT
cmake -B build 2>&1 | grep -E "error|warning|platform|PLATFORM"
cmake --build build --parallel $(nproc) 2>&1 | tail -5
ctest --test-dir build --output-on-failure 2>&1 | tail -20
```

Pass criteria: no CMake errors, `-- CommRaT platform: STD (standard Linux)` present, build clean.

### evl-compile (platform header changes)

```bash
cd /home/muddy/src/CommRaT
cmake -B build-evl -DCOMMRAT_PLATFORM=EVL -DCOMMRAT_BUILD_TESTS=OFF -DCOMMRAT_BUILD_EXAMPLES=OFF 2>&1 | tail -5
cmake --build build-evl 2>&1 | grep -E "error:|#error" | head -10
rm -rf build-evl
```

Pass criteria: expected to fail with `#error` on EVL stubs — confirm it fails at the right point
(threading_impl.hpp or timestamp_impl.hpp), not from a CMake or link error.

### yaml-lint (workflow file changes)

```bash
python3 -c "
import yaml, sys
for f in sys.argv[1:]:
    try:
        yaml.safe_load(open(f))
        print(f'OK: {f}')
    except yaml.YAMLError as e:
        print(f'ERROR: {f}: {e}')
        sys.exit(1)
" .github/workflows/*.yml
```

If `python3` + `yaml` unavailable, fall back to:
```bash
for f in .github/workflows/*.yml; do
    python3 -c "import yaml; yaml.safe_load(open('$f'))" && echo "OK: $f" || echo "FAIL: $f"
done
```

Pass criteria: all workflow files parse without YAML errors.

### shell-syntax (CI script changes)

```bash
# Syntax check (always available)
bash -n scripts/ci/run-evl-tests.sh && echo "OK: syntax valid"

# Full lint if shellcheck installed
if command -v shellcheck &>/dev/null; then
    shellcheck scripts/ci/run-evl-tests.sh
fi
```

Pass criteria: `bash -n` exits 0. Shellcheck warnings are advisory (SC2086 etc. acceptable if intentional).

### doxygen (Doxyfile or header changes)

```bash
cd /home/muddy/src/CommRaT
doxygen Doxyfile 2>&1 | grep -E "warning:|error:" | head -20
```

Pass criteria: zero errors. Warnings about undocumented members in internal headers are acceptable;
warnings about public API members in `include/commrat/` should be fixed.

## Step 3: Report

After running, state clearly:
- Which categories were tested
- Pass / fail per category
- Any unexpected failures that should block the commit
- Failures that are expected (e.g. EVL `#error` stubs)

## Quick Reference: Category by Scenario

| What you did | Run |
|---|---|
| Fixed a bug in a header | build + ctest |
| Added a new test | cmake-reconfigure + ctest |
| Changed CMakeLists.txt | cmake-reconfigure + build + ctest |
| Edited CI workflow | yaml-lint |
| Edited platform headers | build + ctest + evl-compile |
| Updated Doxyfile | doxygen |
| Edited run-evl-tests.sh | shell-syntax |
| Only edited .md files | nothing (no build artifact impact) |

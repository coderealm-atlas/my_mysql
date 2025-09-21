# Test Coverage Guide

This document explains how to build, run, generate, and interpret C++ test coverage for this project. It is focused on GCC + gcov/gcovr instrumentation that is already wired into the CMake build.

## Overview

We support line coverage (and implicitly function coverage) via GCC's `--coverage` flags (gcov). A dedicated CMake option `ENABLE_COVERAGE` instruments all targets when ON. A custom `coverage` target (if `gcovr` is installed) produces consolidated HTML, XML, and text reports.

Key points:
- Use GCC for coverage. Disable `FORCE_CLANG` when configuring.
- Build type should generally be `Debug` (optimization disabled) for accurate line mapping.
- Run the tests before invoking the `coverage` target so `.gcda` data files are generated.
- Coverage artifacts are ignored by `.gitignore` (patterns for `*.gcov` etc.) to avoid repository clutter.

## Prerequisites

1. GCC toolchain (g++, gcov) – version corresponding to your system (e.g., GCC 13+).
2. `gcovr` (optional but recommended) for aggregated HTML/XML reports.
   - Install via pip: `pip install gcovr`
   - Or via system package manager (e.g., Ubuntu): `sudo apt-get install gcovr`
3. Existing project dependencies resolved through vcpkg / CPM as usual.

## Configuration Matrix

| Purpose | Option | Value |
|---------|--------|-------|
| Enable GCC coverage instrumentation | `-DENABLE_COVERAGE` | `ON` |
| Force clang (should be OFF for coverage) | `-DFORCE_CLANG` | `OFF` |
| Build type (recommended) | `-DCMAKE_BUILD_TYPE` | `Debug` |

## One-Time Configure (Separate Build Directory)

It's strongly recommended to use a **separate build directory** for coverage to keep instrumentation isolated:

```bash
cmake -S . -B build-coverage -DENABLE_COVERAGE=ON -DFORCE_CLANG=OFF -DCMAKE_BUILD_TYPE=Debug
```

Notes:
- If you previously configured with clang in `build/`, don't reuse that directory for coverage.
- You can add other options (triplets/toolchain) as needed; ensure they still resolve to GCC.

## Build All Targets

```bash
cmake --build build-coverage -j
```

This compiles sources with `--coverage -O0 -g` injected (see `CMakeLists.txt`). Object files and linked binaries now produce `.gcda` execution data when run.

## Run Tests (Generate .gcda)

From the coverage build directory:

```bash
ctest --test-dir build-coverage --output-on-failure
```

Or directly run test executables (examples – adjust if names differ):

```bash
./build-coverage/tests/my_mysql_test
./build-coverage/tests/sakila_integration_test
```

After successful execution you should see `.gcda` and `.gcno` files scattered under `build-coverage/` mirroring object file locations.

## Generate Aggregated Coverage Report (gcovr target)

If `gcovr` is present, a custom target named `coverage` is available:

```bash
cmake --build build-coverage --target coverage
```

This will create a `coverage/` directory inside `build-coverage/` containing:
- `index.html` (detailed HTML report with per-file drill-down)
- `coverage.xml` (Cobertura XML format for CI tools)
- `summary.txt` (plain-text summary suitable for quick inspection / logs)

The `gcovr` invocation (defined in `CMakeLists.txt`) applies filters:
- `--filter <project_root>/include` restricts to public/project headers (adjust if you add more top-level source paths)
- `--exclude` patterns remove `vcpkg_installed`, other build dirs, and test sources from metrics.

To view HTML coverage locally:

```bash
xdg-open build-coverage/coverage/index.html  # Linux
# or: open build-coverage/coverage/index.html  # macOS
```

### One-Step Full Pipeline (coverage_full target)

If you prefer a single command that (re)configures (if necessary), builds, runs tests, and generates reports, use the custom target `coverage_full` (added on top of the lower-level `coverage` target). This wraps the script `scripts/run_coverage.sh`:

```bash
cmake --build build-coverage --target coverage_full
```

You can also call the script directly for more options:

```bash
bash scripts/run_coverage.sh --help
```

Common script flags:
- `--with-src` Include both `include/` and `src/` paths in coverage.
- `--clean-data` Remove existing `.gcda` before test run (fresh measurement).
- `--reconfigure` Force a fresh CMake configure (deletes `build-coverage`).
- `--parallel[=N]` Pass through parallel build switch.
- `--stop-on-test-fail` Abort after failing tests (default is to proceed and still emit reports).
- `--cmake-arg=...` Forward extra arguments to CMake on (re)configure.

Example including sources and forcing reconfigure:

```bash
cmake -E chdir . cmake --build build-coverage --target coverage_full -- -j
# or direct script usage
bash scripts/run_coverage.sh --with-src --reconfigure --parallel=8
```

## Interpreting Results

`summary.txt` example (structure):
```
------------------------------------------------------------------------------
File                                   Lines    Exec  Cover   Missing
include/mysql_base.hpp                  169      119  70.4%   (list of line numbers)
... (other files) ...
------------------------------------------------------------------------------
TOTAL                                   XXXX     YYYY  ZZ.Z%
```

Focus guidance:
- Low coverage in error-handling branches is common; add targeted tests if those paths are critical.
- Headers aggregating templated or inline logic (e.g., `mysql_base.hpp`) may dominate line counts.
- If coverage seems unexpectedly low, ensure tests actually executed (look for zeroed `.gcda` timestamps) and that optimization is off.

## Manual gcov (Optional / Debugging)

If you need raw `.gcov` files for a specific source:

```bash
cd build-coverage
gcov -p -o CMakeFiles/<target>.dir/path/to/source.cpp.o  ../../path/to/source.cpp
```

However, `gcovr` already automates collection; direct `gcov` usage is rarely needed unless debugging anomalies.

## Cleaning Coverage Artifacts

To reset instrumentation data without a full rebuild (e.g., before a second measurement run):

```bash
find build-coverage -name '*.gcda' -delete
```

To remove the entire coverage build:

```bash
rm -rf build-coverage
```

Because `.gcov` output files appear in the **source root** when you run `gcov` directly, they are ignored by `.gitignore`. Prefer using the `coverage` target (which leaves artifacts inside `build-coverage/coverage/`). Remove stray top-level `*.gcov` with:

```bash
rm -f *.gcov
```

## Common Issues & Remedies

| Symptom | Cause | Fix |
|---------|-------|-----|
| Coverage target missing | `gcovr` not installed | Install `gcovr` and re-run `cmake --build ... --target coverage` |
| All files 0% coverage | Tests not executed / wrong build dir | Re-run tests in the instrumented build directory (`build-coverage`) before `coverage` target |
| Some sources absent | Filters exclude them | Adjust `--filter` / `--exclude` in `CMakeLists.txt` (or extend) |
| Mixed compiler warnings about `--coverage` | Using Clang with coverage ON | Reconfigure with `-DFORCE_CLANG=OFF` to force GCC |
| Stale line numbers after refactor | Old `.gcda` files | Delete `*.gcda` (see cleaning) and re-run tests |
| Assertion failures only under coverage | Timing sensitive / instrumentation overhead | Investigate async races; optionally re-run with `ASAN` off or add synchronization |

## Adding More Source Paths to Coverage

Currently only `include/` is filtered in. If you want to include `src/` C++ implementation files, modify the `gcovr` command in `CMakeLists.txt`:

```cmake
--filter "${CMAKE_SOURCE_DIR}/(include|src)"
```

Be mindful of regex escaping in CMake. After editing, re-run:

```bash
cmake --build build-coverage --target coverage --clean-first
```

## Integrating with CI

You can upload `coverage.xml` to services expecting Cobertura format (e.g., GitLab, Jenkins plugins, some GitHub Actions). Outline:
1. Configure coverage build in CI job.
2. Run tests (`ctest`).
3. Build `coverage` target.
4. Archive / publish `build-coverage/coverage/index.html` and parse `coverage.xml`.

Example (pseudo GitHub Actions step):
```yaml
- name: Configure (coverage)
  run: cmake -S . -B build-coverage -DENABLE_COVERAGE=ON -DFORCE_CLANG=OFF -DCMAKE_BUILD_TYPE=Debug
- name: Build
  run: cmake --build build-coverage -j
- name: Test
  run: ctest --test-dir build-coverage --output-on-failure
- name: Coverage
  run: cmake --build build-coverage --target coverage
- name: Upload artifact
  uses: actions/upload-artifact@v4
  with:
    name: coverage-html
    path: build-coverage/coverage
```

## Advanced: Temporary Suppression / Exclusion of Code

To exclude specific code blocks from coverage (e.g., defensive branches), you can use gcov pragmas:
```cpp
// LCOV_EXCL_START
... rarely executed code ...
// LCOV_EXCL_STOP
```
If you adopt lcov tooling later, these markers are honored. `gcovr` also supports `// GCOVR_EXCL_START/STOP` and `// GCOVR_EXCL_LINE`.

## Next Steps / Improving Coverage

- Add tests for error branches in `expect_one_value<T>` when types mismatch.
- Exercise SSL branch in `params()` by supplying a config with `ssl > 0` and valid base64 cert/key data.
- Add negative tests for index out-of-bounds and multi-result mismatch cases.
- Investigate (and fix) async session leak assertion that appears only under instrumentation to ensure accurate metrics.

---
Maintainers: Update this guide as new targets, filters, or tooling (e.g., clang-based source-based coverage) are introduced.

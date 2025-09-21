#!/usr/bin/env bash
set -euo pipefail

# -----------------------------------------------------------------------------
# run_coverage.sh
# One-shot helper to:
#   1. Configure a separate coverage build directory (GCC)
#   2. Build instrumented targets
#   3. Run tests (ctest) to produce .gcda data
#   4. Generate gcovr HTML/XML/TXT reports (if gcovr installed)
#   5. Print summary coverage percentage
# Re-runnable: detects existing build directory and can optionally clean data.
# -----------------------------------------------------------------------------

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")"/.. && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build-coverage"
CMAKE_BUILD_TYPE="Debug"
CMAKE_GENERATOR_ARGS=()
COVERAGE_FILTER_INCLUDE="${PROJECT_ROOT}/include"
INCLUDE_SRC=0   # set to 1 via --with-src to include src/ as well
CLEAN_GCDA=0    # set to 1 via --clean-data to remove old .gcda before running tests
PARALLEL=""    # e.g. -j or -jN
ADDITIONAL_CMAKE_ARGS=()
STOP_ON_TEST_FAIL=0  # default: continue to produce coverage even if tests fail

usage() {
  cat <<EOF
Usage: $0 [options]

Options:
  --with-src            Include both include/ and src/ in coverage filter.
  --clean-data          Delete existing *.gcda before running tests.
  --reconfigure         Force a fresh CMake configure (deletes build dir).
  --parallel[=N]        Pass -j (or -jN) to build.
  --cmake-arg=ARG       Extra argument passed to CMake (repeatable).
  --stop-on-test-fail   Exit immediately if ctest reports failures.
  --help                Show this help.

Examples:
  $0 --with-src --clean-data --parallel=8
  $0 --cmake-arg=-DMY_FLAG=ON
EOF
}

for arg in "$@"; do
  case "$arg" in
    --with-src) INCLUDE_SRC=1 ;;
    --clean-data) CLEAN_GCDA=1 ;;
    --reconfigure) RECONFIGURE=1 ;;
    --parallel) PARALLEL="-j" ;;
    --parallel=*) PARALLEL="-j${arg#*=}" ;;
    --cmake-arg=*) ADDITIONAL_CMAKE_ARGS+=("${arg#*=}") ;;
    --stop-on-test-fail) STOP_ON_TEST_FAIL=1 ;;
    --help) usage; exit 0 ;;
    *) echo "Unknown argument: $arg" >&2; usage; exit 1 ;;
  esac
done

RECONFIGURE=${RECONFIGURE:-0}

log() { printf '[run_coverage] %s\n' "$*"; }
warn() { printf '[run_coverage][WARN] %s\n' "$*" >&2; }
err()  { printf '[run_coverage][ERROR] %s\n' "$*" >&2; exit 1; }

command -v gcov >/dev/null || err "gcov not found (need GCC toolchain)."
if ! command -v gcovr >/dev/null; then
  warn "gcovr not found: XML/HTML/TXT reports will NOT be generated. Install gcovr to enable (apt-get install -y gcovr or pip install gcovr)."
fi

if [[ ${RECONFIGURE} -eq 1 ]]; then
  log "Removing existing build directory for fresh configure." 
  rm -rf "${BUILD_DIR}" || true
fi

if [[ ! -d "${BUILD_DIR}" ]]; then
  log "Configuring coverage build directory: ${BUILD_DIR}" 
  cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" \
    -DENABLE_COVERAGE=ON \
    -DFORCE_CLANG=OFF \
    -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}" \
    "${ADDITIONAL_CMAKE_ARGS[@]}"
else
  log "Using existing build directory (pass --reconfigure to recreate)."
fi

log "Building (coverage instrumented) targets." 
cmake --build "${BUILD_DIR}" ${PARALLEL}

if [[ ${CLEAN_GCDA} -eq 1 ]]; then
  log "Cleaning existing .gcda files." 
  find "${BUILD_DIR}" -name '*.gcda' -delete || true
fi

log "Running tests (ctest)." 
set +e
ctest --test-dir "${BUILD_DIR}" --output-on-failure
CTEST_STATUS=$?
set -e
if [[ $CTEST_STATUS -ne 0 ]]; then
  warn "Tests reported failures (exit=$CTEST_STATUS)."
  if [[ ${STOP_ON_TEST_FAIL} -eq 1 ]]; then
    err "Aborting because --stop-on-test-fail provided."
  else
    log "Continuing to generate coverage anyway."
  fi
fi

FILTER_ARG=("--filter" "${COVERAGE_FILTER_INCLUDE}")
if [[ ${INCLUDE_SRC} -eq 1 ]]; then
  FILTER_ARG=("--filter" "${PROJECT_ROOT}/(include|src)")
fi

COVERAGE_OUT_DIR="${BUILD_DIR}/coverage"
mkdir -p "${COVERAGE_OUT_DIR}"

if command -v gcovr >/dev/null; then
  log "Running gcovr aggregation." 
  # Build exclusion arguments
  EXCLUDES=(
    "--exclude" ".*vcpkg_installed/.*"
    "--exclude" ".*/tests/.*"
    "--exclude" ".*build-.*"
  )
  # Generate all reports
  set +e
  gcovr -r "${PROJECT_ROOT}" "${FILTER_ARG[@]}" "${EXCLUDES[@]}" \
    --html-details -o "${COVERAGE_OUT_DIR}/index.html" \
    --xml -o "${COVERAGE_OUT_DIR}/coverage.xml" \
    --txt -o "${COVERAGE_OUT_DIR}/summary.txt" \
    --print-summary > "${COVERAGE_OUT_DIR}/console_summary.txt"
  GCOVR_STATUS=$?
  set -e
  if [[ $GCOVR_STATUS -ne 0 ]]; then
    warn "gcovr exited with status $GCOVR_STATUS (reports may be incomplete)."
  fi
  if [[ -f "${COVERAGE_OUT_DIR}/summary.txt" ]]; then
    log "Coverage summary (from summary.txt):" 
    awk 'NR<=50' "${COVERAGE_OUT_DIR}/summary.txt" | sed 's/^/[coverage] /'
  else
    warn "summary.txt not generated (filter may have excluded all files)."
  fi
else
  warn "Skipping gcovr report generation (gcovr not installed)."
fi

log "Done. Outputs (if gcovr present):"
log "  HTML:   ${COVERAGE_OUT_DIR}/index.html"
log "  XML:    ${COVERAGE_OUT_DIR}/coverage.xml"
log "  TXT:    ${COVERAGE_OUT_DIR}/summary.txt"
log "  Console summary: ${COVERAGE_OUT_DIR}/console_summary.txt"

exit 0

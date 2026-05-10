#!/usr/bin/env bash
set -euo pipefail

# Run from an MSYS2 UCRT64 shell.
#
# This helper mirrors the local workflow shape from the trading-system repo:
# 1. optionally stage required third-party source trees
# 2. optionally build the local dependency prefix under dependencies/ucrt64
# 3. configure a repo-local CMake build directory
# 4. build the parser/solver targets
# 5. run tests and a small solver smoke check
#
# Environment overrides:
#   BUILD_DIR=/abs/path/to/build-dir
#   BUILD_TYPE=Debug
#   GENERATOR=Ninja
#   CLEAN_BUILD=0
#   STAGE_THIRD_PARTY=1
#   BUILD_DEPENDENCIES=0
#   RUN_TESTS=1
#   RUN_SMOKE=1

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build-ucrt}"
BUILD_TYPE="${BUILD_TYPE:-Debug}"
GENERATOR="${GENERATOR:-Ninja}"
STAGE_THIRD_PARTY="${STAGE_THIRD_PARTY:-1}"
BUILD_DEPENDENCIES="${BUILD_DEPENDENCIES:-0}"
RUN_TESTS="${RUN_TESTS:-1}"
RUN_SMOKE="${RUN_SMOKE:-1}"
DEPS_PREFIX="${ROOT_DIR}/dependencies/ucrt64/install"

ensure_tool() {
  local tool="$1"
  if ! command -v "${tool}" >/dev/null 2>&1; then
    echo "ERROR: required tool '${tool}' is not available in PATH."
    exit 1
  fi
}

cpu_count() {
  if command -v nproc >/dev/null 2>&1; then
    nproc
    return
  fi
  echo 1
}

if [[ -x /ucrt64/bin/gcc.exe ]]; then
  export PATH="/ucrt64/bin:${PATH}"
  export CC="${CC:-$(cygpath -m /ucrt64/bin/gcc.exe)}"
  export CXX="${CXX:-$(cygpath -m /ucrt64/bin/g++.exe)}"
fi

ensure_tool cmake
ensure_tool ctest

if [[ "${STAGE_THIRD_PARTY}" == "1" ]]; then
  echo "==> Staging third-party source trees"
  chmod +x "${ROOT_DIR}/scripts/stage_third_party_sources_ucrt.sh"
  "${ROOT_DIR}/scripts/stage_third_party_sources_ucrt.sh"
fi

if [[ "${BUILD_DEPENDENCIES}" == "1" ]]; then
  echo "==> Building UCRT dependency prefix"
  chmod +x "${ROOT_DIR}/scripts/build_third_party_dependencies_ucrt.sh"
  "${ROOT_DIR}/scripts/build_third_party_dependencies_ucrt.sh"
fi

if [[ "${CLEAN_BUILD:-0}" == "1" ]]; then
  echo "==> Removing ${BUILD_DIR}"
  rm -rf "${BUILD_DIR}"
fi

configure_args=(
  -S "${ROOT_DIR}"
  -B "${BUILD_DIR}"
  -G "${GENERATOR}"
  "-DSYGUS_BUILD_TESTS=ON"
  "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}"
)

if [[ -n "${CXX:-}" ]]; then
  configure_args+=("-DCMAKE_CXX_COMPILER=${CXX}")
fi

if [[ -d "${DEPS_PREFIX}" ]]; then
  configure_args+=("-DCMAKE_PREFIX_PATH=${DEPS_PREFIX}")
  configure_args+=("-DSYGUS_REQUIRE_CVC5=ON")
fi

if [[ "$#" -gt 0 ]]; then
  configure_args+=("$@")
fi

echo "==> Configuring ${BUILD_DIR}"
cmake "${configure_args[@]}"

echo "==> Building targets"
cmake --build "${BUILD_DIR}" -j"$(cpu_count)"

if [[ "${RUN_TESTS}" == "1" ]]; then
  echo "==> Running tests"
  ctest --test-dir "${BUILD_DIR}" --output-on-failure
fi

if [[ "${RUN_SMOKE}" == "1" ]]; then
  solver_cli="${BUILD_DIR}/sygus_solve"
  if [[ -x "${solver_cli}.exe" ]]; then
    solver_cli="${solver_cli}.exe"
  fi

  if [[ -x "${solver_cli}" ]]; then
    echo "==> Smoke solving tests/data/lia_max2.sl"
    if [[ -d "${DEPS_PREFIX}" ]]; then
      "${solver_cli}" --json "${ROOT_DIR}/tests/data/lia_max2.sl"
    else
      "${solver_cli}" --json --no-cvc5-verify \
        "${ROOT_DIR}/tests/data/lia_max2.sl"
    fi
  else
    echo "WARNING: solver CLI was not found under ${BUILD_DIR}; skipping smoke run."
  fi
fi

echo "Done."
echo "  build dir: ${BUILD_DIR}"

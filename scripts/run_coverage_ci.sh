#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build-coverage"
PREFIX_PATH="${CMAKE_PREFIX_PATH:-}"

if ! command -v cmake >/dev/null 2>&1; then
  echo "cmake is not available in PATH"
  exit 1
fi

if ! command -v lcov >/dev/null 2>&1; then
  echo "lcov is not available in PATH"
  exit 1
fi

if ! command -v genhtml >/dev/null 2>&1; then
  echo "genhtml is not available in PATH"
  exit 1
fi

rm -rf "${BUILD_DIR}"

if [[ -z "${PREFIX_PATH}" && -d "${ROOT_DIR}/dependencies/linux/install" ]]; then
  PREFIX_PATH="${ROOT_DIR}/dependencies/linux/install"
fi
cmake_args=(
  -S "${ROOT_DIR}"
  -B "${BUILD_DIR}"
  -D SYGUS_BUILD_TESTS=ON
  -D SYGUS_BUILD_SOLVER=ON
  -D SYGUS_ENABLE_COVERAGE=ON
  -D CMAKE_BUILD_TYPE=Debug
)
if [[ -n "${PREFIX_PATH}" ]]; then
  cmake_args+=("-DCMAKE_PREFIX_PATH=${PREFIX_PATH}")
  cmake_args+=("-DSYGUS_REQUIRE_CVC5=ON")
fi

cmake "${cmake_args[@]}"

cmake --build "${BUILD_DIR}" -j"$(nproc)"
ctest --test-dir "${BUILD_DIR}" --output-on-failure

lcov --capture --directory "${BUILD_DIR}" --output-file "${BUILD_DIR}/coverage.info"
lcov --remove "${BUILD_DIR}/coverage.info" \
  '/usr/*' \
  '*/tests/*' \
  '*/build-coverage/*' \
  --output-file "${BUILD_DIR}/coverage.filtered.info"
genhtml "${BUILD_DIR}/coverage.filtered.info" --output-directory "${BUILD_DIR}/coverage-html"

echo "Coverage report generated under ${BUILD_DIR}/coverage-html"

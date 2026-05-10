#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if ! command -v clang-format >/dev/null 2>&1; then
  echo "clang-format is not available in PATH"
  exit 1
fi

# Keep this curated list aligned with scripts/check_clang_format.sh so we do
# not accidentally reformat legacy solver code that is still being migrated.
cpp_files=(
  "${ROOT_DIR}/include/invariant.h"
  "${ROOT_DIR}/include/sygus_ast.hpp"
  "${ROOT_DIR}/include/sygus_parser.hpp"
  "${ROOT_DIR}/src/sygus_parser.cpp"
  "${ROOT_DIR}/src/sygus_parse_main.cpp"
  "${ROOT_DIR}/tests/test_harness.hpp"
  "${ROOT_DIR}/tests/sygus_ut.cpp"
  "${ROOT_DIR}/tests/sygus_mt.cpp"
)

if [[ "${#cpp_files[@]}" -eq 0 ]]; then
  echo "No C++ files found to format."
  exit 0
fi

clang-format -i -style=file "${cpp_files[@]}"
echo "Formatted ${#cpp_files[@]} files using .clang-format"

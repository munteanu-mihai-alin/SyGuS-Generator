#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if ! command -v clang-format >/dev/null 2>&1; then
  echo "clang-format is not available in PATH"
  exit 1
fi

cpp_files=(
  "${ROOT_DIR}/include/invariant.h"
  "${ROOT_DIR}/include/sygus_ast.hpp"
  "${ROOT_DIR}/include/sygus_parser.hpp"
  "${ROOT_DIR}/include/sygus_solver.hpp"
  "${ROOT_DIR}/src/sygus_parser.cpp"
  "${ROOT_DIR}/src/sygus_parse_main.cpp"
  "${ROOT_DIR}/src/sygus_solver.cpp"
  "${ROOT_DIR}/src/sygus_solve_main.cpp"
  "${ROOT_DIR}/tests/test_harness.hpp"
  "${ROOT_DIR}/tests/sygus_ut.cpp"
  "${ROOT_DIR}/tests/sygus_mt.cpp"
)

if [[ "${#cpp_files[@]}" -eq 0 ]]; then
  echo "No C++ files found to format-check."
  exit 0
fi

tmp_dir="$(mktemp -d)"
trap 'rm -rf "${tmp_dir}"' EXIT

failed=0
for file in "${cpp_files[@]}"; do
  rel_path="${file#"${ROOT_DIR}/"}"
  formatted_file="${tmp_dir}/$(basename "${file}")"
  clang-format -style=file "${file}" > "${formatted_file}"
  if ! cmp -s "${file}" "${formatted_file}"; then
    echo "Formatting differs for ${rel_path}"
    failed=1
  fi
done

if [[ "${failed}" -ne 0 ]]; then
  echo "Run clang-format on the files above."
  exit 1
fi

echo "clang-format check passed."

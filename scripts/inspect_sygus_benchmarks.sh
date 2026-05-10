#!/usr/bin/env bash
set -euo pipefail

# Run after ./scripts/stage_third_party_sources_ucrt.sh.
#
# Usage:
#   ./scripts/inspect_sygus_benchmarks.sh
#   ./scripts/inspect_sygus_benchmarks.sh 2019

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BENCHMARKS_DIR="${ROOT_DIR}/third-party/sygus-benchmarks"
COMP_DIR="${BENCHMARKS_DIR}/comp"

benchmark_file_count() {
  local search_root="$1"
  find "${search_root}" -type f \
    \( -name '*.sl' -o -name '*.sl.gz' -o -name '*.sy' -o -name '*.sygus' -o -name '*.smt2' \) \
    | wc -l | tr -d ' '
}

require_benchmarks_tree() {
  if [[ ! -d "${COMP_DIR}" ]]; then
    echo "ERROR: ${COMP_DIR} does not exist."
    echo "Run ./scripts/stage_third_party_sources_ucrt.sh first."
    exit 1
  fi
}

print_year_overview() {
  echo "SyGuS competition benchmark years under ${COMP_DIR}:"

  local found_any=0
  while IFS= read -r year_dir; do
    found_any=1
    local year_name
    local benchmark_count
    local group_count
    year_name="$(basename "${year_dir}")"
    benchmark_count="$(benchmark_file_count "${year_dir}")"
    group_count="$(find "${year_dir}" -mindepth 1 -maxdepth 1 -type d | wc -l | tr -d ' ')"
    printf '  %s  benchmark-files=%-6s top-level-groups=%s\n' \
      "${year_name}" "${benchmark_count}" "${group_count}"
  done < <(find "${COMP_DIR}" -mindepth 1 -maxdepth 1 -type d | sort)

  if [[ "${found_any}" -eq 0 ]]; then
    echo "  none"
  fi
}

print_year_details() {
  local requested_year="$1"
  local year_dir="${COMP_DIR}/${requested_year}"

  if [[ ! -d "${year_dir}" ]]; then
    echo "ERROR: ${year_dir} does not exist."
    echo "Run without arguments to see the available years."
    exit 1
  fi

  echo "SyGuS competition benchmark summary for ${requested_year}:"
  echo "  total benchmark files: $(benchmark_file_count "${year_dir}")"
  echo "  top-level groups:"

  local found_group=0
  while IFS= read -r group_dir; do
    found_group=1
    local group_name
    local benchmark_count
    group_name="$(basename "${group_dir}")"
    benchmark_count="$(benchmark_file_count "${group_dir}")"
    printf '    %-32s %s\n' "${group_name}" "${benchmark_count}"
  done < <(find "${year_dir}" -mindepth 1 -maxdepth 1 -type d | sort)

  if [[ "${found_group}" -eq 0 ]]; then
    echo "    (no top-level subdirectories found; benchmarks may be stored directly under the year directory)"
  fi
}

require_benchmarks_tree

if [[ $# -eq 0 ]]; then
  print_year_overview
elif [[ $# -eq 1 ]]; then
  print_year_details "$1"
else
  echo "ERROR: expected zero or one argument."
  echo "Usage: ./scripts/inspect_sygus_benchmarks.sh [year]"
  exit 1
fi

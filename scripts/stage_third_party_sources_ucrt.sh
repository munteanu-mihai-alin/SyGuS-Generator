#!/usr/bin/env bash
set -euo pipefail

# Run from an MSYS2 UCRT64 shell.
#
# This script stages source archives under third-party/_downloads and then
# unpacks the finished source trees into third-party/.
#
# Default trees:
#   third-party/cvc5
#   third-party/sygus-benchmarks
#
# Environment overrides:
#   CVC5_VERSION=cvc5-1.2.1
#   CVC5_ARCHIVE_URL=https://github.com/cvc5/cvc5/archive/refs/tags/cvc5-1.2.1.tar.gz
#   SYGUS_BENCHMARKS_REF=master
#   SYGUS_BENCHMARKS_ARCHIVE_URL=https://github.com/SyGuS-Org/benchmarks/archive/refs/heads/master.tar.gz
#   FORCE_RESTAGE_THIRD_PARTY=1
#
# The default cvc5 version is pinned to 1.2.1 because this repository already
# contains a cvc5 1.2.1 binary/include drop and still uses the older deprecated
# C++ API style that remained compatible with that release line.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
THIRD_PARTY_DIR="${ROOT_DIR}/third-party"
DOWNLOADS_DIR="${THIRD_PARTY_DIR}/_downloads"

CVC5_VERSION="${CVC5_VERSION:-cvc5-1.2.1}"
CVC5_ARCHIVE_URL="${CVC5_ARCHIVE_URL:-https://github.com/cvc5/cvc5/archive/refs/tags/${CVC5_VERSION}.tar.gz}"
SYGUS_BENCHMARKS_REF="${SYGUS_BENCHMARKS_REF:-master}"
SYGUS_BENCHMARKS_ARCHIVE_URL="${SYGUS_BENCHMARKS_ARCHIVE_URL:-https://github.com/SyGuS-Org/benchmarks/archive/refs/heads/${SYGUS_BENCHMARKS_REF}.tar.gz}"
FORCE_RESTAGE_THIRD_PARTY="${FORCE_RESTAGE_THIRD_PARTY:-0}"

CVC5_DIR="${THIRD_PARTY_DIR}/cvc5"
SYGUS_BENCHMARKS_DIR="${THIRD_PARTY_DIR}/sygus-benchmarks"

mkdir -p "${THIRD_PARTY_DIR}" "${DOWNLOADS_DIR}"

status_line() {
  local name="$1"
  local status="$2"
  local action="$3"
  printf '==> %-20s status=%-8s action=%s\n' "${name}" "${status}" "${action}"
}

ensure_tool() {
  local tool="$1"
  if ! command -v "${tool}" >/dev/null 2>&1; then
    echo "ERROR: required tool '${tool}' is not available in PATH."
    exit 1
  fi
}

download_archive() {
  local url="$1"
  local destination="$2"
  echo "    downloading ${url}"
  curl -fL --retry 3 --retry-delay 2 -o "${destination}" "${url}"
}

single_subdirectory() {
  local parent="$1"
  local entries=()

  while IFS= read -r entry; do
    entries+=("${entry}")
  done < <(find "${parent}" -mindepth 1 -maxdepth 1 -type d | sort)

  if [[ "${#entries[@]}" -ne 1 ]]; then
    return 1
  fi

  printf '%s\n' "${entries[0]}"
}

extract_tarball_to_target() {
  local name="$1"
  local archive_path="$2"
  local extract_dir="$3"
  local target_dir="$4"

  rm -rf "${extract_dir}" "${target_dir}"
  mkdir -p "${extract_dir}"
  tar -xzf "${archive_path}" -C "${extract_dir}"

  local extracted_root
  if ! extracted_root="$(single_subdirectory "${extract_dir}")"; then
    echo "ERROR: expected exactly one extracted root for ${name} under ${extract_dir}"
    find "${extract_dir}" -mindepth 1 -maxdepth 2 | sort || true
    exit 1
  fi

  mv "${extracted_root}" "${target_dir}"
  rm -rf "${extract_dir}"
}

has_cvc5() {
  [[ -f "${CVC5_DIR}/CMakeLists.txt" ]] &&
  [[ -f "${CVC5_DIR}/include/cvc5/cvc5.h" ]] &&
  [[ -d "${CVC5_DIR}/src" ]]
}

has_sygus_benchmarks() {
  [[ -f "${SYGUS_BENCHMARKS_DIR}/README.md" ]] &&
  [[ -d "${SYGUS_BENCHMARKS_DIR}/comp" ]] &&
  [[ -d "${SYGUS_BENCHMARKS_DIR}/lib" ]]
}

benchmark_file_count() {
  local search_root="$1"
  find "${search_root}" -type f \
    \( -name '*.sl' -o -name '*.sl.gz' -o -name '*.sy' -o -name '*.sygus' -o -name '*.smt2' \) \
    | wc -l | tr -d ' '
}

print_competition_years() {
  if [[ ! -d "${SYGUS_BENCHMARKS_DIR}/comp" ]]; then
    echo "==> No SyGuS competition benchmark year directories found yet."
    return 0
  fi

  echo "==> SyGuS competition benchmark years detected:"

  local any_year=0
  while IFS= read -r year_dir; do
    any_year=1
    local year_name
    local year_count
    year_name="$(basename "${year_dir}")"
    year_count="$(benchmark_file_count "${year_dir}")"
    printf '    %s  benchmark-files=%s\n' "${year_name}" "${year_count}"
  done < <(find "${SYGUS_BENCHMARKS_DIR}/comp" -mindepth 1 -maxdepth 1 -type d | sort)

  if [[ "${any_year}" -eq 0 ]]; then
    echo "    none"
  fi
}

stage_cvc5() {
  local archive_path="${DOWNLOADS_DIR}/cvc5-${CVC5_VERSION}.tar.gz"
  local extract_dir="${DOWNLOADS_DIR}/cvc5.extract"

  rm -f "${archive_path}"
  download_archive "${CVC5_ARCHIVE_URL}" "${archive_path}"
  extract_tarball_to_target "cvc5" "${archive_path}" "${extract_dir}" "${CVC5_DIR}"

  if ! has_cvc5; then
    echo "ERROR: downloaded cvc5 tree is invalid at ${CVC5_DIR}"
    exit 1
  fi
}

stage_sygus_benchmarks() {
  local archive_path="${DOWNLOADS_DIR}/sygus-benchmarks-${SYGUS_BENCHMARKS_REF}.tar.gz"
  local extract_dir="${DOWNLOADS_DIR}/sygus-benchmarks.extract"

  rm -f "${archive_path}"
  download_archive "${SYGUS_BENCHMARKS_ARCHIVE_URL}" "${archive_path}"
  extract_tarball_to_target "sygus-benchmarks" "${archive_path}" "${extract_dir}" "${SYGUS_BENCHMARKS_DIR}"

  if ! has_sygus_benchmarks; then
    echo "ERROR: downloaded SyGuS benchmark tree is invalid at ${SYGUS_BENCHMARKS_DIR}"
    exit 1
  fi
}

ensure_tool curl
ensure_tool tar

if [[ "${FORCE_RESTAGE_THIRD_PARTY}" == "1" ]]; then
  echo "==> FORCE_RESTAGE_THIRD_PARTY=1: removing staged source trees"
  rm -rf "${CVC5_DIR}" "${SYGUS_BENCHMARKS_DIR}"
fi

echo "==> Staging third-party sources under ${THIRD_PARTY_DIR}"

if has_cvc5; then
  status_line "cvc5" "present" "keep existing"
else
  status_line "cvc5" "missing" "download to third-party/_downloads then unpack into third-party/cvc5"
  stage_cvc5
fi

if has_sygus_benchmarks; then
  status_line "sygus-benchmarks" "present" "keep existing"
else
  status_line "sygus-benchmarks" "missing" "download to third-party/_downloads then unpack into third-party/sygus-benchmarks"
  stage_sygus_benchmarks
fi

print_competition_years

echo "Done."
echo "  cvc5 source: ${CVC5_DIR}"
echo "  benchmarks:  ${SYGUS_BENCHMARKS_DIR}"
echo "  downloads:   ${DOWNLOADS_DIR}"
echo "Use ./scripts/inspect_sygus_benchmarks.sh [year] for a per-year summary."

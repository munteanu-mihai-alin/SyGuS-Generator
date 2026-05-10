#!/usr/bin/env bash
set -euo pipefail

# Build the reusable Linux cvc5 dependency bundle for CI.
#
# Produces:
#   dependencies/linux/install
#   dependencies/linux/sygus-linux-deps-ubuntu-latest.tar.gz
#
# Environment overrides:
#   FORCE_REBUILD_DEPS=1
#   DRY_RUN=1
#   SKIP_STAGE=1
#   CVC5_CONFIG=production
#   CVC5_EXTRA_CONFIGURE_ARGS="--assertions"

ROOT_DIR="${ROOT_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
THIRD_PARTY_DIR="${ROOT_DIR}/third-party"
DEPS_DIR="${ROOT_DIR}/dependencies/linux"
BUILD_DIR="${DEPS_DIR}/build"
INSTALL_DIR="${DEPS_DIR}/install"
ARCHIVE_PATH="${DEPS_DIR}/sygus-linux-deps-ubuntu-latest.tar.gz"
PYTHON_VENV_DIR="${DEPS_DIR}/python-venv"
CVC5_SRC="${THIRD_PARTY_DIR}/cvc5"
CVC5_BUILD_DIR="${BUILD_DIR}/cvc5"
STAGE_SCRIPT="${ROOT_DIR}/scripts/stage_third_party_sources_ucrt.sh"

FORCE_REBUILD_DEPS="${FORCE_REBUILD_DEPS:-0}"
DRY_RUN="${DRY_RUN:-0}"
SKIP_STAGE="${SKIP_STAGE:-0}"
CVC5_CONFIG="${CVC5_CONFIG:-production}"
CVC5_EXTRA_CONFIGURE_ARGS="${CVC5_EXTRA_CONFIGURE_ARGS:-}"

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

prepare_python_env() {
  if [[ -n "${PYTHON_EXECUTABLE:-}" ]]; then
    echo "==> Using caller-supplied Python: ${PYTHON_EXECUTABLE}"
    return
  fi

  local system_python
  system_python="$(command -v python3)"
  if "${system_python}" -c "import pyparsing" >/dev/null 2>&1; then
    PYTHON_EXECUTABLE="${system_python}"
    echo "==> Reusing system Python with pyparsing: ${PYTHON_EXECUTABLE}"
    return
  fi

  echo "==> Creating repo-local Python venv for cvc5 helpers"
  rm -rf "${PYTHON_VENV_DIR}"
  "${system_python}" -m venv "${PYTHON_VENV_DIR}"

  local venv_python="${PYTHON_VENV_DIR}/bin/python"
  if [[ -x "${venv_python}.exe" ]]; then
    venv_python="${venv_python}.exe"
  fi

  "${venv_python}" -m pip install pyparsing
  PYTHON_EXECUTABLE="${venv_python}"
  echo "==> Using repo-local Python: ${PYTHON_EXECUTABLE}"
}

has_installed_cvc5() {
  [[ -d "${INSTALL_DIR}/include/cvc5" ]] &&
  ([[ -f "${INSTALL_DIR}/lib/libcvc5.a" ]] ||
   [[ -f "${INSTALL_DIR}/bin/cvc5" ]])
}

ensure_tool bash
ensure_tool cmake
ensure_tool python3
ensure_tool tar
ensure_tool gzip

if [[ "${SKIP_STAGE}" != "1" ]]; then
  echo "==> Staging third-party source trees"
  chmod +x "${STAGE_SCRIPT}"
  "${STAGE_SCRIPT}"
fi

if [[ ! -f "${CVC5_SRC}/configure.sh" ]]; then
  echo "ERROR: missing cvc5 source tree at ${CVC5_SRC}"
  exit 1
fi

if [[ "${FORCE_REBUILD_DEPS}" != "1" ]] && [[ -f "${ARCHIVE_PATH}" ]] &&
   has_installed_cvc5; then
  echo "==> Reusing existing Linux dependency bundle at ${ARCHIVE_PATH}"
  echo "Done."
  echo "  archive: ${ARCHIVE_PATH}"
  exit 0
fi

declare -a extra_args=()
if [[ -n "${CVC5_EXTRA_CONFIGURE_ARGS}" ]]; then
  # shellcheck disable=SC2206
  extra_args=(${CVC5_EXTRA_CONFIGURE_ARGS})
fi

echo "==> Linux dependency root: ${DEPS_DIR}"
echo "==> cvc5 source: ${CVC5_SRC}"
echo "==> archive path: ${ARCHIVE_PATH}"

if [[ "${DRY_RUN}" == "1" ]]; then
  echo "DRY_RUN=1 -> would build cvc5 into ${INSTALL_DIR} and archive ${ARCHIVE_PATH}"
  echo "  ${CVC5_SRC}/configure.sh ${CVC5_CONFIG} --ninja --static --auto-download --no-pyvenv --no-poly --prefix=${INSTALL_DIR} --name=${CVC5_BUILD_DIR} -DPython_EXECUTABLE=<prepared-python> ${CVC5_EXTRA_CONFIGURE_ARGS}"
  exit 0
fi

rm -rf "${CVC5_BUILD_DIR}" "${INSTALL_DIR}" "${ARCHIVE_PATH}"
mkdir -p "${BUILD_DIR}" "${INSTALL_DIR}"

prepare_python_env

echo "==> Tool versions"
which cmake || true
cmake --version | head -n 1 || true
which python3 || true
python3 --version || true
which g++ || true
g++ --version | head -n 1 || true

echo "==> Configuring cvc5 (${CVC5_CONFIG})"
(
  cd "${CVC5_SRC}"
  ./configure.sh "${CVC5_CONFIG}" \
    --ninja \
    --static \
    --auto-download \
    --no-pyvenv \
    --no-poly \
    --prefix="${INSTALL_DIR}" \
    --name="${CVC5_BUILD_DIR}" \
    "-DPython_EXECUTABLE=${PYTHON_EXECUTABLE}" \
    "${extra_args[@]}"
)

echo "==> Building cvc5"
cmake --build "${CVC5_BUILD_DIR}" -j"$(cpu_count)"

echo "==> Installing cvc5 into ${INSTALL_DIR}"
cmake --install "${CVC5_BUILD_DIR}"

if ! has_installed_cvc5; then
  echo "ERROR: cvc5 install prefix is incomplete under ${INSTALL_DIR}"
  exit 1
fi

echo "==> Creating archive ${ARCHIVE_PATH}"
tar -czf "${ARCHIVE_PATH}" -C "${ROOT_DIR}" dependencies/linux/install

echo "Done."
echo "  archive: ${ARCHIVE_PATH}"

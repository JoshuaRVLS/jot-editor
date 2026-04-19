#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
DEFAULT_HOME="${HOME}"
if [[ "${EUID}" -eq 0 ]] && [[ -n "${SUDO_USER:-}" ]] && command -v getent >/dev/null 2>&1; then
  SUDO_HOME="$(getent passwd "${SUDO_USER}" | cut -d: -f6 || true)"
  if [[ -n "${SUDO_HOME}" ]]; then
    DEFAULT_HOME="${SUDO_HOME}"
  fi
fi

INSTALL_PREFIX="${DEFAULT_HOME}/.local"
BUILD_TYPE="Release"
RUN_TESTS=0
USE_SUDO=0
INSTALL_LSP=1
JOBS=""
PREFIX_EXPLICIT=0

print_help() {
  cat <<'USAGE'
Usage: ./install.sh [options]

Build and install jot using CMake.

Options:
  --prefix <path>       Install prefix (default: $HOME/.local)
  --build-dir <path>    Build directory (default: ./build)
  --debug               Build with Debug configuration
  --release             Build with Release configuration (default)
  --run-tests           Run CTest after building
  --skip-lsp            Skip auto-install of built-in LSP servers
  --sudo                Run install step with sudo
  -j, --jobs <N>        Parallel build jobs (default: auto)
  -h, --help            Show this help message

Examples:
  ./install.sh
  ./install.sh --prefix /usr/local --sudo
  ./install.sh --skip-lsp
  ./install.sh --build-dir ./build_release --release -j 8
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --prefix)
      [[ $# -ge 2 ]] || { echo "Missing value for --prefix" >&2; exit 1; }
      INSTALL_PREFIX="$2"
      PREFIX_EXPLICIT=1
      shift 2
      ;;
    --build-dir)
      [[ $# -ge 2 ]] || { echo "Missing value for --build-dir" >&2; exit 1; }
      BUILD_DIR="$2"
      shift 2
      ;;
    --debug)
      BUILD_TYPE="Debug"
      shift
      ;;
    --release)
      BUILD_TYPE="Release"
      shift
      ;;
    --run-tests)
      RUN_TESTS=1
      shift
      ;;
    --skip-lsp)
      INSTALL_LSP=0
      shift
      ;;
    --sudo)
      USE_SUDO=1
      shift
      ;;
    -j|--jobs)
      [[ $# -ge 2 ]] || { echo "Missing value for $1" >&2; exit 1; }
      JOBS="$2"
      shift 2
      ;;
    -h|--help)
      print_help
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      print_help
      exit 1
      ;;
  esac
done

if [[ "${EUID}" -eq 0 ]] && [[ "${PREFIX_EXPLICIT}" -eq 0 ]]; then
  if [[ "${INSTALL_PREFIX}" == "/root/.local" ]] && [[ -n "${SUDO_USER:-}" ]]; then
    echo "[jot] Running as root via sudo, using ${DEFAULT_HOME}/.local for ${SUDO_USER}"
  elif [[ "${INSTALL_PREFIX}" == "/root/.local" ]]; then
    echo "[jot] Warning: install prefix is /root/.local"
    echo "[jot] If this is not intended, run as your normal user or pass --prefix <path>."
  fi
fi

if ! command -v cmake >/dev/null 2>&1; then
  echo "Error: cmake not found in PATH" >&2
  exit 1
fi

if [[ "${USE_SUDO}" -eq 1 ]] && ! command -v sudo >/dev/null 2>&1; then
  echo "Error: --sudo requested but sudo is not available" >&2
  exit 1
fi

run_maybe_sudo() {
  if [[ "${USE_SUDO}" -eq 1 ]]; then
    sudo "$@"
  else
    "$@"
  fi
}

run_as_default_user() {
  if [[ "${EUID}" -eq 0 ]] && [[ -n "${SUDO_USER:-}" ]]; then
    sudo -u "${SUDO_USER}" "$@"
  else
    "$@"
  fi
}

attempt_cmd() {
  local desc="$1"
  shift
  echo "[jot:lsp] ${desc}"
  if "$@"; then
    echo "[jot:lsp] OK: ${desc}"
    return 0
  fi
  echo "[jot:lsp] Failed: ${desc}" >&2
  return 1
}

install_python_lsp() {
  if command -v pylsp >/dev/null 2>&1; then
    echo "[jot:lsp] pylsp already installed"
    return 0
  fi

  # Prefer distro package managers first (works cleanly with PEP 668 environments).
  if command -v pacman >/dev/null 2>&1; then
    attempt_cmd "Installing python-lsp-server via pacman" \
      run_maybe_sudo pacman -Sy --noconfirm python-lsp-server && return 0
  fi

  if command -v apt-get >/dev/null 2>&1; then
    attempt_cmd "Installing python3-pylsp via apt-get" \
      run_maybe_sudo bash -lc "apt-get update && apt-get install -y python3-pylsp" && return 0
  fi

  if command -v dnf >/dev/null 2>&1; then
    attempt_cmd "Installing python3-pylsp via dnf" \
      run_maybe_sudo dnf install -y python3-pylsp && return 0
  fi

  if command -v yum >/dev/null 2>&1; then
    attempt_cmd "Installing python3-pylsp via yum" \
      run_maybe_sudo yum install -y python3-pylsp && return 0
  fi

  if command -v zypper >/dev/null 2>&1; then
    attempt_cmd "Installing python3-python-lsp-server via zypper" \
      run_maybe_sudo zypper --non-interactive install python3-python-lsp-server && return 0
  fi

  # Fallback: isolated venv for Jot-managed Python tooling.
  if command -v python3 >/dev/null 2>&1; then
    local venv_dir="${DEFAULT_HOME}/.local/share/jot/venvs/lsp"
    local user_bin="${DEFAULT_HOME}/.local/bin"

    attempt_cmd "Creating venv for python-lsp-server at ${venv_dir}" \
      run_as_default_user python3 -m venv "${venv_dir}" || true

    if attempt_cmd "Installing python-lsp-server in Jot venv" \
      run_as_default_user "${venv_dir}/bin/python" -m pip install -U python-lsp-server; then
      run_as_default_user mkdir -p "${user_bin}"
      run_as_default_user ln -sf "${venv_dir}/bin/pylsp" "${user_bin}/pylsp"
      echo "[jot:lsp] Installed pylsp in venv and linked to ${user_bin}/pylsp"
      return 0
    fi
  fi

  echo "[jot:lsp] Warning: unable to install pylsp automatically." >&2
  return 1
}

install_typescript_lsp() {
  if command -v typescript-language-server >/dev/null 2>&1; then
    echo "[jot:lsp] typescript-language-server already installed"
    return 0
  fi
  if command -v npm >/dev/null 2>&1; then
    if attempt_cmd "Installing typescript + typescript-language-server via npm -g" \
      run_maybe_sudo npm install -g typescript typescript-language-server; then
      return 0
    fi
  fi
  echo "[jot:lsp] Warning: unable to install typescript-language-server automatically." >&2
  return 1
}

install_clangd() {
  if command -v clangd >/dev/null 2>&1; then
    echo "[jot:lsp] clangd already installed"
    return 0
  fi

  if command -v apt-get >/dev/null 2>&1; then
    attempt_cmd "Installing clangd via apt-get" \
      run_maybe_sudo bash -lc "apt-get update && apt-get install -y clangd" && return 0
  fi

  if command -v dnf >/dev/null 2>&1; then
    attempt_cmd "Installing clang-tools-extra via dnf" \
      run_maybe_sudo dnf install -y clang-tools-extra && return 0
  fi

  if command -v yum >/dev/null 2>&1; then
    attempt_cmd "Installing clang-tools-extra via yum" \
      run_maybe_sudo yum install -y clang-tools-extra && return 0
  fi

  if command -v pacman >/dev/null 2>&1; then
    attempt_cmd "Installing clang via pacman" \
      run_maybe_sudo pacman -Sy --noconfirm clang && return 0
  fi

  if command -v zypper >/dev/null 2>&1; then
    attempt_cmd "Installing clang-tools via zypper" \
      run_maybe_sudo zypper --non-interactive install clang-tools && return 0
  fi

  if command -v brew >/dev/null 2>&1; then
    attempt_cmd "Installing llvm via brew (contains clangd)" \
      brew install llvm && return 0
  fi

  echo "[jot:lsp] Warning: unable to install clangd automatically." >&2
  return 1
}

install_prettier() {
  if command -v prettier >/dev/null 2>&1; then
    echo "[jot] prettier already installed"
    return 0
  fi

  if command -v pacman >/dev/null 2>&1; then
    attempt_cmd "Installing prettier via pacman" \
      run_maybe_sudo pacman -Sy --noconfirm prettier && return 0
  fi

  if command -v apt-get >/dev/null 2>&1; then
    attempt_cmd "Installing prettier via apt-get" \
      run_maybe_sudo bash -lc "apt-get update && apt-get install -y prettier" && return 0
  fi

  if command -v dnf >/dev/null 2>&1; then
    attempt_cmd "Installing prettier via dnf" \
      run_maybe_sudo dnf install -y prettier && return 0
  fi

  if command -v yum >/dev/null 2>&1; then
    attempt_cmd "Installing prettier via yum" \
      run_maybe_sudo yum install -y prettier && return 0
  fi

  if command -v zypper >/dev/null 2>&1; then
    attempt_cmd "Installing prettier via zypper" \
      run_maybe_sudo zypper --non-interactive install nodejs-prettier && return 0
  fi

  if command -v npm >/dev/null 2>&1; then
    if [[ "${USE_SUDO}" -eq 1 ]]; then
      attempt_cmd "Installing prettier via npm -g (sudo)" \
        run_maybe_sudo npm install -g prettier && return 0
    else
      if attempt_cmd "Installing prettier via npm -g" \
        npm install -g prettier; then
        return 0
      fi
      local user_prefix="${DEFAULT_HOME}/.local"
      run_as_default_user mkdir -p "${user_prefix}"
      if attempt_cmd "Installing prettier via npm (user prefix ${user_prefix})" \
        run_as_default_user npm install --prefix "${user_prefix}" prettier; then
        run_as_default_user mkdir -p "${DEFAULT_HOME}/.local/bin"
        if [[ -x "${user_prefix}/node_modules/.bin/prettier" ]]; then
          run_as_default_user ln -sf "${user_prefix}/node_modules/.bin/prettier" \
            "${DEFAULT_HOME}/.local/bin/prettier"
        fi
        return 0
      fi
    fi
  fi

  echo "[jot] Warning: unable to install prettier automatically." >&2
  return 1
}

install_builtin_lsps() {
  echo "[jot:lsp] Installing built-in LSP servers (python/typescript/cpp)"
  local failures=0

  install_python_lsp || failures=$((failures + 1))
  install_typescript_lsp || failures=$((failures + 1))
  install_clangd || failures=$((failures + 1))

  if [[ "${failures}" -gt 0 ]]; then
    echo "[jot:lsp] Completed with ${failures} warning(s). Some LSP servers may need manual install." >&2
  else
    echo "[jot:lsp] All built-in LSP servers are installed."
  fi
}

mkdir -p "${BUILD_DIR}"

CMAKE_ARGS=(
  -S "${PROJECT_ROOT}"
  -B "${BUILD_DIR}"
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
  -DCMAKE_INSTALL_PREFIX="${INSTALL_PREFIX}"
)

BUILD_ARGS=(--build "${BUILD_DIR}")
if [[ -n "${JOBS}" ]]; then
  BUILD_ARGS+=(--parallel "${JOBS}")
else
  BUILD_ARGS+=(--parallel)
fi

echo "[jot] Configuring (${BUILD_TYPE})"
cmake "${CMAKE_ARGS[@]}"

echo "[jot] Building"
cmake "${BUILD_ARGS[@]}"

if [[ "${RUN_TESTS}" -eq 1 ]]; then
  echo "[jot] Running tests"
  ctest --test-dir "${BUILD_DIR}" --output-on-failure
fi

echo "[jot] Installing to ${INSTALL_PREFIX}"
if [[ "${USE_SUDO}" -eq 1 ]]; then
  sudo cmake --install "${BUILD_DIR}"
else
  cmake --install "${BUILD_DIR}"
fi

echo "[jot] Installing formatter dependency (prettier)"
install_prettier || true

if [[ "${INSTALL_LSP}" -eq 1 ]]; then
  install_builtin_lsps
else
  echo "[jot:lsp] Skipped (--skip-lsp)"
fi

echo "[jot] Done"
echo "Add ${INSTALL_PREFIX}/bin to PATH if needed."

EXPECTED_BIN="${INSTALL_PREFIX}/bin/jot"
ACTIVE_JOT="$(command -v jot 2>/dev/null || true)"

echo "[jot] Installed binary: ${EXPECTED_BIN}"
if [[ -n "${ACTIVE_JOT}" ]]; then
  ACTIVE_REAL="$(realpath "${ACTIVE_JOT}" 2>/dev/null || echo "${ACTIVE_JOT}")"
  EXPECTED_REAL="$(realpath "${EXPECTED_BIN}" 2>/dev/null || echo "${EXPECTED_BIN}")"
  echo "[jot] Active jot in PATH: ${ACTIVE_REAL}"
  if [[ "${ACTIVE_REAL}" != "${EXPECTED_REAL}" ]]; then
    echo "[jot] WARNING: active 'jot' is not the one just installed." >&2
    echo "[jot] Use '${EXPECTED_BIN}' directly or fix PATH order." >&2
    echo "[jot] Tip: run 'hash -r' after updating PATH in current shell." >&2
  fi
else
  echo "[jot] No 'jot' found in PATH yet."
  echo "[jot] Run '${EXPECTED_BIN}' directly or add '${INSTALL_PREFIX}/bin' to PATH."
fi

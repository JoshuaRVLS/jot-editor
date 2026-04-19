#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INSTALL_PREFIX="${HOME}/.local"
BUILD_TYPE="Release"
RUN_TESTS=0
USE_SUDO=0
INSTALL_LSP=1
JOBS=""

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
  if command -v python3 >/dev/null 2>&1; then
    if attempt_cmd "Installing python-lsp-server via pip --user" \
      python3 -m pip install --user -U python-lsp-server; then
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

if [[ "${INSTALL_LSP}" -eq 1 ]]; then
  install_builtin_lsps
else
  echo "[jot:lsp] Skipped (--skip-lsp)"
fi

echo "[jot] Done"
echo "Add ${INSTALL_PREFIX}/bin to PATH if needed."

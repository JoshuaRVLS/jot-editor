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
RUN_TESTS=1
USE_SUDO=0
INSTALL_LSP=0
INSTALL_TOOLS=0
INSTALL_TREESITTER=1
JOBS="2"
PREFIX_EXPLICIT=0
BUILD_DIR_EXPLICIT=0

# --- logging helpers ---------------------------------------------------------
# Colors are used only when attached to a terminal (and NO_COLOR is unset),
# so piped output stays plain.
if [[ -t 1 ]] && [[ -z "${NO_COLOR:-}" ]]; then
  C_BOLD=$'\033[1m'
  C_CYAN=$'\033[36m'
  C_GREEN=$'\033[32m'
  C_YELLOW=$'\033[33m'
  C_RED=$'\033[31m'
  C_RESET=$'\033[0m'
else
  C_BOLD=''
  C_CYAN=''
  C_GREEN=''
  C_YELLOW=''
  C_RED=''
  C_RESET=''
fi

log_step()  { printf '%s==>%s %s%s%s\n' "${C_CYAN}" "${C_RESET}" "${C_BOLD}" "$*" "${C_RESET}"; }
log_ok()    { printf '%s✓%s %s\n' "${C_GREEN}" "${C_RESET}" "$*"; }
log_info()  { printf '%s\n' "$*"; }
log_warn()  { printf '%s!%s %s\n' "${C_YELLOW}" "${C_RESET}" "$*" >&2; }
log_error() { printf '%s✗%s %s\n' "${C_RED}" "${C_RESET}" "$*" >&2; }

print_help() {
  cat <<'USAGE'
Usage: ./install.sh [options]

Build and install jot using CMake.

Options:
  --prefix <path>       Install prefix (default: $HOME/.local)
  --build-dir <path>    Build directory (default: ./build)
  --debug               Build with Debug configuration
  --release             Build with Release configuration (default)
  --run-tests           Run CTest after building (default)
  --skip-tests          Skip CTest after building
  --with-tools          Install optional formatter tooling (prettier)
  --with-lsp            Install optional built-in LSP servers
  --with-treesitter     Install Tree-sitter runtime package (default)
  --skip-treesitter     Skip Tree-sitter dependency install attempt
  --skip-lsp            Deprecated alias; LSP installs are skipped by default
  --sudo                Run install step with sudo
  -j, --jobs <N>        Parallel build jobs (default: 2)
  -h, --help            Show this help message

Examples:
  ./install.sh
  ./install.sh --prefix /usr/local --sudo
  ./install.sh --skip-tests
  ./install.sh --skip-treesitter
  ./install.sh --with-tools --with-lsp
  ./install.sh --build-dir ./build_release --release -j 4
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --prefix)
      [[ $# -ge 2 ]] || { log_error "Missing value for --prefix"; exit 1; }
      INSTALL_PREFIX="$2"
      PREFIX_EXPLICIT=1
      shift 2
      ;;
    --build-dir)
      [[ $# -ge 2 ]] || { log_error "Missing value for --build-dir"; exit 1; }
      BUILD_DIR="$2"
      BUILD_DIR_EXPLICIT=1
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
    --skip-tests)
      RUN_TESTS=0
      shift
      ;;
    --with-tools)
      INSTALL_TOOLS=1
      shift
      ;;
    --with-lsp)
      INSTALL_LSP=1
      shift
      ;;
    --with-treesitter)
      INSTALL_TREESITTER=1
      shift
      ;;
    --skip-treesitter)
      INSTALL_TREESITTER=0
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
      [[ $# -ge 2 ]] || { log_error "Missing value for $1"; exit 1; }
      JOBS="$2"
      shift 2
      ;;
    -h|--help)
      print_help
      exit 0
      ;;
    *)
      log_error "Unknown option: $1"
      print_help
      exit 1
      ;;
  esac
done

if ! [[ "${JOBS}" =~ ^[1-9][0-9]*$ ]]; then
  log_error "--jobs must be a positive number"
  exit 1
fi

if [[ "${EUID}" -eq 0 ]] && [[ "${PREFIX_EXPLICIT}" -eq 0 ]]; then
  if [[ "${INSTALL_PREFIX}" == "/root/.local" ]] && [[ -n "${SUDO_USER:-}" ]]; then
    log_info "Running as root via sudo; installing to ${DEFAULT_HOME}/.local for ${SUDO_USER}"
  elif [[ "${INSTALL_PREFIX}" == "/root/.local" ]]; then
    log_warn "Install prefix is /root/.local"
    log_warn "If this is not intended, run as your normal user or pass --prefix <path>."
  fi
fi

if ! command -v cmake >/dev/null 2>&1; then
  log_error "cmake not found in PATH"
  exit 1
fi

if ! command -v pkg-config >/dev/null 2>&1; then
  log_error "pkg-config not found in PATH"
  exit 1
fi

if [[ "${USE_SUDO}" -eq 1 ]] && ! command -v sudo >/dev/null 2>&1; then
  log_error "--sudo requested but sudo is not available"
  exit 1
fi

ninja_command() {
  if command -v ninja >/dev/null 2>&1; then
    command -v ninja
    return 0
  fi
  if command -v ninja-build >/dev/null 2>&1; then
    command -v ninja-build
    return 0
  fi
  return 1
}

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

is_valid_jot_binary() {
  local binary="$1"
  if [[ ! -s "${binary}" ]]; then
    return 1
  fi
  if command -v file >/dev/null 2>&1; then
    local kind
    kind="$(file -b "${binary}" 2>/dev/null || true)"
    case "${kind}" in
      *ELF*"executable"*|*ELF*"shared object"*|*Mach-O*"executable"*) return 0 ;;
      *) return 1 ;;
    esac
  fi
  return 0
}

ensure_valid_built_jot() {
  local binary="${BUILD_DIR}/apps/jot/jot"
  if is_valid_jot_binary "${binary}"; then
    return 0
  fi

  log_warn "Invalid build artifact at ${binary}; rebuilding jot"
  rm -f "${binary}"
  cmake --build "${BUILD_DIR}" --target jot --parallel "${JOBS}"

  if ! is_valid_jot_binary "${binary}"; then
    log_error "Build did not produce a valid jot executable at ${binary}"
    return 1
  fi
}

# Runs a command silently; only reports when it fails.
attempt_cmd() {
  local desc="$1"
  shift
  if "$@"; then
    return 0
  fi
  log_warn "Failed: ${desc}"
  return 1
}

ensure_prefix_pkg_config_path() {
  local prefix="$1"
  local pkg_paths=(
    "${prefix}/lib/pkgconfig"
    "${prefix}/share/pkgconfig"
  )
  for path in "${pkg_paths[@]}"; do
    if [[ -d "${path}" ]]; then
      case ":${PKG_CONFIG_PATH:-}:" in
        *:"${path}":*) ;;
        *) export PKG_CONFIG_PATH="${path}:${PKG_CONFIG_PATH:-}" ;;
      esac
    fi
  done
}

install_python_lsp() {
  if command -v pylsp >/dev/null 2>&1; then
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
      return 0
    fi
  fi

  log_warn "Unable to install pylsp automatically"
  return 1
}

install_typescript_lsp() {
  if command -v typescript-language-server >/dev/null 2>&1; then
    return 0
  fi
  if command -v npm >/dev/null 2>&1; then
    if attempt_cmd "Installing typescript + typescript-language-server via npm -g" \
      run_maybe_sudo npm install -g typescript typescript-language-server; then
      return 0
    fi
  fi
  log_warn "Unable to install typescript-language-server automatically"
  return 1
}

install_html_lsp() {
  if command -v vscode-html-language-server >/dev/null 2>&1; then
    return 0
  fi
  if command -v npm >/dev/null 2>&1; then
    if attempt_cmd "Installing vscode HTML language server via npm -g" \
      run_maybe_sudo npm install -g vscode-langservers-extracted; then
      return 0
    fi
  fi
  log_warn "Unable to install vscode-html-language-server automatically"
  return 1
}

install_bash_lsp() {
  if command -v bash-language-server >/dev/null 2>&1; then
    return 0
  fi
  if command -v npm >/dev/null 2>&1; then
    if attempt_cmd "Installing bash-language-server via npm -g" \
      run_maybe_sudo npm install -g bash-language-server; then
      return 0
    fi
  fi
  log_warn "Unable to install bash-language-server automatically"
  return 1
}

install_rust_analyzer() {
  if command -v rust-analyzer >/dev/null 2>&1; then
    return 0
  fi

  if command -v apt-get >/dev/null 2>&1; then
    attempt_cmd "Installing rust-analyzer via apt-get" \
      run_maybe_sudo bash -lc "apt-get update && apt-get install -y rust-analyzer" && return 0
  fi
  if command -v dnf >/dev/null 2>&1; then
    attempt_cmd "Installing rust-analyzer via dnf" \
      run_maybe_sudo dnf install -y rust-analyzer && return 0
  fi
  if command -v yum >/dev/null 2>&1; then
    attempt_cmd "Installing rust-analyzer via yum" \
      run_maybe_sudo yum install -y rust-analyzer && return 0
  fi
  if command -v pacman >/dev/null 2>&1; then
    attempt_cmd "Installing rust-analyzer via pacman" \
      run_maybe_sudo pacman -Sy --noconfirm rust-analyzer && return 0
  fi
  if command -v zypper >/dev/null 2>&1; then
    attempt_cmd "Installing rust-analyzer via zypper" \
      run_maybe_sudo zypper --non-interactive install rust-analyzer && return 0
  fi
  if command -v brew >/dev/null 2>&1; then
    attempt_cmd "Installing rust-analyzer via brew" \
      brew install rust-analyzer && return 0
  fi

  log_warn "Unable to install rust-analyzer automatically"
  return 1
}

install_gopls() {
  if command -v gopls >/dev/null 2>&1; then
    return 0
  fi

  if command -v go >/dev/null 2>&1; then
    if attempt_cmd "Installing gopls via go install" \
      run_as_default_user go install golang.org/x/tools/gopls@latest; then
      local gopath="${GOPATH:-${DEFAULT_HOME}/go}"
      local gopls_bin="${gopath}/bin/gopls"
      if [[ -x "${gopls_bin}" ]]; then
        run_as_default_user mkdir -p "${DEFAULT_HOME}/.local/bin"
        run_as_default_user ln -sf "${gopls_bin}" "${DEFAULT_HOME}/.local/bin/gopls"
      fi
      return 0
    fi
  fi

  if command -v apt-get >/dev/null 2>&1; then
    attempt_cmd "Installing gopls via apt-get" \
      run_maybe_sudo bash -lc "apt-get update && apt-get install -y gopls" && return 0
  fi
  if command -v pacman >/dev/null 2>&1; then
    attempt_cmd "Installing gopls via pacman" \
      run_maybe_sudo pacman -Sy --noconfirm gopls && return 0
  fi
  if command -v brew >/dev/null 2>&1; then
    attempt_cmd "Installing gopls via brew" \
      brew install gopls && return 0
  fi

  log_warn "Unable to install gopls automatically"
  return 1
}

install_lua_ls() {
  if command -v lua-language-server >/dev/null 2>&1; then
    return 0
  fi
  if command -v lua_ls >/dev/null 2>&1; then
    run_as_default_user mkdir -p "${DEFAULT_HOME}/.local/bin"
    run_as_default_user ln -sf "$(command -v lua_ls)" "${DEFAULT_HOME}/.local/bin/lua-language-server"
    return 0
  fi

  if command -v apt-get >/dev/null 2>&1; then
    attempt_cmd "Installing lua-language-server via apt-get" \
      run_maybe_sudo bash -lc "apt-get update && apt-get install -y lua-language-server" && return 0
  fi
  if command -v dnf >/dev/null 2>&1; then
    attempt_cmd "Installing lua-language-server via dnf" \
      run_maybe_sudo dnf install -y lua-language-server && return 0
  fi
  if command -v yum >/dev/null 2>&1; then
    attempt_cmd "Installing lua-language-server via yum" \
      run_maybe_sudo yum install -y lua-language-server && return 0
  fi
  if command -v pacman >/dev/null 2>&1; then
    attempt_cmd "Installing lua-language-server via pacman" \
      run_maybe_sudo pacman -Sy --noconfirm lua-language-server && return 0
  fi
  if command -v zypper >/dev/null 2>&1; then
    attempt_cmd "Installing lua-language-server via zypper" \
      run_maybe_sudo zypper --non-interactive install lua-language-server && return 0
  fi
  if command -v brew >/dev/null 2>&1; then
    attempt_cmd "Installing lua-language-server via brew" \
      brew install lua-language-server && return 0
  fi

  if command -v lua_ls >/dev/null 2>&1; then
    run_as_default_user mkdir -p "${DEFAULT_HOME}/.local/bin"
    run_as_default_user ln -sf "$(command -v lua_ls)" "${DEFAULT_HOME}/.local/bin/lua-language-server"
    return 0
  fi

  log_warn "Unable to install lua-language-server automatically"
  return 1
}

install_clangd() {
  if command -v clangd >/dev/null 2>&1; then
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

  log_warn "Unable to install clangd automatically"
  return 1
}

install_prettier() {
  if command -v prettier >/dev/null 2>&1; then
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

  log_warn "Unable to install prettier automatically"
  return 1
}

install_treesitter_deps() {
  log_step "Tree-sitter runtime"
  local failures=0
  ensure_prefix_pkg_config_path "${INSTALL_PREFIX}"

  if pkg-config --exists tree-sitter >/dev/null 2>&1; then
    log_ok "Tree-sitter runtime already available via pkg-config"
    return 0
  fi

  can_use_package_manager() {
    if [[ "${EUID}" -eq 0 ]]; then
      return 0
    fi
    sudo -n true >/dev/null 2>&1
  }

  if ! can_use_package_manager; then
    log_info "Skipped system Tree-sitter package install (requires root); the bundled build works and"
    log_info "per-user parsers install later with :tsinstall <language>."
    return 0
  fi

  if command -v pacman >/dev/null 2>&1; then
    attempt_cmd "Installing Tree-sitter runtime via pacman" \
      run_maybe_sudo pacman -Sy --noconfirm tree-sitter || failures=$((failures + 1))
  elif command -v apt-get >/dev/null 2>&1; then
    attempt_cmd "Installing Tree-sitter runtime via apt-get" \
      run_maybe_sudo bash -lc "apt-get update && apt-get install -y libtree-sitter-dev" || failures=$((failures + 1))
  elif command -v dnf >/dev/null 2>&1; then
    attempt_cmd "Installing Tree-sitter runtime via dnf" \
      run_maybe_sudo dnf install -y tree-sitter-devel || failures=$((failures + 1))
  elif command -v yum >/dev/null 2>&1; then
    attempt_cmd "Installing Tree-sitter runtime via yum" \
      run_maybe_sudo yum install -y tree-sitter-devel || failures=$((failures + 1))
  elif command -v zypper >/dev/null 2>&1; then
    attempt_cmd "Installing Tree-sitter runtime via zypper" \
      run_maybe_sudo zypper --non-interactive install tree-sitter-devel || failures=$((failures + 1))
  elif command -v brew >/dev/null 2>&1; then
    attempt_cmd "Installing Tree-sitter runtime via brew" \
      brew install tree-sitter || failures=$((failures + 1))
  else
    log_warn "No supported package manager found for Tree-sitter"
    failures=$((failures + 1))
  fi

  if [[ "${failures}" -gt 0 ]]; then
    log_warn "Tree-sitter install had warnings; highlighting may fall back to regex"
  else
    log_ok "Tree-sitter runtime ready"
  fi
}

install_required_native_deps() {
  native_deps_available() {
    pkg-config --exists vterm termkey libuv &&
      (pkg-config --exists libutf8proc || pkg-config --exists utf8proc)
  }

  build_tools_available() {
    ninja_command >/dev/null
  }

  install_build_tools() {
    if build_tools_available && command -v ccache >/dev/null 2>&1; then
      return 0
    fi

    log_step "Build tools (ninja, ccache)"
    if command -v pacman >/dev/null 2>&1; then
      attempt_cmd "Installing ninja and ccache via pacman" \
        run_maybe_sudo pacman -Sy --noconfirm ninja ccache || true
    elif command -v apt-get >/dev/null 2>&1; then
      attempt_cmd "Installing ninja-build and ccache via apt-get" \
        run_maybe_sudo bash -lc "apt-get update && apt-get install -y ninja-build ccache" || true
    elif command -v dnf >/dev/null 2>&1; then
      attempt_cmd "Installing ninja-build and ccache via dnf" \
        run_maybe_sudo dnf install -y ninja-build ccache || true
    elif command -v yum >/dev/null 2>&1; then
      attempt_cmd "Installing ninja-build and ccache via yum" \
        run_maybe_sudo yum install -y ninja-build ccache || true
    elif command -v zypper >/dev/null 2>&1; then
      attempt_cmd "Installing ninja and ccache via zypper" \
        run_maybe_sudo zypper --non-interactive install ninja ccache || true
    elif command -v brew >/dev/null 2>&1; then
      attempt_cmd "Installing ninja and ccache via brew" \
        brew install ninja ccache || true
    else
      log_warn "No supported package manager found for ninja/ccache"
    fi
  }

  install_build_tools

  if native_deps_available; then
    return 0
  fi

  log_step "Required native packages (libvterm, libtermkey, libuv, utf8proc)"
  ensure_prefix_pkg_config_path "${INSTALL_PREFIX}"

  if command -v pacman >/dev/null 2>&1; then
    attempt_cmd "Installing libvterm via pacman" \
      run_maybe_sudo pacman -Sy --noconfirm libvterm || true
    attempt_cmd "Installing libtermkey via pacman" \
      run_maybe_sudo pacman -Sy --noconfirm libtermkey || true
    attempt_cmd "Installing libuv via pacman" \
      run_maybe_sudo pacman -Sy --noconfirm libuv || true
    attempt_cmd "Installing utf8proc via pacman" \
      run_maybe_sudo pacman -Sy --noconfirm utf8proc || true
    if native_deps_available; then
      return 0
    fi
    if [[ "${USE_SUDO}" -eq 0 ]]; then
      if command -v paru >/dev/null 2>&1; then
        attempt_cmd "Installing libtermkey via paru" \
          paru -S --needed --noconfirm libtermkey || true
        if native_deps_available; then
          return 0
        fi
      elif command -v yay >/dev/null 2>&1; then
        attempt_cmd "Installing libtermkey via yay" \
          yay -S --needed --noconfirm libtermkey || true
        if native_deps_available; then
          return 0
        fi
      fi
    fi
  elif command -v apt-get >/dev/null 2>&1; then
    attempt_cmd "Installing required native packages via apt-get" \
      run_maybe_sudo bash -lc "apt-get update && apt-get install -y libvterm-dev libtermkey-dev libuv1-dev libutf8proc-dev" && return 0
  elif command -v dnf >/dev/null 2>&1; then
    attempt_cmd "Installing required native packages via dnf" \
      run_maybe_sudo dnf install -y libvterm-devel libtermkey-devel libuv-devel utf8proc-devel && return 0
  elif command -v yum >/dev/null 2>&1; then
    attempt_cmd "Installing required native packages via yum" \
      run_maybe_sudo yum install -y libvterm-devel libtermkey-devel libuv-devel utf8proc-devel && return 0
  elif command -v zypper >/dev/null 2>&1; then
    attempt_cmd "Installing required native packages via zypper" \
      run_maybe_sudo zypper --non-interactive install libvterm-devel libtermkey-devel libuv-devel utf8proc-devel && return 0
  elif command -v brew >/dev/null 2>&1; then
    attempt_cmd "Installing required native packages via brew" \
      brew install libvterm libtermkey libuv utf8proc && return 0
  fi

  log_error "Install libvterm, libtermkey, libuv, and utf8proc development packages, then rerun install.sh"
  log_info "Arch note: libtermkey may be available from AUR as 'libtermkey'."
  return 1
}

install_builtin_lsps() {
  log_step "LSP servers (python/typescript/js/jsx/tsx/html/cpp/rust/go/lua/bash)"
  local failures=0

  install_python_lsp || failures=$((failures + 1))
  install_typescript_lsp || failures=$((failures + 1))
  install_html_lsp || failures=$((failures + 1))
  install_clangd || failures=$((failures + 1))
  install_rust_analyzer || failures=$((failures + 1))
  install_gopls || failures=$((failures + 1))
  install_lua_ls || failures=$((failures + 1))
  install_bash_lsp || failures=$((failures + 1))

  if [[ "${failures}" -gt 0 ]]; then
    log_warn "${failures} LSP server(s) could not be installed automatically; install them manually"
  else
    log_ok "All built-in LSP servers installed"
  fi
}

choose_build_dir() {
  local default_relocate="${XDG_CACHE_HOME:-${HOME}/.cache}/jot/build"
  if [[ "${BUILD_DIR_EXPLICIT}" -eq 1 ]]; then
    if [[ -d "${BUILD_DIR}" && ! -w "${BUILD_DIR}" ]]; then
      log_error "--build-dir is not writable: ${BUILD_DIR}"
      log_info "Fix ownership first: sudo chown -R \"$(id -un)\" \"${BUILD_DIR}\""
      exit 1
    fi
    return 0
  fi
  if [[ ! -d "${BUILD_DIR}" ]]; then
    if mkdir -p "${BUILD_DIR}" 2>/dev/null; then
      return 0
    fi
    log_info "${PROJECT_ROOT} is not writable; building in ${default_relocate}"
    BUILD_DIR="${default_relocate}"
    return 0
  fi
  if [[ -w "${BUILD_DIR}" ]]; then
    return 0
  fi
  log_info "Default build dir is root-owned: ${BUILD_DIR}; building in ${default_relocate} instead"
  BUILD_DIR="${default_relocate}"
}

choose_build_dir
mkdir -p "${BUILD_DIR}"

install_required_native_deps

NINJA_BIN="$(ninja_command || true)"
if [[ -z "${NINJA_BIN}" ]]; then
  log_error "Ninja is required for install builds. Install ninja or ninja-build, then rerun install.sh."
  exit 1
fi
if ! command -v ccache >/dev/null 2>&1; then
  log_info "ccache not found; rebuilds will be slower"
fi

if [[ "${INSTALL_TREESITTER}" -eq 1 ]]; then
  install_treesitter_deps || true
else
  log_info "Skipped Tree-sitter dependency install (--with-treesitter to enable)"
fi

ensure_prefix_pkg_config_path "${INSTALL_PREFIX}"

if [[ "${USE_SUDO}" -eq 0 ]]; then
  prefix_blocked=0
  if [[ -e "${INSTALL_PREFIX}/bin/jot" && ! -w "${INSTALL_PREFIX}/bin/jot" ]]; then
    prefix_blocked=1
  fi
  if [[ -d "${INSTALL_PREFIX}" && ! -w "${INSTALL_PREFIX}" ]]; then
    prefix_blocked=1
  fi
  if [[ "${prefix_blocked}" -eq 1 ]]; then
    log_error "Install prefix is not writable: ${INSTALL_PREFIX}"
    log_info "This is usually left over from a previous 'sudo ./install.sh'."
    log_info "Fix ownership once, then rerun without sudo: sudo chown -R \"$(id -un)\" \"${INSTALL_PREFIX}\""
    exit 1
  fi
fi

CMAKE_ARGS=(
  -S "${PROJECT_ROOT}"
  -B "${BUILD_DIR}"
  -G Ninja
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
  -DCMAKE_INSTALL_PREFIX="${INSTALL_PREFIX}"
)

BUILD_ARGS=(--build "${BUILD_DIR}" --parallel "${JOBS}")

log_step "Configuring (${BUILD_TYPE})"
cmake "${CMAKE_ARGS[@]}"

log_step "Building"
cmake "${BUILD_ARGS[@]}"
ensure_valid_built_jot

if [[ "${RUN_TESTS}" -eq 1 ]]; then
  log_step "Running tests"
  ctest --test-dir "${BUILD_DIR}" --output-on-failure
fi

log_step "Installing to ${INSTALL_PREFIX}"
if [[ "${USE_SUDO}" -eq 1 ]]; then
  sudo cmake --install "${BUILD_DIR}"
else
  cmake --install "${BUILD_DIR}"
fi

if [[ "${INSTALL_TOOLS}" -eq 1 ]]; then
  log_step "Formatter tooling (prettier)"
  install_prettier || true
else
  log_info "Skipped optional formatter tooling (--with-tools to enable)"
fi

if [[ "${INSTALL_LSP}" -eq 1 ]]; then
  install_builtin_lsps
else
  log_info "Skipped optional LSP server install (--with-lsp to enable)"
fi

EXPECTED_BIN="${INSTALL_PREFIX}/bin/jot"
ACTIVE_JOT="$(command -v jot 2>/dev/null || true)"

log_ok "jot installed at ${EXPECTED_BIN}"
if [[ -n "${ACTIVE_JOT}" ]]; then
  ACTIVE_REAL="$(realpath "${ACTIVE_JOT}" 2>/dev/null || echo "${ACTIVE_JOT}")"
  EXPECTED_REAL="$(realpath "${EXPECTED_BIN}" 2>/dev/null || echo "${EXPECTED_BIN}")"
  if [[ "${ACTIVE_REAL}" != "${EXPECTED_REAL}" ]]; then
    log_warn "Active 'jot' in PATH is ${ACTIVE_REAL}, not the one just installed"
    log_info "Use '${EXPECTED_BIN}' directly or fix PATH order (run 'hash -r' in the current shell)."
  fi
else
  log_info "Run '${EXPECTED_BIN}' directly, or add '${INSTALL_PREFIX}/bin' to PATH."
fi

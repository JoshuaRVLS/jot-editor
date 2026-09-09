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
INSTALL_TREESITTER=1
JOBS="2"
PREFIX_EXPLICIT=0
BUILD_DIR_EXPLICIT=0
# Comma-separated list of optional LSP / formatter components to install.
# Empty means "ask interactively when attached to a terminal, else install
# nothing". --with-lsp / --with-tools fill this in.
SELECTED_COMPONENTS=""
ASK_COMPONENTS=1

# --- component catalog (optional LSP servers + formatters) ------------------
# Order here drives the interactive picker and the list installed by
# --with-lsp / --with-tools.  Every entry has:  label | function | check-cmd.
# The check-cmd decides whether the tool already exists (so the picker can
# show it as installed and the installer can skip it).
FORMATTER_COMPONENTS="prettier"
LSP_COMPONENTS="clangd python typescript html bash rust_analyzer gopls lua_ls"

component_present() {
  local comp="$1"
  case "$comp" in
    clangd)          command -v clangd >/dev/null 2>&1 ;;
    python)          command -v pylsp >/dev/null 2>&1 ;;
    typescript)      command -v typescript-language-server >/dev/null 2>&1 ;;
    html)            command -v vscode-html-language-server >/dev/null 2>&1 ;;
    bash)            command -v bash-language-server >/dev/null 2>&1 ;;
    rust_analyzer)   command -v rust-analyzer >/dev/null 2>&1 ;;
    gopls)           command -v gopls >/dev/null 2>&1 ;;
    lua_ls)          command -v lua-language-server >/dev/null 2>&1 ;;
    prettier)        command -v prettier >/dev/null 2>&1 ;;
    *) return 1 ;;
  esac
}

component_install() {
  local comp="$1"
  if component_present "$comp"; then
    log_ok "${comp} already installed"
    return 0
  fi
  case "$comp" in
    clangd)        install_clangd ;;
    python)        install_python_lsp ;;
    typescript)    install_typescript_lsp ;;
    html)          install_html_lsp ;;
    bash)          install_bash_lsp ;;
    rust_analyzer) install_rust_analyzer ;;
    gopls)         install_gopls ;;
    lua_ls)        install_lua_ls ;;
    prettier)      install_prettier ;;
    *) log_warn "Unknown component: ${comp}" ;;
  esac
}

component_label() {
  local comp="$1"
  case "$comp" in
    clangd)        echo "clangd (C/C++)" ;;
    python)        echo "python-lsp-server (Python)" ;;
    typescript)    echo "typescript-language-server (TS/JS)" ;;
    html)          echo "vscode-html-language-server (HTML)" ;;
    bash)          echo "bash-language-server (bash)" ;;
    rust_analyzer) echo "rust-analyzer (Rust)" ;;
    gopls)         echo "gopls (Go)" ;;
    lua_ls)        echo "lua-language-server (Lua)" ;;
    prettier)      echo "prettier (formatter)" ;;
    *) echo "$comp" ;;
  esac
}

ALL_COMPONENTS="${LSP_COMPONENTS} ${FORMATTER_COMPONENTS}"

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

Run without options for an interactive installer that asks which optional
LSP servers and formatters to install.

Options:
  --prefix <path>       Install prefix (default: $HOME/.local)
  --build-dir <path>    Build directory (default: ./build)
  --debug               Build with Debug configuration
  --release             Build with Release configuration (default)
  --run-tests           Build the test suite and run CTest (dev builds only)
  --skip-tests          Skip the test suite entirely (default)
  --with-tools          Install prettier without prompting
  --with-lsp            Install the built-in LSP servers without prompting
  --component <name>    Install one component without prompting; repeatable,
                        e.g. --component clangd --component lua_ls. Run
                        ./install.sh --list-components for the full list.
  --list-components     Print known component names and exit
  --no-components       Never prompt; install only the jot binary (default
                        when stdin is not a terminal)
  --with-treesitter     Install Tree-sitter runtime package (default)
  --skip-treesitter     Skip Tree-sitter dependency install attempt
  --sudo                Run install step with sudo
  -j, --jobs <N>        Parallel build jobs (default: 2)
  -h, --help            Show this help message

Examples:
  ./install.sh                          # interactive picker for extras
  ./install.sh --component clangd --component prettier
  ./install.sh --skip-tests --with-lsp
USAGE
}
#
#print_component_list() {
  #printf '%s\n' "Known optional components:"
  #local comp
  #for comp in ${ALL_COMPONENTS}; do
    #printf '  %-14s %s\n' "${comp}" "$(component_label "${comp}")"
  #done
#}
#
# Interactive picker for the optional tooling.  Presents a numbered list,
# accepts a comma/space separated list (or a single number to toggle a row),
# then prints the chosen component names as a single line to stdout.  Rows for
# tools that are already installed are pre-checked and installing them later is
# a no-op.
#pick_components() {
  #local comp
  #local n=0
  #local total=0
  #chosen=""
  #for comp in ${ALL_COMPONENTS}; do
    #total=$((total + 1))
    #if component_present "${comp}"; then
      #chosen="${chosen} ${comp}"
    #fi
  #done
#
  #name_at() {  # $1 = 1-based row -> component name (prints nothing if OOB)
    #local idx=0
    #local c
    #for c in ${ALL_COMPONENTS}; do
      #idx=$((idx + 1))
      #if [[ "$1" -eq "${idx}" ]]; then
        #printf '%s' "${c}"
        #return 0
      #fi
    #done
    #return 1
  #}
#
  # All menu rendering goes to stderr (the terminal): the caller captures
  # stdout as the selection result, so anything printed to stdout would be
  # swallowed instead of shown.
  #draw_menu() {
    #n=0
    #for comp in ${ALL_COMPONENTS}; do
      #n=$((n + 1))
      #local mark=" "
      #case " ${chosen} " in
        #*" ${comp} "*) mark="x" ;;
      #esac
      #printf '  [%s] %2d) %s\n' "${mark}" "${n}" "$(component_label "${comp}")" >&2
    #done
  #}
#
  #local prompt_lines=6   # blank + title + 3 hints + blank before the list
#
  #clear_block() {  # move up over the whole checklist block and wipe it
    #local lines=$((total + prompt_lines + 1))  # list rows + prompt line
    #local i
    #for ((i = 0; i < lines; i++)); do
      #printf '\033[1A\033[K' >&2
    #done
  #}
#
  #print_block() {
    #printf '\n' >&2
    #printf '%s\n' "${C_BOLD}Optional tooling${C_RESET}" >&2
    #printf '%s\n' "Numbers toggle a row; \"all\" selects everything; Enter installs what is [x]." >&2
    #printf '%s\n' "Already-installed tools are pre-checked and will simply be skipped." >&2
    #printf '%s\n' "Any of the 100+ LSP servers can also be installed later from inside jot with :lspinstall." >&2
    #printf '\n' >&2
    #draw_menu
    #printf '%s' 'Numbers (Enter when done): ' >&2
  #}
#
  #print_block
#
  #local done=0
  #while [[ "${done}" -eq 0 ]]; do
    #local reply=""
    #IFS= read -r reply || reply=""
    # The tty echoes the typed line; clear it plus the block before redraw.
    #clear_block
    #if [[ -z "${reply}" ]]; then
      #done=1
      #break
    #fi
    #case "${reply}" in
      #all|ALL|a)
        #chosen=" ${ALL_COMPONENTS} "
        #done=1
        #break
        #;;
    #esac
    #local tok
    #local parsed=0
    #IFS=', ' read -r -a nums <<< "${reply}"
    #for tok in "${nums[@]:-}"; do
      #[[ -z "${tok}" ]] && continue
      #if [[ "${tok}" =~ ^[0-9]+$ ]] && ((tok >= 1)) && ((tok <= total)); then
        #local name
        #name="$(name_at "${tok}")" || continue
        #if [[ -n "${name}" ]]; then
          #if [[ " ${chosen} " == *" ${name} "* ]]; then
            #chosen="${chosen// ${name}/ }"
          #else
            #chosen="${chosen} ${name}"
          #fi
          #parsed=1
        #fi
      #fi
    #done
    #if [[ "${parsed}" -eq 0 ]]; then
      #case "${reply}" in
        #done|0|q) done=1 ;;
      #esac
    #fi
    #if [[ "${done}" -eq 0 ]]; then
      #print_block
    #fi
  #done
  #printf '\n' >&2
#
  # Normalize and print the final selection as a single line (stdout -> the
  # caller's SELECTED_COMPONENTS variable).
  #local out=""
  #local seen=" "
  #for comp in ${chosen}; do
    #case "${seen}" in
      #*" ${comp} "*) ;;
      #*) seen="${seen}${comp} "; out="${out} ${comp}" ;;
    #esac
  #done
  #printf '%s\n' "${out# }"
#}
#
#while [[ $# -gt 0 ]]; do
  #case "$1" in
    #--prefix)
      #[[ $# -ge 2 ]] || { log_error "Missing value for --prefix"; exit 1; }
      #INSTALL_PREFIX="$2"
      #PREFIX_EXPLICIT=1
      #shift 2
      #;;
    #--build-dir)
      #[[ $# -ge 2 ]] || { log_error "Missing value for --build-dir"; exit 1; }
      #BUILD_DIR="$2"
      #BUILD_DIR_EXPLICIT=1
      #shift 2
      #;;
    #--debug)
      #BUILD_TYPE="Debug"
      #shift
      #;;
    #--release)
      #BUILD_TYPE="Release"
      #shift
      #;;
    #--run-tests)
      #RUN_TESTS=1
      #shift
      #;;
    #--skip-tests)
      #RUN_TESTS=0
      #shift
      #;;
    #--with-tools)
      #SELECTED_COMPONENTS="${SELECTED_COMPONENTS} prettier"
      #ASK_COMPONENTS=0
      #shift
      #;;
    #--with-lsp)
      #SELECTED_COMPONENTS="${SELECTED_COMPONENTS} ${LSP_COMPONENTS}"
      #ASK_COMPONENTS=0
      #shift
      #;;
    #--component)
      #[[ $# -ge 2 ]] || { log_error "Missing value for --component"; exit 1; }
      #SELECTED_COMPONENTS="${SELECTED_COMPONENTS} $2"
      #ASK_COMPONENTS=0
      #shift 2
      #;;
    #--no-components)
      #ASK_COMPONENTS=0
      #shift
      #;;
    #--list-components)
      #print_component_list
      #exit 0
      #;;
    #--with-treesitter)
      #INSTALL_TREESITTER=1
      #shift
      #;;
    #--skip-treesitter)
      #INSTALL_TREESITTER=0
      #shift
      #;;
    #--skip-lsp)
      # Deprecated alias kept for compatibility: explicit component selection
      # is empty by default, so this is a no-op now.
      #SELECTED_COMPONENTS=""
      #ASK_COMPONENTS=0
      #shift
      #;;
    #--sudo)
      #USE_SUDO=1
      #shift
      #;;
    #-j|--jobs)
      #[[ $# -ge 2 ]] || { log_error "Missing value for $1"; exit 1; }
      #JOBS="$2"
      #shift 2
      #;;
    #-h|--help)
      #print_help
      #exit 0
      #;;
    #*)
      #log_error "Unknown option: $1"
      #print_help
      #exit 1
      #;;
  #esac
#done
#
# Validate any explicitly requested component names early.
#for want in ${SELECTED_COMPONENTS}; do
  #case " ${ALL_COMPONENTS} " in
    #*" ${want} "*) ;;
    #*) log_error "Unknown component: ${want}"; print_component_list; exit 1 ;;
  #esac
#done

# The interactive picker runs after the build (see below) so a failed build
# does not waste the user's choices. Explicit flags skip it entirely; when
# stdin/stdout are not terminals (CI, scripts) nothing is prompted.
INTERACTIVE_TTY=0
if [[ -t 0 ]]; then
  INTERACTIVE_TTY=1
fi

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

# Runs a command capturing its output; on failure the captured output is
# printed so the user sees why, on success only a short ✓ line is shown.
attempt_cmd() {
  local desc="$1"
  shift
  local log_file
  log_file="$(mktemp)"
  if "$@" >"${log_file}" 2>&1; then
    rm -f "${log_file}"
    return 0
  fi
  log_warn "Failed: ${desc}"
  if [[ -s "${log_file}" ]]; then
    sed 's/^/    /' "${log_file}" | tail -n 15
  fi
  rm -f "${log_file}"
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
      env HOMEBREW_NO_AUTO_UPDATE=1 brew install --quiet rust-analyzer && return 0
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
      env HOMEBREW_NO_AUTO_UPDATE=1 brew install --quiet gopls && return 0
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
      env HOMEBREW_NO_AUTO_UPDATE=1 brew install --quiet lua-language-server && return 0
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
      env HOMEBREW_NO_AUTO_UPDATE=1 brew install --quiet llvm && return 0
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
    log_info "Skipped system Tree-sitter package install (requires root); CMake will build"
    log_info "the bundled tree-sitter runtime from source instead, and per-user parsers"
    log_info "install later with :tsinstall <language>."
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
      env HOMEBREW_NO_AUTO_UPDATE=1 brew install --quiet tree-sitter || failures=$((failures + 1))
  else
    log_warn "No supported package manager found for Tree-sitter"
    failures=$((failures + 1))
  fi

  if [[ "${failures}" -gt 0 ]]; then
    log_warn "Tree-sitter install had warnings; CMake falls back to building the bundled runtime"
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

if [[ "${RUN_TESTS}" -eq 0 ]]; then
  # Production installs skip the test target entirely: it pulls a Catch2
  # dependency and doubles the build time. --run-tests turns it back on for
  # developers and CI.
  CMAKE_ARGS+=(-DBUILD_TESTING=OFF)
else
  CMAKE_ARGS+=(-DBUILD_TESTING=ON)
fi

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
# cmake --install prints one line per copied Lua file; collapse that to a
# single summary line unless the install itself fails.
if [[ "${USE_SUDO}" -eq 1 ]]; then
  INSTALL_OUTPUT="$(sudo cmake --install "${BUILD_DIR}" 2>&1)" || { printf '%s\n' "${INSTALL_OUTPUT}"; exit 1; }
else
  INSTALL_OUTPUT="$(cmake --install "${BUILD_DIR}" 2>&1)" || { printf '%s\n' "${INSTALL_OUTPUT}"; exit 1; }
fi
if grep -q "JOT_GUI_ENABLED:INTERNAL=TRUE" "${BUILD_DIR}/CMakeCache.txt" 2>/dev/null; then
  log_ok "Installed jot (GUI frontend via 'jot --gui', binary/configs/themes/bundled files) into ${INSTALL_PREFIX}"
else
  log_ok "Installed jot (binary, configs, themes and bundled language files) into ${INSTALL_PREFIX}"
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

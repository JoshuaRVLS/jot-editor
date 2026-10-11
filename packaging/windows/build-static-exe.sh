#!/usr/bin/env bash
# Builds the Windows release of jot: a single self-contained jot.exe, cross-built
# with mingw-w64, plus the staged install tree (bin/jot.exe + share/jot) the
# release zips up.
#
# "Self-contained" is checked, not assumed. Windows always resolves KERNEL32 and
# friends from the OS, and Windows 10+ ships the Universal CRT, so those may
# appear in the import table; anything else -- libuv, utf8proc, tree-sitter, Lua,
# libstdc++, libgcc, libwinpthread -- would mean the user has to drop a DLL next
# to the exe, and this script fails instead of shipping that.
#
# Usage: packaging/windows/build-static-exe.sh [source-dir] [build-dir] [stage-dir]
# Exit codes: 0 ok, 1 build or import-gate failure.
set -euo pipefail

src_dir=${1:-$(cd "$(dirname "$0")/../.." && pwd)}
build_dir=${2:-${src_dir}/build-windows-x86_64}
stage_dir=${3:-${src_dir}/dist/jot-windows-x86_64}

toolchain_prefix=x86_64-w64-mingw32
for tool in "${toolchain_prefix}-gcc" "${toolchain_prefix}-g++" "${toolchain_prefix}-objdump"; do
  if ! command -v "${tool}" >/dev/null 2>&1; then
    echo "windows build: missing ${tool} (install mingw-w64)" >&2
    exit 1
  fi
done

echo "windows build: configuring ${build_dir}"
cmake -S "${src_dir}" -B "${build_dir}" -G Ninja \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_C_COMPILER="${toolchain_prefix}-gcc" \
  -DCMAKE_CXX_COMPILER="${toolchain_prefix}-g++" \
  -DCMAKE_RC_COMPILER="${toolchain_prefix}-windres" \
  -DCMAKE_BUILD_TYPE=Release \
  -DJOT_GUI=OFF \
  -DBUILD_TESTING=OFF \
  -DJOT_ENABLE_CCACHE=OFF \
  -DCMAKE_INSTALL_PREFIX="${stage_dir}" \
  -DCMAKE_EXE_LINKER_FLAGS="-static -static-libgcc -static-libstdc++"

echo "windows build: compiling"
cmake --build "${build_dir}" --parallel 4

exe="${build_dir}/apps/jot/jot.exe"
if [ ! -f "${exe}" ]; then
  echo "windows build: no exe at ${exe}" >&2
  exit 1
fi

# Import table gate. Everything here ships with Windows itself: KERNEL32/USER32/
# ADVAPI32/SHELL32/ole32 are the Win32 API, WS2_32/IPHLPAPI/USERENV/dbghelp come
# from libuv's networking, stack traces and environment handling, CRYPT32/
# bcrypt/WLDAP32 from curl's Schannel and WinLDAP support, and the C runtime is
# one of the two Windows carries -- msvcrt.dll on the mingw builds that target
# it, ucrtbase.dll plus the api-ms-win-crt-* forwarders on the ones that target
# the Universal CRT (Windows 10 and newer). Neither is something a user
# installs. What must never appear is a library of jot's own: libuv, utf8proc,
# tree-sitter, Lua, curl, libstdc++, libgcc, libwinpthread.
echo "windows build: checking the exe's imports"
imports=$("${toolchain_prefix}-objdump" -p "${exe}" | sed -n 's/^\s*DLL Name: //p' | sort -u)
unexpected=$(printf '%s\n' "${imports}" \
  | grep -viE '^(kernel32|user32|advapi32|shell32|ole32|ws2_32|iphlpapi|userenv|dbghelp|msvcrt|ucrtbase|crypt32|bcrypt|wldap32)\.dll$' \
  | grep -viE '^api-ms-win-crt-[a-z0-9-]+\.dll$' || true)
if [ -n "${unexpected}" ]; then
  echo "windows build: FAIL - the exe needs libraries of its own:" >&2
  printf '  %s\n' ${unexpected} >&2
  exit 1
fi
echo "windows build: imports are Windows-only: $(printf '%s ' ${imports})"

echo "windows build: installing to ${stage_dir}"
cmake --install "${build_dir}" --prefix "${stage_dir}"

# cmake --install also runs the install rules of the dependencies this build
# compiles from source, so curl's curl-config and mk-ca-bundle.pl and
# tree-sitter's pkg-config file land in the same prefix. The staged tree is the
# payload the installer and the zip carry, so bin/ keeps the exe alone and
# share/ keeps the runtime and the licenses.
find "${stage_dir}/bin" -mindepth 1 -maxdepth 1 ! -name 'jot.exe' -exec rm -rf {} +
find "${stage_dir}/share" -mindepth 1 -maxdepth 1 ! -name 'jot' ! -name 'doc' -exec rm -rf {} +

echo "windows build: $(du -h "${stage_dir}/bin/jot.exe" | cut -f1) exe at ${stage_dir}/bin/jot.exe"

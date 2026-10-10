#!/usr/bin/env bash
# Builds the Windows installer: jot.exe plus the share/jot tree it reads at
# runtime, wrapped in a per-user setup exe. It takes the tree
# packaging/windows/build-static-exe.sh staged, so run that first (the release
# and CI jobs run the two scripts in that order).
#
# makensis runs on Linux, so the installer is built in the same job as the exe,
# from the same gated tree: what the import check passed is what the installer
# hands the user.
#
# Usage: packaging/windows/build-installer.sh [stage-dir] [out-file] [version]
#   stage-dir  staged tree, default dist/jot-windows-x86_64
#   out-file   installer to write, default dist/jot-windows-x86_64-setup.exe
#   version    shown by the installer and in Apps & features; defaults to the
#              newest git tag.
# Exit codes: 0 ok, 1 missing input, missing NSIS, or a failed build.
set -euo pipefail

src_dir=$(cd "$(dirname "$0")/../.." && pwd)
stage_dir=${1:-${src_dir}/dist/jot-windows-x86_64}
out_file=${2:-${src_dir}/dist/jot-windows-x86_64-setup.exe}
version=${3:-}

if [ -z "${version}" ]; then
  version=$(git -C "${src_dir}" describe --tags --abbrev=0 2>/dev/null || true)
fi
version=${version:-0.0.0}
display_version=${version#v}

# The staged tree has to be the one build-static-exe.sh produced: an installer
# built from anything else would ship a binary the import gate never saw, or an
# empty share/ tree that leaves the installed jot without its bundled runtime.
# Checked before makensis, because "build the exe first" is the fix a caller is
# more likely to need and the one they can act on without installing anything.
if [ ! -f "${stage_dir}/bin/jot.exe" ]; then
  echo "windows installer: no exe at ${stage_dir}/bin/jot.exe (run packaging/windows/build-static-exe.sh first)" >&2
  exit 1
fi
if [ ! -d "${stage_dir}/share/jot" ]; then
  echo "windows installer: no data tree at ${stage_dir}/share/jot" >&2
  exit 1
fi

if ! command -v makensis >/dev/null 2>&1; then
  echo "windows installer: missing makensis (Debian/Ubuntu: apt-get install nsis)" >&2
  exit 1
fi

# NSIS version resources demand four numeric fields; the tag may be a bare
# "3.2.0", a "v3.2.0" or something with a suffix. Each field keeps its leading
# digits and the missing fields are zero, so "v3.2.0-rc1" is 3.2.0.0, not
# 3.2.01.0 from the '1' inside the suffix.
version_number() {
  local out="" i part
  local IFS=.
  # shellcheck disable=SC2086
  set -- ${1#v}
  for i in 1 2 3 4; do
    part=${!i:-0}
    part=${part%%[!0-9]*}
    [ -n "${part}" ] || part=0
    out="${out}${out:+.}${part}"
  done
  printf '%s' "${out}"
}
version_num=$(version_number "${display_version}")

mkdir -p "$(dirname "${out_file}")"

echo "windows installer: building ${out_file} (jot ${display_version})"
# -V2 keeps the file-by-file log out of the way; the script reports what it
# built itself. Everything the script needs to say to the script is a -D:
# STAGE_DIR (the input tree), OUT_FILE, JOT_VERSION/JOT_VERSION_NUM (the
# version resources) and ICON_FILE (the same .ico the exe and the shortcuts
# use).
makensis -V2 \
  -DSTAGE_DIR="${stage_dir}" \
  -DOUT_FILE="${out_file}" \
  -DJOT_VERSION="${display_version}" \
  -DJOT_VERSION_NUM="${version_num}" \
  -DICON_FILE="${src_dir}/packaging/windows/jot.ico" \
  "${src_dir}/packaging/windows/jot.nsi"

if [ ! -f "${out_file}" ]; then
  echo "windows installer: makensis produced no ${out_file}" >&2
  exit 1
fi
# A PE executable starts with "MZ"; a truncated or text file would not.
if [ "$(head -c 2 "${out_file}")" != "MZ" ]; then
  echo "windows installer: ${out_file} is not a Windows executable" >&2
  exit 1
fi

echo "windows installer: $(du -h "${out_file}" | cut -f1) installer at ${out_file}"

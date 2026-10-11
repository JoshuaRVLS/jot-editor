#!/usr/bin/env bash
# Builds the Linux AppImage from the staged install tree, so `jot-*.AppImage`
# is a single file the user downloads, marks executable and runs -- no package
# manager, no `apt install libvterm`, and no unpacking into a directory of its
# own. It takes the tree the release job staged with `cmake --install`, the same
# one the .tar.gz and the Arch package are cut from, so all three hold the same
# binary and the same share/jot data.
#
# What makes it portable is the library sweep below: everything the binary
# links against except the pieces only the host can provide (glibc, the
# compiler runtime, the GPU driver stack) travels inside the AppImage, so a
# distro whose libuv, tree-sitter or Lua is a different version -- or missing
# entirely -- still runs this build.
#
# Usage: packaging/linux/build-appimage.sh [stage-dir] [out-file] [version]
#   stage-dir  staged tree (bin/jot + share/jot), default dist/jot-linux-x86_64
#   out-file   AppImage to write, default dist/jot-linux-x86_64.AppImage
#   version    recorded in the .desktop file's X-AppImage-Version; defaults to
#              the newest git tag.
#
# Environment:
#   APPIMAGETOOL  path to appimagetool (skips the download)
#   JOT_APPIMAGE_ARCH  AppImage ARCH, default x86_64
# Exit codes: 0 ok, 1 missing input, a failed download, or a failed build.
set -euo pipefail

src_dir=$(cd "$(dirname "$0")/../.." && pwd)
stage_dir=${1:-${src_dir}/dist/jot-linux-x86_64}
out_file=${2:-${src_dir}/dist/jot-linux-x86_64.AppImage}
version=${3:-}

if [ -z "${version}" ]; then
  version=$(git -C "${src_dir}" describe --tags --abbrev=0 2>/dev/null || true)
fi
version=${version:-0.0.0}
display_version=${version#v}
appimage_arch=${JOT_APPIMAGE_ARCH:-x86_64}

# The staged tree is the input, and an AppImage built from anything else would
# ship a binary the release gate never saw or an empty share/ that leaves the
# user with an editor and no runtime. Checked first, because "stage it first" is
# the fix a caller can act on.
if [ ! -x "${stage_dir}/bin/jot" ]; then
  echo "appimage: no binary at ${stage_dir}/bin/jot (run cmake --install into it first)" >&2
  exit 1
fi
if [ ! -d "${stage_dir}/share/jot" ]; then
  echo "appimage: no data tree at ${stage_dir}/share/jot" >&2
  exit 1
fi

work_dir=$(mktemp -d)
trap 'rm -rf "${work_dir}"' EXIT
appdir=${work_dir}/jot.AppDir
mkdir -p "${appdir}/usr/bin" "${appdir}/usr/lib" "${appdir}/usr/share"

cp -a "${stage_dir}/bin/jot" "${appdir}/usr/bin/jot"
cp -a "${stage_dir}/share/jot" "${appdir}/usr/share/jot"

# ── the library sweep ────────────────────────────────────────────────────────
# Every soname the binary resolves through ldd is copied in under that soname
# (cp -L follows the version suffix to the real file), which is what the
# dynamic loader will look for at runtime. The host keeps the pieces below: the
# loader and libc must be the host's or nothing else on the machine would work,
# libgcc_s unwinds into the host's libc, and the GL/DRM stack is the graphics
# driver's -- bundling it would hand the user a driver that cannot talk to their
# GPU. A soname that ldd cannot resolve is a build-machine problem, not
# something to paper over: the sweep fails instead of writing an AppImage that
# only starts on the machine that built it.
echo "appimage: bundling libraries"
ldd "${stage_dir}/bin/jot" > "${work_dir}/ldd.txt"
awk '$2 == "=>" { print $1, $3 }' "${work_dir}/ldd.txt" > "${work_dir}/libs.txt"
if grep -q "not found" "${work_dir}/ldd.txt"; then
  echo "appimage: FAIL - the binary needs libraries this machine does not have:" >&2
  grep "not found" "${work_dir}/ldd.txt" >&2
  exit 1
fi

never_bundle=(
  'ld-linux*' 'linux-vdso.so*'
  'libc.so.6' 'libm.so.6' 'libdl.so.2' 'libpthread.so.0' 'librt.so.1'
  'libnsl.so.1' 'libresolv.so.2' 'libutil.so.1' 'libgcc_s.so.1'
  'libGL.so*' 'libEGL.so*' 'libGLX.so*' 'libGLdispatch.so*' 'libOpenGL.so*'
  'libdrm.so*' 'libgbm.so*' 'libvulkan.so*'
)
bundled=0
while read -r soname path; do
  [ -n "${soname}" ] || continue
  # ldd prints the loader itself as "/lib64/ld-linux-x86-64.so.2 => ...", so the
  # name is taken as a basename before it is matched or copied: the host's
  # loader is one of the pieces below, and a path in the destination would
  # write outside usr/lib.
  soname=$(basename "${soname}")
  skip=0
  for pattern in "${never_bundle[@]}"; do
    # shellcheck disable=SC2254 # the patterns are globs on purpose
    case "${soname}" in ${pattern}) skip=1 ;; esac
  done
  if [ "${skip}" = 1 ]; then
    continue
  fi
  cp -L "${path}" "${appdir}/usr/lib/${soname}"
  bundled=$((bundled + 1))
done < "${work_dir}/libs.txt"
echo "appimage: ${bundled} libraries bundled, $(du -sh "${appdir}/usr/lib" | cut -f1) on disk"

# ── the entry point ──────────────────────────────────────────────────────────
# AppRun is what the runtime executes: it prepends the bundled libraries to
# LD_LIBRARY_PATH (the binary has no RPATH, and it should not carry one -- the
# same binary is shipped as a tarball and in a package), then hands over. The
# data directory needs nothing: the binary finds share/jot from its own path
# (src/base/exe_path.h), and usr/bin -> usr/share is the layout it expects.
cat > "${appdir}/AppRun" <<'APPRUN'
#!/bin/sh
# Entry point of the jot AppImage: run the bundled editor with the bundled
# libraries, wherever the AppImage happens to be mounted.
HERE=$(dirname "$(readlink -f "$0")")
export LD_LIBRARY_PATH="${HERE}/usr/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
exec "${HERE}/usr/bin/jot" "$@"
APPRUN
chmod 755 "${appdir}/AppRun"

# Terminal=true, because jot is a terminal editor: a file manager that launches
# this desktop entry has to give it a terminal, and the AppImage runtime has no
# way to say that on its own.
cat > "${appdir}/jot.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=jot
GenericName=Text Editor
Comment=A modal-free terminal code editor
Exec=jot %F
Icon=jot
Terminal=true
Categories=Development;TextEditor;Utility;
Keywords=editor;text;code;terminal;
X-AppImage-Version=${display_version}
DESKTOP

# The window icon, and the icon the desktop file names: appimagetool wants the
# PNG at the AppDir root by the name `Icon=` gives, and .DirIcon is what file
# managers show for the file itself.
if [ -f "${src_dir}/packaging/windows/icon.png" ]; then
  cp -a "${src_dir}/packaging/windows/icon.png" "${appdir}/jot.png"
  cp -a "${src_dir}/packaging/windows/icon.png" "${appdir}/.DirIcon"
else
  echo "appimage: warning - no packaging/windows/icon.png, the AppImage carries no icon" >&2
fi

# ── appimagetool ─────────────────────────────────────────────────────────────
tool=${APPIMAGETOOL:-}
if [ -z "${tool}" ]; then
  tool=${work_dir}/appimagetool
  url="https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-${appimage_arch}.AppImage"
  echo "appimage: fetching ${url}"
  if ! curl -fsSL -o "${tool}" "${url}"; then
    echo "appimage: FAIL - could not download appimagetool (set APPIMAGETOOL to a local copy)" >&2
    exit 1
  fi
  chmod 755 "${tool}"
fi
if [ ! -x "${tool}" ]; then
  echo "appimage: FAIL - ${tool} is not executable" >&2
  exit 1
fi

mkdir -p "$(dirname "${out_file}")"
echo "appimage: building ${out_file} (jot ${display_version})"
# --appimage-extract-and-run: CI has no FUSE, and asking the user to install
# fuse2 to cut a release would be a bug in the release, not in their machine.
# --no-appstream: no AppStream metadata to generate for a single-binary project,
# and the step would otherwise fail on a missing appstreamcli.
ARCH="${appimage_arch}" "${tool}" --appimage-extract-and-run --no-appstream "${appdir}" "${out_file}"
if [ ! -s "${out_file}" ]; then
  echo "appimage: FAIL - appimagetool wrote no ${out_file}" >&2
  exit 1
fi

# The packaging is verified, not assumed: an AppImage is an executable with a
# squashfs image appended, so extracting it through its own runtime is exactly
# what a user's launch does, minus the editor running. A missing AppRun or an
# unbundled library would still be caught here rather than by the first person
# to download it.
echo "appimage: verifying the image"
verify_dir=${work_dir}/verify
mkdir -p "${verify_dir}"
(cd "${verify_dir}" && "${out_file}" --appimage-extract >/dev/null)
for needed in squashfs-root/AppRun squashfs-root/usr/bin/jot squashfs-root/usr/share/jot; do
  if [ ! -e "${verify_dir}/${needed}" ]; then
    echo "appimage: FAIL - ${needed} is missing from the image" >&2
    exit 1
  fi
done
if [ ! -x "${verify_dir}/squashfs-root/AppRun" ]; then
  echo "appimage: FAIL - AppRun is not executable inside the image" >&2
  exit 1
fi

echo "appimage: $(du -h "${out_file}" | cut -f1) AppImage at ${out_file}"

#!/usr/bin/env bash
# Builds the Arch package (.pkg.tar.zst) from the staged install tree, so Arch
# and CachyOS/EndeavourOS users get `pacman -U jot-<version>-1-x86_64.pkg.tar.zst`
# instead of unpacking a tarball into ~/.local by hand.
#
# The package is assembled here rather than through makepkg, because the release
# job runs on an Ubuntu runner where makepkg (and pacman) do not exist. What
# makepkg would put in the archive is what this writes: .PKGINFO, a .MTREE, and
# the files under usr/. The metadata is not guessed either -- the dependencies
# are read out of the binary's own NEEDED entries and mapped to the packages
# that provide them on Arch, so a new library cannot ship without its
# dependency, and an entry nobody has mapped stops the build instead of
# installing an editor that will not start.
#
# Usage: packaging/linux/make-arch-package.sh [stage-dir] [out-dir] [version] [pkgrel]
#   stage-dir  staged tree (bin/jot + share/jot), default dist/jot-linux-x86_64
#   out-dir    directory to write the package into, default dist
#   version    package version, default the newest git tag (v3.3.0 -> 3.3.0)
#   pkgrel     package release, default 1
# Exit codes: 0 ok, 1 missing input or a failed assembly/verification.
set -euo pipefail

src_dir=$(cd "$(dirname "$0")/../.." && pwd)
stage_dir=${1:-${src_dir}/dist/jot-linux-x86_64}
out_dir=${2:-${src_dir}/dist}
version=${3:-}
pkgrel=${4:-1}

if [ -z "${version}" ]; then
  version=$(git -C "${src_dir}" describe --tags --abbrev=0 2>/dev/null || true)
fi
version=${version:-0.0.0}
pkgver=${version#v}
# Arch's version fields take no dash (that is the separator before pkgrel), so a
# pre-release tag keeps its suffix as an underscore.
pkgver=${pkgver//-/_}
if ! printf '%s' "${pkgver}" | grep -qE '^[A-Za-z0-9._+]+$'; then
  echo "arch package: unusable version '${version}'" >&2
  exit 1
fi

if [ ! -x "${stage_dir}/bin/jot" ]; then
  echo "arch package: no binary at ${stage_dir}/bin/jot (run cmake --install into it first)" >&2
  exit 1
fi
if [ ! -d "${stage_dir}/share/jot" ]; then
  echo "arch package: no data tree at ${stage_dir}/share/jot" >&2
  exit 1
fi

# ── dependencies, read off the binary ────────────────────────────────────────
# NEEDED entries, not the full ldd closure: what curl itself needs on Arch is
# `curl`'s own dependency list, and repeating its transitive libraries here
# would pin versions the distro does not.
needed_tool=$(command -v readelf || command -v objdump || true)
if [ -z "${needed_tool}" ]; then
  echo "arch package: missing readelf/objdump (Debian/Ubuntu: apt-get install binutils)" >&2
  exit 1
fi
echo "arch package: reading dependencies from ${stage_dir}/bin/jot"

# soname pattern -> Arch package. The core entries are provided by base (glibc)
# and gcc-libs, which every Arch install has, so they need no dependency line.
# Keep the table sorted the way the failure message prints it.
map_soname() {
  case "$1" in
    libc.so.6 | libm.so.6 | libdl.so.2 | libpthread.so.0 | librt.so.1 | libresolv.so.2 | libnsl.so.1 | libutil.so.1 | libstdc++.so.6 | libgcc_s.so.1)
      printf '%s' ''
      ;;
    libuv.so.*) printf '%s' libuv ;;
    libtermkey.so.*) printf '%s' libtermkey ;;
    libutf8proc.so.*) printf '%s' libutf8proc ;;
    libtree-sitter.so.*) printf '%s' tree-sitter ;;
    libcurl.so.*) printf '%s' curl ;;
    liblua5.4.so.*) printf '%s' lua54 ;;
    libvterm.so.*) printf '%s' libvterm ;;
    # Only when the GUI frontend was compiled in (SDL2 + FreeType found at
    # configure time). Current Arch provides the SDL2 soname through
    # sdl2-compat, the SDL3-based drop-in.
    libSDL2-2.0.so.0) printf '%s' sdl2-compat ;;
    libfreetype.so.6) printf '%s' freetype2 ;;
    libharfbuzz.so.0) printf '%s' harfbuzz ;;
    *) printf '%s' '?' ;;
  esac
}

needed_sonames=$(
  if command -v readelf >/dev/null 2>&1; then
    readelf -d "${stage_dir}/bin/jot" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p'
  else
    objdump -p "${stage_dir}/bin/jot" | sed -n 's/^\s*NEEDED\s*//p'
  fi | sort -u
)
if [ -z "${needed_sonames}" ]; then
  echo "arch package: FAIL - could not read the binary's NEEDED libraries" >&2
  exit 1
fi

depends=()
for soname in ${needed_sonames}; do
  package=$(map_soname "${soname}")
  if [ "${package}" = '?' ]; then
    echo "arch package: FAIL - no Arch package mapped for ${soname}." >&2
    echo "  Add it to map_soname() in $0 (check: pacman -F ${soname})." >&2
    exit 1
  fi
  if [ -n "${package}" ]; then
    depends+=("${package}")
  fi
done
if [ ${#depends[@]} -gt 0 ]; then
  echo "arch package: depends on $(printf '%s ' "${depends[@]}")"
else
  echo "arch package: no external dependencies beyond base"
fi

# ── the package root ─────────────────────────────────────────────────────────
# usr/bin and usr/share are the layout the binary expects (it finds share/jot
# from its own path), which is also the layout the tarball and the AppImage use,
# so all three hold the same tree.
work_dir=$(mktemp -d)
trap 'rm -rf "${work_dir}"' EXIT
pkg_root=${work_dir}/pkg
mkdir -p "${pkg_root}/usr/bin" "${pkg_root}/usr/share" "${pkg_root}/usr/share/licenses/jot"

cp -a "${stage_dir}/bin/jot" "${pkg_root}/usr/bin/jot"
cp -a "${stage_dir}/share/jot" "${pkg_root}/usr/share/jot"
if [ -f "${src_dir}/LICENSE" ]; then
  cp -a "${src_dir}/LICENSE" "${pkg_root}/usr/share/licenses/jot/LICENSE"
fi

pkgname=jot
pkgfile=${out_dir}/${pkgname}-${pkgver}-${pkgrel}-x86_64.pkg.tar.zst
pkgdesc="A modal-free terminal code editor with a GPU frontend (jot --gui)"
builddate=$(date +%s)
# What pacman reports as the installed size: the sum of the file sizes, the
# same number makepkg records.
pkg_size=$(du -sb "${pkg_root}" | cut -f1)

{
  echo "# Generated by packaging/linux/make-arch-package.sh"
  echo "pkgname = ${pkgname}"
  echo "pkgbase = ${pkgname}"
  # One version field, release included: pacman has no `pkgrel` key and rejects
  # a description that carries one (the filename still spells it out).
  echo "pkgver = ${pkgver}-${pkgrel}"
  echo "pkgdesc = ${pkgdesc}"
  echo "url = https://github.com/JoshuaRVLS/jot-editor"
  echo "builddate = ${builddate}"
  echo "packager = jot release workflow"
  echo "size = ${pkg_size}"
  echo "arch = x86_64"
  echo "license = MIT"
  for dep in ${depends[@]+"${depends[@]}"}; do
    echo "depend = ${dep}"
  done
} > "${pkg_root}/.PKGINFO"

# The .MTREE is what `pacman -Qkk` verifies an installation against. It is
# optional for pacman itself, so a runner without bsdtar still produces a
# package -- but the release job installs libarchive-tools to get one, and this
# says so rather than leaving it to be noticed later.
if command -v bsdtar >/dev/null 2>&1; then
  (cd "${pkg_root}" && bsdtar -czf .MTREE --format=mtree \
    --options='!all,use-set,type,uid,gid,mode,time,size,md5,sha256,link' \
    .PKGINFO usr)
else
  echo "arch package: warning - no bsdtar, writing the package without .MTREE" >&2
fi

# ── the archive ──────────────────────────────────────────────────────────────
# makepkg's own settings: zstd, root-owned entries, and one deterministic
# timestamp so the same tree produces the same package. .PKGINFO goes in first,
# the way makepkg writes it.
mkdir -p "${out_dir}"
echo "arch package: building ${pkgfile}"
entries=(.PKGINFO usr)
if [ -f "${pkg_root}/.MTREE" ]; then
  entries=(.PKGINFO .MTREE usr)
fi
tar --owner=0 --group=0 --numeric-owner --mtime="@${builddate}" \
  --use-compress-program='zstd -T0 -19 -q' \
  -cf "${pkgfile}" -C "${pkg_root}" "${entries[@]}"

if [ ! -s "${pkgfile}" ]; then
  echo "arch package: FAIL - no package at ${pkgfile}" >&2
  exit 1
fi

# Verification, not assumption: the archive has to decompress, hold the pieces
# a package is made of, and -- when pacman is around (a local build, or any
# Arch runner) -- parse as metadata and as a file list. The end-to-end install
# path is `pacman -U`, which needs a real root, so the checks stop at what
# pacman reads out of the file.
echo "arch package: verifying ${pkgfile}"
zstd -tq "${pkgfile}"
listing=$(tar -tf "${pkgfile}")
for needed in .PKGINFO usr/bin/jot usr/share/jot; do
  # tar lists the directories with a trailing slash and the files without one;
  # the dot in .PKGINFO is escaped so it cannot match another character.
  if ! printf '%s\n' "${listing}" | grep -qE "^\.?/?${needed//./\\.}/?$"; then
    echo "arch package: FAIL - ${needed} is missing from the package" >&2
    exit 1
  fi
done
if command -v pacman >/dev/null 2>&1; then
  pacman -Qip "${pkgfile}" >/dev/null
  pacman -Qlp "${pkgfile}" >/dev/null
  echo "arch package: pacman parsed the metadata and the file list"
else
  echo "arch package: pacman not installed here, metadata checked structurally"
fi

echo "arch package: $(du -h "${pkgfile}" | cut -f1) package at ${pkgfile}"

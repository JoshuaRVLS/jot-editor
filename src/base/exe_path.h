// Locating the tree the running binary was shipped in (src/base/exe_path.h).
//
// The release archives are unpacked wherever the user wants them and the
// per-user Windows installer writes to a directory the build machine cannot
// know, so a data path baked in at configure time (JOT_DEFAULT_DATA_DIR) is a
// guess. Everything that has to find bundled data next to the binary - the Lua
// runtime, the colour schemes, the vendored LSP payload - asks here instead of
// each keeping its own GetModuleFileNameW / _NSGetExecutablePath / procfs copy.
//
// Header-only and dependency-free, like base/shell_util.h.

#ifndef EXE_PATH_H
#define EXE_PATH_H

#include <filesystem>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <climits>
#include <mach-o/dyld.h>
#else
#include <climits>
#include <unistd.h>
#endif

namespace exe_path
{
  // The directory the running executable lives in, or an empty path when the
  // platform will not say. Linux reads /proc/self/exe, which resolves a
  // symlinked `jot` to the real binary; macOS asks _NSGetExecutablePath, which
  // is the same view of the world.
  inline std::filesystem::path dir()
  {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH)
    {
      return std::filesystem::path(buf).parent_path();
    }
#elif defined(__APPLE__)
    char buf[PATH_MAX];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0)
    {
      return std::filesystem::path(buf).parent_path();
    }
#else
    char buf[PATH_MAX];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0)
    {
      buf[n] = '\0';
      return std::filesystem::path(buf).parent_path();
    }
#endif
    return {};
  }

  // The data directory of the installed layout, which every artifact uses:
  //
  //   <root>/bin/jot        <root>/share/jot/{configs,lua,payload}
  //
  // so it is <dir>/../share/jot. The binary's own directory is tried as well,
  // because a binary unpacked beside its data is the same tree flattened one
  // level, and neither costs anything to check. Empty when neither exists: a
  // caller that finds nothing here falls back to the embedded bytes / the
  // compile-time prefix / built-in defaults, in that order.
  //
  // The rule is a separate entry point from the platform lookup so a test can
  // drive it with a directory it made (test/test_exe_path.cpp).
  inline std::filesystem::path data_dir_from(const std::filesystem::path &exe_dir)
  {
    if (exe_dir.empty())
    {
      return {};
    }
    std::error_code ec;
    const std::filesystem::path installed = exe_dir.parent_path() / "share" / "jot";
    if (std::filesystem::is_directory(installed, ec))
    {
      return installed;
    }
    const std::filesystem::path beside = exe_dir / "share" / "jot";
    if (std::filesystem::is_directory(beside, ec))
    {
      return beside;
    }
    return {};
  }

  // data_dir_from() for the running binary.
  inline std::filesystem::path data_dir()
  {
    return data_dir_from(dir());
  }
} // namespace exe_path

#endif // EXE_PATH_H

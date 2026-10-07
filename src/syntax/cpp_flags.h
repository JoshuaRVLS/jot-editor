#ifndef CPP_FLAGS_H
#define CPP_FLAGS_H

#include <string>
#include <vector>

// The flags clangd needs to judge C and C++ code.
//
// A language server without the project's compile flags parses every file with
// the compiler's own default standard, and on modern code that reads as broken
// rather than as "not configured": a `requires` clause is a missing ';', a
// concept name is an unknown type, `<=>` is a bad operator. The errors land on
// lines that hold no mistake at all.
//
// clangd looks for a compile_commands.json beside the sources and in the
// directories above them. These helpers cover the two cases that leaves:
//
//   * the database is in the build directory (`build/compile_commands.json`,
//     which is what CMake writes and what most projects never copy to the
//     root), and clangd does not look there, and
//   * there is no database anywhere -- a folder of sources, a checkout before
//     the first configure -- where the standard has to come from somewhere.
namespace CppFlags
{
  // Extra clangd arguments for this file: `--compile-commands-dir=<dir>` aimed at
  // the project's database, or at a generated one when the project has neither.
  // Empty when clangd's own search already finds the flags.
  std::vector<std::string> clangd_args_for(const std::string &root, const std::string &filepath);

  // Directory holding the generated database for a workspace. It lives under the
  // data directory, so a project is never written to.
  std::string fallback_database_dir(const std::string &root);

  // The C and C++ sources under `root`, output directories skipped, sorted.
  // Exposed for the tests.
  std::vector<std::string> source_files_under(const std::string &root);
} // namespace CppFlags

#endif

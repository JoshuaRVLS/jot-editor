#include "tree_sitter/install.h"
#include "tree_sitter/catalog.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace {
std::string shell_quote(const std::string &s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'') {
      out += "'\\''";
    } else {
      out += c;
    }
  }
  out += "'";
  return out;
}

std::string shell_var_quote(const std::string &s) {
  return shell_quote(s);
}

std::string library_stem(const TreeSitterCatalogEntry &entry) {
  std::string name = entry.library_names.empty()
                         ? ("libtree-sitter-" + entry.name + ".so")
                         : entry.library_names.front();
  if (name.size() > 3 && name.substr(name.size() - 3) == ".so") {
    name.erase(name.size() - 3);
  } else if (name.size() > 6 && name.substr(name.size() - 6) == ".dylib") {
    name.erase(name.size() - 6);
  }
  return name;
}

std::string source_build_command(const TreeSitterCatalogEntry &entry,
                                 const std::string &prefix) {
  const std::string lib_stem = library_stem(entry);
  std::ostringstream cmd;
  cmd << "set -e; ";
  cmd << "trap 'rc=$?; if [ \"$rc\" -ne 0 ]; then echo \"[jot:treesitter] "
         "failed "
      << entry.name << " exit=$rc\"; fi' EXIT; ";
  cmd << "echo '[jot:treesitter] start " << entry.name << "'; ";
  if (prefix.empty()) {
    cmd << "prefix=\"${JOT_TREESITTER_PREFIX:-$HOME/.local}\"; ";
  } else {
    cmd << "prefix=" << shell_var_quote(prefix) << "; ";
  }
  cmd << "libdir=\"$prefix/lib/jot/tree-sitter\"; ";
  cmd << "querydir=\"$prefix/share/jot/treesitter/queries/"
      << entry.name << "\"; ";
  cmd << "case \"$(uname)\" in Darwin) libext=dylib; linkflag=-dynamiclib ;; "
         "*) libext=so; linkflag=-shared ;; esac; ";
  cmd << "libfile=\"" << lib_stem << ".$libext\"; ";
  cmd << "work=\"${TMPDIR:-/tmp}/jot-tree-sitter-" << entry.name << "\"; ";
  cmd << "rm -rf \"$work\"; ";
  cmd << "mkdir -p \"$libdir\" \"$querydir\"; ";
  cmd << "echo '[jot:treesitter] clone " << entry.name << "'; ";
  cmd << "git clone --depth 1 " << shell_quote(entry.url) << " \"$work\"; ";
  cmd << "src=\"$work";
  if (!entry.source_subdir.empty()) {
    cmd << "/" << entry.source_subdir;
  }
  cmd << "/src\"; ";
  cmd << "objdir=\"$work/.jot-build\"; ";
  cmd << "mkdir -p \"$objdir\"; ";
  cmd << "cc=${CC:-cc}; cxx=${CXX:-c++}; ";
  cmd << "echo '[jot:treesitter] build " << entry.name << "'; ";
  cmd << "set --; ";
  cmd << "if [ -f \"$src/parser.c\" ]; then "
         "$cc -fPIC -I\"$src\" -c \"$src/parser.c\" -o \"$objdir/parser.o\"; "
         "set -- \"$@\" \"$objdir/parser.o\"; "
         "fi; ";
  cmd << "if [ -f \"$src/scanner.c\" ]; then "
         "$cc -fPIC -I\"$src\" -c \"$src/scanner.c\" -o \"$objdir/scanner_c.o\"; "
         "set -- \"$@\" \"$objdir/scanner_c.o\"; "
         "fi; ";
  cmd << "if [ -f \"$src/scanner.cc\" ]; then "
         "$cxx -fPIC -I\"$src\" -c \"$src/scanner.cc\" -o \"$objdir/scanner_cc.o\"; "
         "set -- \"$@\" \"$objdir/scanner_cc.o\"; "
         "fi; ";
  cmd << "if [ \"$#\" -eq 0 ]; then "
         "echo '[jot:treesitter] No generated parser sources found.'; exit 1; "
         "fi; ";
  cmd << "echo '[jot:treesitter] link " << entry.name << "'; ";
  cmd << "$cxx \"$linkflag\" \"$@\" -o \"$libdir/$libfile\"; ";
  cmd << "echo '[jot:treesitter] query " << entry.name << "'; ";
  cmd << "if [ -d \"$work/queries\" ]; then "
         "find \"$work/queries\" -maxdepth 1 -type f -name '*.scm' "
         "-exec cp {} \"$querydir/\" \\;; "
         "fi; ";
  cmd << "if [ -f \"$work/highlights.scm\" ]; then "
         "cp \"$work/highlights.scm\" \"$querydir/highlights.scm\"; "
         "fi; ";
  cmd << "echo '[jot:treesitter] success " << entry.name << "'; ";
  cmd << "trap - EXIT";
  return cmd.str();
}
} // namespace

namespace TreeSitterInstall {
const std::vector<std::string> &supported_languages() {
  static const std::vector<std::string> languages =
      TreeSitterCatalog::language_names();
  return languages;
}

bool is_supported_language(const std::string &language) {
  std::string normalized = TreeSitterCatalog::normalize_language_name(language);
  return TreeSitterCatalog::find_language(normalized) != nullptr ||
         TreeSitterCatalog::is_github_url(language);
}

TreeSitterInstallCommand command_for_language(const std::string &language) {
  return command_for_language(language, "");
}

TreeSitterInstallCommand command_for_language(const std::string &language,
                                             const std::string &prefix) {
  TreeSitterInstallCommand result;
  result.language = TreeSitterCatalog::normalize_language_name(language);
  TreeSitterCatalogEntry url_entry;
  const TreeSitterCatalogEntry *entry = TreeSitterCatalog::find_language(result.language);
  if (!entry && TreeSitterCatalog::is_github_url(language)) {
    url_entry = TreeSitterCatalog::entry_for_github_url(language);
    entry = &url_entry;
    result.language = entry->name;
  }
  if (!entry) {
    result.message = "Unsupported Tree-sitter language: " + language;
    return result;
  }

  result.supported = true;
  result.command = source_build_command(*entry, prefix);
  result.message =
      "Installing Tree-sitter " + result.language + " in terminal";
  return result;
}
} // namespace TreeSitterInstall

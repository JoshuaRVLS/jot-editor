#include "tree_sitter/install.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_map>

namespace
{
  std::unordered_map<std::string, TreeSitterInstallMetadata> metadata;
  std::string shell_quote(const std::string &s)
  {
    std::string out = "'";
    for (char c : s)
    {
      if (c == '\'')
      {
        out += "'\\''";
      }
      else
      {
        out += c;
      }
    }
    out += "'";
    return out;
  }

  std::string shell_var_quote(const std::string &s)
  {
    return shell_quote(s);
  }

  std::string library_stem(const TreeSitterInstallMetadata &entry)
  {
    std::string name = entry.library_names.empty() ? ("libtree-sitter-" + entry.name + ".so")
                                                   : entry.library_names.front();
    if (name.size() > 3 && name.substr(name.size() - 3) == ".so")
    {
      name.erase(name.size() - 3);
    }
    else if (name.size() > 6 && name.substr(name.size() - 6) == ".dylib")
    {
      name.erase(name.size() - 6);
    }
    return name;
  }

  std::string normalize_install_language(const std::string &language)
  {
    std::string normalized = language;
    std::transform(normalized.begin(),
                   normalized.end(),
                   normalized.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    for (char &c : normalized)
      if (c == '-' || c == ' ')
        c = '_';
    if (normalized == "jsx")
    {
      return "javascript";
    }
    return normalized;
  }

  std::string source_build_command(const TreeSitterInstallMetadata &entry,
                                   const std::string &prefix)
  {
    const std::string lib_stem = library_stem(entry);
    std::ostringstream cmd;
    cmd << "set -e; ";
    cmd << "trap 'rc=$?; if [ \"$rc\" -ne 0 ]; then echo \"[jot:treesitter] "
           "failed "
        << entry.name << " exit=$rc\"; fi' EXIT; ";
    cmd << "echo '[jot:treesitter] start " << entry.name << "'; ";
    if (prefix.empty())
    {
      cmd << "prefix=\"${JOT_TREESITTER_PREFIX:-${XDG_DATA_HOME:-$HOME/.local/share}/jot/"
             "treesitter}\"; ";
      cmd << "if ! mkdir -p \"$prefix\" 2>/dev/null || [ ! -w \"$prefix\" ]; then ";
      cmd << "prefix=\"${XDG_CACHE_HOME:-$HOME/.cache}/jot/treesitter\"; ";
      cmd << "if ! mkdir -p \"$prefix\" 2>/dev/null || [ ! -w \"$prefix\" ]; then echo "
             "\"[jot:treesitter] install root is not writable: $prefix\"; exit 1; fi; ";
      cmd << "echo \"[jot:treesitter] using cache fallback: $prefix\"; ";
      cmd << "fi; ";
    }
    else
    {
      cmd << "prefix=" << shell_var_quote(prefix) << "; ";
      cmd << "if ! mkdir -p \"$prefix\" 2>/dev/null || [ ! -w \"$prefix\" ]; then echo "
             "\"[jot:treesitter] install root is not writable: $prefix\"; exit 1; fi; ";
    }
    // Report the resolved install root so the editor can search it regardless
    // of environment differences between the editor and the shell.
    cmd << "echo \"[jot:treesitter] prefix $prefix\"; ";
    cmd << "libdir=\"$prefix/parsers\"; ";
    cmd << "querydir=\"$prefix/queries/" << entry.name << "\"; ";
    cmd << "case \"$(uname)\" in Darwin) libext=dylib; linkflag=-dynamiclib ;; "
           "*) libext=so; linkflag=-shared ;; esac; ";
    cmd << "libfile=\"" << lib_stem << ".$libext\"; ";
    cmd << "work_root=\"$(mktemp -d \"${TMPDIR:-/tmp}/jot-tree-sitter-XXXXXX\" 2>/dev/null || echo "
           "\"${TMPDIR:-/tmp}/jot-tree-sitter-"
        << entry.name << "-$$\")\"; ";
    cmd << "work=\"$work_root/repo\"; ";
    cmd << "mkdir -p \"$work_root\" 2>/dev/null || { echo \"[jot:treesitter] failed " << entry.name
        << "\"; exit 1; }; ";
    cmd << "mkdir -p \"$libdir\" \"$querydir\"; ";
    cmd << "echo '[jot:treesitter] clone " << entry.name << "'; ";
    cmd << "git clone --depth 1 " << shell_quote(entry.url) << " \"$work\"; ";
    cmd << "src=\"$work";
    if (!entry.source_subdir.empty())
    {
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
    cmd << "if [ ! -s \"$libdir/$libfile\" ]; then echo \"[jot:treesitter] failed " << entry.name
        << "\"; exit 1; fi; ";
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

namespace TreeSitterInstall
{
  void clear_languages()
  {
    metadata.clear();
  }

  void register_language(const TreeSitterInstallMetadata &entry)
  {
    if (!entry.name.empty())
      metadata[entry.name] = entry;
  }

  const std::vector<std::string> &supported_languages()
  {
    static std::vector<std::string> languages;
    languages.clear();
    for (const auto &entry : metadata)
      languages.push_back(entry.first);
    if (metadata.find("javascript") != metadata.end())
      languages.push_back("jsx");
    std::sort(languages.begin(), languages.end());
    return languages;
  }

  bool is_supported_language(const std::string &language)
  {
    std::string normalized = normalize_install_language(language);
    return metadata.find(normalized) != metadata.end()
           || language.rfind("https://github.com/", 0) == 0
           || language.rfind("github.com/", 0) == 0;
  }

  TreeSitterInstallCommand command_for_language(const std::string &language)
  {
    return command_for_language(language, "");
  }

  TreeSitterInstallCommand command_for_language(const std::string &language,
                                                const std::string &prefix)
  {
    TreeSitterInstallCommand result;
    result.language = normalize_install_language(language);
    TreeSitterInstallMetadata url_entry;
    auto found = metadata.find(result.language);
    const TreeSitterInstallMetadata *entry = found == metadata.end() ? nullptr : &found->second;
    if (!entry
        && (language.rfind("https://github.com/", 0) == 0 || language.rfind("github.com/", 0) == 0))
    {
      url_entry.url = language.rfind("github.com/", 0) == 0 ? "https://" + language : language;
      std::string repo = url_entry.url.substr(url_entry.url.find_last_of('/') + 1);
      if (repo.size() > 4 && repo.substr(repo.size() - 4) == ".git")
        repo.resize(repo.size() - 4);
      if (repo.rfind("tree-sitter-", 0) == 0)
        repo.erase(0, 12);
      url_entry.name = normalize_install_language(repo);
      url_entry.library_names = {"libtree-sitter-" + url_entry.name + ".so"};
      entry = &url_entry;
      result.language = entry->name;
    }
    if (!entry)
    {
      result.message = "Unsupported Tree-sitter language: " + language;
      return result;
    }

#ifdef _WIN32
    result.message = "Tree-sitter source install is not implemented on Windows; use parser DLLs";
    return result;
#else
    result.supported = true;
    result.command = source_build_command(*entry, prefix);
    result.message = "Installing Tree-sitter " + result.language + "…";
    return result;
#endif
  }
} // namespace TreeSitterInstall

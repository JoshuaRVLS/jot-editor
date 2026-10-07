#include "syntax/install.h"
#include "base/shell_util.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <unordered_map>

namespace fs = std::filesystem;

namespace
{
  std::unordered_map<std::string, TreeSitterInstallMetadata> metadata;

  std::string shell_var_quote(const std::string &s)
  {
    return shell_util::shell_quote(s);
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

  // Windows quoting for cmd.exe: double quotes, doubled inside.
  std::string win_quote(const std::string &value)
  {
    std::string out = "\"";
    for (char c : value)
    {
      if (c == '"')
      {
        out += "\"\"";
      }
      else
      {
        out.push_back(c);
      }
    }
    out.push_back('"');
    return out;
  }

  std::string win_join(const std::string &dir, const std::string &leaf)
  {
    if (dir.empty())
    {
      return leaf;
    }
    const char last = dir.back();
    return (last == '\\' || last == '/') ? dir + leaf : dir + "\\" + leaf;
  }

  std::string win_env(const char *name)
  {
    const char *value = std::getenv(name);
    return value && *value ? std::string(value) : std::string();
  }

  // Install root for a source build: the same chain the POSIX script walks in
  // its own shell, resolved here because a cmd command line expands every path
  // before it runs, so the prefix has to be a literal in the generated script.
  std::string windows_install_prefix(const std::string &override_prefix)
  {
    if (!override_prefix.empty())
    {
      return override_prefix;
    }
    const std::string explicit_prefix = win_env("JOT_TREESITTER_PREFIX");
    if (!explicit_prefix.empty())
    {
      return explicit_prefix;
    }
    // LOCALAPPDATA first, which is where the runtime looks for parsers.
    for (const char *name : {"LOCALAPPDATA", "APPDATA"})
    {
      const std::string base = win_env(name);
      if (!base.empty())
      {
        return (fs::path(base) / "jot" / "treesitter").string();
      }
    }
    const std::string home = win_env("USERPROFILE");
    if (!home.empty())
    {
      return (fs::path(home) / ".local" / "share" / "jot" / "treesitter").string();
    }
    return std::string();
  }

  std::string windows_temp_dir(const std::string &fallback)
  {
    for (const char *name : {"TEMP", "TMP"})
    {
      const std::string value = win_env(name);
      if (!value.empty())
      {
        return value;
      }
    }
    return fallback;
  }

  std::string windows_tool(const char *env_name, const char *fallback)
  {
    const std::string value = win_env(env_name);
    return value.empty() ? std::string(fallback) : value;
  }

  // Windows source build: the same steps rendered as cmd.exe lines, because the
  // POSIX script cannot run there (no sh, mktemp, uname or find). Success is
  // decided by the parser DLL being on disk, the one signal cmd cannot get
  // wrong.
  std::string source_build_command_win(const TreeSitterInstallMetadata &entry,
                                       const std::string &prefix_in)
  {
    const std::string prefix = windows_install_prefix(prefix_in);
    const std::string temp = windows_temp_dir(prefix);
    const std::string lib_stem = library_stem(entry);
    const std::string cc = windows_tool("CC", "gcc");
    const std::string cxx = windows_tool("CXX", "g++");
    const std::string libdir = win_join(prefix, "parsers");
    const std::string querydir = win_join(win_join(prefix, "queries"), entry.name);
    const std::string work = win_join(temp, "jot-tree-sitter-" + entry.name);
    const std::string src = entry.source_subdir.empty()
                                ? win_join(work, "src")
                                : win_join(win_join(work, entry.source_subdir), "src");
    const std::string libfile = win_join(libdir, lib_stem + ".dll");
    const std::string parser = win_join(src, "parser.c");
    const std::string scanner_c = win_join(src, "scanner.c");
    const std::string scanner_cc = win_join(src, "scanner.cc");

    // Static runtimes: the release ships no DLLs, so a parser DLL must not be
    // what reintroduces a libstdc++/libgcc dependency.
    const std::string cxx_flags = " -shared -o " + win_quote(libfile)
                                  + " -Wl,--export-all-symbols -static-libgcc"
                                    " -static-libstdc++ -I"
                                  + win_quote(src);

    std::ostringstream cmd;
    auto line = [&cmd](const std::string &text) { cmd << text << "\n"; };
    // Markers get their own short lines: the command is echoed into the
    // terminal it runs in, and a wrapped row must never start with a marker.
    line("echo [jot:treesitter] start " + entry.name);
    line("if not exist " + win_quote(prefix) + " mkdir " + win_quote(prefix));
    line("echo [jot:treesitter] prefix " + prefix);
    line("if not exist " + win_quote(libdir) + " mkdir " + win_quote(libdir));
    line("if not exist " + win_quote(querydir) + " mkdir " + win_quote(querydir));
    line("if exist " + win_quote(work) + " rd /s /q " + win_quote(work));
    line("echo [jot:treesitter] clone " + entry.name);
    line("git clone --depth 1 " + win_quote(entry.url) + " " + win_quote(work));
    line("echo [jot:treesitter] build " + entry.name);
    // -x forces the language per source: parser.c is C99, which the C++ front
    // end would reject.
    line("if exist " + win_quote(scanner_cc) + " " + cxx + cxx_flags + " -x c " + win_quote(parser)
         + " -x c++ " + win_quote(scanner_cc));
    line("if not exist " + win_quote(scanner_cc) + " if exist " + win_quote(scanner_c) + " " + cc
         + " -shared -o " + win_quote(libfile) + " -Wl,--export-all-symbols -I" + win_quote(src)
         + " " + win_quote(parser) + " " + win_quote(scanner_c));
    line("if not exist " + win_quote(scanner_cc) + " if not exist " + win_quote(scanner_c) + " "
         + cc + " -shared -o " + win_quote(libfile) + " -Wl,--export-all-symbols -I"
         + win_quote(src) + " " + win_quote(parser));
    line("echo [jot:treesitter] query " + entry.name);
    line("if exist " + win_quote(win_join(work, "queries") + "\\*.scm") + " copy /Y "
         + win_quote(win_join(work, "queries") + "\\*.scm") + " " + win_quote(querydir + "\\")
         + " >NUL");
    line("if exist " + win_quote(win_join(work, "highlights.scm")) + " copy /Y "
         + win_quote(win_join(work, "highlights.scm")) + " "
         + win_quote(win_join(querydir, "highlights.scm")) + " >NUL");
    line("if exist " + win_quote(work) + " rd /s /q " + win_quote(work));
    // The library on disk is the only honest completion signal: cmd has no
    // equivalent of `set -e`, so exit codes from the steps above are not one.
    line("set \"_jot_ts=\"");
    line("if exist " + win_quote(libfile) + " set \"_jot_ts=1\"");
    line("if defined _jot_ts echo [jot:treesitter] success " + entry.name);
    line("if not defined _jot_ts echo [jot:treesitter] failed " + entry.name + " exit=1");
    return cmd.str();
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
    cmd << "git clone --depth 1 " << shell_util::shell_quote(entry.url) << " \"$work\"; ";
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

    result.supported = true;
    const bool windows = shell_util::install_platform() == "win";
    if (windows && windows_install_prefix(prefix).empty())
    {
      // Every path in the generated script is literal, so with no install root
      // to write into there is nothing to generate.
      result.supported = false;
      result.message = "Tree-sitter install needs an install root: JOT_TREESITTER_PREFIX "
                       "or LOCALAPPDATA is not set";
      return result;
    }
    result.command =
        windows ? source_build_command_win(*entry, prefix) : source_build_command(*entry, prefix);
    result.message = "Installing Tree-sitter " + result.language + "…";
    return result;
  }
} // namespace TreeSitterInstall

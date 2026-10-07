// The compilation flags clangd is handed for a C or C++ workspace (see
// cpp_flags.h for why it needs them).
//
// The search is a short cascade, in the order of how much the answer can be
// trusted:
//
//   1. a database or a compile_flags.txt beside the sources or above them --
//      clangd finds those itself, so nothing is passed and its own rules (which
//      also cover interpolation between a header and a nearby source) stay in
//      charge;
//   2. a database in a build directory under the workspace, which is where a
//      configure puts it: that directory is passed as clangd's
//      --compile-commands-dir;
//   3. nothing at all, so a database is generated for the workspace and passed
//      the same way. It is written under the data directory, never into the
//      project, and it carries the standards and include directories a project
//      with no flags of its own would have had.
#include "cpp_flags.h"
#include "base/file_util.h"
#include "lsp/install.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

namespace
{
  constexpr const char *kDatabaseFile = "compile_commands.json";
  constexpr const char *kFlagsFile = "compile_flags.txt";

  // C++20 is the floor modern code is written against (clangd's own default is
  // the compiler's, gnu++17 here), and gnu17 for C so a `.c` file in the same
  // workspace is never handed a C++ standard, which clang rejects outright.
  constexpr const char *kCppStandard = "-std=gnu++20";
  constexpr const char *kCStandard = "-std=gnu17";

  bool is_regular(const fs::path &path)
  {
    std::error_code ec;
    return fs::is_regular_file(path, ec);
  }

  bool directory_exists(const fs::path &path)
  {
    std::error_code ec;
    return fs::is_directory(path, ec);
  }

  // A directory whose contents are build products rather than sources. Skipped
  // while scanning, so a generated database describes the project and not the
  // tree of objects and copies under it.
  bool is_output_directory(const std::string &name)
  {
    if (name.empty() || name[0] == '.' || name[0] == '_')
    {
      return true;
    }
    return name == "build" || name.rfind("build-", 0) == 0 || name == "cmake-build"
           || name.rfind("cmake-build-", 0) == 0 || name == "out" || name == "dist"
           || name == "node_modules" || name == "venv";
  }

  bool is_source_extension(const std::string &extension)
  {
    static const char *const kExtensions[] = {
        ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl", ".ipp", ".tpp"};
    for (const char *const candidate : kExtensions)
    {
      if (extension == candidate)
      {
        return true;
      }
    }
    return false;
  }

  // clangd's own search, kept in step with it: a compilation database or a
  // compile_flags.txt in the file's directory or any directory above it.
  bool flags_are_findable_from(const fs::path &filepath)
  {
    std::error_code ec;
    fs::path directory = fs::absolute(filepath, ec).parent_path();
    if (ec)
    {
      directory = filepath.parent_path();
    }
    for (int depth = 0; depth < 64 && !directory.empty(); depth++)
    {
      if (is_regular(directory / kDatabaseFile) || is_regular(directory / kFlagsFile))
      {
        return true;
      }
      const fs::path parent = directory.parent_path();
      if (parent == directory)
      {
        break;
      }
      directory = parent;
    }
    return false;
  }

  // A compile_commands.json one level below a directory the file lives in:
  // `build/`, `cmake-build-debug/`, `out/` -- where a configure leaves it, and
  // where clangd's own search never looks. The newest wins, since a tree
  // usually has more than one and the one configured last is the one in use.
  std::string database_in_build_directory(const std::string &root, const std::string &filepath)
  {
    struct Candidate
    {
      fs::path directory;
      fs::file_time_type stamp;
    };

    std::vector<Candidate> candidates;
    std::error_code ec;
    fs::path directory = fs::absolute(filepath, ec).parent_path();
    if (ec)
    {
      return {};
    }
    const fs::path limit = fs::absolute(root, ec);
    for (int depth = 0; depth < 64; depth++)
    {
      std::error_code iterate_error;
      for (fs::directory_iterator it(
               directory, fs::directory_options::skip_permission_denied, iterate_error);
           !iterate_error && it != fs::directory_iterator();
           it.increment(iterate_error))
      {
        std::error_code entry_error;
        if (!it->is_directory(entry_error) || entry_error)
        {
          continue;
        }
        const fs::path database = it->path() / kDatabaseFile;
        if (!is_regular(database))
        {
          continue;
        }
        std::error_code time_error;
        const fs::file_time_type stamp = fs::last_write_time(database, time_error);
        candidates.push_back({it->path(), time_error ? fs::file_time_type::min() : stamp});
      }

      if (directory == limit)
      {
        break;
      }
      const fs::path parent = directory.parent_path();
      if (parent == directory)
      {
        break;
      }
      directory = parent;
    }

    if (candidates.empty())
    {
      return {};
    }
    std::sort(candidates.begin(),
              candidates.end(),
              [](const Candidate &a, const Candidate &b)
              {
                if (a.stamp != b.stamp)
                {
                  return a.stamp > b.stamp;
                }
                return a.directory.string() < b.directory.string();
              });
    return candidates.front().directory.string();
  }

  std::string json_escape(const std::string &value)
  {
    std::string out;
    out.reserve(value.size() + 8);
    for (const char c : value)
    {
      switch (c)
      {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20)
        {
          char buffer[8];
          std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
          out += buffer;
        }
        else
        {
          out.push_back(c);
        }
      }
    }
    return out;
  }

  // One generated entry. The command names the dialect (C or C++, by the
  // file's own extension) and the workspace's include directories, which is
  // what a project's database would have carried.
  std::string database_entry(const std::string &root, const std::string &filepath)
  {
    const std::string extension = fs::path(filepath).extension().string();
    const bool c_file = extension == ".c";

    // Quoted, because a workspace under a path with a space in it is otherwise
    // tokenized into two arguments and the whole entry is useless.
    std::string command = c_file ? "clang " : "clang++ ";
    command += c_file ? kCStandard : kCppStandard;
    command += " -I\"";
    command += root;
    command += "\"";
    for (const char *const subdirectory : {"/src", "/include"})
    {
      if (directory_exists(fs::path(root) / (subdirectory + 1)))
      {
        command += " -I\"";
        command += root;
        command += subdirectory;
        command += "\"";
      }
    }
    command += " -c \"";
    command += filepath;
    command += "\"";

    std::ostringstream out;
    out << "  {\n"
        << "    \"directory\": \"" << json_escape(root) << "\",\n"
        << "    \"file\": \"" << json_escape(filepath) << "\",\n"
        << "    \"command\": \"" << json_escape(command) << "\"\n"
        << "  }";
    return out.str();
  }

  std::string database_text(const std::string &root, const std::vector<std::string> &files)
  {
    std::string text = "[\n";
    for (size_t i = 0; i < files.size(); i++)
    {
      if (i > 0)
      {
        text += ",\n";
      }
      text += database_entry(root, files[i]);
    }
    text += "\n]\n";
    return text;
  }

  // True when the database's text already carries an entry for `filepath`, so
  // the append can be repeated without the database growing a duplicate of the
  // same translation unit.
  bool database_lists(const std::string &text, const std::string &filepath)
  {
    return text.find("\"file\": \"" + json_escape(filepath) + "\"") != std::string::npos;
  }

  bool append_database_entry(std::string &text, const std::string &entry)
  {
    const size_t close = text.rfind(']');
    if (close == std::string::npos)
    {
      return false;
    }
    std::string head = text.substr(0, close);
    if (head.find('}') != std::string::npos)
    {
      head += ",\n";
    }
    head += entry;
    head += "\n]\n";
    text = std::move(head);
    return true;
  }

  std::string read_file(const std::string &path)
  {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open())
    {
      return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
  }

  // The generated database for a workspace, with an entry for `filepath`
  // guaranteed: a whole scan the first time, a single appended entry after
  // that, which is what keeps opening a second file (or a file created since
  // the scan) from costing another walk of the tree.
  std::string ensure_fallback_database(const std::string &root, const std::string &filepath)
  {
    const std::string directory = CppFlags::fallback_database_dir(root);
    if (directory.empty())
    {
      return {};
    }
    std::error_code ec;
    fs::create_directories(directory, ec);
    if (ec)
    {
      return {};
    }

    const std::string path = (fs::path(directory) / kDatabaseFile).string();
    std::error_code absolute_error;
    const std::string absolute = fs::absolute(filepath, absolute_error).string();
    if (absolute_error)
    {
      return {};
    }

    std::string text = read_file(path);
    if (!text.empty() && database_lists(text, absolute))
    {
      return directory; // already listed: nothing to rewrite
    }
    if (text.empty() || !append_database_entry(text, database_entry(root, absolute)))
    {
      // No database yet, or one too damaged to append to: a whole scan, with
      // the file at hand in it either way.
      std::vector<std::string> files = CppFlags::source_files_under(root);
      if (std::find(files.begin(), files.end(), absolute) == files.end())
      {
        files.push_back(absolute);
        std::sort(files.begin(), files.end());
      }
      text = database_text(root, files);
    }

    if (!file_util::write_file_atomic(path, text).empty())
    {
      return {};
    }
    return directory;
  }

  // A stable directory name for a workspace: std::hash is not required to give
  // the same answer between runs, and this database is reused by later
  // sessions, so the name is a small FNV-1a of the root path.
  std::string workspace_slug(const std::string &root)
  {
    uint64_t hash = 1469598103934665603ULL;
    for (const char c : root)
    {
      hash ^= static_cast<unsigned char>(c);
      hash *= 1099511628211ULL;
    }
    char buffer[17];
    std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(hash));
    return buffer;
  }
} // namespace

namespace CppFlags
{
  std::vector<std::string> clangd_args_for(const std::string &root, const std::string &filepath)
  {
    if (root.empty() || filepath.empty())
    {
      return {};
    }
    if (flags_are_findable_from(filepath))
    {
      return {};
    }
    const std::string build = database_in_build_directory(root, filepath);
    if (!build.empty())
    {
      return {"--compile-commands-dir=" + build};
    }
    const std::string generated = ensure_fallback_database(root, filepath);
    if (generated.empty())
    {
      return {};
    }
    return {"--compile-commands-dir=" + generated};
  }

  std::string fallback_database_dir(const std::string &root)
  {
    if (root.empty())
    {
      return {};
    }
    const std::string install_root = LspInstall::install_root();
    if (install_root.empty())
    {
      return {};
    }
    return (fs::path(install_root) / "clangd-cdb" / workspace_slug(root)).string();
  }

  std::vector<std::string> source_files_under(const std::string &root)
  {
    std::vector<std::string> files;
    std::error_code ec;
    const fs::path start(root);
    if (!directory_exists(start))
    {
      return files;
    }

    fs::recursive_directory_iterator it(start, fs::directory_options::skip_permission_denied, ec);
    for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
    {
      std::error_code entry_error;
      if (it->is_directory(entry_error))
      {
        if (entry_error || is_output_directory(it->path().filename().string()))
        {
          it.disable_recursion_pending();
        }
        continue;
      }
      if (!it->is_regular_file(entry_error) || entry_error)
      {
        continue;
      }
      if (is_source_extension(it->path().extension().string()))
      {
        files.push_back(it->path().string());
      }
    }
    std::sort(files.begin(), files.end());
    return files;
  }
} // namespace CppFlags

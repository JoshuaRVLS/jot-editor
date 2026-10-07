// The checker half of src/features/cpp_definitions.*: what the parsed records
// mean together.
//
// One signature is one key (scope, name, canonical parameters, cv/ref suffix),
// so the work is grouping by it and asking two questions:
//
//   * does a declaration have a body anywhere in the workspace, and
//   * does a signature have more than one body the linker would see?
//
// The rules for "a body that counts" are the ones C++ itself applies: `= 0` and
// `= delete` need none, `= default` writes its own, an `inline` / `constexpr` /
// template body and a class-body definition may repeat once per translation
// unit, and a `static` (or anonymous-namespace) body is private to its own. A
// body in a header that is *not* inline is reported separately -- it is the same
// multiple-definition error one translation unit away. A file-scope `main` is
// left out of the cross-file check: a folder of standalone programs has one
// entry point per translation unit and they are compiled one by one. When
// *every* source file in the workspace holds its own entry point the whole
// cross-file check goes quiet, since no two of those files can share a link.
#include "base/string_util.h"
#include "cpp_definitions.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace CppDefinitions
{
  namespace
  {
    namespace fs = std::filesystem;

    std::string location(const FunctionRecord &record)
    {
      return record.file + ":" + std::to_string(record.line + 1);
    }

    // The platform a translation unit belongs to, when its name says so:
    // `terminal_win32.cpp` and `discord_rpc_transport_unix.cpp` are alternatives
    // the build system picks between, so the same function in two differently
    // tagged files is not a duplicate. Two files with the *same* tag still are.
    std::string platform_tag(const std::string &path)
    {
      static const std::set<std::string> tags = {"win32",
                                                 "win64",
                                                 "windows",
                                                 "unix",
                                                 "posix",
                                                 "linux",
                                                 "mac",
                                                 "macos",
                                                 "darwin",
                                                 "apple",
                                                 "bsd",
                                                 "freebsd",
                                                 "android",
                                                 "ios",
                                                 "wasm",
                                                 "emscripten"};
      const fs::path file(path);
      const std::string stem = string_util::lower_copy(file.stem().string());
      for (size_t at = stem.find('_'); at != std::string::npos;)
      {
        const size_t end = stem.find('_', at + 1);
        const std::string part =
            stem.substr(at + 1, end == std::string::npos ? std::string::npos : end - at - 1);
        if (tags.count(part) != 0)
        {
          return part;
        }
        at = end;
      }
      for (const fs::path &component : file.parent_path())
      {
        const std::string name = string_util::lower_copy(component.string());
        if (tags.count(name) != 0)
        {
          return name;
        }
      }
      return std::string();
    }

    // Whether two scopes could be the same function seen from two places: they
    // share a component, or one of them is empty. Without this, a `clear()` in
    // one class points at a `clear()` in an unrelated one.
    bool scopes_related(const std::string &a, const std::string &b)
    {
      if (a.empty() || b.empty() || a == b)
      {
        return true;
      }
      for (size_t at = 0; at <= a.size();)
      {
        const size_t end = a.find("::", at);
        const std::string component =
            a.substr(at, end == std::string::npos ? std::string::npos : end - at);
        for (size_t bat = 0; bat <= b.size();)
        {
          const size_t bend = b.find("::", bat);
          const std::string other =
              b.substr(bat, bend == std::string::npos ? std::string::npos : bend - bat);
          if (component == other)
          {
            return true;
          }
          if (bend == std::string::npos)
          {
            break;
          }
          bat = bend + 2;
        }
        if (end == std::string::npos)
        {
          break;
        }
        at = end + 2;
      }
      return false;
    }

    // A file-scope `main` is a program's entry point, so a workspace may hold
    // one per translation unit on purpose: a folder of standalone programs
    // (competitive programming, samples, one-off probes) is compiled file by
    // file, and the duplicates the linker would see never meet. A `main` in a
    // namespace or class is an ordinary function and keeps the rule. When every
    // source file holds one, analyze() turns the whole cross-file check off
    // rather than exempting the entry points alone.
    bool is_program_entry(const FunctionRecord &record)
    {
      return record.name == "main" && record.scope.empty();
    }

    Issue issue_at(const FunctionRecord &record)
    {
      Issue issue;
      issue.file = record.file;
      issue.line = record.line;
      issue.col = record.col;
      issue.end_line = record.end_line;
      issue.end_col = record.end_col;
      return issue;
    }

    // Directories whose C++ is not this project's to check: build trees, VCS
    // metadata, vendored trees. The search walker skips the same names, with
    // every dot-directory covered by the prefix rule.
    bool should_skip_dir(const std::string &name)
    {
      if (name.empty() || name[0] == '.')
      {
        return true;
      }
      static const std::set<std::string> skipped = {"node_modules",
                                                    "dist",
                                                    "build",
                                                    "out",
                                                    "bin",
                                                    "obj",
                                                    "target",
                                                    "venv",
                                                    "external",
                                                    "third_party",
                                                    "vendor",
                                                    "deps",
                                                    "CMakeFiles",
                                                    "__pycache__"};
      if (skipped.count(name) != 0)
      {
        return true;
      }
      if (name.rfind("build-", 0) == 0 || name.rfind("build_", 0) == 0
          || name.rfind("cmake-build", 0) == 0)
      {
        return true;
      }
      return false;
    }
  } // namespace

  std::vector<Issue> analyze(const std::vector<FunctionRecord> &records, ScanStats *stats_out)
  {
    ScanStats stats;
    std::vector<Issue> issues;

    std::set<std::string> source_files;
    std::set<std::string> program_files;
    for (const FunctionRecord &record : records)
    {
      stats.classification_records++;
      if (!record.is_header)
      {
        source_files.insert(record.file);
      }
      if (record.definition && is_program_entry(record))
      {
        program_files.insert(record.file);
      }
    }

    // File classification is workspace-wide, not a per-signature question.
    const bool standalone_programs =
        !source_files.empty()
        && std::all_of(source_files.begin(),
                       source_files.end(),
                       [&](const std::string &file) { return program_files.count(file) != 0; });

    std::map<std::string, std::vector<const FunctionRecord *>> by_key;
    std::map<std::string, std::vector<const FunctionRecord *>> by_name;
    for (const FunctionRecord &record : records)
    {
      if (record.pure_virtual)
      {
        continue; // a pure virtual needs no body
      }
      by_key[record.key].push_back(&record);
      by_name[record.name].push_back(&record);
      if (record.definition)
      {
        stats.definitions++;
      }
      else
      {
        stats.declarations++;
      }
    }

    for (const auto &key_entry : by_key)
    {
      std::vector<const FunctionRecord *> declarations;
      std::vector<const FunctionRecord *> definitions;
      bool satisfied = false;
      for (const FunctionRecord *record : key_entry.second)
      {
        if (record->deleted_or_default)
        {
          satisfied = true; // `= default` writes the body, `= delete` forbids one
          continue;
        }
        if (record->definition)
        {
          definitions.push_back(record);
        }
        else
        {
          declarations.push_back(record);
        }
      }
      if (!definitions.empty())
      {
        satisfied = true;
      }

      // Missing implementations: a declaration with no body anywhere in the
      // workspace, from a header or from a source file of its own.
      {
        for (const FunctionRecord *decl : declarations)
        {
          // Separate programs cannot provide each other's implementations, even
          // when their helper signatures happen to be identical.
          const bool local_program = !decl->is_header && program_files.count(decl->file) != 0;
          const auto can_implement = [&](const FunctionRecord *candidate)
          {
            return !local_program || candidate->file == decl->file
                   || (!candidate->is_header && program_files.count(candidate->file) == 0)
                   || (candidate->is_header && candidate->inline_function);
          };
          if ((!local_program && satisfied)
              || std::any_of(definitions.begin(), definitions.end(), can_implement))
          {
            continue;
          }
          const FunctionRecord *other_scope = nullptr;
          const FunctionRecord *other_params = nullptr;
          // A declaration with no scope of its own is either a file-scope one or
          // a fragment's member (a header included inside a class body, whose
          // class name lives in the includer). For both, a body with the same
          // signature under *any* scope counts as the implementation: reporting
          // these would be a wall of phantom misses, and a real miss of this
          // shape is indistinguishable from an includer's class membership.
          bool body_elsewhere = false;
          const auto same_name = by_name.find(decl->name);
          if (same_name != by_name.end())
          {
            for (const FunctionRecord *candidate : same_name->second)
            {
              if (candidate == decl || !can_implement(candidate)
                  || (!candidate->definition && !candidate->deleted_or_default))
              {
                continue;
              }
              if (candidate->params == decl->params && candidate->suffix == decl->suffix)
              {
                if (decl->scope.empty())
                {
                  body_elsewhere = true;
                }
                else if (candidate->scope != decl->scope && other_scope == nullptr
                         && scopes_related(candidate->scope, decl->scope))
                {
                  other_scope = candidate;
                }
              }
              else if (other_params == nullptr)
              {
                other_params = candidate;
              }
            }
          }
          if (body_elsewhere)
          {
            continue;
          }

          Issue issue = issue_at(*decl);
          if (other_scope != nullptr)
          {
            // The same signature under a different scope: usually a namespace
            // qualification the parser could not join up, so say it rather than
            // call it missing.
            const std::string other_scope_name =
                other_scope->scope.empty() ? std::string("file scope") : other_scope->scope;
            issue.severity = 3;
            issue.message = "\"" + decl->display + "\" has no definition in scope \"" + decl->scope
                            + "\" (defined in \"" + other_scope_name + "\" at "
                            + location(*other_scope) + ")";
          }
          else
          {
            // Mark the leading half of the declared function name, rather than
            // the full signature: the name is the unresolved symbol to act on.
            issue.line = decl->name_line;
            issue.col = decl->name_col;
            issue.end_line = decl->name_end_line;
            issue.end_col =
                decl->name_col + std::max(1, (decl->name_end_col - decl->name_col + 1) / 2);
            // A template body may live in a file that is not scanned, and a
            // conditional declaration may not be compiled at all: both are hints.
            issue.severity = (decl->template_function || decl->conditional) ? 3 : 2;
            issue.message = "No definition found for \"" + decl->display + "\"";
            if (other_params != nullptr)
            {
              issue.message +=
                  " (a definition with different parameters is at " + location(*other_params) + ")";
            }
          }
          issues.push_back(std::move(issue));
          stats.missing++;
        }
      }

      // A body under a `#if` may not be compiled at all, and `#ifdef X` /
      // `#else` pairs are the ordinary way to write two implementations with one
      // signature: neither can repeat the other, so both stay out of the
      // duplicate checks (they still satisfy a declaration).
      std::vector<const FunctionRecord *> comparable;
      for (const FunctionRecord *record : definitions)
      {
        if (!record->conditional)
        {
          comparable.push_back(record);
        }
      }

      // Two bodies for one signature in the same file: a redefinition, whatever
      // the linkage.
      std::map<std::string, std::vector<const FunctionRecord *>> per_file;
      for (const FunctionRecord *record : comparable)
      {
        per_file[record->file].push_back(record);
      }
      for (const auto &file_entry : per_file)
      {
        const std::vector<const FunctionRecord *> &bodies = file_entry.second;
        for (size_t k = 1; k < bodies.size(); k++)
        {
          Issue issue = issue_at(*bodies[k]);
          issue.severity = 1;
          issue.message = "\"" + bodies[k]->display + "\" is defined more than once in this file"
                          + " (also at line " + std::to_string(bodies[0]->line + 1) + ")";
          issues.push_back(std::move(issue));
          stats.duplicates++;
        }
      }

      // A collection of standalone programs -- every source file that parsed to
      // anything carries its own file-scope `main` -- is compiled one file at a
      // time, so no two of those files are ever in one link. That is the only
      // thing the cross-file check assumes, so it stays off here: a helper
      // repeated across a folder of competitive-programming solutions, samples
      // or probes is not the linker's multiple definition. A workspace where
      // even one source file has no entry point keeps the check, since that
      // file can be linked with any of the programs; the same-file rule is
      // unaffected either way, since two bodies in one file really are a
      // redefinition.

      // Across files only a strong body repeats: an `inline`, `constexpr` or
      // template body and a class-body definition may each appear once per
      // translation unit, and a `static` body is one copy per unit. The
      // comparison happens within one platform, so the Windows and POSIX
      // implementations of one function are alternatives rather than duplicates.
      std::map<std::string, std::vector<const FunctionRecord *>> strong_by_platform;
      if (!standalone_programs)
      {
        for (const FunctionRecord *record : comparable)
        {
          if (record->inline_function || record->template_function || record->internal_linkage
              || is_program_entry(*record))
          {
            continue;
          }
          strong_by_platform[platform_tag(record->file)].push_back(record);
        }
      }
      for (const auto &platform_entry : strong_by_platform)
      {
        std::set<std::string> files;
        std::vector<const FunctionRecord *> strong;
        for (const FunctionRecord *record : platform_entry.second)
        {
          if (files.insert(record->file).second)
          {
            strong.push_back(record); // the same-file rule already covered repeats
          }
        }
        for (size_t k = 1; k < strong.size(); k++)
        {
          Issue issue = issue_at(*strong[k]);
          issue.severity = 1;
          issue.message = "\"" + strong[k]->display
                          + "\" is defined more than once (first definition" + " at "
                          + location(*strong[0]) + ")";
          issues.push_back(std::move(issue));
          stats.duplicates++;
        }
      }

      // A header body that is not inline: the same error waiting for its second
      // translation unit.
      for (const FunctionRecord *record : comparable)
      {
        if (!record->is_header || record->inline_function || record->template_function
            || record->internal_linkage)
        {
          continue;
        }
        Issue issue = issue_at(*record);
        issue.severity = 3;
        issue.message = "\"" + record->display
                        + "\" is defined in a header without inline: including it from more"
                          " than one translation unit gives a multiple-definition link error";
        issues.push_back(std::move(issue));
      }
    }

    std::stable_sort(issues.begin(),
                     issues.end(),
                     [](const Issue &a, const Issue &b)
                     {
                       if (a.file != b.file)
                       {
                         return a.file < b.file;
                       }
                       if (a.line != b.line)
                       {
                         return a.line < b.line;
                       }
                       return a.col < b.col;
                     });
    issues.erase(std::unique(issues.begin(),
                             issues.end(),
                             [](const Issue &a, const Issue &b)
                             {
                               return a.file == b.file && a.line == b.line && a.col == b.col
                                      && a.severity == b.severity && a.message == b.message;
                             }),
                 issues.end());
    if (stats_out != nullptr)
    {
      // Only the analysis counters: the walk's own counts (files, bytes, time)
      // belong to the caller and must survive this call.
      stats_out->declarations = stats.declarations;
      stats_out->definitions = stats.definitions;
      stats_out->missing = stats.missing;
      stats_out->duplicates = stats.duplicates;
      stats_out->classification_records = stats.classification_records;
    }
    return issues;
  }

  std::string signature_of(const std::string &path, const std::string &text)
  {
    // Ignore body text so ordinary expression edits do not trigger a workspace walk.
    const std::vector<FunctionRecord> records = parse_file(path, text);
    unsigned long long hash = 14695981039346656037ULL; // FNV-1a
    const auto fold = [&hash](const std::string &value)
    {
      for (const char c : value)
      {
        hash ^= (unsigned char)c;
        hash *= 1099511628211ULL;
      }
      hash ^= 0x1f; // a separator, so "ab"+"c" and "a"+"bc" differ
      hash *= 1099511628211ULL;
    };
    for (const FunctionRecord &record : records)
    {
      fold(record.key);
      fold(record.display);
      // Moving a declaration must move its squiggle, even when its key stays unchanged.
      fold(std::to_string(record.line) + ":" + std::to_string(record.col));
      fold(std::to_string(record.name_line) + ":" + std::to_string(record.name_col));
      fold(std::to_string(record.name_end_line) + ":" + std::to_string(record.name_end_col));
      const bool flags[] = {record.definition,
                            record.deleted_or_default,
                            record.pure_virtual,
                            record.template_function,
                            record.conditional,
                            record.inline_function,
                            record.internal_linkage,
                            record.in_class_definition,
                            record.is_header};
      std::string flag_text;
      for (const bool flag : flags)
      {
        flag_text += flag ? '1' : '0';
      }
      fold(flag_text);
    }
    return std::to_string(records.size()) + ":" + std::to_string(hash);
  }

  ScanResult scan_workspace(const std::string &root,
                            const ScanLimits &limits,
                            const std::map<std::string, std::string> &unsaved)
  {
    const auto started = std::chrono::steady_clock::now();
    ScanResult result;

    std::error_code ec;
    fs::path root_path = root.empty() ? fs::current_path(ec) : fs::path(root);
    root_path = fs::absolute(root_path, ec).lexically_normal();
    if (ec || !fs::exists(root_path, ec) || !fs::is_directory(root_path, ec))
    {
      return result;
    }

    std::vector<std::string> paths;
    fs::recursive_directory_iterator it(
        root_path, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    for (; !ec && it != end && (int)paths.size() < limits.max_files; it.increment(ec))
    {
      const fs::directory_entry &entry = *it;
      const std::string name = entry.path().filename().string();
      if (entry.is_directory(ec))
      {
        if (should_skip_dir(name))
        {
          it.disable_recursion_pending();
        }
        continue;
      }
      if (!entry.is_regular_file(ec) || !is_parseable_file(name))
      {
        continue;
      }
      paths.push_back(entry.path().string());
    }
    // Normalize overlay paths so editor text replaces the disk entry instead
    // of adding a second copy; new unsaved files belong to the scan too.
    std::map<std::string, std::string> overlay;
    const std::string root_prefix =
        root_path == root_path.root_path() ? root_path.string() : root_path.string() + "/";
    for (const auto &entry : unsaved)
    {
      std::error_code path_ec;
      const std::string path =
          fs::absolute(fs::path(entry.first), path_ec).lexically_normal().string();
      if (!path_ec && path.rfind(root_prefix, 0) == 0 && is_parseable_file(path)
          && (long long)entry.second.size() <= limits.max_file_bytes)
      {
        overlay[path] = entry.second;
      }
    }
    for (const auto &entry : overlay)
    {
      if (entry.first.rfind(root_prefix, 0) != 0)
      {
        continue; // outside this workspace: another root's buffer
      }
      if (std::find(paths.begin(), paths.end(), entry.first) == paths.end()
          && (int)paths.size() < limits.max_files)
      {
        paths.push_back(entry.first);
      }
    }
    std::sort(paths.begin(), paths.end()); // a stable diagnostics order

    std::vector<FunctionRecord> records;
    for (const std::string &path : paths)
    {
      if (limits.max_total_bytes > 0 && result.stats.bytes_read > limits.max_total_bytes)
      {
        break;
      }
      const auto edited = overlay.find(path);
      std::string text;
      if (edited != overlay.end())
      {
        text = edited->second;
      }
      else
      {
        std::error_code size_ec;
        const std::uintmax_t size = fs::file_size(path, size_ec);
        if (size_ec || (long long)size > limits.max_file_bytes)
        {
          continue;
        }
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
        {
          continue;
        }
        text.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
      }
      if (text.find('\0') != std::string::npos)
      {
        continue; // not text
      }
      result.stats.files_scanned++;
      result.stats.bytes_read += (long long)text.size();
      std::vector<FunctionRecord> parsed = parse_file(path, text);
      records.insert(records.end(),
                     std::make_move_iterator(parsed.begin()),
                     std::make_move_iterator(parsed.end()));
    }

    result.issues = analyze(records, &result.stats);
    result.stats.elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now() - started)
                                  .count();
    return result;
  }
} // namespace CppDefinitions

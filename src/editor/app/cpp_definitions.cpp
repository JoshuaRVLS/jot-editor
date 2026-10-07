// The editor side of the C++ definition checks: when to scan, and what to do
// with what comes back.
//
// The scan itself (syntax/cpp_definitions.*) is pure -- a root in, issues out
// -- so it runs on the worker thread and only its result is applied here, on the
// main thread. Three things have to hold for that to be safe:
//
//   * the result is dropped when the editor is shutting down (`running`),
//   * the result is dropped when the workspace moved on or a newer scan was
//     asked for (the root and the epoch it was started with are carried along),
//   * only one scan is ever in flight, and a request that arrives during one
//     marks another as wanted instead of stacking up; a request that arrives
//     before the worker queue exists (the workspace main() opens at startup)
//     is remembered the same way and starts with the first frame.
//
// The diagnostics land in cpp_def_diags, one slice per file, in the same shape
// the LSP slices use: the per-buffer merge, the explorer's badges and the
// Problems list read all three stores the same way.
#include "syntax/cpp_definitions.h"
#include "editor.h"

#include <chrono>
#include <string>
#include <utility>

namespace
{
  long long steady_now_ms()
  {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }

  Diagnostic diagnostic_for(const CppDefinitions::Issue &issue)
  {
    Diagnostic diagnostic;
    diagnostic.line = issue.line;
    diagnostic.col = issue.col;
    diagnostic.end_line = issue.end_line;
    diagnostic.end_col = issue.end_col;
    diagnostic.message = issue.message;
    diagnostic.severity = issue.severity;
    return diagnostic;
  }
} // namespace

std::string Editor::cpp_definitions_summary() const
{
  std::size_t hints = 0;
  for (const auto &entry : cpp_def_diags)
  {
    for (const Diagnostic &diagnostic : entry.second)
    {
      if (diagnostic.severity >= 3)
      {
        hints++;
      }
    }
  }

  if (cpp_defs_stats.files_scanned == 0)
  {
    return "C++ definitions: nothing to scan";
  }

  std::string out = "C++ definitions: ";
  bool wrote = false;
  auto add = [&](int count, const std::string &what)
  {
    if (count <= 0)
    {
      return;
    }
    if (wrote)
    {
      out += ", ";
    }
    out += std::to_string(count) + " " + what;
    wrote = true;
  };
  add(cpp_defs_stats.missing, "missing");
  add(cpp_defs_stats.duplicates, "repeated");
  add((int)hints, "hints");
  if (!wrote)
  {
    out += "no missing or repeated implementations";
  }
  out += " across " + std::to_string(cpp_defs_stats.files_scanned) + " files";
  out += " (" + std::to_string(cpp_defs_stats.elapsed_ms) + " ms)";
  return out;
}

void Editor::request_cpp_definitions_scan(bool announce)
{
  if (!cpp_defs_enabled || root_dir.empty())
  {
    return;
  }
  cpp_defs_dirty_signature = cpp_definitions_dirty_signature();
  if (announce)
  {
    cpp_defs_announce = true;
  }
  if (cpp_defs_scan_running)
  {
    cpp_defs_scan_pending = true;
    return;
  }
  if (!task_queue_)
  {
    // A workspace opened during startup (main() runs before Editor::run()
    // creates the worker queue) asks for its scan before there is anywhere to
    // run it. Remember that it is wanted; run() starts it once the queue
    // exists.
    cpp_defs_scan_pending = true;
    return;
  }

  // Snapshot editor text on the main thread; the worker must not read live buffers.
  const auto unsaved = cpp_definitions_unsaved_texts();
  const std::string root = root_dir;
  const unsigned long long epoch = ++cpp_defs_scan_epoch;
  cpp_defs_scan_root = root;
  cpp_defs_scan_running = true;

  task_queue_->submit_val<CppDefinitions::ScanResult>(
      [root, unsaved]() -> CppDefinitions::ScanResult
      { return CppDefinitions::scan_workspace(root, CppDefinitions::ScanLimits(), unsaved); },
      [this, root, epoch](CppDefinitions::ScanResult result)
      {
        cpp_defs_scan_running = false;
        // The editor may have shut down, or the workspace may have changed, or a
        // newer scan may already have been asked for: this result is stale.
        if (!running || root != root_dir || epoch != cpp_defs_scan_epoch)
        {
          if (running && cpp_defs_scan_pending && root == root_dir)
          {
            cpp_defs_scan_pending = false;
            request_cpp_definitions_scan(false);
          }
          return;
        }
        apply_cpp_definitions(std::move(result));
        if (cpp_defs_scan_pending)
        {
          cpp_defs_scan_pending = false;
          request_cpp_definitions_scan(false);
        }
      });
}

std::map<std::string, std::string> Editor::cpp_definitions_unsaved_texts() const
{
  // Clean buffers are already on disk; oversized buffers must obey the scan limit.
  std::map<std::string, std::string> texts;
  for (const FileBuffer &buf : buffers)
  {
    if (!buf.modified || buf.filepath.empty() || !CppDefinitions::is_parseable_file(buf.filepath))
    {
      continue;
    }
    const std::string text = get_buffer_text(buf);
    if (text.size() > (std::size_t)CppDefinitions::ScanLimits{}.max_file_bytes)
    {
      continue;
    }
    texts[buf.filepath] = text;
  }
  return texts;
}

std::string Editor::cpp_definitions_dirty_signature() const
{
  std::string signature;
  for (const auto &entry : cpp_definitions_unsaved_texts())
  {
    signature += entry.first;
    signature += '=';
    signature += CppDefinitions::signature_of(entry.first, entry.second);
    signature += '\n';
  }
  return signature;
}

bool Editor::cpp_definitions_dirty_changed()
{
  if (!cpp_defs_enabled || root_dir.empty())
  {
    return false;
  }
  std::string revision;
  for (const FileBuffer &buf : buffers)
  {
    if (buf.modified && CppDefinitions::is_parseable_file(buf.filepath))
    {
      revision += buf.filepath + ":" + std::to_string(buf.edit_generation) + "\n";
    }
  }
  if (revision == cpp_defs_dirty_revision)
  {
    return false;
  }
  cpp_defs_dirty_revision = revision;
  const std::string signature = cpp_definitions_dirty_signature();
  if (signature == cpp_defs_dirty_signature)
  {
    return false;
  }
  cpp_defs_dirty_signature = signature;
  return true;
}

void Editor::clear_cpp_definitions()
{
  cpp_defs_dirty_signature.clear();
  cpp_defs_dirty_revision.clear();
  cpp_defs_scan_pending = false;
  cpp_defs_announce = false;
  cpp_defs_pending_jump = 0;
  if (cpp_def_diags.empty())
  {
    return;
  }
  std::vector<std::string> affected;
  affected.reserve(cpp_def_diags.size());
  for (const auto &entry : cpp_def_diags)
  {
    affected.push_back(entry.first);
  }
  cpp_def_diags.clear();
  invalidate_sidebar_diagnostics_cache();
  for (const std::string &file : affected)
  {
    refresh_lsp_diagnostics_for(file);
  }
  needs_redraw = true;
}

void Editor::apply_cpp_definitions(CppDefinitions::ScanResult result)
{
  const bool announce = cpp_defs_announce;
  cpp_defs_announce = false;
  cpp_defs_stats = result.stats;
  cpp_defs_last_scan_ms = steady_now_ms();

  std::map<std::string, std::vector<Diagnostic>> fresh;
  for (const CppDefinitions::Issue &issue : result.issues)
  {
    fresh[issue.file].push_back(diagnostic_for(issue));
  }

  // Both sides need a refresh: a file the scan reported on now, and one it no
  // longer does (its old diagnostics have to go away).
  std::vector<std::string> affected;
  affected.reserve(cpp_def_diags.size() + fresh.size());
  for (const auto &entry : cpp_def_diags)
  {
    affected.push_back(entry.first);
  }
  for (const auto &entry : fresh)
  {
    affected.push_back(entry.first);
  }

  cpp_def_diags = std::move(fresh);
  invalidate_sidebar_diagnostics_cache();
  for (const std::string &file : affected)
  {
    refresh_lsp_diagnostics_for(file);
  }
  needs_redraw = true;

  if (announce)
  {
    // `:cppcheck` asked to see the result: the list opens on its findings, and
    // the summary is delivered as a toast -- `set_message` alone writes the
    // statusline member, which the Lua status line no longer reads, so without
    // the flag the command would look like it did nothing. It lives a little
    // longer than a toast's default: there are two counts and a file tally to
    // read.
    show_problems_panel();
    set_transient_message(cpp_definitions_summary(), 6000, true);
  }
  // The command is also the way *to* the findings: land on the next one from the
  // caret. The direction survives the wait -- a `:cppcheck next` typed while the
  // scan was still running recorded its own -- and with no findings the summary
  // (or the walk's own message) is the whole answer.
  const int jump = cpp_defs_pending_jump;
  cpp_defs_pending_jump = 0;
  if (jump != 0 && !cpp_def_diags.empty())
  {
    goto_next_cpp_definition_issue(jump);
  }
}

// The editor side of the workspace CSS vocabulary: when to scan for it, and
// what to do with it (see features/web_completion.h for the scan itself).
//
// The scan is a walk of the whole tree, so it runs on the worker queue and only
// its result is applied here, on the main thread. The bookkeeping is the same
// triple CppDefsState uses, for the same reasons:
//
//   * the result is dropped when the editor is shutting down (`running`),
//   * the result is dropped when the workspace moved on or a newer scan was
//     asked for (the root and the epoch it was started with are carried along),
//   * only one scan is ever in flight, and a request that arrives during one
//     marks another as wanted instead of stacking up; a request that arrives
//     before the worker queue exists (the workspace main() opens at startup)
//     is remembered the same way and starts with the first frame.
//
// Unlike the C++ definition checks the index is not a diagnostic: nothing is
// published anywhere, it is only read when a completion is being built.
#include "editor.h"
#include "features/web_completion.h"

#include <utility>

void Editor::request_web_index_scan()
{
  if (root_dir.empty())
  {
    return;
  }
  if (web_index_scan_running)
  {
    web_index_scan_pending = true;
    return;
  }
  if (!task_queue_)
  {
    web_index_scan_pending = true;
    return;
  }

  const std::string root = root_dir;
  const unsigned long long epoch = ++web_index_scan_epoch;
  web_index_scan_root = root;
  web_index_scan_running = true;

  task_queue_->submit_val<WebCompletion::Index>(
      [root]() -> WebCompletion::Index { return WebCompletion::scan_workspace(root); },
      [this, root, epoch](WebCompletion::Index result)
      {
        web_index_scan_running = false;
        if (!running || root != root_dir || epoch != web_index_scan_epoch)
        {
          if (running && web_index_scan_pending && root == root_dir)
          {
            web_index_scan_pending = false;
            request_web_index_scan();
          }
          return;
        }
        apply_web_index(std::move(result));
        if (web_index_scan_pending)
        {
          web_index_scan_pending = false;
          request_web_index_scan();
        }
      });
}

void Editor::apply_web_index(WebCompletion::Index index)
{
  web_index = std::move(index);
  // Nothing is repainted: the index only feeds a popup that is either already
  // up (in which case the next keystroke filters against the new names) or is
  // not (in which case there is nothing to show).
}

bool Editor::append_web_index_completions(std::vector<LSPCompletionItem> &items)
{
  if (web_index.empty())
  {
    return false;
  }
  if (buffers.empty() || panes.empty())
  {
    return false;
  }

  auto &buf = get_buffer();
  if (buf.filepath.empty() || buf.cursor.y < 0 || buf.cursor.y >= (int)buf.lines.size())
  {
    return false;
  }

  const std::string &path = buf.filepath;
  // The buffer's language decides which half of the context test applies: a
  // style sheet spells its contexts differently from markup, and a class name
  // is never a valid completion in one.
  const bool style_file = WebCompletion::is_style_path(path);

  const WebCompletion::Context context =
      WebCompletion::context_at(buf.line(buf.cursor.y), buf.cursor.x, style_file);
  if (context == WebCompletion::Context::None)
  {
    return false;
  }

  const std::vector<std::string> &names =
      context == WebCompletion::Context::ClassName ? web_index.classes : web_index.css_vars;
  if (names.empty())
  {
    return false;
  }

  const bool class_names = context == WebCompletion::Context::ClassName;
  const std::string detail = class_names ? "Workspace class" : "Workspace custom property";
  const int kind = class_names ? 7 : 6; // Class, Variable
  items.reserve(items.size() + names.size());
  for (const std::string &name : names)
  {
    LSPCompletionItem item;
    item.label = name;
    item.insert_text = name;
    item.filter_text = name;
    item.detail = detail;
    item.kind = kind;
    item.insert_text_format = 1;
    items.push_back(std::move(item));
  }
  return true;
}

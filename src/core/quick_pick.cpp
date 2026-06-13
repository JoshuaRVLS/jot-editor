#include "editor.h"
#include "symbol_index.h"
#include "workspace_search.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

namespace {
std::string lower_copy(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return s;
}

std::string trim_preview(std::string s) {
  for (char &c : s) {
    if (c == '\t' || c == '\r' || c == '\n') {
      c = ' ';
    }
  }
  size_t start = s.find_first_not_of(' ');
  if (start == std::string::npos) {
    return "";
  }
  s.erase(0, start);
  if (s.size() > 220) {
    s = s.substr(0, 217) + "...";
  }
  return s;
}

bool same_path(const std::string &a, const std::string &b) {
  if (a == b) {
    return true;
  }
  if (a.empty() || b.empty()) {
    return false;
  }
  std::error_code ec;
  return fs::exists(a, ec) && fs::exists(b, ec) &&
         fs::equivalent(a, b, ec) && !ec;
}

std::string diagnostic_label(int severity) {
  switch (severity) {
  case 1:
    return "Error";
  case 2:
    return "Warning";
  case 3:
    return "Info";
  case 4:
    return "Hint";
  default:
    return "Diagnostic";
  }
}

int diagnostic_rank(int severity) {
  switch (severity) {
  case 1:
    return 0;
  case 2:
    return 1;
  case 3:
    return 2;
  case 4:
    return 3;
  default:
    return 4;
  }
}
} // namespace

int Editor::quick_pick_match_score(const std::string &query,
                                   const QuickPickItem &item) const {
  if (query.empty()) {
    return 1;
  }
  const std::string needle = lower_copy(query);
  const std::string hay =
      lower_copy(item.label + " " + item.detail + " " + item.preview);
  size_t exact = hay.find(needle);
  if (exact != std::string::npos) {
    return 10000 - (int)exact;
  }

  size_t pos = 0;
  int score = 2000;
  for (char qc : needle) {
    bool found = false;
    for (; pos < hay.size(); pos++) {
      if (hay[pos] == qc) {
        score -= (int)pos;
        pos++;
        found = true;
        break;
      }
    }
    if (!found) {
      return 0;
    }
  }
  return std::max(1, score);
}

void Editor::open_quick_pick(QuickPickKind kind, const std::string &title,
                             std::vector<QuickPickItem> items,
                             const std::string &query) {
  quick_pick_kind = kind;
  quick_pick_title = title;
  quick_pick_query = query;
  quick_pick_all_items = std::move(items);
  quick_pick_selected = 0;
  show_quick_pick = true;
  refresh_quick_pick();
  needs_redraw = true;
}

void Editor::close_quick_pick() {
  show_quick_pick = false;
  quick_pick_kind = QUICK_PICK_NONE;
  quick_pick_title.clear();
  quick_pick_query.clear();
  quick_pick_all_items.clear();
  quick_pick_items.clear();
  quick_pick_selected = 0;
  needs_redraw = true;
}

void Editor::refresh_quick_pick() {
  std::vector<std::pair<int, QuickPickItem>> ranked;
  ranked.reserve(quick_pick_all_items.size());
  for (const auto &item : quick_pick_all_items) {
    int score = quick_pick_match_score(quick_pick_query, item);
    if (quick_pick_query.empty() || score > 0) {
      ranked.push_back({score, item});
    }
  }
  std::stable_sort(ranked.begin(), ranked.end(),
                   [](const auto &a, const auto &b) {
                     if (a.first != b.first) {
                       return a.first > b.first;
                     }
                     if (a.second.filepath != b.second.filepath) {
                       return a.second.filepath < b.second.filepath;
                     }
                     if (a.second.line != b.second.line) {
                       return a.second.line < b.second.line;
                     }
                     return a.second.col < b.second.col;
                   });

  quick_pick_items.clear();
  const int max_items = quick_pick_kind == QUICK_PICK_PROJECT_SEARCH ? 1000 : 500;
  for (int i = 0; i < (int)ranked.size() && i < max_items; i++) {
    quick_pick_items.push_back(std::move(ranked[(size_t)i].second));
  }
  quick_pick_selected =
      std::clamp(quick_pick_selected, 0,
                 std::max(0, (int)quick_pick_items.size() - 1));
}

void Editor::accept_quick_pick() {
  if (quick_pick_items.empty()) {
    return;
  }
  QuickPickItem item =
      quick_pick_items[(size_t)std::clamp(quick_pick_selected, 0,
                                          (int)quick_pick_items.size() - 1)];
  if (item.filepath.empty()) {
    return;
  }

  close_quick_pick();
  open_file(item.filepath);
  if (current_buffer >= 0 && current_buffer < (int)buffers.size()) {
    auto &buf = buffers[(size_t)current_buffer];
    if (same_path(buf.filepath, item.filepath) && buf.line_count() > 0) {
      buf.cursor.y =
          std::clamp(item.line, 0, std::max(0, (int)buf.line_count() - 1));
      buf.cursor.x =
          std::clamp(item.col, 0, (int)buf.line(buf.cursor.y).size());
      buf.preferred_x = buf.cursor.x;
      clear_selection();
      ensure_cursor_visible();
      set_message(get_filename(buf.filepath) + ":" +
                  std::to_string(buf.cursor.y + 1));
    }
  }
  needs_redraw = true;
}

bool Editor::handle_quick_pick_input(int ch) {
  if (!show_quick_pick) {
    return false;
  }

  if (ch == 27) {
    close_quick_pick();
    return true;
  }
  if (ch == '\n' || ch == 13) {
    if (quick_pick_kind == QUICK_PICK_PROJECT_SEARCH &&
        quick_pick_items.empty() && !quick_pick_query.empty()) {
      show_project_search(quick_pick_query);
      return true;
    }
    accept_quick_pick();
    return true;
  }
  if (ch == 1008) {
    quick_pick_selected = std::max(0, quick_pick_selected - 1);
    needs_redraw = true;
    return true;
  }
  if (ch == 1009) {
    quick_pick_selected =
        std::min(std::max(0, (int)quick_pick_items.size() - 1),
                 quick_pick_selected + 1);
    needs_redraw = true;
    return true;
  }
  if (ch == 1012) {
    quick_pick_selected = 0;
    needs_redraw = true;
    return true;
  }
  if (ch == 1013) {
    quick_pick_selected = std::max(0, (int)quick_pick_items.size() - 1);
    needs_redraw = true;
    return true;
  }
  if (ch == 127 || ch == 8) {
    if (!quick_pick_query.empty()) {
      quick_pick_query.pop_back();
      quick_pick_selected = 0;
      if (quick_pick_kind == QUICK_PICK_PROJECT_SEARCH) {
        show_project_search(quick_pick_query);
      } else {
        refresh_quick_pick();
        needs_redraw = true;
      }
    }
    return true;
  }
  if (ch >= 32 && ch < 1000) {
    quick_pick_query.push_back((char)ch);
    quick_pick_selected = 0;
    if (quick_pick_kind == QUICK_PICK_PROJECT_SEARCH) {
      show_project_search(quick_pick_query);
    } else {
      refresh_quick_pick();
      needs_redraw = true;
    }
    return true;
  }
  return true;
}

void Editor::show_project_search(const std::string &query) {
  std::vector<QuickPickItem> items;
  if (!query.empty()) {
    const std::string root = root_dir.empty() ? "." : root_dir;
    for (const auto &result : WorkspaceSearch::search(root, query)) {
      QuickPickItem item;
      item.filepath = result.path;
      item.line = result.line;
      item.col = result.column;
      item.label = result.relative_path + ":" + std::to_string(result.line + 1) +
                   ":" + std::to_string(result.column + 1);
      item.detail = result.line_text;
      item.preview = result.line_text;
      items.push_back(std::move(item));
    }
  }
  open_quick_pick(QUICK_PICK_PROJECT_SEARCH, "Project Search", std::move(items),
                  query);
  if (query.empty()) {
    set_message("Type to search workspace");
  } else {
    set_message("Project search: " + std::to_string(quick_pick_all_items.size()) +
                " result(s)");
  }
}

void Editor::show_diagnostics_picker() {
  auto items = diagnostic_quick_pick_items();
  open_quick_pick(QUICK_PICK_DIAGNOSTICS, "Diagnostics", std::move(items));
  if (quick_pick_all_items.empty()) {
    set_message("No diagnostics");
  }
}

bool Editor::goto_next_diagnostic(int direction) {
  auto items = diagnostic_quick_pick_items();
  if (items.empty()) {
    set_message("No diagnostics");
    return false;
  }

  auto &buf = get_buffer();
  int selected = -1;
  if (direction >= 0) {
    for (int i = 0; i < (int)items.size(); i++) {
      const auto &item = items[(size_t)i];
      if (same_path(item.filepath, buf.filepath)) {
        if (item.line > buf.cursor.y ||
            (item.line == buf.cursor.y && item.col > buf.cursor.x)) {
          selected = i;
          break;
        }
      } else if (selected < 0 && item.filepath > buf.filepath) {
        selected = i;
      }
    }
    if (selected < 0) {
      selected = 0;
    }
  } else {
    for (int i = (int)items.size() - 1; i >= 0; i--) {
      const auto &item = items[(size_t)i];
      if (same_path(item.filepath, buf.filepath)) {
        if (item.line < buf.cursor.y ||
            (item.line == buf.cursor.y && item.col < buf.cursor.x)) {
          selected = i;
          break;
        }
      } else if (selected < 0 && item.filepath < buf.filepath) {
        selected = i;
      }
    }
    if (selected < 0) {
      selected = (int)items.size() - 1;
    }
  }

  open_quick_pick(QUICK_PICK_DIAGNOSTICS, "Diagnostics", std::move(items));
  quick_pick_selected = std::clamp(selected, 0,
                                   std::max(0, (int)quick_pick_items.size() - 1));
  accept_quick_pick();
  return true;
}

std::vector<Editor::QuickPickItem> Editor::diagnostic_quick_pick_items() const {
  std::vector<QuickPickItem> items;
  for (const auto &buf : buffers) {
    if (buf.filepath.empty()) {
      continue;
    }
    for (const auto &diag : buf.diagnostics) {
      QuickPickItem item;
      item.filepath = buf.filepath;
      item.line = std::max(0, diag.line);
      item.col = std::max(0, diag.col);
      item.severity = diag.severity;
      item.label = diagnostic_label(diag.severity) + ": " + diag.message;
      item.detail = fs::path(buf.filepath).filename().string() + ":" +
                    std::to_string(item.line + 1) + ":" +
                    std::to_string(item.col + 1);
      if (item.line >= 0 && item.line < (int)buf.line_count()) {
        item.preview = trim_preview(buf.line(item.line));
      }
      items.push_back(std::move(item));
    }
  }
  std::stable_sort(items.begin(), items.end(),
                   [](const auto &a, const auto &b) {
                     if (diagnostic_rank(a.severity) !=
                         diagnostic_rank(b.severity)) {
                       return diagnostic_rank(a.severity) <
                              diagnostic_rank(b.severity);
                     }
                     if (a.filepath != b.filepath) {
                       return a.filepath < b.filepath;
                     }
                     if (a.line != b.line) {
                       return a.line < b.line;
                     }
                     return a.col < b.col;
                   });
  return items;
}

std::vector<Editor::QuickPickItem> Editor::fallback_symbol_items() {
  std::vector<QuickPickItem> items;
  if (buffers.empty() || current_buffer < 0 ||
      current_buffer >= (int)buffers.size()) {
    return items;
  }
  const auto &buf = get_buffer();
  if (buf.is_lazy()) {
    return items;
  }
  for (const auto &symbol :
       SymbolIndex::extract_document_symbols(buf.lines, buf.filepath)) {
    QuickPickItem item;
    item.filepath = buf.filepath;
    item.line = symbol.line;
    item.col = symbol.column;
    item.label = symbol.name;
    item.detail = symbol.kind + "  " + std::to_string(symbol.line + 1);
    item.preview = symbol.detail;
    items.push_back(std::move(item));
  }
  return items;
}

void Editor::show_symbol_picker() {
  auto items = fallback_symbol_items();
  open_quick_pick(QUICK_PICK_SYMBOLS, "Document Symbols", std::move(items));
  request_document_symbols();
  if (quick_pick_all_items.empty()) {
    set_message("No symbols found yet");
  }
}

void Editor::request_document_symbols() {
  if (buffers.empty() || current_buffer < 0 ||
      current_buffer >= (int)buffers.size()) {
    return;
  }
  auto &buf = get_buffer();
  if (buf.is_lazy() || buf.filepath.empty()) {
    return;
  }
  LSPClient *client = ensure_lsp_for_file(buf.filepath);
  if (!client) {
    return;
  }
  lsp_pending_changes.erase(buf.filepath);
  client->did_change(buf.filepath, get_buffer_text(buf));
  client->request_document_symbols(buf.filepath);
}

void Editor::handle_document_symbols_result(
    const LSPDocumentSymbolResult &result) {
  if (!show_quick_pick || quick_pick_kind != QUICK_PICK_SYMBOLS ||
      buffers.empty() || current_buffer < 0 ||
      current_buffer >= (int)buffers.size()) {
    return;
  }
  const auto &buf = get_buffer();
  if (!same_path(buf.filepath, result.filepath) || result.symbols.empty()) {
    return;
  }

  std::vector<QuickPickItem> items;
  for (const auto &symbol : result.symbols) {
    QuickPickItem item;
    item.filepath = symbol.filepath.empty() ? result.filepath : symbol.filepath;
    item.line = symbol.line;
    item.col = symbol.character;
    item.label = symbol.name;
    item.detail = symbol.kind + "  " + std::to_string(symbol.line + 1);
    item.preview = symbol.detail;
    items.push_back(std::move(item));
  }
  quick_pick_all_items = std::move(items);
  quick_pick_selected = 0;
  refresh_quick_pick();
  set_message("LSP symbols loaded: " +
              std::to_string(quick_pick_all_items.size()));
  needs_redraw = true;
}

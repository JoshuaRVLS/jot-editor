#include "editor.h"
#include "folding.h"
#include "lazy_line_provider.h"
#include "python_bridge/api.h"
#ifdef JOT_TREESITTER
#include <tree_sitter/api.h>
#endif
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <dirent.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <unordered_map>

namespace fs = std::filesystem;

namespace {
constexpr int kMaxRecentFiles = 50;
constexpr int kMaxRecentWorkspaces = 30;
constexpr int kMaxClosedBufferHistory = 20;

std::string normalize_existing_path(const std::string &path) {
  if (path.empty()) {
    return "";
  }
  std::error_code ec;
  fs::path p(path);
  fs::path absolute = fs::absolute(p, ec);
  if (ec) {
    return path;
  }
  fs::path canonical = fs::weakly_canonical(absolute, ec);
  if (!ec) {
    return canonical.string();
  }
  return absolute.string();
}

std::string sanitize_input_path(const std::string &path) {
  std::string out = path;
  out.erase(out.begin(),
            std::find_if(out.begin(), out.end(),
                         [](unsigned char c) { return !std::isspace(c); }));
  out.erase(std::find_if(out.rbegin(), out.rend(),
                         [](unsigned char c) { return !std::isspace(c); })
                .base(),
            out.end());
  if (out.size() >= 2 &&
      ((out.front() == '"' && out.back() == '"') ||
       (out.front() == '\'' && out.back() == '\''))) {
    out = out.substr(1, out.size() - 2);
  }
  return out;
}

std::string recent_files_path() {
  const char *home = std::getenv("HOME");
  if (!home || !*home) {
    return "";
  }
  fs::path p = fs::path(home) / ".config" / "jot" / "configs" /
               "recent_files.txt";
  return p.string();
}

std::string recent_workspaces_path() {
  const char *home = std::getenv("HOME");
  if (!home || !*home) {
    return "";
  }
  fs::path p = fs::path(home) / ".config" / "jot" / "configs" /
               "recent_workspaces.txt";
  return p.string();
}

std::string file_fold_states_path() {
  const char *home = std::getenv("HOME");
  if (!home || !*home) {
    return "";
  }
  fs::path p = fs::path(home) / ".config" / "jot" / "configs" /
               "fold_states.txt";
  return p.string();
}

std::string escape_state_field(const std::string &input) {
  std::string out;
  out.reserve(input.size());
  for (char c : input) {
    if (c == '\\') {
      out += "\\\\";
    } else if (c == '\t') {
      out += "\\t";
    } else if (c == '\n') {
      out += "\\n";
    } else {
      out.push_back(c);
    }
  }
  return out;
}

std::string unescape_state_field(const std::string &input) {
  std::string out;
  out.reserve(input.size());
  for (size_t i = 0; i < input.size(); i++) {
    if (input[i] == '\\' && i + 1 < input.size()) {
      char n = input[i + 1];
      if (n == 't') {
        out.push_back('\t');
        i++;
        continue;
      }
      if (n == 'n') {
        out.push_back('\n');
        i++;
        continue;
      }
      if (n == '\\') {
        out.push_back('\\');
        i++;
        continue;
      }
    }
    out.push_back(input[i]);
  }
  return out;
}

std::vector<std::string> split_state_tab(const std::string &line) {
  std::vector<std::string> parts;
  size_t start = 0;
  while (start <= line.size()) {
    size_t pos = line.find('\t', start);
    if (pos == std::string::npos) {
      parts.push_back(line.substr(start));
      break;
    }
    parts.push_back(line.substr(start, pos - start));
    start = pos + 1;
  }
  return parts;
}

std::unordered_map<std::string, std::string> load_file_fold_state_map() {
  std::unordered_map<std::string, std::string> states;
  const std::string path = file_fold_states_path();
  if (path.empty()) {
    return states;
  }

  std::ifstream file(path);
  if (!file.is_open()) {
    return states;
  }

  std::string line;
  while (std::getline(file, line)) {
    if (line.empty()) {
      continue;
    }
    std::vector<std::string> parts = split_state_tab(line);
    if (parts.size() < 2) {
      continue;
    }
    const std::string key =
        normalize_existing_path(unescape_state_field(parts[0]));
    const std::string payload = unescape_state_field(parts[1]);
    if (!key.empty()) {
      states[key] = payload;
    }
  }
  return states;
}

void write_file_fold_state_map(
    const std::unordered_map<std::string, std::string> &states) {
  const std::string path = file_fold_states_path();
  if (path.empty()) {
    return;
  }

  std::error_code ec;
  fs::path output_path(path);
  fs::create_directories(output_path.parent_path(), ec);
  if (ec) {
    return;
  }

  std::ofstream file(path, std::ios::trunc);
  if (!file.is_open()) {
    return;
  }

  for (const auto &[key, payload] : states) {
    if (key.empty() || payload.empty()) {
      continue;
    }
    file << escape_state_field(key) << '\t'
         << escape_state_field(payload) << '\n';
  }
}

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

std::string detect_prettier_runner() {
  static int mode = -1; // -1 unknown, 0 unavailable, 1 prettier, 2 npx
  if (mode == -1) {
    if (std::system("command -v prettier >/dev/null 2>&1") == 0) {
      mode = 1;
    } else if (std::system("command -v npx >/dev/null 2>&1") == 0) {
      mode = 2;
    } else {
      mode = 0;
    }
  }
  if (mode == 1) {
    return "prettier";
  }
  if (mode == 2) {
    return "npx --yes prettier";
  }
  return "";
}

std::string detect_clang_format_runner() {
  static int mode = -1; // -1 unknown, 0 unavailable, 1 clang-format
  if (mode == -1) {
    mode = (std::system("command -v clang-format >/dev/null 2>&1") == 0) ? 1 : 0;
  }
  return mode == 1 ? "clang-format" : "";
}

bool supports_prettier_on_save(const std::string &path) {
  std::error_code ec;
  fs::path p(path);
  const std::string name = p.filename().string();
  std::string ext = p.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });

  static const std::set<std::string> exts = {
      ".js",    ".jsx",  ".cjs",  ".mjs",  ".ts",   ".tsx",  ".json",
      ".css",   ".scss", ".less", ".html", ".md",   ".mdx",  ".yaml",
      ".yml",   ".vue",  ".svelte", ".gql", ".graphql"};

  if (exts.find(ext) != exts.end()) {
    return true;
  }

  static const std::set<std::string> names = {
      ".prettierrc", ".prettierrc.json", ".prettierrc.yaml", ".prettierrc.yml",
      ".prettierrc.js", ".prettierrc.cjs", ".prettierrc.mjs"};
  return names.find(name) != names.end() && fs::exists(p, ec) && !ec;
}

bool supports_clang_format_on_save(const std::string &path) {
  fs::path p(path);
  std::string ext = p.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  static const std::set<std::string> exts = {
      ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".m", ".mm"};
  return exts.find(ext) != exts.end();
}

bool read_file_lines(const std::string &path, std::vector<std::string> &out) {
  std::ifstream file(path);
  if (!file.is_open()) {
    return false;
  }
  out.clear();
  std::string line;
  while (std::getline(file, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    out.push_back(line);
  }
  if (out.empty()) {
    out.push_back("");
  }
  return true;
}

bool is_supported_image_path(const std::string &path) {
  fs::path p(path);
  std::string ext = p.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  static const std::set<std::string> exts = {
      ".jpg",  ".jpeg", ".png",  ".gif",  ".bmp",  ".svg", ".webp",
      ".ico",  ".tif",  ".tiff", ".avif", ".heic", ".ppm", ".pgm",
      ".pbm",  ".xpm",  ".jxl"};
  return exts.find(ext) != exts.end();
}

void normalize_buffer_after_external_edit(FileBuffer &buf) {
  if (buf.is_lazy())
    return;

  if (buf.line_count() == 0) {
    buf.lines.push_back("");
  }

  buf.cursor.y = std::clamp(buf.cursor.y, 0, std::max(0, (int)buf.line_count() - 1));
  buf.cursor.x =
      std::clamp(buf.cursor.x, 0, (int)buf.line(buf.cursor.y).size());
  buf.preferred_x = buf.cursor.x;

  buf.scroll_offset =
      std::clamp(buf.scroll_offset, 0, std::max(0, (int)buf.line_count() - 1));
  buf.scroll_x = std::max(0, buf.scroll_x);

  if (buf.selection.active) {
    buf.selection.start.y =
        std::clamp(buf.selection.start.y, 0, std::max(0, (int)buf.line_count() - 1));
    buf.selection.end.y =
        std::clamp(buf.selection.end.y, 0, std::max(0, (int)buf.line_count() - 1));
    buf.selection.start.x =
        std::clamp(buf.selection.start.x, 0, (int)buf.line(buf.selection.start.y).size());
    buf.selection.end.x =
        std::clamp(buf.selection.end.x, 0, (int)buf.line(buf.selection.end.y).size());
  }
}
} // namespace

void Editor::load_file(const std::string &fname) { open_file(fname, false); }

int Editor::detect_indent_width(const std::vector<std::string> &lines) const {
  std::map<int, int> delta_score;
  int tab_indented_lines = 0;
  int space_indented_lines = 0;

  auto count_leading = [](const std::string &line, char ch) {
    int n = 0;
    while (n < (int)line.size() && line[n] == ch)
      n++;
    return n;
  };

  int prev_space_indent = -1;
  for (const auto &line : lines) {
    if (line.empty()) {
      continue;
    }

    int tabs = count_leading(line, '\t');
    int spaces = count_leading(line, ' ');
    if (tabs > 0) {
      tab_indented_lines++;
    } else if (spaces > 0) {
      space_indented_lines++;
    }

    if (tabs > 0 || spaces == 0) {
      prev_space_indent = -1;
      continue;
    }

    if (prev_space_indent >= 0 && spaces != prev_space_indent) {
      int delta = std::abs(spaces - prev_space_indent);
      if (delta >= 1 && delta <= 8) {
        delta_score[delta]++;
      }
    }
    prev_space_indent = spaces;
  }

  if (tab_indented_lines > space_indented_lines && tab_indented_lines >= 3) {
    return 4;
  }

  int best_width = -1;
  int best_score = 0;
  for (const auto &[width, score] : delta_score) {
    if (score > best_score || (score == best_score && width < best_width)) {
      best_width = width;
      best_score = score;
    }
  }

  if (best_width >= 1 && best_width <= 8 && best_score >= 2) {
    return best_width;
  }

  return tab_size;
}

void Editor::open_file(const std::string &path, bool preview) {
  show_home_menu = false;
  hide_lsp_completion();

  const std::string clean_path = sanitize_input_path(path);
  if (clean_path.empty()) {
    set_message("Open failed: empty path");
    return;
  }

  const std::string normalized = normalize_existing_path(clean_path);
  const std::string path_to_open = normalized.empty() ? clean_path : normalized;

  {
    std::error_code ec;
    if (is_supported_image_path(path_to_open) && fs::exists(path_to_open, ec) &&
        !ec && fs::is_regular_file(path_to_open, ec) && !ec) {
      image_viewer.open(path_to_open);
      track_recent_file(path_to_open);
      refresh_git_status(true);
      needs_redraw = true;
      return;
    }
  }

  if (image_viewer.is_active()) {
    image_viewer.close();
  }

  auto find_open_index = [&]() {
    for (size_t i = 0; i < buffers.size(); i++) {
      const std::string candidate = normalize_existing_path(buffers[i].filepath);
      if (!candidate.empty() && candidate == path_to_open) {
        return (int)i;
      }
      if (buffers[i].filepath == path_to_open ||
          buffers[i].filepath == clean_path || buffers[i].filepath == path) {
        return (int)i;
      }
    }
    return -1;
  };

  int existing_index = find_open_index();

  if (preview && preview_buffer_index >= 0 &&
      preview_buffer_index < (int)buffers.size() &&
      preview_buffer_index != existing_index) {
    const bool can_replace_preview =
        buffers[preview_buffer_index].is_preview &&
        !buffers[preview_buffer_index].modified;
    if (can_replace_preview) {
      close_buffer_at(preview_buffer_index);
      existing_index = find_open_index();
    }
  }

  if (existing_index >= 0 && existing_index < (int)buffers.size()) {
    current_buffer = existing_index;
    auto &pane = get_pane();
    pane.buffer_id = existing_index;
    pane.tab_buffer_ids.erase(
        std::remove_if(pane.tab_buffer_ids.begin(), pane.tab_buffer_ids.end(),
                       [this](int id) {
                         return id >= 0 && id < (int)buffers.size() &&
                                buffers[id].is_placeholder &&
                                !buffers[id].modified &&
                                buffers[id].filepath.empty();
                       }),
        pane.tab_buffer_ids.end());
    if (std::find(pane.tab_buffer_ids.begin(), pane.tab_buffer_ids.end(),
                  existing_index) == pane.tab_buffer_ids.end()) {
      pane.tab_buffer_ids.push_back(existing_index);
    }
    int draw_w = std::max(1, pane.w);
    if (show_minimap && draw_w > 20) {
      draw_w = std::max(1, draw_w - minimap_width);
    }
    reveal_local_tab(pane, find_local_tab_index(pane, existing_index), draw_w);
    if (!preview && buffers[existing_index].is_preview) {
      buffers[existing_index].is_preview = false;
      if (preview_buffer_index == existing_index) {
        preview_buffer_index = -1;
      }
    }
    if (preview && buffers[existing_index].is_preview) {
      preview_buffer_index = existing_index;
    }
    track_recent_file(path_to_open);
    refresh_git_status(true);
    needs_redraw = true;
    return;
  }

  FileBuffer fb;
  fb.filepath = path_to_open;
  fb.cursor = {0, 0};
  fb.preferred_x = 0;
  fb.selection = {{0, 0}, {0, 0}, false};
  fb.scroll_offset = 0;
  fb.scroll_x = 0;
  fb.modified = false;
  fb.is_preview = preview;
  fb.is_placeholder = false;

  {
    std::error_code ec;
    std::uintmax_t file_size = fs::file_size(path_to_open, ec);
    if (!ec && file_size > kFileSizeLazyThreshold) {
      fb.lazy_provider = LazyLineProvider::open(path_to_open);
      if (!fb.lazy_provider)
        set_message("Failed to open large file with lazy loading");
    }
  }

  if (!fb.lazy_provider) {
    if (task_queue_) {
      auto shared_fb = std::make_shared<FileBuffer>(std::move(fb));
      task_queue_->submit(
          [path_to_open, shared_fb]() {
            std::ifstream file(path_to_open);
            if (file.is_open()) {
              std::string line;
              while (std::getline(file, line)) {
                if (!line.empty() && line.back() == '\r')
                  line.pop_back();
                shared_fb->lines.push_back(line);
              }
            }
            if (shared_fb->lines.empty())
              shared_fb->lines.push_back("");
          },
          [this, path_to_open, preview, shared_fb]() mutable {
            // Editor may have shut down while the file was being
            // read on the worker thread. Drop the result rather than
            // touching freed state.
            if (!running)
              return;
            finish_open_file(std::move(*shared_fb), path_to_open, preview);
          });
      return;
    }

    std::ifstream file(path_to_open);
    if (file.is_open()) {
      std::string line;
      while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
          line.pop_back();
        }
        fb.lines.push_back(line);
      }
      file.close();
    }
    if (fb.lines.empty())
      fb.lines.push_back("");
  }

  finish_open_file(std::move(fb), path_to_open, preview);
}

void Editor::finish_open_file(FileBuffer fb, const std::string &path_to_open,
                              bool preview) {

  if (!fb.is_lazy() && config.get_bool("auto_detect_indent", false)) {
    int detected_tab_size = detect_indent_width(fb.lines);
    if (detected_tab_size != tab_size && detected_tab_size >= 1 &&
        detected_tab_size <= 8) {
      tab_size = detected_tab_size;
      message = "Indent detected: " + std::to_string(tab_size) + " spaces";
    }
  }

  buffers.push_back(std::move(fb));
  current_buffer = buffers.size() - 1;
  auto &pane = get_pane();
  pane.buffer_id = current_buffer;
  pane.tab_buffer_ids.erase(
      std::remove_if(pane.tab_buffer_ids.begin(), pane.tab_buffer_ids.end(),
                     [this](int id) {
                       return id >= 0 && id < (int)buffers.size() &&
                              buffers[id].is_placeholder &&
                              !buffers[id].modified &&
                              buffers[id].filepath.empty();
                     }),
      pane.tab_buffer_ids.end());
  if (std::find(pane.tab_buffer_ids.begin(), pane.tab_buffer_ids.end(),
                current_buffer) == pane.tab_buffer_ids.end()) {
    pane.tab_buffer_ids.push_back(current_buffer);
  }
  int draw_w = std::max(1, pane.w);
  if (show_minimap && draw_w > 20) {
    draw_w = std::max(1, draw_w - minimap_width);
  }
  reveal_local_tab(pane, find_local_tab_index(pane, current_buffer), draw_w);
  if (preview) {
    preview_buffer_index = current_buffer;
  }
  track_recent_file(path_to_open);

  highlighter.set_language(get_file_extension(path_to_open));

#ifdef JOT_TREESITTER
  init_ts_for_buffer(buffers.back());
#endif

  restore_file_fold_state(buffers.back());

  if (python_api)
    python_api->on_buffer_open(path_to_open);
  notify_lsp_open(path_to_open);
  refresh_git_status(true);
  apply_pending_lsp_definition_jump();
  apply_pending_lsp_back_jump();
  needs_redraw = true;
}

void Editor::create_new_buffer() {
  show_home_menu = false;
  hide_lsp_completion();

  FileBuffer fb;
  fb.lines.push_back("");
  fb.cursor = {0, 0};
  fb.preferred_x = 0;
  fb.selection = {{0, 0}, {0, 0}, false};
  fb.scroll_offset = 0;
  fb.scroll_x = 0;
  fb.modified = false;
  fb.is_preview = false;
  fb.is_placeholder = false;
  buffers.push_back(std::move(fb));
  current_buffer = buffers.size() - 1;
  auto &pane = get_pane();
  pane.buffer_id = current_buffer;
  pane.tab_buffer_ids.erase(
      std::remove_if(pane.tab_buffer_ids.begin(), pane.tab_buffer_ids.end(),
                     [this](int id) {
                       return id >= 0 && id < (int)buffers.size() &&
                              buffers[id].is_placeholder &&
                              !buffers[id].modified &&
                              buffers[id].filepath.empty();
                     }),
      pane.tab_buffer_ids.end());
  if (std::find(pane.tab_buffer_ids.begin(), pane.tab_buffer_ids.end(),
                current_buffer) == pane.tab_buffer_ids.end()) {
    pane.tab_buffer_ids.push_back(current_buffer);
  }
  int draw_w = std::max(1, pane.w);
  if (show_minimap && draw_w > 20) {
    draw_w = std::max(1, draw_w - minimap_width);
  }
  reveal_local_tab(pane, find_local_tab_index(pane, current_buffer), draw_w);
}

void Editor::save_file() {
  const auto &buf = get_buffer();
  if (buf.filepath.empty()) {
    show_save_prompt = true;
    save_prompt_input.clear();
    needs_redraw = true;
    return;
  }
  save_buffer_at(current_buffer, true);
}

bool Editor::save_buffer_at(int index, bool announce) {
  if (index < 0 || index >= (int)buffers.size()) {
    return false;
  }
  auto &buf = buffers[index];
  if (buf.filepath.empty()) {
    return false;
  }

  if (buf.is_lazy() && !buf.modified) {
    if (announce) {
      message = "Saved: " + get_filename(buf.filepath);
      needs_redraw = true;
    }
    return true;
  }

  if (buf.is_lazy()) {
    buf.materialize();
  }

  std::ofstream file(buf.filepath);
  if (!file.is_open()) {
    if (announce) {
      message = "Save failed: cannot open " + buf.filepath;
      needs_redraw = true;
    }
    return false;
  }
  for (const auto &line : buf.lines) {
    file << line << '\n';
  }
  if (!file.good()) {
    if (announce) {
      message = "Save failed: write error";
      needs_redraw = true;
    }
    return false;
  }
  file.close();

  auto run_formatter = [this, &buf](const std::string &runner) -> bool {
    std::string cmd = runner + " --write " + shell_quote(buf.filepath) +
                      " >/dev/null 2>&1";
    if (std::system(cmd.c_str()) != 0)
      return false;
    std::vector<std::string> refreshed_lines;
    if (!read_file_lines(buf.filepath, refreshed_lines))
      return false;
    buf.lines.swap(refreshed_lines);
    normalize_buffer_after_external_edit(buf);
    buf.fold_ranges.clear();
    invalidate_syntax_cache(buf);
    return true;
  };

  std::string prettier_runner;
  bool do_prettier =
      config.get_bool("prettier_on_save", true) &&
      supports_prettier_on_save(buf.filepath) &&
      !(prettier_runner = detect_prettier_runner()).empty();

  std::string clang_runner;
  bool do_clang =
      config.get_bool("clang_format_on_save", true) &&
      supports_clang_format_on_save(buf.filepath) &&
      !(clang_runner = detect_clang_format_runner()).empty();

  if (task_queue_ && (do_prettier || do_clang)) {
    std::string runner = do_prettier ? prettier_runner : clang_runner;
    std::string fmt_name = do_prettier ? "prettier" : "clang-format";
    std::string filepath = buf.filepath;  // captured by value below

    task_queue_->submit_val<std::vector<std::string>>(
        [filepath, runner = std::move(runner)]() -> std::vector<std::string> {
          std::string cmd = runner + " --write " + shell_quote(filepath) +
                            " >/dev/null 2>&1";
          std::vector<std::string> result;
          if (std::system(cmd.c_str()) == 0) {
            read_file_lines(filepath, result);
          }
          return result;
        },
        [this, filepath, announce,
         fmt_name](std::vector<std::string> refreshed) {
          // Editor may have shut down while the formatter was
          // running on the worker thread. Drop the result rather
          // than touching freed state.
          if (!running)
            return;
          int found = -1;
          for (int i = 0; i < (int)buffers.size(); i++) {
            if (buffers[i].filepath == filepath) { found = i; break; }
          }
          if (found < 0) return;
          auto &b = buffers[found];
          if (b.filepath.empty())
            return;

          if (!refreshed.empty()) {
            b.lines.swap(refreshed);
          }
          normalize_buffer_after_external_edit(b);
          b.fold_ranges.clear();
          invalidate_syntax_cache(b);
          b.modified = false;
          b.is_placeholder = false;
          if (b.is_preview) {
            b.is_preview = false;
            if (preview_buffer_index == found)
              preview_buffer_index = -1;
          }
          track_recent_file(b.filepath);
          if (announce) {
            message = "Saved: " + get_filename(b.filepath) +
                      " (formatted: " + fmt_name + ")";
            needs_redraw = true;
          }
          if (python_api)
            python_api->on_buffer_save(b.filepath);
          notify_lsp_save(b.filepath);
          refresh_git_status(true);
        });
    buf.modified = false;
    buf.is_placeholder = false;
    return true;
  }

  bool formatted_with_prettier = false;
  bool formatted_with_clang = false;
  if (do_prettier && run_formatter(prettier_runner))
    formatted_with_prettier = true;
  if (!formatted_with_prettier && do_clang && run_formatter(clang_runner))
    formatted_with_clang = true;

  buf.modified = false;
  buf.is_placeholder = false;
  if (buf.is_preview) {
    buf.is_preview = false;
    if (preview_buffer_index == index) {
      preview_buffer_index = -1;
    }
  }
  track_recent_file(buf.filepath);
  if (announce) {
    message = "Saved: " + get_filename(buf.filepath);
    if (formatted_with_prettier) {
      message += " (formatted: prettier)";
    } else if (formatted_with_clang) {
      message += " (formatted: clang-format)";
    }
    needs_redraw = true;
  }
  if (python_api)
    python_api->on_buffer_save(buf.filepath);
  notify_lsp_save(buf.filepath);
  refresh_git_status(true);
  return true;
}

void Editor::save_file_as() {
  show_command_palette = true;
  command_palette_query = "w ";
  command_palette_selected = 0;
  command_palette_theme_mode = false;
  command_palette_theme_original.clear();
  refresh_command_palette();
  needs_redraw = true;
}

void Editor::close_buffer_at(int index) {
  if (index < 0 || index >= (int)buffers.size())
    return;

  if (!buffers[index].filepath.empty()) {
    save_file_fold_state(buffers[index]);
  }

  const FileBuffer &snapshot_source = buffers[index];
  invalidate_sidebar_diagnostics_cache();

#ifdef JOT_TREESITTER
  {
    FileBuffer &mut_buf = buffers[index];
    if (mut_buf.ts_tree) { ts_tree_delete(mut_buf.ts_tree); mut_buf.ts_tree = nullptr; }
    if (mut_buf.ts_parser) { ts_parser_delete(mut_buf.ts_parser); mut_buf.ts_parser = nullptr; }
  }
#endif

  if (closed_buffer_history.size() >= kMaxClosedBufferHistory) {
    closed_buffer_history.erase(closed_buffer_history.begin());
  }
  if (!snapshot_source.is_lazy() &&
      (!snapshot_source.filepath.empty() ||
       (snapshot_source.modified && !snapshot_source.lines.empty()))) {
    closed_buffer_history.push_back(
        {snapshot_source.filepath, snapshot_source.lines, snapshot_source.cursor,
         snapshot_source.selection, snapshot_source.scroll_offset,
         snapshot_source.scroll_x, snapshot_source.modified,
         snapshot_source.fold_ranges});
  }

  if (buffers.size() == 1) {
    FileBuffer &buf = buffers[0];
    if (buf.is_lazy()) buf.materialize();
    buf.filepath.clear();
    buf.lines.clear();
    buf.lazy_provider.reset();
    buf.lines.push_back("");
    buf.cursor = {0, 0};
    buf.preferred_x = 0;
    buf.selection = {{0, 0}, {0, 0}, false};
    buf.scroll_offset = 0;
    buf.scroll_x = 0;
    buf.modified = false;
    buf.is_preview = false;
    buf.is_placeholder = true;
    buf.undo_stack = std::stack<State>();
    buf.redo_stack = std::stack<State>();
    buf.bookmarks.clear();
    buf.diagnostics.clear();
    buf.fold_ranges.clear();
    invalidate_sidebar_diagnostics_cache();
#ifdef JOT_TREESITTER
    if (buf.ts_tree) { ts_tree_delete(buf.ts_tree); buf.ts_tree = nullptr; }
    if (buf.ts_parser) { ts_parser_delete(buf.ts_parser); buf.ts_parser = nullptr; }
#endif
    current_buffer = 0;
    tab_scroll_index = 0;
    preview_buffer_index = -1;
    for (auto &pane : panes) {
      pane.buffer_id = 0;
      pane.tab_buffer_ids.clear();
      pane.tab_buffer_ids.push_back(0);
      pane.tab_scroll_index = 0;
    }
    message = "Closed file";
    needs_redraw = true;
    return;
  }

  int removed = index;

  if (preview_buffer_index == removed) {
    preview_buffer_index = -1;
  }
  buffers.erase(buffers.begin() + removed);

  if (current_buffer > removed) {
    current_buffer--;
  } else if (current_buffer >= (int)buffers.size()) {
    current_buffer = (int)buffers.size() - 1;
  }

  if (tab_scroll_index > removed) {
    tab_scroll_index--;
  }
  tab_scroll_index = std::clamp(tab_scroll_index, 0,
                                std::max(0, (int)buffers.size() - 1));
  if (preview_buffer_index > removed) {
    preview_buffer_index--;
  }
  if (preview_buffer_index < 0 || preview_buffer_index >= (int)buffers.size() ||
      (preview_buffer_index >= 0 && !buffers[preview_buffer_index].is_preview)) {
    preview_buffer_index = -1;
  }

  for (auto &pane : panes) {
    for (auto &id : pane.tab_buffer_ids) {
      if (id > removed) {
        id--;
      }
    }
    pane.tab_buffer_ids.erase(
        std::remove(pane.tab_buffer_ids.begin(), pane.tab_buffer_ids.end(),
                    removed),
        pane.tab_buffer_ids.end());

    if (pane.buffer_id == removed) {
      if (!pane.tab_buffer_ids.empty()) {
        pane.buffer_id = pane.tab_buffer_ids.front();
      } else {
        pane.buffer_id = std::clamp(current_buffer, 0,
                                    std::max(0, (int)buffers.size() - 1));
        pane.tab_buffer_ids.push_back(pane.buffer_id);
      }
    } else if (pane.buffer_id > removed) {
      pane.buffer_id--;
    }

    if (std::find(pane.tab_buffer_ids.begin(), pane.tab_buffer_ids.end(),
                  pane.buffer_id) == pane.tab_buffer_ids.end()) {
      pane.tab_buffer_ids.push_back(pane.buffer_id);
    }
    clamp_tab_scroll(pane);
    int draw_w = std::max(1, pane.w);
    if (show_minimap && draw_w > 20) {
      draw_w = std::max(1, draw_w - minimap_width);
    }
    reveal_local_tab(pane, find_local_tab_index(pane, pane.buffer_id), draw_w);
  }

  if (!panes.empty()) {
    auto &pane = get_pane();
    pane.buffer_id = current_buffer;
    if (std::find(pane.tab_buffer_ids.begin(), pane.tab_buffer_ids.end(),
                  current_buffer) == pane.tab_buffer_ids.end()) {
      pane.tab_buffer_ids.push_back(current_buffer);
    }
    clamp_tab_scroll(pane);
    int draw_w = std::max(1, pane.w);
    if (show_minimap && draw_w > 20) {
      draw_w = std::max(1, draw_w - minimap_width);
    }
    reveal_local_tab(pane, find_local_tab_index(pane, current_buffer), draw_w);
  }
  message = "Closed file";
  needs_redraw = true;
}

void Editor::close_buffer() {
  close_buffer_at(current_buffer);
}

void Editor::reopen_last_closed_buffer() {
  if (closed_buffer_history.empty()) {
    set_message("No recently closed buffer");
    return;
  }

  ClosedBufferSnapshot snap = closed_buffer_history.back();
  closed_buffer_history.pop_back();

  FileBuffer fb;
  fb.filepath = snap.filepath;
  fb.lines = snap.lines;
  if (fb.line_count() == 0) {
    fb.lines.push_back("");
  }
  fb.cursor = snap.cursor;
  fb.preferred_x = snap.cursor.x;
  fb.selection = snap.selection;
  fb.scroll_offset = std::max(0, snap.scroll_offset);
  fb.scroll_x = std::max(0, snap.scroll_x);
  fb.modified = snap.modified;
  fb.fold_ranges = snap.collapsed_folds;
  fb.is_preview = false;
  fb.is_placeholder = false;

  buffers.push_back(std::move(fb));
  current_buffer = (int)buffers.size() - 1;
  tab_scroll_index = std::min(tab_scroll_index, current_buffer);
  preview_buffer_index = -1;
  auto &pane = get_pane();
  pane.buffer_id = current_buffer;
  if (std::find(pane.tab_buffer_ids.begin(), pane.tab_buffer_ids.end(),
                current_buffer) == pane.tab_buffer_ids.end()) {
    pane.tab_buffer_ids.push_back(current_buffer);
  }
  int draw_w = std::max(1, pane.w);
  if (show_minimap && draw_w > 20) {
    draw_w = std::max(1, draw_w - minimap_width);
  }
  reveal_local_tab(pane, find_local_tab_index(pane, current_buffer), draw_w);
  clamp_cursor(current_buffer);
  ensure_cursor_visible();

  if (!buffers[current_buffer].filepath.empty()) {
    track_recent_file(buffers[current_buffer].filepath);
    highlighter.set_language(get_file_extension(buffers[current_buffer].filepath));
    notify_lsp_open(buffers[current_buffer].filepath);
  }

  set_message("Reopened closed buffer");
}

void Editor::track_recent_file(const std::string &path) {
  const std::string normalized = normalize_existing_path(path);
  if (normalized.empty()) {
    return;
  }

  recent_files.erase(
      std::remove(recent_files.begin(), recent_files.end(), normalized),
      recent_files.end());
  recent_files.insert(recent_files.begin(), normalized);
  if ((int)recent_files.size() > kMaxRecentFiles) {
    recent_files.resize(kMaxRecentFiles);
  }
}

void Editor::track_recent_workspace(const std::string &path) {
  const std::string normalized = normalize_existing_path(path);
  if (normalized.empty()) {
    return;
  }

  std::error_code ec;
  if (!fs::exists(normalized, ec) || ec || !fs::is_directory(normalized, ec)) {
    return;
  }

  recent_workspaces.erase(
      std::remove(recent_workspaces.begin(), recent_workspaces.end(),
                  normalized),
      recent_workspaces.end());
  recent_workspaces.insert(recent_workspaces.begin(), normalized);
  if ((int)recent_workspaces.size() > kMaxRecentWorkspaces) {
    recent_workspaces.resize(kMaxRecentWorkspaces);
  }
}

void Editor::load_recent_files() {
  recent_files.clear();
  const std::string path = recent_files_path();
  if (path.empty()) {
    return;
  }

  std::ifstream file(path);
  if (!file.is_open()) {
    return;
  }

  std::string line;
  std::set<std::string> seen;
  while (std::getline(file, line)) {
    if (line.empty()) {
      continue;
    }
    const std::string normalized = normalize_existing_path(line);
    if (normalized.empty()) {
      continue;
    }
    std::error_code ec;
    if (!fs::exists(normalized, ec) || ec) {
      continue;
    }
    if (seen.find(normalized) != seen.end()) {
      continue;
    }
    seen.insert(normalized);
    recent_files.push_back(normalized);
    if ((int)recent_files.size() >= kMaxRecentFiles) {
      break;
    }
  }
}

void Editor::load_recent_workspaces() {
  recent_workspaces.clear();
  const std::string path = recent_workspaces_path();
  if (path.empty()) {
    return;
  }

  std::ifstream file(path);
  if (!file.is_open()) {
    return;
  }

  std::string line;
  std::set<std::string> seen;
  while (std::getline(file, line)) {
    if (line.empty()) {
      continue;
    }
    const std::string normalized = normalize_existing_path(line);
    if (normalized.empty()) {
      continue;
    }
    std::error_code ec;
    if (!fs::exists(normalized, ec) || ec || !fs::is_directory(normalized, ec)) {
      continue;
    }
    if (seen.find(normalized) != seen.end()) {
      continue;
    }
    seen.insert(normalized);
    recent_workspaces.push_back(normalized);
    if ((int)recent_workspaces.size() >= kMaxRecentWorkspaces) {
      break;
    }
  }
}

void Editor::save_recent_files() {
  const std::string path = recent_files_path();
  if (path.empty()) {
    return;
  }

  std::error_code ec;
  fs::path output_path(path);
  fs::create_directories(output_path.parent_path(), ec);

  std::ofstream file(path);
  if (!file.is_open()) {
    return;
  }
  for (const auto &entry : recent_files) {
    file << entry << '\n';
  }
}

void Editor::save_recent_workspaces() {
  const std::string path = recent_workspaces_path();
  if (path.empty()) {
    return;
  }

  std::error_code ec;
  fs::path output_path(path);
  fs::create_directories(output_path.parent_path(), ec);

  std::ofstream file(path);
  if (!file.is_open()) {
    return;
  }
  for (const auto &entry : recent_workspaces) {
    file << entry << '\n';
  }
}

void Editor::save_file_fold_state(FileBuffer &buf) {
  if (buf.filepath.empty() || buf.is_lazy()) {
    return;
  }

  Folding::refresh_ranges(buf.fold_ranges, buf.lines,
                          get_file_extension(buf.filepath));
  const std::string normalized = normalize_existing_path(buf.filepath);
  if (normalized.empty()) {
    return;
  }

  auto states = load_file_fold_state_map();
  const std::string payload = Folding::encode_collapsed_ranges(buf.fold_ranges);
  if (payload.empty()) {
    states.erase(normalized);
  } else {
    states[normalized] = payload;
  }
  write_file_fold_state_map(states);
}

void Editor::save_file_fold_states() {
  auto states = load_file_fold_state_map();
  bool changed = false;

  for (auto &buf : buffers) {
    if (buf.filepath.empty() || buf.is_lazy()) {
      continue;
    }
    Folding::refresh_ranges(buf.fold_ranges, buf.lines,
                            get_file_extension(buf.filepath));
    const std::string normalized = normalize_existing_path(buf.filepath);
    if (normalized.empty()) {
      continue;
    }
    const std::string payload =
        Folding::encode_collapsed_ranges(buf.fold_ranges);
    if (payload.empty()) {
      changed = states.erase(normalized) > 0 || changed;
    } else if (states[normalized] != payload) {
      states[normalized] = payload;
      changed = true;
    }
  }

  if (changed) {
    write_file_fold_state_map(states);
  }
}

void Editor::restore_file_fold_state(FileBuffer &buf) {
  if (buf.filepath.empty() || buf.is_lazy()) {
    return;
  }

  const std::string normalized = normalize_existing_path(buf.filepath);
  if (normalized.empty()) {
    return;
  }

  auto states = load_file_fold_state_map();
  auto it = states.find(normalized);
  if (it == states.end() || it->second.empty()) {
    return;
  }

  Folding::refresh_ranges(buf.fold_ranges, buf.lines,
                          get_file_extension(buf.filepath));
  Folding::apply_collapsed_ranges(
      buf.fold_ranges, Folding::decode_collapsed_ranges(it->second));
  while (buf.cursor.y > 0 &&
         Folding::is_line_hidden(buf.fold_ranges, buf.cursor.y)) {
    buf.cursor.y--;
  }
  buf.cursor.x = std::clamp(buf.cursor.x, 0, (int)buf.line(buf.cursor.y).size());
  buf.preferred_x = buf.cursor.x;
}

void Editor::open_recent_file(const std::string &query) {
  if (recent_files.empty()) {
    set_message("Recent files list is empty");
    return;
  }

  auto open_path = [&](const std::string &path) {
    std::error_code ec;
    if (!fs::exists(path, ec) || ec) {
      set_message("Recent file missing: " + path);
      recent_files.erase(
          std::remove(recent_files.begin(), recent_files.end(), path),
          recent_files.end());
      return;
    }
    open_file(path);
    set_message("Opened recent: " + get_filename(path));
  };

  if (query.empty()) {
    open_path(recent_files.front());
    return;
  }

  std::string query_trimmed = query;
  query_trimmed.erase(query_trimmed.begin(),
                      std::find_if(query_trimmed.begin(), query_trimmed.end(),
                                   [](unsigned char ch) { return !std::isspace(ch); }));
  query_trimmed.erase(
      std::find_if(query_trimmed.rbegin(), query_trimmed.rend(),
                   [](unsigned char ch) { return !std::isspace(ch); })
          .base(),
      query_trimmed.end());

  bool numeric = !query_trimmed.empty();
  for (char c : query_trimmed) {
    if (!std::isdigit((unsigned char)c)) {
      numeric = false;
      break;
    }
  }
  if (numeric) {
    try {
      long long idx = std::stoll(query_trimmed);
      if (idx >= 1 && idx <= (long long)recent_files.size()) {
        open_path(recent_files[(size_t)idx - 1]);
        return;
      }
      set_message("Recent index out of range: " + query_trimmed);
      return;
    } catch (...) {
      set_message("Invalid recent index: " + query_trimmed);
      return;
    }
  }

  std::string needle = query_trimmed.empty() ? query : query_trimmed;
  std::transform(needle.begin(), needle.end(), needle.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  for (const auto &path : recent_files) {
    std::string haystack = path;
    std::transform(haystack.begin(), haystack.end(), haystack.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (haystack.find(needle) != std::string::npos) {
      open_path(path);
      return;
    }
  }

  set_message("No recent file matched: " + query);
}

void Editor::set_auto_save(bool enabled, bool persist) {
  auto_save_enabled = enabled;
  if (persist) {
    config.set("auto_save", enabled ? "true" : "false");
    config.save();
  }
}

void Editor::set_auto_save_interval(int interval_ms, bool persist) {
  auto_save_interval_ms = std::clamp(interval_ms, 250, 60000);
  if (persist) {
    config.set("auto_save_interval_ms", std::to_string(auto_save_interval_ms));
    config.save();
  }
}

void FileBuffer::materialize() {
  if (!lazy_provider)
    return;
  lines = lazy_provider->copy_all_lines();
  lazy_provider.reset();
}

void Editor::auto_save_modified_buffers() {
  if (!auto_save_enabled) {
    return;
  }

  int saved = 0;
  for (int i = 0; i < (int)buffers.size(); i++) {
    if (!buffers[i].modified || buffers[i].filepath.empty()) {
      continue;
    }
    if (save_buffer_at(i, false)) {
      saved++;
    }
  }

  if (saved > 0) {
    set_message("Auto-saved " + std::to_string(saved) + " file(s)");
  }
}

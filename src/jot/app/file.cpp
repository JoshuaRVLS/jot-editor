// Core file open/save: load, finish-open, new buffers, save (with formatter
// detection and external-edit normalization), and save-as.
#include "editor.h"
#include "folding.h"
#include "jot/app/file_internal.h"
#include "jot/lua/api.h"
#include "cpp_assist.h"
#include "lazy_line_provider.h"
#include "tools/shell_util.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

using namespace file_internal;

namespace
{

  std::string sanitize_input_path(const std::string &path)
  {
    std::string out = path;
    out.erase(
        out.begin(),
        std::find_if(out.begin(), out.end(), [](unsigned char c) { return !std::isspace(c); }));
    out.erase(
        std::find_if(out.rbegin(), out.rend(), [](unsigned char c) { return !std::isspace(c); })
            .base(),
        out.end());
    if (out.size() >= 2
        && ((out.front() == '"' && out.back() == '"')
            || (out.front() == '\'' && out.back() == '\'')))
    {
      out = out.substr(1, out.size() - 2);
    }
    return out;
  }

  std::string command_silence_redirect()
  {
#ifdef _WIN32
    return " >NUL 2>NUL";
#else
    return " >/dev/null 2>&1";
#endif
  }

  std::string detect_prettier_runner()
  {
    static int mode = -1; // -1 unknown, 0 unavailable, 1 prettier, 2 npx
    if (mode == -1)
    {
      if (shell_util::command_exists("prettier"))
      {
        mode = 1;
      }
      else if (shell_util::command_exists("npx"))
      {
        mode = 2;
      }
      else
      {
        mode = 0;
      }
    }
    if (mode == 1)
    {
      return "prettier";
    }
    if (mode == 2)
    {
      return "npx --yes prettier";
    }
    return "";
  }

  std::string detect_clang_format_runner()
  {
    static int mode = -1; // -1 unknown, 0 unavailable, 1 clang-format
    if (mode == -1)
    {
      mode = shell_util::command_exists("clang-format") ? 1 : 0;
    }
    return mode == 1 ? "clang-format" : "";
  }

  bool supports_prettier_on_save(const std::string &path)
  {
    std::error_code ec;
    fs::path p(path);
    const std::string name = p.filename().string();
    std::string ext = p.extension().string();
    std::transform(
        ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });

    static const std::set<std::string> exts = {
        ".js",  ".jsx",  ".cjs", ".mjs",  ".ts",     ".tsx",  ".mts",
        ".cts", ".json", ".css", ".scss", ".less",   ".html", ".md",
        ".mdx", ".yaml", ".yml", ".vue",  ".svelte", ".gql",  ".graphql"};

    if (exts.find(ext) != exts.end())
    {
      return true;
    }

    static const std::set<std::string> names = {".prettierrc",
                                                ".prettierrc.json",
                                                ".prettierrc.yaml",
                                                ".prettierrc.yml",
                                                ".prettierrc.js",
                                                ".prettierrc.cjs",
                                                ".prettierrc.mjs"};
    return names.find(name) != names.end() && fs::exists(p, ec) && !ec;
  }

  bool supports_clang_format_on_save(const std::string &path)
  {
    fs::path p(path);
    std::string ext = p.extension().string();
    std::transform(
        ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    static const std::set<std::string> exts = {
        ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".m", ".mm"};
    return exts.find(ext) != exts.end();
  }

  bool read_file_lines(const std::string &path, std::vector<std::string> &out)
  {
    std::ifstream file(path);
    if (!file.is_open())
    {
      return false;
    }
    out.clear();
    std::string line;
    while (std::getline(file, line))
    {
      if (!line.empty() && line.back() == '\r')
      {
        line.pop_back();
      }
      out.push_back(line);
    }
    if (out.empty())
    {
      out.push_back("");
    }
    return true;
  }

  bool is_supported_image_path(const std::string &path)
  {
    fs::path p(path);
    std::string ext = p.extension().string();
    std::transform(
        ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    static const std::set<std::string> exts = {".jpg",
                                               ".jpeg",
                                               ".png",
                                               ".gif",
                                               ".bmp",
                                               ".svg",
                                               ".webp",
                                               ".ico",
                                               ".tif",
                                               ".tiff",
                                               ".avif",
                                               ".heic",
                                               ".ppm",
                                               ".pgm",
                                               ".pbm",
                                               ".xpm",
                                               ".jxl"};
    return exts.find(ext) != exts.end();
  }

  void normalize_buffer_after_external_edit(FileBuffer &buf)
  {
    if (buf.is_lazy())
      return;

    if (buf.line_count() == 0)
    {
      buf.lines.push_back("");
    }

    buf.cursor.y = std::clamp(buf.cursor.y, 0, std::max(0, (int)buf.line_count() - 1));
    buf.cursor.x = std::clamp(buf.cursor.x, 0, (int)buf.line(buf.cursor.y).size());
    buf.preferred_x = buf.cursor.x;

    buf.scroll_offset = std::clamp(buf.scroll_offset, 0, std::max(0, (int)buf.line_count() - 1));
    buf.scroll_x = std::max(0, buf.scroll_x);

    if (buf.selection.active)
    {
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
void Editor::load_file(const std::string &fname)
{
  open_file(fname, false);
}

int Editor::detect_indent_width(const std::vector<std::string> &lines) const
{
  std::map<int, int> delta_score;
  int tab_indented_lines = 0;
  int space_indented_lines = 0;

  auto count_leading = [](const std::string &line, char ch)
  {
    int n = 0;
    while (n < (int)line.size() && line[n] == ch)
      n++;
    return n;
  };

  int prev_space_indent = -1;
  for (const auto &line : lines)
  {
    if (line.empty())
    {
      continue;
    }

    int tabs = count_leading(line, '\t');
    int spaces = count_leading(line, ' ');
    if (tabs > 0)
    {
      tab_indented_lines++;
    }
    else if (spaces > 0)
    {
      space_indented_lines++;
    }

    if (tabs > 0 || spaces == 0)
    {
      prev_space_indent = -1;
      continue;
    }

    if (prev_space_indent >= 0 && spaces != prev_space_indent)
    {
      int delta = std::abs(spaces - prev_space_indent);
      if (delta >= 1 && delta <= 8)
      {
        delta_score[delta]++;
      }
    }
    prev_space_indent = spaces;
  }

  if (tab_indented_lines > space_indented_lines && tab_indented_lines >= 3)
  {
    return 4;
  }

  int best_width = -1;
  int best_score = 0;
  for (const auto &[width, score] : delta_score)
  {
    if (score > best_score || (score == best_score && width < best_width))
    {
      best_width = width;
      best_score = score;
    }
  }

  if (best_width >= 1 && best_width <= 8 && best_score >= 2)
  {
    return best_width;
  }

  return tab_size;
}
void Editor::open_file(const std::string &path, bool preview)
{
  show_home_menu = false;
  hide_lsp_completion();
  hide_lsp_signature();

  const std::string clean_path = sanitize_input_path(path);
  if (clean_path.empty())
  {
    set_message("Open failed: empty path");
    return;
  }

  const std::string normalized = normalize_existing_path(clean_path);
  const std::string path_to_open = normalized.empty() ? clean_path : normalized;

  {
    std::error_code ec;
    if (is_supported_image_path(path_to_open) && fs::exists(path_to_open, ec) && !ec
        && fs::is_regular_file(path_to_open, ec) && !ec)
    {
      image_viewer.open(path_to_open);
      track_recent_file(path_to_open);
      refresh_git_status(true);
      // A tab for the image, built empty rather than read: loading the file as
      // text turns a whole PNG into one enormous line and the frame never
      // finishes. Nothing ever renders this buffer's text -- render_pane draws
      // the viewer for a buffer whose path is an image.
      int existing = -1;
      for (size_t i = 0; i < buffers.size(); i++)
      {
        if (buffers[i].filepath == path_to_open)
        {
          existing = (int)i;
          break;
        }
      }
      if (existing < 0)
      {
        FileBuffer fb;
        fb.filepath = path_to_open;
        fb.lines.push_back("");
        fb.is_preview = false;
        fb.is_placeholder = false;
        buffers.push_back(std::move(fb));
        existing = (int)buffers.size() - 1;
      }
      current_buffer = existing;
      tab_scroll_index = std::min(tab_scroll_index, current_buffer);
      preview_buffer_index = -1;
      {
        auto &pane = get_pane();
        capture_pane_view(current_pane);
        pane.buffer_id = current_buffer;
        restore_pane_view(current_pane);
        if (std::find(pane.tab_buffer_ids.begin(), pane.tab_buffer_ids.end(), current_buffer)
            == pane.tab_buffer_ids.end())
        {
          pane.tab_buffer_ids.push_back(current_buffer);
        }
        int draw_w = std::max(1, pane.w);
        if (show_minimap && draw_w > 20)
        {
          draw_w = std::max(1, draw_w - minimap_width);
        }
        reveal_local_tab(pane, find_local_tab_index(pane, current_buffer), draw_w);
      }
      needs_redraw = true;
      return;
    }
  }

  auto find_open_index = [&]()
  {
    for (size_t i = 0; i < buffers.size(); i++)
    {
      const std::string candidate = normalize_existing_path(buffers[i].filepath);
      if (!candidate.empty() && candidate == path_to_open)
      {
        return (int)i;
      }
      if (buffers[i].filepath == path_to_open || buffers[i].filepath == clean_path
          || buffers[i].filepath == path)
      {
        return (int)i;
      }
    }
    return -1;
  };

  int existing_index = find_open_index();

  if (preview && preview_buffer_index >= 0 && preview_buffer_index < (int)buffers.size()
      && preview_buffer_index != existing_index)
  {
    const bool can_replace_preview =
        buffers[preview_buffer_index].is_preview && !buffers[preview_buffer_index].modified;
    if (can_replace_preview)
    {
      close_buffer_at(preview_buffer_index);
      existing_index = find_open_index();
    }
  }

  if (existing_index >= 0 && existing_index < (int)buffers.size())
  {
    current_buffer = existing_index;
    auto &pane = get_pane();
    capture_pane_view(current_pane);
    pane.buffer_id = existing_index;
    restore_pane_view(current_pane);
    pane.tab_buffer_ids.erase(std::remove_if(pane.tab_buffer_ids.begin(),
                                             pane.tab_buffer_ids.end(),
                                             [this](int id)
                                             {
                                               return id >= 0 && id < (int)buffers.size()
                                                      && buffers[id].is_placeholder
                                                      && !buffers[id].modified
                                                      && buffers[id].filepath.empty();
                                             }),
                              pane.tab_buffer_ids.end());
    if (std::find(pane.tab_buffer_ids.begin(), pane.tab_buffer_ids.end(), existing_index)
        == pane.tab_buffer_ids.end())
    {
      pane.tab_buffer_ids.push_back(existing_index);
    }
    int draw_w = std::max(1, pane.w);
    if (show_minimap && draw_w > 20)
    {
      draw_w = std::max(1, draw_w - minimap_width);
    }
    reveal_local_tab(pane, find_local_tab_index(pane, existing_index), draw_w);
    if (!preview && buffers[existing_index].is_preview)
    {
      buffers[existing_index].is_preview = false;
      if (preview_buffer_index == existing_index)
      {
        preview_buffer_index = -1;
      }
    }
    if (preview && buffers[existing_index].is_preview)
    {
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
    if (!ec && file_size > kFileSizeLazyThreshold)
    {
      fb.lazy_provider = LazyLineProvider::open(path_to_open);
      if (!fb.lazy_provider)
        set_message("Failed to open large file with lazy loading");
    }
  }

  if (!fb.lazy_provider)
  {
    if (task_queue_)
    {
      auto shared_fb = std::make_shared<FileBuffer>(std::move(fb));
      task_queue_->submit(
          [path_to_open, shared_fb]()
          {
            std::ifstream file(path_to_open);
            if (file.is_open())
            {
              std::string line;
              while (std::getline(file, line))
              {
                if (!line.empty() && line.back() == '\r')
                  line.pop_back();
                shared_fb->lines.push_back(line);
              }
            }
            if (shared_fb->lines.empty())
              shared_fb->lines.push_back("");
          },
          [this, path_to_open, preview, shared_fb]() mutable
          {
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
    if (file.is_open())
    {
      std::string line;
      while (std::getline(file, line))
      {
        if (!line.empty() && line.back() == '\r')
        {
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
void Editor::finish_open_file(FileBuffer fb, const std::string &path_to_open, bool preview)
{
  if (!fb.is_lazy() && config.get_bool("auto_detect_indent", false))
  {
    int detected_tab_size = detect_indent_width(fb.lines);
    if (detected_tab_size != tab_size && detected_tab_size >= 1 && detected_tab_size <= 8)
    {
      tab_size = detected_tab_size;
      message = "Indent detected: " + std::to_string(tab_size) + " spaces";
    }
  }

  buffers.push_back(std::move(fb));
  current_buffer = buffers.size() - 1;
  auto &pane = get_pane();
  capture_pane_view(current_pane);
  pane.buffer_id = current_buffer;
  pane.tab_buffer_ids.erase(std::remove_if(pane.tab_buffer_ids.begin(),
                                           pane.tab_buffer_ids.end(),
                                           [this](int id)
                                           {
                                             return id >= 0 && id < (int)buffers.size()
                                                    && buffers[id].is_placeholder
                                                    && !buffers[id].modified
                                                    && buffers[id].filepath.empty();
                                           }),
                            pane.tab_buffer_ids.end());
  if (std::find(pane.tab_buffer_ids.begin(), pane.tab_buffer_ids.end(), current_buffer)
      == pane.tab_buffer_ids.end())
  {
    pane.tab_buffer_ids.push_back(current_buffer);
  }
  int draw_w = std::max(1, pane.w);
  if (show_minimap && draw_w > 20)
  {
    draw_w = std::max(1, draw_w - minimap_width);
  }
  reveal_local_tab(pane, find_local_tab_index(pane, current_buffer), draw_w);
  if (preview)
  {
    preview_buffer_index = current_buffer;
  }
  track_recent_file(path_to_open);

  highlighter.set_language(get_file_extension(path_to_open));

#ifdef JOT_TREESITTER
  init_ts_for_buffer(buffers.back());
#endif

  restore_file_fold_state(buffers.back());

  if (lua_api)
  {
    lua_api->on_buffer_open(path_to_open);
  }
  else
  {
    notify_lsp_open(path_to_open);
  }
  refresh_git_status(true);
  apply_pending_lsp_definition_jump();
  apply_pending_jump();
  needs_redraw = true;
}
void Editor::create_new_buffer()
{
  show_home_menu = false;
  hide_lsp_completion();
  hide_lsp_signature();

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
  capture_pane_view(current_pane);
  pane.buffer_id = current_buffer;
  pane.tab_buffer_ids.erase(std::remove_if(pane.tab_buffer_ids.begin(),
                                           pane.tab_buffer_ids.end(),
                                           [this](int id)
                                           {
                                             return id >= 0 && id < (int)buffers.size()
                                                    && buffers[id].is_placeholder
                                                    && !buffers[id].modified
                                                    && buffers[id].filepath.empty();
                                           }),
                            pane.tab_buffer_ids.end());
  if (std::find(pane.tab_buffer_ids.begin(), pane.tab_buffer_ids.end(), current_buffer)
      == pane.tab_buffer_ids.end())
  {
    pane.tab_buffer_ids.push_back(current_buffer);
  }
  int draw_w = std::max(1, pane.w);
  if (show_minimap && draw_w > 20)
  {
    draw_w = std::max(1, draw_w - minimap_width);
  }
  reveal_local_tab(pane, find_local_tab_index(pane, current_buffer), draw_w);
}
void Editor::save_file()
{
  const auto &buf = get_buffer();
  if (buf.filepath.empty())
  {
    show_save_prompt = true;
    save_prompt_input.clear();
    needs_redraw = true;
    return;
  }
  save_buffer_at(current_buffer, true);
}
bool Editor::save_buffer_at(int index, bool announce)
{
  if (index < 0 || index >= (int)buffers.size())
  {
    return false;
  }
  auto &buf = buffers[index];
  if (buf.filepath.empty())
  {
    return false;
  }

  if (buf.is_lazy() && !buf.modified)
  {
    if (announce)
    {
      message = "Saved: " + get_filename(buf.filepath);
      needs_redraw = true;
    }
    return true;
  }

  if (buf.is_lazy())
  {
    buf.materialize();
  }

  std::ofstream file(buf.filepath);
  if (!file.is_open())
  {
    if (announce)
    {
      message = "Save failed: cannot open " + buf.filepath;
      needs_redraw = true;
    }
    return false;
  }
  for (const auto &line : buf.lines)
  {
    file << line << '\n';
  }
  if (!file.good())
  {
    if (announce)
    {
      message = "Save failed: write error";
      needs_redraw = true;
    }
    return false;
  }
  file.close();

  auto run_formatter = [this, &buf](const std::string &runner) -> bool
  {
    std::string cmd =
        runner + " --write " + shell_util::shell_quote(buf.filepath) + command_silence_redirect();
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
  bool do_prettier = config.get_bool("prettier_on_save", true)
                     && supports_prettier_on_save(buf.filepath)
                     && !(prettier_runner = detect_prettier_runner()).empty();

  std::string clang_runner;
  bool do_clang = config.get_bool("clang_format_on_save", true)
                  && supports_clang_format_on_save(buf.filepath)
                  && !(clang_runner = detect_clang_format_runner()).empty();

  if (task_queue_ && (do_prettier || do_clang))
  {
    std::string runner = do_prettier ? prettier_runner : clang_runner;
    std::string fmt_name = do_prettier ? "prettier" : "clang-format";
    std::string filepath = buf.filepath; // captured by value below

    task_queue_->submit_val<std::vector<std::string>>(
        [filepath, runner = std::move(runner)]() -> std::vector<std::string>
        {
          std::string cmd =
              runner + " --write " + shell_util::shell_quote(filepath) + command_silence_redirect();
          std::vector<std::string> result;
          if (std::system(cmd.c_str()) == 0)
          {
            read_file_lines(filepath, result);
          }
          return result;
        },
        [this, filepath, announce, fmt_name](std::vector<std::string> refreshed)
        {
          // Editor may have shut down while the formatter was
          // running on the worker thread. Drop the result rather
          // than touching freed state.
          if (!running)
            return;
          int found = -1;
          for (int i = 0; i < (int)buffers.size(); i++)
          {
            if (buffers[i].filepath == filepath)
            {
              found = i;
              break;
            }
          }
          if (found < 0)
            return;
          auto &b = buffers[found];
          if (b.filepath.empty())
            return;

          if (!refreshed.empty())
          {
            b.lines.swap(refreshed);
          }
          normalize_buffer_after_external_edit(b);
          b.fold_ranges.clear();
          invalidate_syntax_cache(b);
          b.modified = false;
          b.is_placeholder = false;
          if (b.is_preview)
          {
            b.is_preview = false;
            if (preview_buffer_index == found)
              preview_buffer_index = -1;
          }
          track_recent_file(b.filepath);
          if (announce)
          {
            message = "Saved: " + get_filename(b.filepath) + " (formatted: " + fmt_name + ")";
            needs_redraw = true;
          }
          if (lua_api)
          {
            lua_api->on_buffer_save(b.filepath);
          }
          else
          {
            notify_lsp_save(b.filepath);
          }
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
  if (buf.is_preview)
  {
    buf.is_preview = false;
    if (preview_buffer_index == index)
    {
      preview_buffer_index = -1;
    }
  }
  track_recent_file(buf.filepath);
  if (announce)
  {
    message = "Saved: " + get_filename(buf.filepath);
    if (formatted_with_prettier)
    {
      message += " (formatted: prettier)";
    }
    else if (formatted_with_clang)
    {
      message += " (formatted: clang-format)";
    }
    needs_redraw = true;
  }
  if (lua_api)
  {
    lua_api->on_buffer_save(buf.filepath);
  }
  else
  {
    notify_lsp_save(buf.filepath);
  }
  refresh_git_status(true);
  return true;
}
void Editor::save_file_as()
{
  show_command_palette = true;
  command_palette_query = "w ";
  command_palette_selected = 0;
  command_palette_theme_mode = false;
  command_palette_theme_original.clear();
  refresh_command_palette();
  needs_redraw = true;
}
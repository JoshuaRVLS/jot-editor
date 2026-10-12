#include "imageviewer.h"
#include "base/shell_util.h"
#include "base/string_util.h"
#include "ui/terminal.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifndef _WIN32
#include <unistd.h>
#else
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace
{
  long long process_id()
  {
#ifdef _WIN32
    return (long long)GetCurrentProcessId();
#else
    return (long long)getpid();
#endif
  }

  bool env_present(const char *name)
  {
    const char *value = std::getenv(name);
    return value && value[0] != '\0';
  }

  std::string getenv_string(const char *name)
  {
    const char *value = std::getenv(name);
    return value ? std::string(value) : std::string();
  }

  // The preview block's ceiling in cells: as wide as a pane's text area used
  // to be sampled, and as tall as fits above the status line without scrolling.
  constexpr int kPreviewMaxW = 56;
  constexpr int kPreviewMaxH = 24;

  // A terminal cell is about twice as tall as it is wide (8x16 is the ratio
  // every terminal this runs in is within a cell of). Both the cell-art
  // preview and a picture's placement are measured in cells, so the ratio has
  // to be applied whenever a pixel size is turned into one -- otherwise a
  // 16:9 photograph reads as a squashed 56x24 block, which is what
  // "stretched" looked like.
  constexpr double kCellAspect = 2.0;

  // The cell grid a preview should be sampled at for a picture of px_w x px_h
  // pixels: one cell per pixel, with the cell aspect above folded in so the art
  // keeps the picture's shape. A 1920x1080 file asks for 56x16, a 1080x1920 one
  // for 27x24. Without a pixel size the full block is used, as it always was.
  void fit_preview_cells(int px_w, int px_h, int &cells_w, int &cells_h)
  {
    cells_w = kPreviewMaxW;
    cells_h = kPreviewMaxH;
    if (px_w <= 0 || px_h <= 0)
    {
      return;
    }
    const double cells_per_height = kCellAspect * (double)px_w / (double)px_h;
    cells_h = (int)std::lround((double)kPreviewMaxW / cells_per_height);
    if (cells_h > kPreviewMaxH)
    {
      cells_h = kPreviewMaxH;
      cells_w = (int)std::lround((double)kPreviewMaxH * cells_per_height);
    }
    cells_w = std::clamp(cells_w, 1, kPreviewMaxW);
    cells_h = std::clamp(cells_h, 1, kPreviewMaxH);
  }

  // The default cell size a picture is measured against when the terminal does
  // not report one (most do not: they report the window in pixels far more
  // often than a cell). Same 8x16 the sixel conversion always assumed.
  constexpr int kAssumedCellPxW = 8;
  constexpr int kAssumedCellPxH = 16;

  int grayscale_to_char_index(int gray)
  {
    static const std::string ramp =
        " .'`^\",:;Il!i~+_-?][}{1)(|\\/*tfjrxnuvczXYUJCLQ0OZmwqpdbkhao*#MW&8%B@$";
    int g = std::clamp(gray, 0, 255);
    int idx = (g * (int)(ramp.size() - 1)) / 255;
    return std::clamp(idx, 0, (int)ramp.size() - 1);
  }

  int rgb_to_xterm256(int r, int g, int b)
  {
    r = std::clamp(r, 0, 255);
    g = std::clamp(g, 0, 255);
    b = std::clamp(b, 0, 255);

    if (std::abs(r - g) < 8 && std::abs(g - b) < 8)
    {
      int gray = (r + g + b) / 3;
      int idx = 232 + (gray * 23 + 127) / 255;
      return std::clamp(idx, 232, 255);
    }

    int rr = (r * 5 + 127) / 255;
    int gg = (g * 5 + 127) / 255;
    int bb = (b * 5 + 127) / 255;
    return 16 + (36 * rr) + (6 * gg) + bb;
  }

  bool parse_rgb_triplet(const std::string &line, int &r, int &g, int &b)
  {
    auto parse_component = [](const std::string &token) -> int
    {
      std::string t;
      for (char c : token)
      {
        if (!std::isspace((unsigned char)c))
        {
          t.push_back(c);
        }
      }
      if (t.empty())
      {
        return -1;
      }
      bool percent = !t.empty() && t.back() == '%';
      if (percent)
      {
        t.pop_back();
      }
      if (t.empty())
      {
        return -1;
      }
      char *end = nullptr;
      double v = std::strtod(t.c_str(), &end);
      if (!end || *end != '\0')
      {
        return -1;
      }
      if (percent)
      {
        v = (v * 255.0) / 100.0;
      }
      return std::clamp((int)(v + 0.5), 0, 255);
    };

    size_t p = line.find('(');
    size_t q = (p == std::string::npos) ? std::string::npos : line.find(')', p + 1);
    if (p != std::string::npos && q != std::string::npos && q > p + 1)
    {
      std::string inner = line.substr(p + 1, q - p - 1);
      std::vector<std::string> parts;
      std::string cur;
      for (char c : inner)
      {
        if (c == ',')
        {
          parts.push_back(cur);
          cur.clear();
        }
        else
        {
          cur.push_back(c);
        }
      }
      if (!cur.empty())
      {
        parts.push_back(cur);
      }

      if (parts.size() >= 3)
      {
        int rr = parse_component(parts[0]);
        int gg = parse_component(parts[1]);
        int bb = parse_component(parts[2]);
        if (rr >= 0 && gg >= 0 && bb >= 0)
        {
          // A fourth component is alpha, and zero means the pixel is fully
          // transparent: its RGB is meaningless (ImageMagick reports
          // "(0,0,0,0)"), so painting it would put opaque black where the
          // image shows nothing at all.
          if (parts.size() >= 4 && parse_component(parts[3]) == 0)
          {
            return false;
          }
          r = rr;
          g = gg;
          b = bb;
          return true;
        }
      }
    }

    size_t h = line.find('#');
    if (h != std::string::npos && h + 6 < line.size())
    {
      char hex[7] = {0};
      bool ok = true;
      for (int i = 0; i < 6; i++)
      {
        char c = line[h + 1 + i];
        if (!std::isxdigit((unsigned char)c))
        {
          ok = false;
          break;
        }
        hex[i] = c;
      }
      if (ok)
      {
        unsigned int v = 0;
        if (sscanf(hex, "%x", &v) == 1)
        {
          r = (int)((v >> 16) & 0xFF);
          g = (int)((v >> 8) & 0xFF);
          b = (int)(v & 0xFF);
          return true;
        }
      }
    }
    return false;
  }

  std::string strip_ansi(const std::string &s)
  {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++)
    {
      if (s[i] == '\x1b' && i + 1 < s.size() && s[i + 1] == '[')
      {
        i += 2;
        while (i < s.size())
        {
          unsigned char c = (unsigned char)s[i];
          if (c >= 0x40 && c <= 0x7E)
          {
            break;
          }
          i++;
        }
        continue;
      }
      out.push_back(s[i]);
    }
    return out;
  }
} // namespace

ImageViewer::ImageViewer()
{
  is_open = false;
  view_x = view_y = view_w = view_h = 0;
  border_fg = 7;
  border_bg = 0;
  has_color_preview = false;
  configured_backend = Backend::Auto;
  active_backend = Backend::Cell;
  graphics_dirty = false;
  graphics_visible = false;
  graphics_backend = Backend::Auto;
  graphics_x = graphics_y = graphics_w = graphics_h = 0;
  status_text.clear();
  image_px_w = 0;
  image_px_h = 0;
  cell_px_w = 0;
  cell_px_h = 0;
  graphics_file.clear();
  remove_graphics_file = false;
  preview_scroll = 0;
  preview_viewport_rows = 1;
}

ImageViewer::Backend ImageViewer::parse_backend(const std::string &name)
{
  std::string n = string_util::lower_copy(name);
  if (n == "kitty")
    return Backend::Kitty;
  if (n == "sixel")
    return Backend::Sixel;
  if (n == "cell" || n == "cells" || n == "ascii" || n == "ansi")
    return Backend::Cell;
  if (n == "off" || n == "none" || n == "disabled")
    return Backend::Off;
  return Backend::Auto;
}

std::string ImageViewer::backend_name(Backend backend)
{
  switch (backend)
  {
  case Backend::Auto:
    return "auto";
  case Backend::Kitty:
    return "kitty";
  case Backend::Sixel:
    return "sixel";
  case Backend::Cell:
    return "cell";
  case Backend::Off:
    return "off";
  }
  return "cell";
}

bool ImageViewer::terminal_supports_kitty()
{
  // The session-wide answer, not a guess from the environment: jot is started
  // only on a terminal that passed the protocol's own handshake (main), and a
  // unit test that never runs one reads the environment instead.
  return Terminal::kitty_graphics_available();
}

bool ImageViewer::terminal_may_support_sixel()
{
  std::string term = string_util::lower_copy(getenv_string("TERM"));
  std::string program = string_util::lower_copy(getenv_string("TERM_PROGRAM"));
  if (term.find("sixel") != std::string::npos)
    return true;
  if (program.find("wezterm") != std::string::npos)
    return true;
  if (env_present("KONSOLE_VERSION"))
    return true;
  if (term.find("mlterm") != std::string::npos)
    return true;
  if (term.find("xterm") != std::string::npos)
    return true;
  return false;
}

bool ImageViewer::terminal_likely_sixel()
{
  std::string term = string_util::lower_copy(getenv_string("TERM"));
  std::string program = string_util::lower_copy(getenv_string("TERM_PROGRAM"));
  if (term.find("sixel") != std::string::npos)
    return true;
  if (term.find("mlterm") != std::string::npos)
    return true;
  if (program.find("wezterm") != std::string::npos)
    return true;
  if (program.find("contour") != std::string::npos)
    return true;
  if (program.find("mintty") != std::string::npos)
    return true;
  if (env_present("KONSOLE_VERSION"))
    return true;
  return false;
}

bool ImageViewer::helper_available(const std::string &cmd)
{
  if (cmd.empty())
    return false;
  return shell_util::command_exists(cmd);
}

std::string ImageViewer::base64_encode(const std::string &input)
{
  static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((input.size() + 2) / 3) * 4);
  for (size_t i = 0; i < input.size(); i += 3)
  {
    unsigned int b0 = (unsigned char)input[i];
    unsigned int b1 = (i + 1 < input.size()) ? (unsigned char)input[i + 1] : 0;
    unsigned int b2 = (i + 2 < input.size()) ? (unsigned char)input[i + 2] : 0;
    out.push_back(alphabet[(b0 >> 2) & 0x3F]);
    out.push_back(alphabet[((b0 & 0x03) << 4) | ((b1 >> 4) & 0x0F)]);
    out.push_back(i + 1 < input.size() ? alphabet[((b1 & 0x0F) << 2) | ((b2 >> 6) & 0x03)] : '=');
    out.push_back(i + 2 < input.size() ? alphabet[b2 & 0x3F] : '=');
  }
  return out;
}

// One image at a time, and it has an id: deleting by id leaves every other
// image on screen untouched, where "delete all" could take out placements
// that were never ours. The value is arbitrary but distinctive, so it does
// not collide with an application that picks 1.
namespace
{
  constexpr int kKittyImageId = 1001;
}

std::string ImageViewer::cursor_move(int x, int y)
{
  return "\x1b[" + std::to_string(std::max(0, y) + 1) + ";" + std::to_string(std::max(0, x) + 1)
         + "H";
}

std::string
ImageViewer::build_kitty_file_command(const std::string &path, int x, int y, int w, int h)
{
  if (path.empty() || w <= 0 || h <= 0)
    return "";
  std::ostringstream out;
  out << cursor_move(x, y);
  out << "\x1b_Ga=T,f=100,t=f,q=2,i=" << kKittyImageId << ",c=" << std::max(1, w)
      << ",r=" << std::max(1, h) << ";"
      << base64_encode(path) << "\x1b\\";
  return out.str();
}

std::string ImageViewer::build_kitty_delete_command()
{
  return "\x1b_Ga=d,d=i,i=" + std::to_string(kKittyImageId) + ",q=2;\x1b\\";
}

std::string ImageViewer::sixel_cache_key(const std::string &path, int px_w, int px_h)
{
  // A unit separator keeps a path from ever reading as part of a size (a path
  // ending in digits must not alias a larger raster).
  return path + "\x1f" + std::to_string(px_w) + "x" + std::to_string(px_h);
}

std::string ImageViewer::build_sixel_command(const std::string &path, int px_w, int px_h)
{
  if (path.empty() || px_w <= 0 || px_h <= 0)
    return "";
  // Both sides are given, and both come from the picture's own ratio: a sixel
  // placement is a raster in the frame buffer, so the size the helper is asked
  // for *is* the picture's size. Asking for two sides that disagreed with the
  // source's shape is what used to squeeze it into a pane-shaped box.
  //
  // Only stderr is silenced: stdout is the sixel payload this call reads back
  // through the pipe. The cursor move that aims the placement at the pane is
  // added to that payload, not to the shell command.
  return "img2sixel -w " + std::to_string(std::max(1, px_w)) + " -h "
         + std::to_string(std::max(1, px_h)) + " " + shell_util::shell_quote(path)
         + shell_util::null_redirect();
}

namespace
{
  // Enough for the image being shown, a second pane's copy of it, and a resize
  // or two; each entry is a converted payload, so this stays small.
  constexpr size_t kSixelCacheMax = 4;
} // namespace

const std::string &ImageViewer::sixel_payload_for(const std::string &path, int px_w, int px_h)
{
  static const std::string empty;
  if (path.empty() || px_w <= 0 || px_h <= 0)
  {
    return empty;
  }

  const std::string key = sixel_cache_key(path, px_w, px_h);
  const auto cached = sixel_cache.find(key);
  if (cached != sixel_cache.end())
  {
    return cached->second;
  }

  std::string payload;
  FILE *pipe = shell_util::open_command_pipe(build_sixel_command(path, px_w, px_h), "r");
  if (pipe)
  {
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
    {
      payload += buffer;
    }
    if (shell_util::command_exit_code(shell_util::close_command_pipe(pipe)) != 0)
    {
      payload.clear();
    }
  }
  // A failure is not cached: the helper may be missing now and present later,
  // and "no payload" is answered by the caller's cell-preview fallback.
  if (payload.empty())
  {
    return empty;
  }

  if (sixel_cache.size() >= kSixelCacheMax)
  {
    sixel_cache.erase(sixel_cache.begin());
  }
  return sixel_cache.emplace(key, std::move(payload)).first->second;
}

void ImageViewer::configure_backend(const std::string &backend)
{
  Backend parsed = parse_backend(backend);
  if (parsed == configured_backend)
    return;
  configured_backend = parsed;
  active_backend = resolve_backend();
  graphics_dirty = true;
}

ImageViewer::Backend ImageViewer::resolve_backend() const
{
  Backend requested = configured_backend;
  if (requested == Backend::Off)
    return Backend::Off;
  if (requested == Backend::Cell)
    return Backend::Cell;
  if (requested == Backend::Kitty)
    return terminal_supports_kitty() ? Backend::Kitty : Backend::Cell;
  if (requested == Backend::Sixel)
  {
    return terminal_may_support_sixel() && helper_available("img2sixel") ? Backend::Sixel
                                                                         : Backend::Cell;
  }
  if (terminal_supports_kitty())
    return Backend::Kitty;
  // Auto asks the narrow question: a placement sent to a terminal that cannot
  // paint sixel is invisible, and believing it landed would hide the cell
  // preview behind a picture that is not there. "xterm" in TERM only means the
  // terminal speaks xterm -- an ssh session, a multiplexer and a plain xterm all
  // report it -- so it is not enough to bet the preview on.
  if (terminal_likely_sixel() && helper_available("img2sixel"))
    return Backend::Sixel;
  return Backend::Cell;
}

void ImageViewer::clear_graphics_file()
{
  if (remove_graphics_file && !graphics_file.empty())
  {
    std::error_code ec;
    fs::remove(graphics_file, ec);
  }
  graphics_file.clear();
  remove_graphics_file = false;
}

std::string ImageViewer::prepare_kitty_graphics_file()
{
  clear_graphics_file();
  if (current_image.empty())
    return "";

  std::string ext = fs::path(current_image).extension().string();
  ext = string_util::lower_copy(ext);
  if (ext == ".png")
  {
    graphics_file = current_image;
    remove_graphics_file = false;
    return graphics_file;
  }

  if (!helper_available("magick") && !helper_available("convert"))
  {
    return "";
  }

  fs::path tmp =
      fs::temp_directory_path() / ("jot-image-viewer-" + std::to_string(process_id()) + ".png");
  std::string cmd;
  if (helper_available("magick"))
  {
    cmd = "magick " + shell_util::shell_quote(current_image) + " -auto-orient " + shell_util::shell_quote(tmp.string())
          + shell_util::null_redirect();
  }
  else
  {
    cmd = "convert " + shell_util::shell_quote(current_image) + " -auto-orient " + shell_util::shell_quote(tmp.string())
          + shell_util::null_redirect();
  }
  if (std::system(cmd.c_str()) != 0 || !fs::exists(tmp))
  {
    return "";
  }

  graphics_file = tmp.string();
  remove_graphics_file = true;
  return graphics_file;
}

bool ImageViewer::is_image_file(const std::string &path)
{
  std::string ext = path;
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

  size_t dot = ext.find_last_of('.');
  if (dot == std::string::npos)
    return false;

  ext = ext.substr(dot);
  return ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".gif" || ext == ".bmp"
         || ext == ".svg" || ext == ".webp" || ext == ".ico" || ext == ".tif" || ext == ".tiff"
         || ext == ".avif" || ext == ".heic" || ext == ".ppm" || ext == ".pgm" || ext == ".pbm"
         || ext == ".xpm" || ext == ".jxl";
}

std::string ImageViewer::get_image_info(const std::string &path)
{
  if (!fs::exists(path))
    return "File not found";

  try
  {
    auto size = fs::file_size(path);
    std::string size_str;
    if (size < 1024)
    {
      size_str = std::to_string(size) + " B";
    }
    else if (size < 1024 * 1024)
    {
      size_str = std::to_string(size / 1024) + " KB";
    }
    else
    {
      size_str = std::to_string(size / (1024 * 1024)) + " MB";
    }

    std::string ext = path.substr(path.find_last_of('.'));
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    std::string dims;
    if (shell_util::command_exists("identify"))
    {
      std::string cmd = "identify -format '%wx%h' " + shell_util::shell_quote(path) + shell_util::null_redirect();
      FILE *pipe = shell_util::open_command_pipe(cmd, "r");
      if (pipe)
      {
        char buf[128] = {0};
        if (fgets(buf, sizeof(buf), pipe) != nullptr)
        {
          dims = std::string(buf);
          while (!dims.empty()
                 && (dims.back() == '\n' || dims.back() == '\r' || dims.back() == ' '))
          {
            dims.pop_back();
          }
        }
        shell_util::close_command_pipe(pipe);
      }
    }

    // The picture's own size, taken from the same identify call the info line
    // comes from: the placement box is measured from it (see natural_box), so
    // reading it here is what keeps the render path free of subprocesses.
    image_px_w = 0;
    image_px_h = 0;
    const size_t times = dims.find('x');
    if (times != std::string::npos)
    {
      image_px_w = std::atoi(dims.substr(0, times).c_str());
      image_px_h = std::atoi(dims.substr(times + 1).c_str());
    }

    if (!dims.empty())
    {
      return ext + " | " + dims + " | " + size_str;
    }
    return ext + " | " + size_str;
  }
  catch (...)
  {
    return "Unknown";
  }
}

void ImageViewer::generate_ascii_preview(const std::string &path)
{
  ascii_preview.clear();
  color_preview_bg.clear();
  has_color_preview = false;

  if (!fs::exists(path))
  {
    ascii_preview.push_back("Image not found");
    return;
  }

  std::string ext = path.substr(path.find_last_of('.'));
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

  std::string info = get_image_info(path);
  ascii_preview.push_back("IMAGE: " + fs::path(path).filename().string());
  ascii_preview.push_back(info);
  ascii_preview.push_back("");

  // The grid the art is sampled at is the picture's own shape (see
  // fit_preview_cells): a fixed block stretched every file into the same
  // 56x24 rectangle.
  int target_w = kPreviewMaxW;
  int target_h = kPreviewMaxH;
  fit_preview_cells(image_px_w, image_px_h, target_w, target_h);

  if (shell_util::command_exists("convert"))
  {
    // -thumbnail rather than -resize: it decodes the source at the size it
    // needs instead of at full resolution and scaling afterwards, which is the
    // difference between a brief pause and an apparently hung editor on a
    // large image. The time limit is the backstop -- this runs on the UI
    // thread, so a pathological file must not be able to hold it forever.
    std::string cmd = "convert -limit time 5 -limit thread 1 " + shell_util::shell_quote(path)
                      + " -auto-orient "
                        "-thumbnail "
                      + std::to_string(target_w) + "x" + std::to_string(target_h) + " txt:-"
                      + shell_util::null_redirect();
    FILE *pipe = shell_util::open_command_pipe(cmd, "r");
    if (pipe)
    {
      // -1 = draw with the default background, which is what a transparent
      // or unparsed pixel should show rather than a fixed colour.
      std::vector<std::vector<int>> colors((size_t)target_h,
                                           std::vector<int>((size_t)target_w, -1));
      bool any = false;
      char buffer[1024];
      while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
      {
        std::string line(buffer);
        if (line.empty() || line[0] == '#')
          continue;

        int px = -1;
        int py = -1;
        if (sscanf(line.c_str(), "%d,%d:", &px, &py) != 2)
          continue;
        if (px < 0 || px >= target_w || py < 0 || py >= target_h)
          continue;

        int r = 0, g = 0, b = 0;
        if (!parse_rgb_triplet(line, r, g, b))
          continue;
        colors[(size_t)py][(size_t)px] = rgb_to_xterm256(r, g, b);
        any = true;
      }
      shell_util::close_command_pipe(pipe);
      if (any)
      {
        color_preview_bg = std::move(colors);
        has_color_preview = true;
        ascii_preview.push_back("Preview: 256-color");
        return;
      }
    }
  }

  // Best quality terminal preview path.
  if (shell_util::command_exists("chafa"))
  {
    std::string cmd = "chafa --format=symbols --colors=none --size=" + std::to_string(target_w)
                      + "x" + std::to_string(target_h) + " " + shell_util::shell_quote(path) + shell_util::null_redirect();
    FILE *pipe = shell_util::open_command_pipe(cmd, "r");
    if (pipe)
    {
      std::vector<std::string> rows;
      char buffer[2048];
      while (fgets(buffer, sizeof(buffer), pipe) != nullptr && (int)rows.size() < target_h)
      {
        std::string row = strip_ansi(std::string(buffer));
        while (!row.empty() && (row.back() == '\n' || row.back() == '\r'))
        {
          row.pop_back();
        }
        if (!row.empty())
        {
          rows.push_back(row);
        }
      }
      shell_util::close_command_pipe(pipe);
      if (!rows.empty())
      {
        ascii_preview.push_back("Preview:");
        for (const auto &r : rows)
        {
          ascii_preview.push_back(r);
        }
        return;
      }
    }
  }

  bool rendered_ascii = false;
  if (shell_util::command_exists("convert"))
  {
    std::string cmd = "convert " + shell_util::shell_quote(path)
                      + " -auto-orient "
                        "-resize "
                      + std::to_string(target_w) + "x" + std::to_string(target_h)
                      + " -colorspace Gray -contrast-stretch 1%x10% txt:-" + shell_util::null_redirect();
    FILE *pipe = shell_util::open_command_pipe(cmd, "r");
    if (pipe)
    {
      std::vector<std::string> rows(target_h, std::string((size_t)target_w, ' '));
      char buffer[512];
      while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
      {
        std::string line(buffer);
        if (line.empty() || line[0] == '#')
          continue;

        int px = -1;
        int py = -1;
        if (sscanf(line.c_str(), "%d,%d:", &px, &py) != 2)
          continue;
        if (px < 0 || px >= target_w || py < 0 || py >= target_h)
          continue;

        int gray = -1;
        size_t gpos = line.find("gray(");
        if (gpos != std::string::npos)
        {
          int gv = -1;
          if (sscanf(line.c_str() + gpos, "gray(%d)", &gv) == 1)
          {
            gray = gv;
          }
        }
        else
        {
          size_t rpos = line.find("rgb(");
          if (rpos != std::string::npos)
          {
            int r = 0, g = 0, b = 0;
            if (sscanf(line.c_str() + rpos, "rgb(%d,%d,%d)", &r, &g, &b) == 3)
            {
              gray = (r + g + b) / 3;
            }
          }
        }

        if (gray < 0)
          continue;
        static const std::string ramp =
            " .'`^\",:;Il!i~+_-?][}{1)(|\\/*tfjrxnuvczXYUJCLQ0OZmwqpdbkhao*#MW&8%B@$";
        rows[py][px] = ramp[(size_t)grayscale_to_char_index(gray)];
      }
      shell_util::close_command_pipe(pipe);

      ascii_preview.push_back("Preview:");
      for (const auto &row : rows)
      {
        ascii_preview.push_back(row);
      }
      rendered_ascii = true;
    }
  }

  if (!rendered_ascii)
  {
    ascii_preview.push_back("Preview unavailable (ImageMagick 'convert' missing)");
    ascii_preview.push_back("");
    ascii_preview.push_back("You can still open with external viewer:");
    ascii_preview.push_back("  xdg-open " + path);
  }
}

void ImageViewer::store_preview_cache(const std::string &path)
{
  // Bounded, and evicted in insertion order: the last few pictures visited
  // stay instant to return to, and a long session cannot grow this without
  // end.
  if (preview_cache.size() >= 8)
  {
    preview_cache.erase(preview_cache.begin());
  }
  PreviewCacheEntry entry;
  entry.status_text = status_text;
  entry.ascii_preview = ascii_preview;
  entry.color_preview_bg = color_preview_bg;
  entry.has_color_preview = has_color_preview;
  entry.backend = active_backend;
  preview_cache[path] = std::move(entry);
}

void ImageViewer::open(const std::string &path)
{
  if (!is_image_file(path))
    return;
  if (!fs::exists(path))
    return;

  current_image = path;
  is_open = true;
  preview_scroll = 0;
  clear_graphics_file();

  const auto cached = preview_cache.find(path);
  if (cached != preview_cache.end())
  {
    status_text = cached->second.status_text;
    ascii_preview = cached->second.ascii_preview;
    color_preview_bg = cached->second.color_preview_bg;
    has_color_preview = cached->second.has_color_preview;
    active_backend = cached->second.backend;
  }
  else
  {
    active_backend = resolve_backend();
    status_text = get_image_info(path);
    generate_ascii_preview(path);
    store_preview_cache(path);
  }
  graphics_dirty = true;
}

void ImageViewer::close()
{
  if (graphics_visible)
  {
    graphics_dirty = true;
  }
  else
  {
    clear_graphics_file();
  }
  is_open = false;
  ascii_preview.clear();
  color_preview_bg.clear();
  has_color_preview = false;
  status_text.clear();
  preview_scroll = 0;
}

void ImageViewer::set_cell_pixel_size(int w, int h)
{
  cell_px_w = std::max(0, w);
  cell_px_h = std::max(0, h);
}

// One image pixel per screen pixel, through the terminal's cell size, is what
// "its own resolution" means here: a 300x200 picture in a cell of 9x18 asks
// for 33x11 cells, and a 6000x4000 photograph asks for more cells than any
// pane has, so it is scaled down -- once, with the aspect ratio deciding both
// sides -- to the pane's own box. A smaller picture is never enlarged: a 4x2
// cell picture stays 4x2, which is how a thumbnail is supposed to look.
void ImageViewer::fit_image_to_cells(int px_w, int px_h, int cell_px_w, int cell_px_h,
                                     int max_w, int max_h, int &out_w, int &out_h)
{
  out_w = std::max(1, max_w);
  out_h = std::max(1, max_h);
  if (px_w <= 0 || px_h <= 0)
  {
    return;
  }
  const int cell_w = cell_px_w > 0 ? cell_px_w : kAssumedCellPxW;
  const int cell_h = cell_px_h > 0 ? cell_px_h : kAssumedCellPxH;

  // Rounded to the nearest cell: half a cell of rounding is not a rescale, and
  // flooring would show every picture a little smaller than it is.
  const int natural_w = std::max(1, (px_w + cell_w / 2) / cell_w);
  const int natural_h = std::max(1, (px_h + cell_h / 2) / cell_h);
  if (natural_w <= out_w && natural_h <= out_h)
  {
    out_w = natural_w;
    out_h = natural_h;
    return;
  }

  // Too big for the pane: shrink as far as the pane asks and no further, so
  // whichever side runs out of room first is the side that decides the scale.
  const double scale = std::min((double)out_w / (double)natural_w,
                                (double)out_h / (double)natural_h);
  out_w = std::clamp((int)(natural_w * scale), 1, std::max(1, max_w));
  out_h = std::clamp((int)(natural_h * scale), 1, std::max(1, max_h));
}

void ImageViewer::natural_box(int max_w, int max_h, int &out_w, int &out_h) const
{
  fit_image_to_cells(image_px_w, image_px_h, cell_px_w, cell_px_h, max_w, max_h, out_w, out_h);
}

void ImageViewer::render(int x, int y, int w, int h, int border_fg, int border_bg)
{
  if (!is_open)
    return;

  view_x = x;
  view_y = y;
  view_w = w;
  view_h = h;
  this->border_fg = border_fg;
  this->border_bg = border_bg;
  // The preview inside this panel starts one row in (the status caption rides
  // on the top border) and stops one row short of the bottom border.
  preview_viewport_rows = std::max(1, h - 2);

  int next_x = x + 1;
  int next_y = y + 2;
  int next_w = std::max(1, w - 2);
  int next_h = std::max(1, h - 3);
  if (graphics_x != next_x || graphics_y != next_y || graphics_w != next_w || graphics_h != next_h)
  {
    graphics_dirty = true;
    graphics_x = next_x;
    graphics_y = next_y;
    graphics_w = next_w;
    graphics_h = next_h;
  }
  active_backend = resolve_backend();
  // A sixel placement is painted into the cells rather than held by the terminal
  // as an overlay the way a kitty placement is, so repainting the pane erases it:
  // it has to go out again on every draw of the image.
  if (active_backend == Backend::Sixel)
  {
    graphics_dirty = true;
  }
}

int ImageViewer::preview_content_rows() const
{
  int rows = (int)ascii_preview.size();
  if (has_color_preview)
  {
    // A blank row separates the caption and ASCII art from the color block.
    rows += 1 + (int)color_preview_bg.size();
  }
  return rows;
}

void ImageViewer::set_preview_scroll(int row)
{
  const int max_scroll =
      std::max(0, preview_content_rows() - std::max(1, preview_viewport_rows));
  preview_scroll = std::clamp(row, 0, max_scroll);
}

void ImageViewer::scroll_preview(int delta)
{
  set_preview_scroll(preview_scroll + delta);
}

std::string ImageViewer::take_graphics_output()
{
  if (!graphics_dirty && !(is_open && uses_real_graphics() && !graphics_visible))
  {
    return "";
  }

  std::string out;
  const bool delete_existing =
      graphics_visible && (!is_open || graphics_dirty || !uses_real_graphics());
  if (delete_existing)
  {
    // Only a kitty placement has a delete command, and only a terminal that
    // understands it should get one: a sixel picture is erased by the cell
    // repaint that is already happening.
    if (graphics_backend == Backend::Kitty && terminal_supports_kitty())
    {
      out += build_kitty_delete_command();
    }
    graphics_visible = false;
  }

  graphics_dirty = false;
  if (!is_open || current_image.empty())
  {
    current_image.clear();
    clear_graphics_file();
    return out;
  }

  active_backend = resolve_backend();
  if (active_backend == Backend::Off)
  {
    status_text = "Image viewer disabled";
    return out;
  }
  if (active_backend == Backend::Kitty)
  {
    std::string file = prepare_kitty_graphics_file();
    if (file.empty())
    {
      active_backend = Backend::Cell;
      status_text = "Image conversion unavailable; using cell preview";
      return out;
    }
    // c/r are the box the cat is placed in, and the box is the picture's own
    // resolution: kitty scales the image into it and, with both sides given,
    // letterboxes rather than distorts, so the placement keeps the file's
    // shape at the size it asked for.
    out += build_kitty_file_command(file, graphics_x, graphics_y, graphics_w, graphics_h);
    graphics_visible = true;
    graphics_backend = Backend::Kitty;
    // No status line of its own: the panel's caption is the picture's own info
    // line (see open), which the cell preview shows the same way.
    return out;
  }
  if (active_backend == Backend::Sixel)
  {
    // Sent on every draw, so the conversion is cached per image and raster size
    // rather than re-run for each redraw. The raster is the placement box in
    // pixels -- the cell size, real or assumed -- so the helper is asked for
    // the size the picture is actually shown at.
    const int cell_w = cell_px_w > 0 ? cell_px_w : kAssumedCellPxW;
    const int cell_h = cell_px_h > 0 ? cell_px_h : kAssumedCellPxH;
    const std::string &payload =
        sixel_payload_for(current_image, graphics_w * cell_w, graphics_h * cell_h);
    if (!payload.empty())
    {
      // Aim the placement at the panel first: the frame writes its graphics
      // after the cells, and by then the terminal cursor is wherever the last
      // painted row left it.
      out += cursor_move(graphics_x, graphics_y);
      out += payload;
      graphics_visible = true;
      graphics_backend = Backend::Sixel;
      return out;
    }
    active_backend = Backend::Cell;
    status_text = "Sixel unavailable; using cell preview";
    return out;
  }

  // Cached: get_image_info runs identify, and this is reached while building
  // the frame, so asking every time froze the editor on an image tab.
  if (cached_info_for != current_image)
  {
    cached_info_for = current_image;
    cached_info = get_image_info(current_image);
  }
  status_text = cached_info;
  return out;
}

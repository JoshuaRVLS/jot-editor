#include "imageviewer.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {
std::string lower_copy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return value;
}

std::string shell_quote(const std::string &s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'')
      out += "'\\''";
    else
      out += c;
  }
  out += "'";
  return out;
}

bool command_exists(const char *cmd) {
  std::string check = std::string("command -v ") + cmd + " >/dev/null 2>&1";
  return std::system(check.c_str()) == 0;
}

bool env_present(const char *name) {
  const char *value = std::getenv(name);
  return value && value[0] != '\0';
}

std::string getenv_string(const char *name) {
  const char *value = std::getenv(name);
  return value ? std::string(value) : std::string();
}

int grayscale_to_char_index(int gray) {
  static const std::string ramp =
      " .'`^\",:;Il!i~+_-?][}{1)(|\\/*tfjrxnuvczXYUJCLQ0OZmwqpdbkhao*#MW&8%B@$";
  int g = std::clamp(gray, 0, 255);
  int idx = (g * (int)(ramp.size() - 1)) / 255;
  return std::clamp(idx, 0, (int)ramp.size() - 1);
}

int rgb_to_xterm256(int r, int g, int b) {
  r = std::clamp(r, 0, 255);
  g = std::clamp(g, 0, 255);
  b = std::clamp(b, 0, 255);

  if (std::abs(r - g) < 8 && std::abs(g - b) < 8) {
    int gray = (r + g + b) / 3;
    int idx = 232 + (gray * 23 + 127) / 255;
    return std::clamp(idx, 232, 255);
  }

  int rr = (r * 5 + 127) / 255;
  int gg = (g * 5 + 127) / 255;
  int bb = (b * 5 + 127) / 255;
  return 16 + (36 * rr) + (6 * gg) + bb;
}

bool parse_rgb_triplet(const std::string &line, int &r, int &g, int &b) {
  auto parse_component = [](const std::string &token) -> int {
    std::string t;
    for (char c : token) {
      if (!std::isspace((unsigned char)c)) {
        t.push_back(c);
      }
    }
    if (t.empty()) {
      return -1;
    }
    bool percent = !t.empty() && t.back() == '%';
    if (percent) {
      t.pop_back();
    }
    if (t.empty()) {
      return -1;
    }
    char *end = nullptr;
    double v = std::strtod(t.c_str(), &end);
    if (!end || *end != '\0') {
      return -1;
    }
    if (percent) {
      v = (v * 255.0) / 100.0;
    }
    return std::clamp((int)(v + 0.5), 0, 255);
  };

  size_t p = line.find('(');
  size_t q = (p == std::string::npos) ? std::string::npos : line.find(')', p + 1);
  if (p != std::string::npos && q != std::string::npos && q > p + 1) {
    std::string inner = line.substr(p + 1, q - p - 1);
    std::vector<std::string> parts;
    std::string cur;
    for (char c : inner) {
      if (c == ',') {
        parts.push_back(cur);
        cur.clear();
      } else {
        cur.push_back(c);
      }
    }
    if (!cur.empty()) {
      parts.push_back(cur);
    }

    if (parts.size() >= 3) {
      int rr = parse_component(parts[0]);
      int gg = parse_component(parts[1]);
      int bb = parse_component(parts[2]);
      if (rr >= 0 && gg >= 0 && bb >= 0) {
        r = rr;
        g = gg;
        b = bb;
        return true;
      }
    }
  }

  size_t h = line.find('#');
  if (h != std::string::npos && h + 6 < line.size()) {
    char hex[7] = {0};
    bool ok = true;
    for (int i = 0; i < 6; i++) {
      char c = line[h + 1 + i];
      if (!std::isxdigit((unsigned char)c)) {
        ok = false;
        break;
      }
      hex[i] = c;
    }
    if (ok) {
      unsigned int v = 0;
      if (sscanf(hex, "%x", &v) == 1) {
        r = (int)((v >> 16) & 0xFF);
        g = (int)((v >> 8) & 0xFF);
        b = (int)(v & 0xFF);
        return true;
      }
    }
  }
  return false;
}

std::string strip_ansi(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\x1b' && i + 1 < s.size() && s[i + 1] == '[') {
      i += 2;
      while (i < s.size()) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 0x40 && c <= 0x7E) {
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

ImageViewer::ImageViewer() {
  is_open = false;
  view_x = view_y = view_w = view_h = 0;
  border_fg = 7;
  border_bg = 0;
  has_color_preview = false;
  configured_backend = Backend::Auto;
  active_backend = Backend::Cell;
  graphics_dirty = false;
  graphics_visible = false;
  graphics_x = graphics_y = graphics_w = graphics_h = 0;
  status_text.clear();
  graphics_file.clear();
  remove_graphics_file = false;
}

ImageViewer::Backend ImageViewer::parse_backend(const std::string &name) {
  std::string n = lower_copy(name);
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

std::string ImageViewer::backend_name(Backend backend) {
  switch (backend) {
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

bool ImageViewer::terminal_supports_kitty() {
  if (env_present("KITTY_WINDOW_ID"))
    return true;
  std::string term = lower_copy(getenv_string("TERM"));
  return term.find("kitty") != std::string::npos;
}

bool ImageViewer::terminal_may_support_sixel() {
  std::string term = lower_copy(getenv_string("TERM"));
  std::string program = lower_copy(getenv_string("TERM_PROGRAM"));
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

bool ImageViewer::helper_available(const std::string &cmd) {
  if (cmd.empty())
    return false;
  return command_exists(cmd.c_str());
}

std::string ImageViewer::base64_encode(const std::string &input) {
  static const char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((input.size() + 2) / 3) * 4);
  for (size_t i = 0; i < input.size(); i += 3) {
    unsigned int b0 = (unsigned char)input[i];
    unsigned int b1 = (i + 1 < input.size()) ? (unsigned char)input[i + 1] : 0;
    unsigned int b2 = (i + 2 < input.size()) ? (unsigned char)input[i + 2] : 0;
    out.push_back(alphabet[(b0 >> 2) & 0x3F]);
    out.push_back(alphabet[((b0 & 0x03) << 4) | ((b1 >> 4) & 0x0F)]);
    out.push_back(i + 1 < input.size()
                      ? alphabet[((b1 & 0x0F) << 2) | ((b2 >> 6) & 0x03)]
                      : '=');
    out.push_back(i + 2 < input.size() ? alphabet[b2 & 0x3F] : '=');
  }
  return out;
}

std::string ImageViewer::build_kitty_file_command(const std::string &path,
                                                  int x, int y, int w, int h) {
  if (path.empty() || w <= 0 || h <= 0)
    return "";
  std::ostringstream out;
  out << "\x1b[" << y + 1 << ";" << x + 1 << "H";
  out << "\x1b_Ga=T,f=100,t=f,q=2,c=" << std::max(1, w)
      << ",r=" << std::max(1, h) << ";"
      << base64_encode(path) << "\x1b\\";
  return out.str();
}

std::string ImageViewer::build_kitty_delete_command() {
  return "\x1b_Ga=d,d=A,q=2;\x1b\\";
}

std::string ImageViewer::build_sixel_command(const std::string &path, int w,
                                             int h) {
  if (path.empty() || w <= 0 || h <= 0)
    return "";
  int px_w = std::max(1, w * 8);
  int px_h = std::max(1, h * 16);
  return "img2sixel -w " + std::to_string(px_w) + " -h " +
         std::to_string(px_h) + " " + shell_quote(path) + " 2>/dev/null";
}

void ImageViewer::configure_backend(const std::string &backend) {
  Backend parsed = parse_backend(backend);
  if (parsed == configured_backend)
    return;
  configured_backend = parsed;
  active_backend = resolve_backend();
  graphics_dirty = true;
}

ImageViewer::Backend ImageViewer::resolve_backend() const {
  Backend requested = configured_backend;
  if (requested == Backend::Off)
    return Backend::Off;
  if (requested == Backend::Cell)
    return Backend::Cell;
  if (requested == Backend::Kitty)
    return terminal_supports_kitty() ? Backend::Kitty : Backend::Cell;
  if (requested == Backend::Sixel) {
    return terminal_may_support_sixel() && helper_available("img2sixel")
               ? Backend::Sixel
               : Backend::Cell;
  }
  if (terminal_supports_kitty())
    return Backend::Kitty;
  if (terminal_may_support_sixel() && helper_available("img2sixel"))
    return Backend::Sixel;
  return Backend::Cell;
}

void ImageViewer::clear_graphics_file() {
  if (remove_graphics_file && !graphics_file.empty()) {
    std::error_code ec;
    fs::remove(graphics_file, ec);
  }
  graphics_file.clear();
  remove_graphics_file = false;
}

std::string ImageViewer::prepare_kitty_graphics_file() {
  clear_graphics_file();
  if (current_image.empty())
    return "";

  std::string ext = fs::path(current_image).extension().string();
  ext = lower_copy(ext);
  if (ext == ".png") {
    graphics_file = current_image;
    remove_graphics_file = false;
    return graphics_file;
  }

  if (!helper_available("magick") && !helper_available("convert")) {
    return "";
  }

  fs::path tmp = fs::temp_directory_path() /
                 ("jot-image-viewer-" + std::to_string((long long)getpid()) +
                  ".png");
  std::string cmd;
  if (helper_available("magick")) {
    cmd = "magick " + shell_quote(current_image) +
          " -auto-orient " + shell_quote(tmp.string()) + " 2>/dev/null";
  } else {
    cmd = "convert " + shell_quote(current_image) +
          " -auto-orient " + shell_quote(tmp.string()) + " 2>/dev/null";
  }
  if (std::system(cmd.c_str()) != 0 || !fs::exists(tmp)) {
    return "";
  }

  graphics_file = tmp.string();
  remove_graphics_file = true;
  return graphics_file;
}

bool ImageViewer::is_image_file(const std::string &path) {
  std::string ext = path;
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

  size_t dot = ext.find_last_of('.');
  if (dot == std::string::npos)
    return false;

  ext = ext.substr(dot);
  return ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".gif" ||
         ext == ".bmp" || ext == ".svg" || ext == ".webp" || ext == ".ico" ||
         ext == ".tif" || ext == ".tiff" || ext == ".avif" || ext == ".heic" ||
         ext == ".ppm" || ext == ".pgm" || ext == ".pbm" || ext == ".xpm" ||
         ext == ".jxl";
}

std::string ImageViewer::get_image_info(const std::string &path) {
  if (!fs::exists(path))
    return "File not found";

  try {
    auto size = fs::file_size(path);
    std::string size_str;
    if (size < 1024) {
      size_str = std::to_string(size) + " B";
    } else if (size < 1024 * 1024) {
      size_str = std::to_string(size / 1024) + " KB";
    } else {
      size_str = std::to_string(size / (1024 * 1024)) + " MB";
    }

    std::string ext = path.substr(path.find_last_of('.'));
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    std::string dims;
    if (command_exists("identify")) {
      std::string cmd = "identify -format '%wx%h' " + shell_quote(path) +
                        " 2>/dev/null";
      FILE *pipe = popen(cmd.c_str(), "r");
      if (pipe) {
        char buf[128] = {0};
        if (fgets(buf, sizeof(buf), pipe) != nullptr) {
          dims = std::string(buf);
          while (!dims.empty() &&
                 (dims.back() == '\n' || dims.back() == '\r' || dims.back() == ' ')) {
            dims.pop_back();
          }
        }
        pclose(pipe);
      }
    }

    if (!dims.empty()) {
      return ext + " | " + dims + " | " + size_str;
    }
    return ext + " | " + size_str;
  } catch (...) {
    return "Unknown";
  }
}

void ImageViewer::generate_ascii_preview(const std::string &path) {
  ascii_preview.clear();
  color_preview_bg.clear();
  has_color_preview = false;

  if (!fs::exists(path)) {
    ascii_preview.push_back("Image not found");
    return;
  }

  std::string ext = path.substr(path.find_last_of('.'));
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

  std::string info = get_image_info(path);
  ascii_preview.push_back("IMAGE: " + fs::path(path).filename().string());
  ascii_preview.push_back(info);
  ascii_preview.push_back("");

  if (command_exists("convert")) {
    const int target_w = 56;
    const int target_h = 24;
    std::string cmd = "convert " + shell_quote(path) + " -auto-orient "
                      "-resize " + std::to_string(target_w) + "x" +
                      std::to_string(target_h) + "\\! txt:- 2>/dev/null";
    FILE *pipe = popen(cmd.c_str(), "r");
    if (pipe) {
      std::vector<std::vector<int>> colors(
          (size_t)target_h, std::vector<int>((size_t)target_w, 16));
      bool any = false;
      char buffer[1024];
      while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
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
      pclose(pipe);
      if (any) {
        color_preview_bg = std::move(colors);
        has_color_preview = true;
        ascii_preview.push_back("Preview: 256-color");
        return;
      }
    }
  }

  // Best quality terminal preview path.
  if (command_exists("chafa")) {
    const int target_w = 56;
    const int target_h = 24;
    std::string cmd = "chafa --format=symbols --colors=none --size=" +
                      std::to_string(target_w) + "x" + std::to_string(target_h) +
                      " " + shell_quote(path) + " 2>/dev/null";
    FILE *pipe = popen(cmd.c_str(), "r");
    if (pipe) {
      std::vector<std::string> rows;
      char buffer[2048];
      while (fgets(buffer, sizeof(buffer), pipe) != nullptr &&
             (int)rows.size() < target_h) {
        std::string row = strip_ansi(std::string(buffer));
        while (!row.empty() &&
               (row.back() == '\n' || row.back() == '\r')) {
          row.pop_back();
        }
        if (!row.empty()) {
          rows.push_back(row);
        }
      }
      pclose(pipe);
      if (!rows.empty()) {
        ascii_preview.push_back("Preview:");
        for (const auto &r : rows) {
          ascii_preview.push_back(r);
        }
        return;
      }
    }
  }

  bool rendered_ascii = false;
  if (command_exists("convert")) {
    const int target_w = 56;
    const int target_h = 24;
    std::string cmd = "convert " + shell_quote(path) + " -auto-orient "
                      "-resize " + std::to_string(target_w) + "x" +
                      std::to_string(target_h) +
                      "\\! -colorspace Gray -contrast-stretch 1%x10% txt:- 2>/dev/null";
    FILE *pipe = popen(cmd.c_str(), "r");
    if (pipe) {
      std::vector<std::string> rows(
          target_h, std::string((size_t)target_w, ' '));
      char buffer[512];
      while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
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
        if (gpos != std::string::npos) {
          int gv = -1;
          if (sscanf(line.c_str() + gpos, "gray(%d)", &gv) == 1) {
            gray = gv;
          }
        } else {
          size_t rpos = line.find("rgb(");
          if (rpos != std::string::npos) {
            int r = 0, g = 0, b = 0;
            if (sscanf(line.c_str() + rpos, "rgb(%d,%d,%d)", &r, &g, &b) == 3) {
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
      pclose(pipe);

      ascii_preview.push_back("Preview:");
      for (const auto &row : rows) {
        ascii_preview.push_back(row);
      }
      rendered_ascii = true;
    }
  }

  if (!rendered_ascii) {
    ascii_preview.push_back("Preview unavailable (ImageMagick 'convert' missing)");
    ascii_preview.push_back("");
    ascii_preview.push_back("You can still open with external viewer:");
    ascii_preview.push_back("  xdg-open " + path);
  }
}

void ImageViewer::open(const std::string &path) {
  if (!is_image_file(path))
    return;
  if (!fs::exists(path))
    return;

  current_image = path;
  is_open = true;
  clear_graphics_file();
  active_backend = resolve_backend();
  status_text = get_image_info(path);
  generate_ascii_preview(path);
  graphics_dirty = true;
}

void ImageViewer::close() {
  if (graphics_visible) {
    graphics_dirty = true;
  } else {
    clear_graphics_file();
  }
  is_open = false;
  ascii_preview.clear();
  color_preview_bg.clear();
  has_color_preview = false;
  status_text.clear();
}

void ImageViewer::render(int x, int y, int w, int h, int border_fg,
                         int border_bg) {
  if (!is_open)
    return;

  view_x = x;
  view_y = y;
  view_w = w;
  view_h = h;
  this->border_fg = border_fg;
  this->border_bg = border_bg;

  int next_x = x + 1;
  int next_y = y + 2;
  int next_w = std::max(1, w - 2);
  int next_h = std::max(1, h - 3);
  if (graphics_x != next_x || graphics_y != next_y || graphics_w != next_w ||
      graphics_h != next_h) {
    graphics_dirty = true;
    graphics_x = next_x;
    graphics_y = next_y;
    graphics_w = next_w;
    graphics_h = next_h;
  }
  active_backend = resolve_backend();
}

std::string ImageViewer::take_graphics_output() {
  if (!graphics_dirty && !(is_open && uses_real_graphics() && !graphics_visible)) {
    return "";
  }

  std::string out;
  const bool delete_existing =
      graphics_visible &&
      (!is_open || graphics_dirty || !uses_real_graphics());
  if (delete_existing) {
    if (terminal_supports_kitty()) {
      out += build_kitty_delete_command();
    }
    graphics_visible = false;
  }

  graphics_dirty = false;
  if (!is_open || current_image.empty()) {
    current_image.clear();
    clear_graphics_file();
    return out;
  }

  active_backend = resolve_backend();
  if (active_backend == Backend::Off) {
    status_text = "Image viewer disabled";
    return out;
  }
  if (active_backend == Backend::Kitty) {
    std::string file = prepare_kitty_graphics_file();
    if (file.empty()) {
      active_backend = Backend::Cell;
      status_text = "Image conversion unavailable; using cell preview";
      return out;
    }
    out += build_kitty_file_command(file, graphics_x, graphics_y,
                                    graphics_w, graphics_h);
    graphics_visible = true;
    status_text = "Real image: kitty";
    return out;
  }
  if (active_backend == Backend::Sixel) {
    std::string cmd = build_sixel_command(current_image, graphics_w, graphics_h);
    FILE *pipe = popen(cmd.c_str(), "r");
    if (pipe) {
      char buffer[4096];
      while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        out += buffer;
      }
      int rc = pclose(pipe);
      if (rc == 0 && !out.empty()) {
        graphics_visible = true;
        status_text = "Real image: sixel";
        return out;
      }
    }
    active_backend = Backend::Cell;
    status_text = "Sixel unavailable; using cell preview";
    return out;
  }

  status_text = get_image_info(current_image);
  return out;
}

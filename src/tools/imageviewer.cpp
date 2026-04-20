#include "imageviewer.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace {
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
  generate_ascii_preview(path);
}

void ImageViewer::close() {
  is_open = false;
  current_image.clear();
  ascii_preview.clear();
  color_preview_bg.clear();
  has_color_preview = false;
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
}

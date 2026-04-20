#ifndef IMAGEVIEWER_H
#define IMAGEVIEWER_H

#include <string>
#include <vector>

// enum {
//     COLOR_IMAGE_BORDER = 10
// };

struct ImageInfo {
  std::string path;
  int width;
  int height;
  std::string format;
};

class ImageViewer {
private:
  std::string current_image;
  bool is_open;
  int view_x, view_y, view_w, view_h;
  int border_fg, border_bg;
  std::vector<std::string> ascii_preview;
  std::vector<std::vector<int>> color_preview_bg;
  bool has_color_preview;

  bool is_image_file(const std::string &path);
  void generate_ascii_preview(const std::string &path);
  std::string get_image_info(const std::string &path);

public:
  ImageViewer();
  void open(const std::string &path);
  void close();
  void render(int x, int y, int w, int h, int border_fg, int border_bg);
  bool is_active() const { return is_open; }
  std::string get_current() const { return current_image; }
  int get_view_x() const { return view_x; }
  int get_view_y() const { return view_y; }
  int get_view_w() const { return view_w; }
  int get_view_h() const { return view_h; }
  int get_border_fg() const { return border_fg; }
  int get_border_bg() const { return border_bg; }
  const std::vector<std::string> &get_preview_lines() const { return ascii_preview; }
  bool has_color_preview_data() const { return has_color_preview; }
  const std::vector<std::vector<int>> &get_color_preview_bg() const {
    return color_preview_bg;
  }
};

#endif

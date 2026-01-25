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
  std::vector<std::string> ascii_preview;

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
};

#endif

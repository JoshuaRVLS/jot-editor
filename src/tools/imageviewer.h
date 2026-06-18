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
public:
  enum class Backend {
    Auto,
    Kitty,
    Sixel,
    Cell,
    Off,
  };

private:
  std::string current_image;
  bool is_open;
  int view_x, view_y, view_w, view_h;
  int border_fg, border_bg;
  std::vector<std::string> ascii_preview;
  std::vector<std::vector<int>> color_preview_bg;
  bool has_color_preview;
  Backend configured_backend;
  Backend active_backend;
  bool graphics_dirty;
  bool graphics_visible;
  int graphics_x, graphics_y, graphics_w, graphics_h;
  std::string status_text;
  std::string graphics_file;
  bool remove_graphics_file;

  bool is_image_file(const std::string &path);
  void generate_ascii_preview(const std::string &path);
  std::string get_image_info(const std::string &path);
  Backend resolve_backend() const;
  void clear_graphics_file();
  std::string prepare_kitty_graphics_file();

public:
  ImageViewer();
  static Backend parse_backend(const std::string &name);
  static std::string backend_name(Backend backend);
  static bool terminal_supports_kitty();
  static bool terminal_may_support_sixel();
  static bool helper_available(const std::string &cmd);
  static std::string base64_encode(const std::string &input);
  static std::string build_kitty_file_command(const std::string &path, int x,
                                              int y, int w, int h);
  static std::string build_kitty_delete_command();
  static std::string build_sixel_command(const std::string &path, int w, int h);

  void configure_backend(const std::string &backend);
  void open(const std::string &path);
  void close();
  void render(int x, int y, int w, int h, int border_fg, int border_bg);
  std::string take_graphics_output();
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
  Backend get_active_backend() const { return active_backend; }
  std::string get_status_text() const { return status_text; }
  bool uses_real_graphics() const {
    return active_backend == Backend::Kitty || active_backend == Backend::Sixel;
  }
  bool has_pending_graphics_output() const { return graphics_dirty; }
};

#endif

#ifndef TERMINAL_H
#define TERMINAL_H

#include <functional>
#include <string>
#include <vector>

enum EventType { EVENT_KEY, EVENT_MOUSE, EVENT_RESIZE, EVENT_REDRAW };

struct KeyEvent {
  int key;
  bool ctrl;
  bool shift;
  bool alt;
};

struct MouseEvent {
  int x, y;
  int button;
  bool pressed;
  bool released;
};

struct ResizeEvent {
  int width, height;
};

struct Event {
  EventType type;
  union {
    KeyEvent key;
    MouseEvent mouse;
    ResizeEvent resize;
  };
};

class Terminal {
private:
  int width, height;
  bool raw_mode;
  std::string buffer;
  std::string mouse_event_buffer;

  void enable_raw_mode();
  void disable_raw_mode();
  void setup_terminal();
  void restore_terminal();
  int read_key();
  void parse_mouse_event(int ch, MouseEvent &event);

public:
  Terminal();
  ~Terminal();

  void init();
  void cleanup();

  int get_width() const { return width; }
  int get_height() const { return height; }

  Event poll_event();
  void flush();

  void clear();
  void move_cursor(int x, int y);
  void hide_cursor();
  void show_cursor();
  void set_color(int fg, int bg);
  void reset_color();
  void set_bold(bool on);
  void set_reverse(bool on);

  void write(const std::string &str);
  void write_char(char c);

  void enable_mouse();
  void disable_mouse();

  void save_cursor();
  void restore_cursor();
  void clear_line();
  void clear_to_end();
};

#endif

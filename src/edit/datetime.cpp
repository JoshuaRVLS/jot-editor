#include "editor.h"
#include <ctime>

void Editor::insert_current_datetime() {
  std::time_t now = std::time(nullptr);
  std::tm tm_now{};
#if defined(_WIN32)
  localtime_s(&tm_now, &now);
#else
  tm_now = *std::localtime(&now);
#endif

  char buf[64] = {0};
  if (std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_now) == 0) {
    set_message("Failed to format datetime");
    return;
  }
  insert_string(std::string(buf));
  needs_redraw = true;
  set_message("Inserted current datetime");
}

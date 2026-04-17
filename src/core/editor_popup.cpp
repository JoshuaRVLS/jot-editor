#include "editor.h"
#include "python_api.h"
#include <sstream>

void Editor::show_popup(const std::string &text, int x, int y) {
  popup.text = text;
  popup.x = x;
  popup.y = y;

  int max_w = 0;
  int h = 0;
  std::stringstream ss(text);
  std::string line;
  while (std::getline(ss, line)) {
    if ((int)line.length() > max_w)
      max_w = line.length();
    h++;
  }
  popup.w = max_w + 2;
  popup.h = h + 2;
  popup.visible = true;
  needs_redraw = true;
}

void Editor::hide_popup() {
  popup.visible = false;
  needs_redraw = true;
}

void Editor::run_python_script(const std::string &script) {
  if (python_api) {
    python_api->execute_code("import sys, os; sys.path.append(os.getcwd())");
    python_api->execute_code(script);
  }
}

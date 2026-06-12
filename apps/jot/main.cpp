#include "jot/editor.hpp"
#include <iostream>

int main(int argc, char *argv[]) {
  Editor editor;
  if (argc > 1) {
    editor.set_home_menu_visible(false);
  } else {
    editor.resume_last_workspace_session();
  }

  if (argc > 1) {
    if (std::filesystem::is_directory(argv[1])) {
      std::error_code ec;
      std::filesystem::path workspace = std::filesystem::absolute(argv[1], ec);
      if (!ec) {
        std::filesystem::current_path(workspace, ec);
      }
      editor.open_workspace(!ec ? workspace.string() : argv[1], true);
    } else {
      editor.load_file(argv[1]);
    }
  }

  editor.run();

  return 0;
}

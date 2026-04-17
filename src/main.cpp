#include "editor.h"
#include <iostream>

int main(int argc, char *argv[]) {
  // Determine config direction
  const char *home = getenv("HOME");
  std::string config_dir = "plugins"; // Default fallback
  if (home) {
    config_dir = std::string(home) + "/.config/jot";
  }

  Editor editor;
  // Set config dir logic here if needed, or Editor handles it?
  // Actually Editor's PythonAPI::load_plugins needs the path.
  // Let's pass it or Editor figures it out.
  // For now, let's assume Editor or PythonAPI handles dynamic path.
  // But wait, Editor initializes PythonAPI.

  // We should probably modify Editor constructor or PythonAPI initialization to
  // take the path. But for simplicity, let's just make sure Editor knows where
  // to look. Actually, PythonAPI::init() calls load_plugins(). Let's modify
  // PythonAPI::init() to check specific paths.

  if (argc > 1) {
    if (std::filesystem::is_directory(argv[1])) {
      std::error_code ec;
      std::filesystem::path workspace = std::filesystem::absolute(argv[1], ec);
      if (!ec) {
        std::filesystem::current_path(workspace, ec);
      }
      editor.load_file_tree(!ec ? workspace.string() : argv[1]);
      editor.toggle_sidebar(); // Show sidebar by default for dirs
    } else {
      editor.load_file(argv[1]);
    }
  }

  editor.run();

  return 0;
}

#include "editor.h"
#include <filesystem>

namespace fs = std::filesystem;

void Editor::copy_current_file_path() {
  auto &buf = get_buffer();
  if (buf.filepath.empty()) {
    set_message("No file path to copy");
    return;
  }
  std::error_code ec;
  fs::path p = fs::absolute(fs::path(buf.filepath), ec);
  clipboard = (ec ? fs::path(buf.filepath) : p).lexically_normal().string();
  set_message("Copied file path");
}

void Editor::copy_current_file_name() {
  auto &buf = get_buffer();
  if (buf.filepath.empty()) {
    set_message("No file name to copy");
    return;
  }
  clipboard = fs::path(buf.filepath).filename().string();
  if (clipboard.empty()) {
    clipboard = buf.filepath;
  }
  set_message("Copied file name");
}

// Auto-save: enabled flag, interval, and the periodic flush of modified
// buffers.
#include "editor.h"
#include "folding.h"
#include "jot/app/file_internal.h"
#include "jot/lua/api.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

using namespace file_internal;


void Editor::set_auto_save(bool enabled, bool persist)
{
  auto_save_enabled = enabled;
  if (persist)
  {
    config.set("auto_save", enabled ? "true" : "false");
    config.save();
  }
}
void Editor::set_auto_save_interval(int interval_ms, bool persist)
{
  auto_save_interval_ms = std::clamp(interval_ms, 250, 60000);
  if (persist)
  {
    config.set("auto_save_interval_ms", std::to_string(auto_save_interval_ms));
    config.save();
  }
}

void FileBuffer::materialize()
{
  if (!lazy_provider)
    return;
  lines = lazy_provider->copy_all_lines();
  lazy_provider.reset();
}
void Editor::auto_save_modified_buffers()
{
  if (!auto_save_enabled)
  {
    return;
  }

  int saved = 0;
  for (int i = 0; i < (int)buffers.size(); i++)
  {
    if (!buffers[i].modified || buffers[i].filepath.empty())
    {
      continue;
    }
    if (save_buffer_at(i, false))
    {
      saved++;
    }
  }

  if (saved > 0)
  {
    set_message("Auto-saved " + std::to_string(saved) + " file(s)");
  }
}
#ifndef JOT_MODEL_SESSION_H
#define JOT_MODEL_SESSION_H

#include "jot/model/buffer.h"
#include <string>
#include <vector>
// One place the cursor has been: what the jumplist stores and restores. The
// scroll fields are what make Ctrl+O feel like "back" rather than "reposition":
// the view you left comes back with the cursor.
struct JumpLocation
{
  std::string filepath;
  Cursor cursor;
  int scroll_offset = 0;
  int scroll_x = 0;
  bool preview = false;
};

struct ClosedBufferSnapshot
{
  std::string filepath;
  std::vector<std::string> lines;
  Cursor cursor;
  Selection selection;
  int scroll_offset;
  int scroll_x;
  bool modified;
  std::vector<FoldRange> collapsed_folds;
};

#endif

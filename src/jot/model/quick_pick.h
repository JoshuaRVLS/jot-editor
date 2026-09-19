#ifndef JOT_MODEL_QUICK_PICK_H
#define JOT_MODEL_QUICK_PICK_H

#include <string>
#include <vector>
struct CommandPaletteSuggestion
{
  std::string insert_text;
  std::string label;
  std::string category;
  std::string detail;
  int score = 0;
  // 0-based byte offsets into `label` matched by the query; used to
  // emphasize matched characters while rendering. Empty when not computed.
  std::vector<int> match;
};

enum QuickPickKind
{
  QUICK_PICK_NONE,
  QUICK_PICK_PROJECT_SEARCH,
  QUICK_PICK_DIAGNOSTICS,
  QUICK_PICK_SYMBOLS,
  QUICK_PICK_REFERENCES,
  QUICK_PICK_CODE_ACTIONS,
  QUICK_PICK_PLUGIN,
  QUICK_PICK_FONT,
  QUICK_PICK_JUMPLIST,
  QUICK_PICK_WORKSPACE_SYMBOLS,
  QUICK_PICK_WORKSPACE_DIAGNOSTICS
};

struct QuickPickItem
{
  std::string label;
  std::string detail;
  std::string preview;
  std::string filepath;
  // The value the row stands for, when it is not a location: the font picker
  // puts the family name here so the label can carry the display text.
  std::string value;
  int line = 0;
  int col = 0;
  int severity = 0;
};

// One row of the cell-based settings menu (:settings / Ctrl+,). Each entry
// wraps a config key with its human label, current value, value type and
// edit state. Bool keys toggle on Enter; int/string keys open an inline
// input row. Lua-registered config keys (jot.config.set) appear here too
// as generic string entries, so the menu doubles as a config browser.
struct SettingsEntry
{
  enum class Type
  {
    Bool,
    Int,
    String
  };
  std::string key;    // config key
  std::string label;  // human-readable label
  std::string value;  // current string value (as stored in settings.conf)
  Type type = Type::String;
  // While the row is being edited, its input text and the row's screen
  // position (set by the render pass, used by mouse hit-testing).
  bool editing = false;
  std::string edit_input;
  int row_x = 0;
  int row_y = 0;
  int row_w = 0;
};

#endif

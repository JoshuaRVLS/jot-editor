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
  // The explanation box's sentence(s) for the selected row. Empty falls back
  // to `detail`, so every command carries an explanation.
  std::string help;
  // Alternate spellings ("write, save") and argument syntax ("<path>"), each
  // shown as its own box line when not empty.
  std::string aliases;
  std::string args;
  int score = 0;
  // 0-based byte offsets into `label` matched by the query; used to
  // emphasize matched characters while rendering. Empty when not computed.
  std::vector<int> match;
};

// The command palette's painted geometry: the results grid (filled column by
// column, like Helix's command menu), the explanation box for the selected row
// above it, and the prompt row at the bottom. Built once per frame by
// Editor::build_command_palette_layout() and read by the renderer, the mouse
// hit-test and the Lua view, so a click and a painted cell cannot disagree.
struct CommandPaletteLayout
{
  int screen_w = 0, screen_h = 0;
  // The whole painted region, from the help box's top (or the grid's when
  // there is none) through the prompt row.
  int x = 0, y = 0, w = 0, h = 0;
  int prompt_y = 0;
  int grid_x = 0, grid_y = 0;
  int grid_cols = 1;
  int grid_col_w = 1;
  int grid_rows = 0;      // grid rows on screen
  int grid_first_row = 0; // first visible row (the scroll window)
  int rows_per_col = 0;   // items in a full column: ceil(count / cols)
  int result_count = 0;
  // The explanation box. help_for is -1 when the selected row has no help,
  // and the box rect is empty then.
  int help_x = 0, help_y = 0, help_w = 0, help_h = 0;
  int help_for = -1;
  std::vector<std::string> help_lines; // wrapped, without the border rows
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
  // The row's own ink, when the picker rows carry one (-1 = the picker's
  // normal foreground). A picker whose rows name a family - the todo list -
  // is colour-coded at a glance.
  int fg = -1;
};

// One row of the cell-based settings menu (:settings / Ctrl+,): a config key
// with its label, value, type and edit state. Lua-registered keys appear here
// as generic string entries, so the menu doubles as a config browser.
struct SettingsEntry
{
  enum class Type
  {
    Bool,
    Int,
    String,
    Enum // a fixed set of choices: cycled with Left/Right, listed by Enter
  };
  std::string key;   // config key
  std::string label; // human-readable label
  std::string value; // current string value (as stored in settings.conf)
  // The group the panel files the row under (the sections table in
  // surfaces/settings.cpp). Entries are kept in section order already.
  std::string section;
  Type type = Type::String;
  // Enum rows: the choices, in the order they are offered. Empty otherwise.
  std::vector<std::string> options;
  // Int rows: what the steppers move by, and the range a stepped or typed value
  // is clamped to. Without `has_range` the value only has a floor of zero.
  int step = 1;
  bool has_range = false;
  int min_value = 0;
  int max_value = 0;
  // While the row is being edited, its input text and the row's screen
  // position (set by the render pass, used by mouse hit-testing).
  bool editing = false;
  std::string edit_input;
  int row_x = 0;
  // -1 until the render pass places the row: a row that is filtered out,
  // scrolled past or simply not painted yet must never answer a hit test.
  int row_y = -1;
  int row_w = 0;
  // The row's position in the filtered list, so a mouse hit names the row it
  // landed on. -1 while the query hides the row.
  int row_pos = -1;
  // The value column and the affordance inside it (an int's steppers, an
  // enum's chevrons), recorded by the layout pass and painted into by the Lua
  // surface, so picture and hit test cannot disagree. -1 means none.
  int value_x = 0;
  int value_w = 0;
  int step_down_x = -1;
  int step_up_x = -1;
};

// One painted row of the settings panel: a section header or one of the
// entries, so the picture, the scroll window and the mouse hit test all speak
// in the same units. `entry_pos` is -1 on a header.
struct SettingsRow
{
  std::string header; // the title, on a header row
  int entry_pos = -1; // position in the selectable list; -1 on a header
};

#endif

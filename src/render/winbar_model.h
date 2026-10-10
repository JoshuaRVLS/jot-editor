#ifndef JOT_FEATURES_WINBAR_H
#define JOT_FEATURES_WINBAR_H

#include "core/theme.h" // Theme (symbol colors, shared with the outline)
#include "syntax/index.h"
#include <string>
#include <vector>

// The winbar: the breadcrumb row a code pane pays above its text, and the
// drop-down menus its crumbs open (Bekaboo/dropbar.nvim's model, ported).
//
// This header is the model only: where the crumbs come from and what a crumb's
// menu offers. A chain is the workspace root, the folders down to the file, the
// file, and the symbol ancestors of the cursor line, whose nesting is recovered
// from each symbol's own column because the index is flat.
namespace Winbar
{
  // `winbar` setting: off = never, auto = code files only, on = every file.
  enum WinbarMode
  {
    WINBAR_MODE_OFF,
    WINBAR_MODE_AUTO,
    WINBAR_MODE_ON
  };

  struct Crumb
  {
    std::string label;       // "render", "tabs.cpp", "render_tabline"
    std::string kind;        // "folder" | "file" | "symbol"
    std::string symbol_kind; // the index's own kind for a symbol ("class", ...)
    std::string icon;        // Nerd Fonts glyph (already resolved)
    int icon_fg = -1;        // brand color for a file icon, -1 = the painter picks
    std::string path;        // absolute path for path crumbs, empty for symbols
    int line = 0;            // symbol target (0-based), 0 for path crumbs
    int col = 0;
    int depth = 0;        // symbol nesting depth, 0 for path crumbs
    bool current = false; // the innermost crumb (the cursor's own symbol)
  };

  struct Entry
  {
    std::string label;
    std::string kind;        // "folder" | "file" | "symbol"
    std::string symbol_kind; // "class", "function", ... for symbol entries
    std::string icon;
    int icon_fg = -1;
    std::string path; // absolute path for folder/file entries
    int line = 0;     // symbol target
    int col = 0;
    bool is_dir = false;
    bool current = false; // the sibling the chain is already on
  };

  // The chain for a buffer at `cursor_line`. `workspace_root` may be empty (no
  // workspace open), in which case the chain starts at the file's own folder.
  std::vector<Crumb> build(const std::string &filepath,
                           const std::string &workspace_root,
                           const std::vector<SymbolMatch> &symbols,
                           int cursor_line);

  // What clicking `crumbs[index]` offers: its siblings at that level (a folder's
  // children, a file's folder, a symbol's scope).
  std::vector<Entry> menu_entries(const std::vector<Crumb> &crumbs,
                                  int index,
                                  const std::vector<SymbolMatch> &symbols);

  // A folder's own entries, folders first, for drilling into a folder crumb.
  // Hidden entries (a leading dot) are left out, like the explorer does.
  std::vector<Entry> directory_entries(const std::string &dir);

  // One directory entry, `current` when it is the file the chain ends at.
  Entry entry_for_path(const std::string &path, bool is_dir, bool current);

  // Parent index of every symbol (-1 at the top level), from symbol columns.
  std::vector<int> symbol_parents(const std::vector<SymbolMatch> &symbols);

  // The symbols enclosing `cursor_line`, outermost first: the chain of parents
  // of the last symbol that starts at or before the cursor.
  std::vector<int> symbol_chain(const std::vector<SymbolMatch> &symbols, int cursor_line);

  // The symbols sharing `index`'s parent scope, in document order.
  std::vector<int> symbol_siblings(const std::vector<SymbolMatch> &symbols,
                                   const std::vector<int> &parents,
                                   int index);

  // One crumb as it lands on the row: `crumbs` holds the chain, this holds the
  // cells, and the painter and the hit test read the same numbers.
  struct WinbarSegment
  {
    int crumb_index = -1; // -1 for the "…" chip standing in for dropped crumbs
    int x = 0;            // the leading separator cell (the row's start when first)
    int icon_x = -1;      // -1 when the crumb has no icon
    int label_x = 0;
    int end_x = 0; // exclusive
    // The text to draw, already measured and ellipsized to what fits.
    std::string label;
    bool hovered = false;
    bool active = false; // the crumb whose menu is open
  };

  struct WinbarLayout
  {
    int x = 0;
    int y = 0;
    int w = 0;
    std::vector<Crumb> crumbs;
    std::vector<WinbarSegment> segments;
    // Some leading crumbs did not fit: the row opens with a "…" chip and
    // `first_crumb` is the first that did. The tail is always kept, since that
    // is what identifies the line.
    bool truncated = false;
    int first_crumb = 0;
  };

  // The Nerd Fonts glyph a symbol kind is drawn with (function, class, ...).
  std::string symbol_icon(const std::string &kind);

  // The theme slot a symbol kind is painted with (the outline panel's own
  // mapping, shared so the two surfaces never disagree about a kind).
  int symbol_color(const Theme &theme, const std::string &kind);
} // namespace Winbar

#endif

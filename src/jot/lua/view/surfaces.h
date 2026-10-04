#ifndef LUA_VIEW_SURFACES_H
#define LUA_VIEW_SURFACES_H

// // ---------------------------------------------------------------------------
// Surface views
// // ---------------------------------------------------------------------------
//
// What every built-in UI surface hands a jot.ui.handler: the palette, quick
// pick, settings menu, popup, prompts, tree-sitter / LSP status modals,
// telescope, completion and signature popups, context menu, menu dropdown,
// home screen, search panel, status line and the side panels.
//
// Split out of jot/lua/api.h, which keeps the LuaAPI class itself and
// includes these headers in order; the structs are unchanged.

#include "text_features.h"
#include "tools/debugger/client.h"
#include "tools/lsp/client.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Views handed to Lua UI surface handlers (jot.ui.handler). Native code fills
// these with state; api_core turns them into a payload table and calls the
// registered handler. Returning true from the handler suppresses the native
// render for that surface.
struct PaletteItemView
{
  std::string label;
  std::string category;
  std::string detail;
  std::vector<int> match; // 0-based byte offsets into label matched by query
};

struct PaletteView
{
  std::string query;
  int selected = 0;
  std::vector<PaletteItemView> results;
  int x = 0, y = 0, w = 0, h = 0;
  int screen_w = 0, screen_h = 0;
};

struct QuickPickItemView
{
  std::string label;
  std::string detail;
  std::string preview;
  int severity = 0;
  int fg = -1; // row ink, -1 = the picker's normal foreground
};

struct QuickPickView
{
  std::string title;
  std::string query;
  int selected = 0;
  int all_count = 0;
  std::vector<QuickPickItemView> items;
  int x = 0, y = 0, w = 0, h = 0;
};

struct SettingsItemView
{
  // A section header rather than a setting: `label` is the group's name and
  // every other field is empty, since there is nothing to toggle or step.
  bool header = false;
  std::string label;
  // Already formatted for display (a bool reads as its toggle glyph and
  // "on"/"off", an empty string as "(empty)").
  std::string value;
  // "bool" | "int" | "string" | "enum" ("" on a header).
  std::string type;
  bool selected = false;
  bool editing = false;
  std::string edit_input;
  // Enum rows: the choices and which one the config holds (-1 when the stored
  // value is none of them).
  std::vector<std::string> options;
  int option_index = -1;
  // Where the value text and the decrease/increase affordances sit on the
  // row (absolute columns; the affordance columns are -1 when the row has
  // none). The natively painted fallback draws the same glyphs into the same
  // cells, so both painters and the mouse hit test agree.
  int value_x = 0;
  int value_w = 0;
  int step_down_x = -1;
  int step_up_x = -1;
  int row_y = 0;
};

struct SettingsView
{
  int x = 0, y = 0, w = 0, h = 0; // panel rect (absolute)
  int selected = 0;               // position in the filtered (selectable) list
  int scroll = 0;                 // first visible item row (headers included)
  int all_count = 0;              // every config key the menu knows
  int match_count = 0;            // how many the search bar keeps
  std::string query;              // the search bar's text ("" = unfiltered)
  int search_x = 0, search_y = 0; // where the search field's text starts
  // The rows to paint, in order: each match with a section header folded in
  // where the group changes. `scroll` indexes this list.
  std::vector<SettingsItemView> items;
  // The choices drop-down (an Enum row opened with Enter). The Lua surface
  // paints it as a second float over the panel.
  bool dropdown_open = false;
  int dropdown_x = 0, dropdown_y = 0, dropdown_w = 0, dropdown_h = 0;
  int dropdown_index = 0;  // the cursor in it
  int dropdown_scroll = 0; // first visible choice
  std::vector<std::string> dropdown_options;
  std::string dropdown_value; // the choice in force (marked in the list)
};

struct PopupView
{
  std::string title;
  std::vector<std::string> lines;
  int scroll = 0;
  int x = 0, y = 0, w = 0, h = 0;
};

struct PromptView
{
  std::string input;
  int x = 0, y = 0, w = 0, h = 0;
};

struct TsStatusRowView
{
  bool section = false;
  std::string label; // section title or language name
  std::string detail;
  int color = 0; // theme color slot for language names (0 = default fg)
};

struct TsStatusView
{
  std::vector<TsStatusRowView> rows;
  int scroll = 0;
  int x = 0, y = 0, w = 0, h = 0;
  int hover = -1; // hovered row index (mouse motion), -1 when none
};


struct TelescopeResultView
{
  std::string name;
  std::string parent_path; // the file's folder, shown dimmed on the row
  bool is_directory = false;
  // True when this file already has a tab: the row's leading dot is lit for an
  // open file and hollow for one that is not.
  bool opened = false;
  std::vector<int> match; // byte offsets into name matched by the query
  std::string icon;       // per-language file glyph (empty for directories)
  int icon_fg = -1;       // brand color index, -1 = use the row foreground
};

struct TelescopePreviewView
{
  std::string title;
  std::string detail;
  std::vector<std::string> lines; // windowed to the visible region
  int start_line = 0;             // 0-based line number of lines[0]
  std::string extension;          // ".cpp" or empty
  bool is_directory = false;
  bool skipped = false;
  bool is_binary = false;
  bool truncated = false;
  std::uintmax_t size_bytes = 0;
};

struct TelescopeView
{
  // Native layout geometry is passed through unchanged so mouse hit-testing
  // (row clicks, wheel regions, query focus) keeps working on the Lua render.
  // x/y/w/h is the list box and preview_x/... the file view box: two separate
  // frames with one column between them.
  int x = 0, y = 0, w = 0, h = 0;
  int region_w = 0;
  int inner_x = 0, inner_y = 0, inner_w = 0, inner_h = 0;
  int query_x = 0, query_y = 0, query_w = 0;
  int body_y = 0, body_h = 0;
  int list_x = 0, list_y = 0, list_w = 0, list_h = 0;
  int preview_x = 0, preview_y = 0, preview_w = 0, preview_h = 0;
  int preview_inner_x = 0, preview_inner_y = 0;
  int preview_inner_w = 0, preview_inner_h = 0;
  int preview_text_y = 0, preview_status_y = 0;
  int footer_y = 0;
  bool show_preview = false;
  std::string query;
  std::string root;
  // The scan root relative to the workspace it was opened in ("src/render"),
  // empty when the picker is at the workspace root.
  std::string folder;
  std::string title;
  int selected = 0;
  int list_scroll = 0;
  int result_count = 0;
  bool scan_pending = false;
  std::string scan_error; // non-empty when the tree walk failed (bad root)
  std::string focus;      // query | results | preview
  std::vector<TelescopeResultView> results; // windowed to visible rows
  TelescopePreviewView preview;
};

struct CompletionItemView
{
  std::string label;
  int kind = 0;
  std::string kind_name; // e.g. "Function", "Keyword", ...
  std::string kind_icon; // nerd-font glyph as rendered natively
  bool deprecated = false;
  std::string detail;
  std::string documentation;
  // The LSP labelDetails pair (empty unless the server sent them). Which of
  // these carries the type differs per server, so the row builder decides; see
  // runtime/lua/features/ui/completion_label.lua.
  std::string label_detail;
  std::string label_description;
  std::vector<int> match; // byte offsets into label matched by the query
};

struct CompletionView
{
  // Content-box geometry (rows + footer); the Lua render wraps it in a
  // bordered float one cell larger on each side, matching the native popup.
  int x = 0, y = 0, w = 0, h = 0;
  int max_items = 0; // visible row window
  int start = 0;     // absolute index of items[0]
  int selected = 0;  // index into the full filtered list
  int total = 0;     // filtered item count
  int all_total = 0; // unfiltered count (footer "filtered" hint)
  bool filtered = false;
  std::string prefix;
  // Which language server produced these items (e.g. "clangd"); the row builder
  // picks per-server label presentation from it, falling back to a generic one.
  std::string server;
  std::vector<CompletionItemView> items; // windowed to max_items rows
};

struct SignatureLineView
{
  std::string text;
  // 0 = signature label, 1 = documentation, 2 = footer hint.
  int role = 0;
};

struct SignatureView
{
  // Content-box geometry; the Lua render wraps it in a bordered float.
  int x = 0, y = 0, w = 0, h = 0;
  // 0-based index of the signature shown (footer "overload i/n"), plus its
  // active-parameter highlight inside the label row (byte offsets, -1 none).
  int active_signature = 0;
  int signature_total = 1;
  int label_hl_start = -1;
  int label_hl_len = -1;
  std::vector<SignatureLineView> lines;
};

struct ContextMenuItemView
{
  std::string label;
  bool enabled = true;
};

struct ContextMenuView
{
  int x = 0, y = 0, w = 0, h = 0;
  int selected = 0;
  std::vector<ContextMenuItemView> items;
};

struct MenuItemView
{
  std::string label;
  bool enabled = true;
};

// The breadcrumb winbar (features/winbar.h). The native layout is passed
// through column by column so a Lua painter draws the same row the mouse
// hit-tests; `kind` picks the styling branch (a folder, the file, a symbol).
struct WinbarCrumbView
{
  std::string label;
  std::string kind;        // "folder" | "file" | "symbol"
  std::string symbol_kind; // the index's own kind for a symbol ("class", ...)
  std::string icon;        // Nerd Fonts glyph, empty when the crumb has none
  int icon_fg = -1;        // brand color for a file icon, -1 = use the row fg
  int x = 0;               // the crumb's leading cell (its separator)
  int icon_x = -1;
  int label_x = 0;
  int end_x = 0;
  bool current = false; // the innermost crumb (the cursor's own)
  bool hovered = false;
  bool active = false;  // its menu is open
  bool ellipsis = false; // the chip standing in for the crumbs that did not fit
};

// One pane's breadcrumb row. Every pane with a row gets one of these, and the
// row names its pane so the painter and the hit-test agree on which row is
// which.
struct WinbarRowView
{
  int x = 0, y = 0, w = 0; // the pane's winbar row (absolute)
  int pane = -1;
  std::string filepath;
  bool truncated = false;
  std::vector<WinbarCrumbView> crumbs;
};

// The winbar surface: every pane's row in one payload, the way the crumb
// cascade carries all of its levels. A painter keyed on a single row cannot
// tell which panes are gone, so it would leave a split's earlier rows unpainted
// and keep re-configuring one float for whoever emitted last.
struct WinbarView
{
  std::vector<WinbarRowView> rows;
};

struct WinbarMenuEntryView
{
  std::string label;
  std::string icon;
  std::string kind; // "folder" | "file" | "symbol"
  int index = 0;    // absolute row index
  bool is_dir = false;
  bool current = false;
};

// One level of the cascade: the crumb's own menu for level 0, and each deeper
// level a folder opened from the row above it. Each level carries its own
// rect, so a handler paints panels that sit exactly where the hit-test reads.
struct WinbarMenuLevelView
{
  int x = 0, y = 0, w = 0, h = 0;
  std::string title; // the crumb's, or the folder's own name
  int selected = 0;
  int scroll = 0; // first visible row
  int total = 0;
  std::vector<WinbarMenuEntryView> entries; // windowed to the visible rows
};

struct WinbarMenuView
{
  std::vector<WinbarMenuLevelView> levels; // level 0 first, deepest last
};

struct MenuDropdownView
{
  std::string menu_label;
  int x = 0, y = 0, w = 0, h = 0;
  int selected = 0;
  std::vector<MenuItemView> items;
};

struct HomeEntryView
{
  std::string label;     // icon + text
  std::string secondary; // dimmed right-aligned path (may be empty)
  std::string key;       // shortcut printed at the right edge (may be empty)
  int x = 0, y = 0, w = 0;
  bool section = false; // section title row
  bool selected = false;
};

struct HomeView
{
  // Content panel geometry (absolute screen coords).
  int panel_x = 0, panel_y = 0, panel_w = 0, panel_h = 0;
  std::string wordmark; // "JOT"
  std::string tagline;  // "Developer workspace"
  std::string context;  // "Here <launch dir>" / "No recent workspace yet"
  std::string filter;   // typed query; empty unless the rows are filtered
  std::string hint;     // the key legend on the wordmark row
  // Section titles and item rows in layout order with absolute rects (mouse
  // hit-testing uses the same rects natively).
  std::vector<HomeEntryView> rows;
};

struct SearchView
{
  int x = 0, y = 0, w = 0, h = 0;
  std::string query;
  std::string replace_text;
  bool replace_visible = false;
  bool focus_replace = false; // false = find field focused
  bool case_sensitive = false;
  bool whole_word = false;
  bool regex = false;
  bool scoped_to_selection = false;
  std::string count; // "3/12" or "0/0"
};

struct StatusSegmentView
{
  std::string text;   // label (icon kept separate so it never truncates)
  int fg = 0, bg = 0; // label color / chip background
  bool bold = false;
  bool optional = false;
  int priority = 100;
  std::string side; // "left" or "right"
  std::string symbol; // leading glyph drawn with its own color
  int symbol_fg = -1; // -1 = draw with `fg`
};

struct StatusView
{
  int x = 0, y = 0, w = 0, h = 0; // full strip geometry (h == status_height)
  std::string message;            // transient message (may be empty)
  std::string context;            // workspace label used when no message
  std::vector<StatusSegmentView> segments;
  bool has_selection = false;
  int sel_lines = 0, sel_cols = 0;
};

struct SidebarPanelRowView
{
  int x = 0, y = 0, w = 0; // absolute row rect for the background fill
  std::string text;        // label, already truncated
  int text_x = 0;          // absolute column where `text` starts
  std::string symbol;      // leading glyph before the text (may be empty)
  int symbol_x = -1;       // absolute column for the symbol, -1 = none
  int symbol_fg = 0;
  bool symbol_bold = false;
  std::string icon;        // per-language file glyph drawn with its brand color
  int icon_x = -1;         // absolute column for the icon, -1 = none
  int icon_fg = 0;
  std::string guide; // tree indent guides (may be empty)
  int guide_x = -1;  // absolute column for the guides, -1 = none
  int guide_fg = 0;
  int fg = 0, bg = 0;
  bool bold = false;
  bool active_file = false; // draw the ▌ marker at the left edge
  int badge_x = -1;         // right badge column (absolute), -1 = none
  std::string badge;        // badge glyph (git symbol)
  int badge_fg = 0;
  int badge2_x = -1; // second badge column (diagnostic symbol), -1 = none
  std::string badge2;
  int badge2_fg = 0;
};

struct SidebarPanelView
{
  int x = 0, y = 0, w = 0, h = 0; // panel rect (absolute)
  int content_x = 0, content_w = 0;
  int rail_w = 0;
  int border_fg = 0;
  int bg = 0;
  bool git_view = false;
  // The rail's git item launches the git panel rather than switching the view,
  // so its marker follows the panel instead of `git_view`.
  bool git_panel_active = false;
  bool resizing = false;
  int rail_explorer_row = -1, rail_git_row = -1; // active rail rows, -1 = none
  std::string header;
  int header_x = 0, header_y = 0, header_fg = 0;
  int header_bg = -1;
  std::string footer;
  int footer_x = 0, footer_y = 0, footer_fg = 0;
  std::vector<SidebarPanelRowView> rows;
};

struct SidePanelRowView
{
  std::string text;   // main text (name / diff line / debug line)
  std::string detail; // right-aligned secondary text (line number), may be empty
  int fg = 0, bg = 0;
  bool bold = false;
  bool selected = false; // full-row selection background
  bool hovered = false;  // mouse hover highlight (never overrides selected)
  // Row category for richer Lua styling (the Lua side_panel kit restyles by
  // kind; native fallback renders all rows identically). Empty = generic.
  // Debugger kinds: "section", "thread", "frame", "var", "memory",
  // "instruction", "output", "empty", "config".
  std::string kind;
  std::string icon; // leading Nerd Fonts glyph (own color), may be empty
  int icon_fg = -1; // icon color, -1 = use fg
  int lead_fg = -1; // color for the first `lead_len` cells of text, -1 = none
  int lead_len = 0;
};

struct SidePanelTabView
{
  std::string label; // e.g. " dbg1 paused "
  bool active = false;
};

struct SidePanelView
{
  int x = 0, y = 0, w = 0, h = 0; // panel rect (absolute)
  std::string title;              // kept in the float frame
  // Panel mode for the Lua kit: "debugger", "git", "" (generic). Lets the
  // side_panel handler pick a styling branch without sniffing tabs/rows.
  std::string mode;
  std::string header;             // content header row (file + counts etc), may be empty
  int header_fg = 0;
  std::string header_icon;        // leading Nerd Fonts glyph for the header, may be empty
  int header_icon_fg = -1;        // -1 = header_fg
  std::string header_detail;      // right-aligned header text (diff stats), may be empty
  int header_detail_fg = 0;
  std::string note; // empty-state message, may be empty
  int note_fg = 0;
  std::string error; // bottom error line (debugger), may be empty
  // Right-dock panel tabs (Git / Diff / Symbols / Debug / Plugin) rendered
  // as the first row of the panel - the VSCode-style tab strip. The plugin
  // tab is labelled with the active panel's name, not a generic "Plugin".
  // Distinct from `tabs` (in-panel view/session tabs).
  std::vector<SidePanelTabView> panel_tabs;
  std::vector<SidePanelTabView> tabs;
  std::vector<SidePanelRowView> rows;
};

#endif

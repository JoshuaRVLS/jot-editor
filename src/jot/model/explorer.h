#ifndef JOT_MODEL_EXPLORER_H
#define JOT_MODEL_EXPLORER_H

#include <string>
#include <unordered_map>
#include <vector>
struct FileNode
{
  std::string name;
  std::string path;
  bool is_dir;
  bool expanded;
  int depth;
  std::vector<FileNode> children;
};

enum SidebarView
{
  SIDEBAR_VIEW_EXPLORER,
  SIDEBAR_VIEW_GIT
};

struct SidebarRenderRow
{
  std::string path;
  std::string normalized_path;
  std::string name;
  std::string label;
  std::string footer_label;
  bool is_dir = false;
  bool expanded = false;
  int depth = 0;
  int diagnostic_severity = 0;
  // Errors and warnings only: they are what the numeric file badge counts.
  int diagnostic_errors = 0;
  int diagnostic_warnings = 0;
  std::string git_status;
  // Per-language icon for files (empty for directories). Painted ahead of
  // the label in its brand color, like the status line.
  std::string icon;
  int icon_fg = -1; // -1 = use the row foreground
  // Tree indent guides ("│ ", "├─ ", "└─ " connectors; directories end with
  // their expander chevron). Empty for flat views.
  std::string guide;
  int guide_cells = 0; // cell width of `guide`
};

struct SidebarRenderCache
{
  std::vector<SidebarRenderRow> rows;
  std::unordered_map<std::string, int> path_to_row;
  std::string root_label;
  std::string normalized_root;
  bool tree_dirty = true;
  bool diagnostics_dirty = true;
  bool git_dirty = true;
};

struct GitSidebarRow
{
  std::string path;
  std::string relative_path;
  std::string status;
};

#endif

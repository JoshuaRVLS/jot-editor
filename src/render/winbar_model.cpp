// The winbar's model: the breadcrumb chain and what each crumb's menu offers.
// See render/winbar_model.h for the shape; render/winbar.cpp paints it.
#include "render/winbar_model.h"
#include "editor/file_icons.h"
#include "editor/model/theme.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace fs = std::filesystem;

namespace
{
  const char *const kFolderGlyph = "\uf07b";     // nf-fa-folder
  const char *const kRootFolderGlyph = "\uf07c"; // nf-fa-folder_open (workspace)
  // A menu never grows past this many rows: a folder with thousands of entries
  // would otherwise build a menu taller than the screen. The list is
  // alphabetical, so the cap keeps the beginning.
  constexpr int kMaxMenuEntries = 300;

  std::string lower_copy(std::string s)
  {
    std::transform(
        s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
  }

  std::string kind_lower(const std::string &kind)
  {
    return lower_copy(kind);
  }

  // The folder a path lives in ("" when it has none).
  std::string parent_of(const std::string &path)
  {
    const fs::path p(path);
    const fs::path parent = p.parent_path();
    return parent.empty() ? std::string() : parent.string();
  }

  bool inside_root(const fs::path &file, const fs::path &root)
  {
    if (root.empty())
    {
      return false;
    }
    std::error_code ec;
    const fs::path rel = fs::relative(file, root, ec);
    if (ec || rel.empty())
    {
      return false;
    }
    const std::string text = rel.generic_string();
    return text.rfind("..", 0) != 0;
  }

  bool hidden_name(const std::string &name)
  {
    return !name.empty() && name[0] == '.';
  }

  std::string display_name(const std::string &path)
  {
    const std::string name = fs::path(path).filename().string();
    return name.empty() ? path : name;
  }

  bool is_dir_entry(const Winbar::Crumb &crumb)
  {
    return crumb.kind == "folder";
  }
} // namespace

namespace Winbar
{
  int symbol_color(const Theme &theme, const std::string &kind)
  {
    const std::string k = kind_lower(kind);
    if (k == "function" || k == "method" || k == "constructor" || k == "macro")
    {
      return theme.fg_function;
    }
    if (k == "class" || k == "struct" || k == "union" || k == "interface" || k == "enum"
        || k == "type" || k == "typedef" || k == "type_alias" || k == "enum_member")
    {
      return theme.fg_type;
    }
    if (k == "namespace" || k == "module" || k == "package")
    {
      return theme.fg_namespace;
    }
    if (k == "variable" || k == "property" || k == "field" || k == "parameter")
    {
      return theme.fg_variable;
    }
    if (k == "constant")
    {
      return theme.fg_constant;
    }
    return theme.fg_command;
  }

  std::string symbol_icon(const std::string &kind)
  {
    const std::string k = kind_lower(kind);
    if (k == "function" || k == "method" || k == "constructor" || k == "macro")
    {
      return "\uf121"; // nf-fa-code
    }
    if (k == "class" || k == "struct" || k == "union" || k == "interface" || k == "enum"
        || k == "type" || k == "typedef" || k == "type_alias" || k == "enum_member")
    {
      return "\uf1b2"; // nf-fa-cube
    }
    if (k == "namespace" || k == "module" || k == "package")
    {
      return "\uf1b3"; // nf-fa-cubes
    }
    if (k == "constant")
    {
      return "\uf0eb"; // nf-fa-lightbulb_o
    }
    if (k == "variable" || k == "property" || k == "field" || k == "parameter")
    {
      return "\uf02b"; // nf-fa-tag
    }
    return "\uf111"; // nf-fa-circle
  }

  std::vector<int> symbol_parents(const std::vector<SymbolMatch> &symbols)
  {
    std::vector<int> parents(symbols.size(), -1);
    std::vector<int> stack;
    for (size_t i = 0; i < symbols.size(); i++)
    {
      const int column = symbols[i].column;
      // A symbol's parent is the innermost symbol that starts further left: the
      // flat index carries the name's indent in `column`, so a stack walk gets
      // back the nesting a real outline shows.
      while (!stack.empty() && symbols[(size_t)stack.back()].column >= column)
      {
        stack.pop_back();
      }
      parents[i] = stack.empty() ? -1 : stack.back();
      stack.push_back((int)i);
    }
    return parents;
  }

  std::vector<int> symbol_chain(const std::vector<SymbolMatch> &symbols, int cursor_line)
  {
    std::vector<int> chain;
    if (symbols.empty())
    {
      return chain;
    }
    int innermost = -1;
    for (int i = (int)symbols.size() - 1; i >= 0; i--)
    {
      if (symbols[(size_t)i].line <= cursor_line)
      {
        innermost = i;
        break;
      }
    }
    if (innermost < 0)
    {
      return chain;
    }
    const std::vector<int> parents = symbol_parents(symbols);
    for (int at = innermost; at >= 0; at = parents[(size_t)at])
    {
      chain.push_back(at);
    }
    std::reverse(chain.begin(), chain.end());
    return chain;
  }

  std::vector<int> symbol_siblings(const std::vector<SymbolMatch> &symbols,
                                   const std::vector<int> &parents,
                                   int index)
  {
    std::vector<int> out;
    if (index < 0 || index >= (int)symbols.size() || parents.size() != symbols.size())
    {
      return out;
    }
    const int parent = parents[(size_t)index];
    for (int i = 0; i < (int)symbols.size(); i++)
    {
      if (parents[(size_t)i] == parent)
      {
        out.push_back(i);
      }
    }
    return out;
  }

  Entry entry_for_path(const std::string &path, bool is_dir, bool current)
  {
    Entry entry;
    entry.label = display_name(path);
    entry.kind = is_dir ? "folder" : "file";
    entry.path = path;
    entry.is_dir = is_dir;
    entry.current = current;
    if (is_dir)
    {
      entry.icon = kFolderGlyph;
    }
    else
    {
      const jot_icons::FileTypeIcon icon = jot_icons::file_type_icon(path);
      entry.icon = icon.glyph;
      entry.icon_fg = icon.color;
    }
    return entry;
  }

  std::vector<Entry> directory_entries(const std::string &dir)
  {
    std::vector<Entry> out;
    if (dir.empty())
    {
      return out;
    }
    std::error_code ec;
    fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
    if (ec)
    {
      return out;
    }
    for (const fs::directory_entry &entry : it)
    {
      const std::string name = entry.path().filename().string();
      if (hidden_name(name))
      {
        continue;
      }
      std::error_code type_ec;
      const bool is_dir = entry.is_directory(type_ec);
      if (type_ec)
      {
        continue;
      }
      out.push_back(entry_for_path(entry.path().string(), is_dir, false));
      if ((int)out.size() >= kMaxMenuEntries)
      {
        break;
      }
    }
    std::sort(out.begin(),
              out.end(),
              [](const Entry &a, const Entry &b)
              {
                if (a.is_dir != b.is_dir)
                {
                  return a.is_dir;
                }
                return lower_copy(a.label) < lower_copy(b.label);
              });
    return out;
  }

  std::vector<Crumb> build(const std::string &filepath,
                           const std::string &workspace_root,
                           const std::vector<SymbolMatch> &symbols,
                           int cursor_line)
  {
    std::vector<Crumb> out;
    if (filepath.empty())
    {
      return out;
    }
    const fs::path file(filepath);
    const fs::path root(workspace_root);

    // The path half: the workspace root (when the file is inside it), the folders
    // down to the file, the file. A file outside the workspace gets its own
    // folder instead of a chain of ancestors.
    std::vector<fs::path> folders;
    if (!root.empty() && inside_root(file, root))
    {
      // A workspace root can be spelled relative to the editor's own directory
      // (`.` when a file was opened with no workspace named), so resolve it
      // before reading the name a literal `.` cannot give.
      fs::path root_path = root;
      const std::string root_name = root_path.filename().string();
      if (root_name.empty() || root_name == "." || root_name == "..")
      {
        std::error_code abs_ec;
        // absolute()/lexically_normal() keep a trailing `.` (a bare "" root
        // resolves to "/cwd/."), and a path ending in a separator has no
        // filename, so step over it to get the folder's name.
        fs::path absolute = fs::absolute(root_path, abs_ec).lexically_normal();
        if (absolute.filename().empty())
        {
          absolute = absolute.parent_path();
        }
        if (!abs_ec && !absolute.filename().empty())
        {
          root_path = absolute;
        }
      }
      const std::string label = root_path.filename().string();
      Crumb workspace;
      workspace.label = label.empty() ? root_path.string() : label;
      workspace.kind = "folder";
      workspace.icon = kRootFolderGlyph;
      workspace.path = root_path.string();
      out.push_back(std::move(workspace));

      std::error_code ec;
      const fs::path rel = fs::relative(file, root, ec);
      fs::path walk;
      for (const fs::path &part : rel)
      {
        walk /= part;
        if (part == rel.filename())
        {
          break;
        }
        folders.push_back(root_path / walk);
      }
    }
    else if (file.has_parent_path())
    {
      folders.push_back(file.parent_path());
    }

    for (const fs::path &folder : folders)
    {
      Crumb crumb;
      crumb.label = folder.filename().string();
      crumb.kind = "folder";
      crumb.icon = kFolderGlyph;
      crumb.path = folder.string();
      out.push_back(std::move(crumb));
    }

    Crumb file_crumb;
    file_crumb.label = file.filename().string();
    file_crumb.kind = "file";
    file_crumb.path = file.string();
    const jot_icons::FileTypeIcon icon = jot_icons::file_type_icon(filepath);
    file_crumb.icon = icon.glyph;
    file_crumb.icon_fg = icon.color;
    out.push_back(std::move(file_crumb));

    // The symbol half: the ancestors of the cursor's own symbol.
    const std::vector<int> chain = symbol_chain(symbols, cursor_line);
    for (size_t i = 0; i < chain.size(); i++)
    {
      const SymbolMatch &symbol = symbols[(size_t)chain[i]];
      Crumb crumb;
      crumb.label = symbol.name;
      crumb.kind = "symbol";
      crumb.symbol_kind = symbol.kind;
      crumb.icon = symbol_icon(symbol.kind);
      crumb.line = symbol.line;
      crumb.col = symbol.column;
      crumb.depth = (int)i;
      crumb.current = (i + 1 == chain.size());
      out.push_back(std::move(crumb));
    }
    return out;
  }

  std::vector<Entry>
  menu_entries(const std::vector<Crumb> &crumbs, int index, const std::vector<SymbolMatch> &symbols)
  {
    std::vector<Entry> out;
    if (index < 0 || index >= (int)crumbs.size())
    {
      return out;
    }
    const Crumb &crumb = crumbs[(size_t)index];

    if (crumb.kind == "symbol")
    {
      const std::vector<int> parents = symbol_parents(symbols);
      int self = -1;
      for (int i = 0; i < (int)symbols.size(); i++)
      {
        if (symbols[(size_t)i].line == crumb.line && symbols[(size_t)i].column == crumb.col)
        {
          self = i;
        }
      }
      if (self < 0)
      {
        return out;
      }
      for (int i : symbol_siblings(symbols, parents, self))
      {
        const SymbolMatch &symbol = symbols[(size_t)i];
        Entry entry;
        entry.label = symbol.name;
        entry.kind = "symbol";
        entry.symbol_kind = symbol.kind;
        entry.icon = symbol_icon(symbol.kind);
        entry.line = symbol.line;
        entry.col = symbol.column;
        entry.current = (i == self);
        out.push_back(std::move(entry));
      }
      return out;
    }

    // A file crumb lists the folder it lives in, so the file's siblings are one
    // click away; a folder crumb lists its own children.
    const std::string dir = is_dir_entry(crumb) ? crumb.path : parent_of(crumb.path);
    out = directory_entries(dir);
    for (Entry &entry : out)
    {
      entry.current = (entry.path == crumb.path);
    }
    return out;
  }
} // namespace Winbar

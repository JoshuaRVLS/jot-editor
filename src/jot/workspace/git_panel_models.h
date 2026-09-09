// Git panel models (src/jot/workspace/git_panel_models.h).
//
// Header-only: the git panel's row structs, view state, and the pure
// porcelain-status mapping helpers (section, rank, glyph). Kept free of
// Editor dependencies so unit tests can exercise the mapping directly.

#ifndef GIT_PANEL_MODELS_H
#define GIT_PANEL_MODELS_H

#include <string>
#include <vector>

namespace jot_git_panel
{
  // Panel views, numbered after lazygit's panel keys (2 files, 3 branches,
  // 4 commits, 5 stash).
  enum class View
  {
    Files = 2,
    Branches = 3,
    Commits = 4,
    Stash = 5,
  };

  struct FileRow
  {
    std::string abs_path; // absolute path (git_file_status key)
    std::string rel_path; // display path relative to the repo root
    std::string status;   // porcelain XY, e.g. "M ", "MM", "??"
  };

  struct BranchRow
  {
    std::string name;
    bool current = false;
    std::string tracking; // "[origin/main]", may be empty
  };

  struct CommitRow
  {
    std::string hash;
    std::string date;
    std::string subject;
  };

  struct StashRow
  {
    std::string ref; // "stash@{0}"
    std::string subject;
  };

  struct State
  {
    View view = View::Files;
    int selected = 0; // index into the current view's row vector
    int scroll = 0;
  // Two-step confirmations (discard / delete / drop): the key that armed
  // the pending action; pressing it again performs it.
  std::string pending_confirm;
  // When the panel opens a file diff, closing the diff panel returns to the
  // git panel instead of dropping the right dock.
  bool return_after_diff = false;

  // Visible row count for the current view (selection clamping helper).
  inline size_t row_count() const
  {
    switch (view)
    {
    case View::Branches:
      return branches.size();
    case View::Commits:
      return commits.size();
    case View::Stash:
      return stashes.size();
    case View::Files:
    default:
      return files.size();
    }
  }

    std::vector<FileRow> files;
    std::vector<BranchRow> branches;
    std::vector<CommitRow> commits;
    std::vector<StashRow> stashes;
  };

  // Rows group into sections in this order; conflicts first, then staged,
  // then unstaged, then untracked. Empty sections are skipped at render.
  inline std::string status_section(const std::string &xy)
  {
    if (xy == "??")
    {
      return "untracked";
    }
    if (xy.size() >= 2)
    {
      const char x = xy[0];
      const char y = xy[1];
      const bool conflict =
          (x == 'U' || y == 'U') || (x == 'A' && y == 'A') || (x == 'D' && y == 'D');
      if (conflict)
      {
        return "conflict";
      }
      if (x != ' ')
      {
        return "staged";
      }
    }
    return "unstaged";
  }

  // Sort rank for grouping: conflicts, staged, unstaged, untracked.
  inline int status_rank(const std::string &xy)
  {
    const std::string section = status_section(xy);
    if (section == "conflict")
      return 0;
    if (section == "staged")
      return 1;
    if (section == "unstaged")
      return 2;
    return 3;
  }

  // Single-letter display glyph for the status pair (X first, then Y).
  inline std::string status_glyph(const std::string &xy)
  {
    if (xy.size() < 2)
    {
      return "?";
    }
    std::string out;
    if (xy[0] != ' ')
    {
      out += xy[0];
    }
    if (xy[1] != ' ')
    {
      out += xy[1];
    }
    return out.empty() ? "-" : out;
  }

  // One renderable row: a section header (index -1) or an entry of the
  // current view's row vector (index >= 0). The renderer and the mouse
  // hit-testing share this flattened layout so click targets match pixels.
  struct FlatRow
  {
    bool section = false;
    std::string label;  // section name or row text
    std::string detail; // right-aligned (status glyph / date)
    int index = -1;     // index into the view's row vector; -1 for sections
  };

  inline std::vector<FlatRow> build_flat_rows(const State &s)
  {
    std::vector<FlatRow> rows;
    if (s.view == View::Files)
    {
      static const char *kSections[] = {"conflict", "staged", "unstaged", "untracked"};
      for (const char *section : kSections)
      {
        bool any = false;
        for (size_t i = 0; i < s.files.size(); i++)
        {
          if (status_section(s.files[i].status) != section)
          {
            continue;
          }
          if (!any)
          {
            FlatRow head;
            head.section = true;
            head.label = section;
            rows.push_back(std::move(head));
            any = true;
          }
          FlatRow row;
          row.label = s.files[i].rel_path;
          row.detail = status_glyph(s.files[i].status);
          row.index = (int)i;
          rows.push_back(std::move(row));
        }
      }
      return rows;
    }
    if (s.view == View::Branches)
    {
      for (size_t i = 0; i < s.branches.size(); i++)
      {
        FlatRow row;
        row.label = (s.branches[i].current ? "* " : "  ") + s.branches[i].name;
        row.index = (int)i;
        rows.push_back(std::move(row));
      }
      return rows;
    }
    if (s.view == View::Commits)
    {
      for (size_t i = 0; i < s.commits.size(); i++)
      {
        FlatRow row;
        row.label = s.commits[i].hash + "  " + s.commits[i].subject;
        row.detail = s.commits[i].date;
        row.index = (int)i;
        rows.push_back(std::move(row));
      }
      return rows;
    }
    for (size_t i = 0; i < s.stashes.size(); i++)
    {
      FlatRow row;
      row.label = s.stashes[i].ref + "  " + s.stashes[i].subject;
      row.index = (int)i;
      rows.push_back(std::move(row));
    }
    return rows;
  }
} // namespace jot_git_panel

#endif // GIT_PANEL_MODELS_H
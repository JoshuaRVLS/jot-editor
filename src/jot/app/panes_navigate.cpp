// Pane navigation: next/prev cycling and directional focus movement.
#include "editor.h"
#include <algorithm>
#include <cctype>
#include <functional>
#include <limits>
#include "jot/app/panes_internal.h"

using namespace pane_internal;
void Editor::next_pane()
{
  if (pane_zoom_active)
  {
    pane_zoom_active = false;
    pane_zoom_pane = -1;
    update_pane_layout();
  }
  if (panes.size() > 1)
  {
    activate_pane((current_pane + 1) % (int)panes.size());
    message = "Switched pane";
    needs_redraw = true;
  }
}

void Editor::prev_pane()
{
  if (pane_zoom_active)
  {
    pane_zoom_active = false;
    pane_zoom_pane = -1;
    update_pane_layout();
  }
  if (panes.size() > 1)
  {
    activate_pane((current_pane - 1 + (int)panes.size()) % (int)panes.size());
    message = "Switched pane";
    needs_redraw = true;
  }
}

bool Editor::focus_pane_direction(char dir)
{
  if (panes.size() < 2 || current_pane < 0 || current_pane >= (int)panes.size())
  {
    return false;
  }

  if (pane_zoom_active)
  {
    pane_zoom_active = false;
    pane_zoom_pane = -1;
    update_pane_layout();
  }

  char d = (char)std::tolower((unsigned char)dir);
  const SplitPane &current = panes[(size_t)current_pane];
  int current_cx = pane_center_x(current);
  int current_cy = pane_center_y(current);
  int best = -1;
  int best_score = std::numeric_limits<int>::max();

  for (int i = 0; i < (int)panes.size(); i++)
  {
    if (i == current_pane)
    {
      continue;
    }
    const SplitPane &candidate = panes[(size_t)i];
    int candidate_cx = pane_center_x(candidate);
    int candidate_cy = pane_center_y(candidate);
    int primary = 0;
    int overlap = 0;
    int secondary = 0;

    if (d == 'h' || d == 'l')
    {
      if (d == 'h')
      {
        if (candidate_cx >= current_cx)
        {
          continue;
        }
        primary = current.x - (candidate.x + candidate.w);
      }
      else
      {
        if (candidate_cx <= current_cx)
        {
          continue;
        }
        primary = candidate.x - (current.x + current.w);
      }
      primary = std::max(0, primary);
      overlap = overlap_amount(current.y, current.h, candidate.y, candidate.h);
      secondary = std::abs(candidate_cy - current_cy);
    }
    else if (d == 'k' || d == 'j')
    {
      if (d == 'k')
      {
        if (candidate_cy >= current_cy)
        {
          continue;
        }
        primary = current.y - (candidate.y + candidate.h);
      }
      else
      {
        if (candidate_cy <= current_cy)
        {
          continue;
        }
        primary = candidate.y - (current.y + current.h);
      }
      primary = std::max(0, primary);
      overlap = overlap_amount(current.x, current.w, candidate.x, candidate.w);
      secondary = std::abs(candidate_cx - current_cx);
    }
    else
    {
      return false;
    }

    int score = primary * 1000 + secondary - overlap * 10;
    if (score < best_score)
    {
      best_score = score;
      best = i;
    }
  }

  if (best < 0)
  {
    return false;
  }

  activate_pane(best);
  focus_state = FOCUS_EDITOR;
  message = "Focused pane";
  needs_redraw = true;
  return true;
}

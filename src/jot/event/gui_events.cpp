// GUI frontend event pump (jot --gui). Drains SDL events -- translated into
// the same Event convention as the terminal path by UIGui::poll_event --
// through the normal handle_terminal_event dispatch, then renders a frame.
// Runs on the 4ms repeating timer installed by run() while gui_mode is set,
// so input-to-paint latency is a few milliseconds and the vsync'd swap
// paces the actual presentation to the monitor refresh.
#include "editor.h"
#include "ui/gui/gui.h"

#include <chrono>

void Editor::pump_gui_events()
{
  UIGui *gui = static_cast<UIGui *>(ui);
  if (!gui)
  {
    return;
  }
  if (gui->quit_requested())
  {
    running = false;
    return;
  }

  constexpr int kMaxDrainPerWake = 128;
  int drained = 0;
  for (;;)
  {
    if (gui->quit_requested())
    {
      running = false;
      break;
    }
    Event ev;
    if (!gui->poll_event(ev))
    {
      break;
    }
    // GUI-only: Ctrl+, toggles the RmlUi settings overlay (consumed
    // entirely here; while it is open, poll_event routes input to it).
    if (ev.type == EVENT_KEY && ev.key.ctrl && ev.key.key == ',')
    {
      gui->settings().toggle();
    }
    // GUI-only font zoom: Ctrl+= / Ctrl+Plus (incl. numpad) zoom in,
    // Ctrl+- zooms out. Intercepted before dispatch so no mode can rebind
    // or swallow it; the editor is told through a synthesized resize event
    // (the grid re-fits the window at the new cell size, panes relayout).
    else if (ev.type == EVENT_KEY && ev.key.ctrl
             && (ev.key.key == '=' || ev.key.key == '+' || ev.key.key == '-'))
    {
      gui->apply_font_zoom(ev.key.key == '-' ? -1 : 1);
      // Persist the zoom so the scale survives a relog (settings.conf).
      config.set_int("gui_font_size", gui->font_px());
      config.save();
      Event rsz;
      rsz.type = EVENT_RESIZE;
      rsz.resize.width = gui->get_width();
      rsz.resize.height = gui->get_height();
      handle_terminal_event(rsz);
    }
    else
    {
      handle_terminal_event(ev);
    }
    if (++drained >= kMaxDrainPerWake)
    {
      break;
    }
  }

  // While a scroll/cursor animation is in flight the GUI keeps repainting
  // on its own (no input needed) so the animation advances at the monitor's
  // refresh rate; once it settles, the editor goes back to idle repaints.
  if (gui->needs_repaint())
  {
    needs_redraw = true;
  }

  // Defensive resize re-sync. Compositors can coalesce or drop resize
  // events (Wayland configure batching, fractional scale changes), which
  // leaves the grid smaller than the window until the next real event --
  // the "editor doesn't fill the window until I fullscreen" symptom.
  // Re-derive the grid from the live window size every frame and re-fit
  // when it disagrees; when only the drawable (pixel) size changed while
  // the cell count is unchanged, repaint so the newly exposed strip is
  // painted instead of stale content.
  {
    int w = 0, h = 0;
    gui->window_size(w, h);
    const int cols = std::max(1, (int)((float)w / gui->cell_w()));
    const int rows = std::max(1, (int)((float)h / gui->cell_h()));
    if (cols != gui->get_width() || rows != gui->get_height())
    {
      Event rsz;
      rsz.type = EVENT_RESIZE;
      rsz.resize.width = cols;
      rsz.resize.height = rows;
      handle_terminal_event(rsz);
    }
    else
    {
      int dw = 0, dh = 0;
      gui->drawable_size(dw, dh);
      if (gui->note_drawable_size(dw, dh))
      {
        needs_redraw = true;
      }
    }
  }

  render_frame();
}
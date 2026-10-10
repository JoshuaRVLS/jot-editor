// The editor main loop: run(), the frame render step, and idle drain.
#include "core/editor.h"
#include "lua/api.h"
#include "ui/cursor_blink.h"

void Editor::render_frame()
{
#ifdef _WIN32
  constexpr int kMaxDrainPerFrame = 64;
  // Same resize coalescing as the POSIX stdin watcher: the console can deliver
  // several size changes per drain and only the last one matters.
  bool resize_pending = false;
  int resize_w = 0;
  int resize_h = 0;
  for (int drained = 0; drained < kMaxDrainPerFrame; drained++)
  {
    Event ev = terminal.poll_event();
    if (ev.type == EVENT_REDRAW)
    {
      break;
    }
    if (ev.type == EVENT_RESIZE)
    {
      resize_pending = true;
      resize_w = ev.resize.width;
      resize_h = ev.resize.height;
      continue;
    }
    handle_terminal_event(ev);
  }
  if (resize_pending)
  {
    apply_resize(resize_w, resize_h);
  }
#endif
  // Terminal size is authoritative in terminal mode. In GUI mode the SDL
  // window (polled by the GUI pump, including its defensive re-sync) owns
  // the grid, so the hosting terminal's SIGWINCH must not resize it.
  Event rsz;
  if (!gui_mode)
  {
    rsz = terminal.check_resize_event();
    if (rsz.type == EVENT_REDRAW)
    {
      // Slow DSR force-probe fallback: ioctl can report a stale size that
      // never changes (alternate-screen/multiplexer quirks), so the
      // per-frame probe above never fires. Re-probe with the cursor
      // position query every couple of seconds to catch it.
      const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now().time_since_epoch())
                              .count();
      static int64_t last_force_probe_ms = 0;
      if (now_ms - last_force_probe_ms >= 2000)
      {
        last_force_probe_ms = now_ms;
        if (terminal.refresh_size(/*force_probe=*/true))
        {
          rsz.type = EVENT_RESIZE;
          rsz.resize.width = terminal.get_width();
          rsz.resize.height = terminal.get_height();
        }
      }
    }
  }
  if (rsz.type == EVENT_RESIZE)
  {
    apply_resize(rsz.resize.width, rsz.resize.height);
  }
  // Deliver autocmds coalesced during this drain (BufChange, CursorMoved)
  // before the frame paints, so Lua handlers see the edits and their UI
  // effects land in the same frame.
  if (lua_api)
  {
    lua_api->flush_pending_autocmds();
  }
  // Smooth scrolling runs on the frame clock (neoscroll's line timer, sampled
  // per frame): advance the animation before the paint so this frame shows the
  // new viewport, and repaint while it is still moving.
  if (advance_smooth_scroll(std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now().time_since_epoch())
                                .count()))
  {
    needs_redraw = true;
  }
  // One blink clock for both frontends. The phase comes from cursor_blink_ms and
  // is re-anchored by restart_blink() on every caret move and keystroke, so the
  // caret lands solid and holds through the input pause. The cycle runs from the
  // pause's end, so the first half after a pause is a whole one. The terminal
  // applies the phase by hiding/showing the hardware cursor; the GUI paints the
  // caret from the same flag and needs a repaint to show the flip.
  {
    bool any_carets = false;
    for (const auto &pane : panes)
    {
      if (pane.buffer_id >= 0 && pane.buffer_id < (int)buffers.size()
          && !buffers[(size_t)pane.buffer_id].extra_carets.empty())
      {
        any_carets = true;
        break;
      }
    }
    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
    const int period_ms = std::clamp(config.get_int("cursor_blink_ms", 500), 0, 2000);
    const std::string style = config.get("cursor_style", "block");
    const bool steady = style == "steady_block" || style == "steadyblock" || style == "steady_bar"
                        || style == "steadybar";
    // The cycle runs from the end of the input pause, not from the keystroke
    // that opened it: a future origin reads as the start of a visible half, so
    // the caret stays solid through the pause and then blinks a whole half.
    // Anchored at the keystroke, the first half was the fragment left when the
    // pause ended -- a 300 ms flash at the default 500 ms half.
    const long long phase_origin = std::max(blink_anchor_ms, blink_suspend_until_ms);
    const bool want_visible = jot_ui::blink_phase_visible(now_ms, phase_origin, period_ms, steady);
    if (want_visible != blink_visible)
    {
      blink_visible = want_visible;
      ui->set_cursor_blink_visible(want_visible);
      // The GUI paints the caret only inside a render, and extra carets are
      // painted cells -- both need a repaint. The terminal's hardware cursor
      // needs only its flush (cursor_needs_flush), which costs no grid walk.
      if (gui_mode || any_carets)
      {
        needs_redraw = true;
      }
    }
  }
  // The status line carries two labels that move on their own -- the local
  // clock and the session duration -- and nothing else on screen changes without
  // input. Asking before the paint is what puts a rolled-over label on the frame
  // it rolls over on; a label that would come out identical buys nothing, so an
  // idle editor still paints only when the blink's own clock calls for a cursor
  // flush. See status_time_due_soon in status_line.cpp.
  if (status_time_due_soon())
  {
    needs_redraw = true;
  }
  if (needs_redraw || ui->cursor_needs_flush())
  {
    render();
  }
  // The completion preview appears once the typing pauses, and the frame clock is
  // what reveals it: when the wait that withheld it is over, ask for the frame
  // that draws it. Deliberately after the paint above -- the frame bought here is
  // the next one, which is the first that can draw the preview. A wait that is
  // still running asks for nothing, so typing does not repaint at the frame rate.
  if (lsp_completion_preview_due_soon())
  {
    needs_redraw = true;
  }
  // The occurrence highlight holds back while typing (word_highlight_delay_ms
  // past the buffer's last edit) and is revealed by the frame clock the same
  // way: when the hold ends, ask for the frame that draws it again.
  if (word_highlight_due_soon())
  {
    needs_redraw = true;
  }
}

void Editor::run()
{
  task_queue_ = std::make_unique<TaskQueue>(&event_loop_);

  // The workspace main() opens is opened before this queue exists, so its C++
  // definition scan is still waiting to start (see app/cpp_definitions.cpp).
  // Nothing else requests one at startup, so this is the first and last time
  // the deferred flag has to be looked at here.
  if (cpp_defs_scan_pending)
  {
    cpp_defs_scan_pending = false;
    request_cpp_definitions_scan(false);
  }
  if (web_index_scan_pending)
  {
    web_index_scan_pending = false;
    request_web_index_scan();
  }
  // The queue exists now, which is all WakaTime's cli check and its first
  // today-total fetch were waiting for (see sync_wakatime).
  sync_wakatime();

  event_loop_.prepare();

  // Re-probe terminal size immediately before the first frame. The size
  // may have changed between Editor construction and run() (e.g. if the
  // window was resized during Python init), or it may still be stuck at
  // the 80x24 fallback if no SIGWINCH has been delivered yet. If the size
  // actually changed, UI::resize() will invalidate (emitting one ESC[2J
  // and resetting last_grid). If it didn't change, we still need the
  // first frame to be a full redraw, which UI's full_redraw_pending flag
  // already covers (set in the constructor). Either way, we never call
  // ui->invalidate() here unconditionally - that would emit a second
  // ESC[2J and a second full-redraw pass on top of the resize path.
  if (!gui_mode)
  {
    if (terminal.refresh_size())
    {
      ui->resize(terminal.get_width(), terminal.get_height());
      update_pane_layout();
    }
    terminal.enable_mouse_hover();
    terminal.enable_focus_reporting();
  }
  lsp_mouse_hover_enabled = true;

  needs_redraw = true;

#ifdef _WIN32
  // Windows consoles consume the Ctrl+S keystroke as the output-pause key
  // (the legacy "ExtendedEditKeys" Ctrl+S -> VK_PAUSE mapping): neither
  // Ctrl+S nor Ctrl+Shift+S (both emit control code 0x13) ever reaches the
  // application, so the save shortcut cannot fire there no matter what the
  // app does. Tell the user once which save paths actually work so they
  // stop hitting a frozen terminal. Classic conhost does not grab Ctrl+S
  // when the user has disabled ExtendedEditKeys, hence the WT_SESSION gate.
  {
    const char *wt = std::getenv("WT_SESSION");
    if (wt && wt[0])
    {
      set_message("Windows Terminal takes Ctrl+S (pause): save with :w or enable :autosave");
    }
  }
#endif

#ifndef _WIN32
  int stdin_fd = -1;
  int stdin_flags = 0;
  if (!gui_mode)
  {
    stdin_fd = terminal.get_input_fd();

    stdin_flags = fcntl(stdin_fd, F_GETFL, 0);
    fcntl(stdin_fd, F_SETFL, stdin_flags | O_NONBLOCK);

    event_loop_.watch_fd(stdin_fd,
                         true,
                         false,
                         [this]
                         {
                           constexpr int kMaxDrainPerWake = 256;
                           int drained = 0;
                           // A drag-resize queues one resize event per step;
                           // only the last size in a wake matters, and applying
                           // each one re-fits every pane and re-runs the
                           // UIResize autocmd for nothing. Same coalescing the
                           // GUI pump does.
                           bool resize_pending = false;
                           int resize_w = 0;
                           int resize_h = 0;
                           for (;;)
                           {
                             Event ev = terminal.read_event();
                             if (ev.type == EVENT_REDRAW)
                               break;
                             if (ev.type == EVENT_RESIZE)
                             {
                               resize_pending = true;
                               resize_w = ev.resize.width;
                               resize_h = ev.resize.height;
                             }
                             else
                             {
                               handle_terminal_event(ev);
                             }
                             if (++drained >= kMaxDrainPerWake)
                               break;
                           }
                           if (resize_pending)
                           {
                             apply_resize(resize_w, resize_h);
                           }
                           render_frame();
                         });
  }
#endif

  // GUI frontend: SDL drives input through a high-frequency pump (input-
  // to-paint latency is a few ms; the vsync'd swap paces presentation).
#ifdef JOT_GUI
  if (gui_mode)
  {
    event_loop_.set_timer(4, true, [this] { pump_gui_events(); });
  }
#endif

  int render_ms = std::max(1, 1000 / std::max(1, render_fps));
  event_loop_.set_timer(render_ms, true, [this] { render_frame(); });
  // The LSP housekeeping clock: the hover arm fires a request once the pointer
  // has rested on a token, and the diagnostics hold paints the findings a
  // finished typing pause released. Both are state checks, so one timer is
  // enough.
  event_loop_.set_timer(50,
                        true,
                        [this]
                        {
                          maybe_fire_lsp_mouse_hover();
                          maybe_paint_held_lsp_diagnostics();
                        });

  // Tree-sitter background work, polled on a repeating timer:
  //  - Bundled highlight queries are compiled off the synchronous boot path
  //    (compiling one dlopens the installed parser, which costs tens of
  //    milliseconds per language for large grammars); the Lua load records
  //    the sources on this timer's first fire and a background thread does
  //    the actual compiles.
  //  - Whole-file parses for large buffers run on a background worker so the
  //    first frame paints immediately; finished trees are installed here.
  // Each tick installs finished results and requests a repaint, so startup
  // and the first frames never block on grammar-sized work. The timer stays
  // alive for the editor's lifetime: ticks are cheap state checks when idle,
  // and keeping it permanent means parses queued mid-session (e.g. opening a
  // big file later) get picked up without restart plumbing.
  event_loop_.set_timer(24,
                        true,
                        [this]
                        {
                          if (!lua_api)
                          {
                            return;
                          }
                          lua_api->flush_deferred_treesitter_queries();
                          lua_api->tick_deferred_treesitter_compile();
                          lua_api->tick_deferred_treesitter_parses();
                        });

  // JOT_SAFE_MODE disables every non-essential background timer so
  // we can isolate native crashes from periodic work. The editor
  // stays usable for input, rendering, and undo; only git status,
  // LSP polling, integrated terminal polling, Discord RPC, and
  // auto-save are skipped. Read once on the main thread at start
  // and capture by value into each timer.
  static int safe_mode_cached = -1;
  if (safe_mode_cached < 0)
  {
    const char *env = std::getenv("JOT_SAFE_MODE");
    safe_mode_cached = (env && env[0] && env[0] != '0') ? 1 : 0;
  }
  const bool safe_mode = safe_mode_cached == 1;

  if (!safe_mode && auto_save_enabled && auto_save_interval_ms > 0)
  {
    event_loop_.set_timer(auto_save_interval_ms,
                          true,
                          [this]
                          {
                            last_auto_save_ms =
                                std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now().time_since_epoch())
                                    .count();
                            auto_save_modified_buffers();
                          });
  }

  if (!safe_mode && git_status_active())
  {
    event_loop_.set_timer(1500, true, [this] { refresh_git_status(false); });
  }
  if (!safe_mode)
  {
    // Unsaved declarations need findings too; unchanged buffers and body-only
    // edits must not repeatedly walk the workspace.
    event_loop_.set_timer(600,
                          true,
                          [this]
                          {
                            if (cpp_definitions_dirty_changed())
                            {
                              request_cpp_definitions_scan(false);
                            }
                          });
  }
  if (!safe_mode)
  {
    event_loop_.set_timer(1000, true, [this] { poll_file_tree_changes(); });
  }
  if (!safe_mode)
  {
    event_loop_.set_timer(250, true, [this] { poll_lsp_clients(); });
  }
  if (!safe_mode)
  {
    event_loop_.set_timer(500, true, [this] { poll_debugger_sessions(); });
  }
  if (!safe_mode)
  {
    event_loop_.set_timer(1000,
                          true,
                          [this]
                          {
                            for (auto &term : integrated_terminals)
                            {
                              int fd = term ? term->get_master_fd() : -1;
                              if (term && term->poll_output() && show_integrated_terminal)
                                needs_redraw = true;
                              if (term && fd >= 0 && term->get_master_fd() != fd)
                                event_loop_.unwatch_fd(fd);
                            }
                            // The same fallback for the floating terminal's
                            // shell, which is nobody's dock tab.
                            if (floating_terminal)
                            {
                              const int fd = floating_terminal->get_master_fd();
                              if (floating_terminal->poll_output() && show_floating_terminal)
                                needs_redraw = true;
                              if (fd >= 0 && floating_terminal->get_master_fd() != fd)
                                event_loop_.unwatch_fd(fd);
                            }
                            poll_tree_sitter_installs();
                            poll_lsp_installs();
                          });
  }
  // Discord presence. The timer is always registered when not in safe mode and
  // the enabled check happens inside: registering it conditionally, as this
  // used to, meant toggling the setting only took effect after a restart.
  if (!safe_mode)
  {
    event_loop_.set_timer(1000,
                          true,
                          [this]
                          {
                            long long now_ms =
                                std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now().time_since_epoch())
                                    .count();
                            discord.poll(now_ms);
                          });
  }

  event_loop_.set_timer(50,
                        true,
                        [this]
                        {
                          if (!running)
                            event_loop_.stop();
                        });

  event_loop_.run();

#ifndef _WIN32
  if (!gui_mode)
  {
    fcntl(stdin_fd, F_SETFL, stdin_flags);
  }
#endif
}

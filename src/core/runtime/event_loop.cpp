#include "event_loop.h"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <utility>
#include <unistd.h>

#include "editor.h"

EventLoop::EventLoop() { main_thread_id_ = std::this_thread::get_id(); }

EventLoop::~EventLoop() {
  stop();
  close_all_handles();
  if (loop_initialized_) {
    uv_run(&loop_, UV_RUN_DEFAULT);
    uv_loop_close(&loop_);
    loop_initialized_ = false;
  }
}

void EventLoop::prepare() {
  if (loop_initialized_)
    return;
  int rc = uv_loop_init(&loop_);
  if (rc != 0) {
    throw std::runtime_error("uv_loop_init failed: " +
                             std::string(uv_strerror(rc)));
  }
  loop_initialized_ = true;
  async_.data = this;
  rc = uv_async_init(&loop_, &async_, [](uv_async_t *handle) {
    auto *loop = static_cast<EventLoop *>(handle->data);
    if (loop) {
      loop->drain_posts();
    }
  });
  if (rc != 0) {
    throw std::runtime_error("uv_async_init failed: " +
                             std::string(uv_strerror(rc)));
  }
  async_initialized_ = true;
}

void EventLoop::watch_fd(int fd, bool read, bool write,
                         std::function<void()> on_ready) {
  if (fd < 0 || (!read && !write) || !on_ready) {
    return;
  }
  assert_main_thread();
  prepare();

  unwatch_fd(fd);

  auto watcher = std::make_unique<FdWatcherHandle>();
  watcher->owner = this;
  watcher->fd = fd;
  watcher->on_read = read ? on_ready : nullptr;
  watcher->on_write = write ? on_ready : nullptr;
  watcher->poll.data = watcher.get();

  int rc = uv_poll_init(&loop_, &watcher->poll, fd);
  if (rc != 0) {
    throw std::runtime_error("uv_poll_init failed: " +
                             std::string(uv_strerror(rc)));
  }

  int events = 0;
  if (read)
    events |= UV_READABLE;
  if (write)
    events |= UV_WRITABLE;
  rc = uv_poll_start(&watcher->poll, events, [](uv_poll_t *handle, int status,
                                                int events) {
    auto *watcher = static_cast<FdWatcherHandle *>(handle->data);
    if (!watcher) {
      return;
    }
    if (status < 0) {
      if (watcher->on_error) {
        watcher->on_error();
      }
      return;
    }
    if ((events & UV_READABLE) && watcher->on_read) {
      watcher->on_read();
    }
    if ((events & UV_WRITABLE) && watcher->on_write) {
      watcher->on_write();
    }
  });
  if (rc != 0) {
    throw std::runtime_error("uv_poll_start failed: " +
                             std::string(uv_strerror(rc)));
  }

  watchers_[fd] = std::move(watcher);
}

void EventLoop::unwatch_fd(int fd) {
  assert_main_thread();
  auto it = watchers_.find(fd);
  if (it == watchers_.end()) {
    return;
  }
  uv_poll_stop(&it->second->poll);
  FdWatcherHandle *watcher = it->second.release();
  uv_close(reinterpret_cast<uv_handle_t *>(&watcher->poll),
           EventLoop::close_delete_watcher);
  watchers_.erase(it);
}

bool EventLoop::is_watching_fd(int fd) const {
  return watchers_.find(fd) != watchers_.end();
}

EventLoop::TimerId EventLoop::set_timer(int interval_ms, bool repeat,
                                        TimerCallback cb) {
  assert_main_thread();
  prepare();
  TimerId id = next_timer_id_++;

  auto timer = std::make_unique<TimerHandle>();
  timer->owner = this;
  timer->id = id;
  timer->repeat = repeat;
  timer->callback = std::move(cb);
  timer->timer.data = timer.get();

  int rc = uv_timer_init(&loop_, &timer->timer);
  if (rc != 0) {
    throw std::runtime_error("uv_timer_init failed: " +
                             std::string(uv_strerror(rc)));
  }
  uint64_t timeout = static_cast<uint64_t>(std::max(0, interval_ms));
  uint64_t repeat_ms = repeat ? timeout : 0;
  rc = uv_timer_start(&timer->timer, [](uv_timer_t *handle) {
    auto *timer = static_cast<TimerHandle *>(handle->data);
    if (!timer || !timer->owner) {
      return;
    }
    EventLoop *owner = timer->owner;
    TimerId id = timer->id;
    bool repeat = timer->repeat;
    timer->callback();
    if (!repeat) {
      owner->cancel_timer(id);
    }
  }, timeout, repeat_ms);
  if (rc != 0) {
    throw std::runtime_error("uv_timer_start failed: " +
                             std::string(uv_strerror(rc)));
  }

  timers_[id] = std::move(timer);
  return id;
}

EventLoop::TimerId EventLoop::set_timeout(int delay_ms, TimerCallback cb) {
  return set_timer(delay_ms, false, std::move(cb));
}

void EventLoop::cancel_timer(TimerId id) {
  assert_main_thread();
  auto it = timers_.find(id);
  if (it == timers_.end()) {
    return;
  }
  uv_timer_stop(&it->second->timer);
  TimerHandle *timer = it->second.release();
  uv_close(reinterpret_cast<uv_handle_t *>(&timer->timer),
           EventLoop::close_delete_timer);
  timers_.erase(it);
}

bool EventLoop::watch_path(const std::string &path, FsEventCallback cb,
                           bool recursive) {
  if (path.empty() || !cb) {
    return false;
  }
  assert_main_thread();
  prepare();

  unwatch_path(path);

  auto watcher = std::make_unique<FsEventHandle>();
  watcher->owner = this;
  watcher->path = path;
  watcher->callback = std::move(cb);
  watcher->event.data = watcher.get();

  int rc = uv_fs_event_init(&loop_, &watcher->event);
  if (rc != 0) {
    return false;
  }

  unsigned int flags = recursive ? UV_FS_EVENT_RECURSIVE : 0;
  rc = uv_fs_event_start(
      &watcher->event,
      [](uv_fs_event_t *handle, const char *filename, int events, int status) {
        (void)events;
        auto *watcher = static_cast<FsEventHandle *>(handle->data);
        if (!watcher || !watcher->callback || status < 0) {
          return;
        }
        std::string changed = watcher->path;
        if (filename && filename[0]) {
          changed += "/";
          changed += filename;
        }
        watcher->callback(changed);
      },
      path.c_str(), flags);
  if (rc != 0) {
    uv_close(reinterpret_cast<uv_handle_t *>(&watcher->event),
             EventLoop::close_delete_fs_event);
    watcher.release();
    return false;
  }

  fs_events_[path] = std::move(watcher);
  return true;
}

void EventLoop::unwatch_path(const std::string &path) {
  assert_main_thread();
  auto it = fs_events_.find(path);
  if (it == fs_events_.end()) {
    return;
  }
  uv_fs_event_stop(&it->second->event);
  FsEventHandle *watcher = it->second.release();
  uv_close(reinterpret_cast<uv_handle_t *>(&watcher->event),
           EventLoop::close_delete_fs_event);
  fs_events_.erase(it);
}

void EventLoop::post(PostCallback cb) {
  prepare();
  {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_posts_.push_back(std::move(cb));
  }
  if (async_initialized_) {
    uv_async_send(&async_);
  }
}

void EventLoop::drain_posts() {
  std::vector<PostCallback> posts;
  {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    posts.swap(pending_posts_);
  }
  for (auto &cb : posts)
    cb();
}

bool EventLoop::is_main_thread() const {
  return std::this_thread::get_id() == main_thread_id_;
}

void EventLoop::assert_main_thread() const {
  if (!is_main_thread())
    throw std::runtime_error("EventLoop method called from non-main thread");
}

void EventLoop::run() {
  assert_main_thread();
  prepare();
  running_ = true;
  uv_run(&loop_, UV_RUN_DEFAULT);
  drain_posts();
}

void EventLoop::stop() {
  running_ = false;
  if (loop_initialized_) {
    uv_stop(&loop_);
  }
}

void EventLoop::close_all_handles() {
  for (auto &entry : watchers_) {
    uv_poll_stop(&entry.second->poll);
    if (!uv_is_closing(reinterpret_cast<uv_handle_t *>(&entry.second->poll))) {
      FdWatcherHandle *watcher = entry.second.release();
      uv_close(reinterpret_cast<uv_handle_t *>(&watcher->poll),
               EventLoop::close_delete_watcher);
    }
  }
  watchers_.clear();

  for (auto &entry : timers_) {
    uv_timer_stop(&entry.second->timer);
    if (!uv_is_closing(reinterpret_cast<uv_handle_t *>(&entry.second->timer))) {
      TimerHandle *timer = entry.second.release();
      uv_close(reinterpret_cast<uv_handle_t *>(&timer->timer),
               EventLoop::close_delete_timer);
    }
  }
  timers_.clear();

  for (auto &entry : fs_events_) {
    uv_fs_event_stop(&entry.second->event);
    if (!uv_is_closing(reinterpret_cast<uv_handle_t *>(&entry.second->event))) {
      FsEventHandle *watcher = entry.second.release();
      uv_close(reinterpret_cast<uv_handle_t *>(&watcher->event),
               EventLoop::close_delete_fs_event);
    }
  }
  fs_events_.clear();

  if (async_initialized_ &&
      !uv_is_closing(reinterpret_cast<uv_handle_t *>(&async_))) {
    uv_close(reinterpret_cast<uv_handle_t *>(&async_), nullptr);
  }
  async_initialized_ = false;
}

void EventLoop::close_delete_watcher(uv_handle_t *handle) {
  auto *watcher = static_cast<FdWatcherHandle *>(handle->data);
  delete watcher;
}

void EventLoop::close_delete_timer(uv_handle_t *handle) {
  auto *timer = static_cast<TimerHandle *>(handle->data);
  delete timer;
}

void EventLoop::close_delete_fs_event(uv_handle_t *handle) {
  auto *watcher = static_cast<FsEventHandle *>(handle->data);
  delete watcher;
}

// ── Editor event loop integration ────────────────────────────────────────────

void Editor::handle_terminal_event(const Event &ev) {
  if (ev.type == EVENT_REDRAW) {
    return;
  }

  if (ev.type == EVENT_RESIZE) {
    ui->invalidate();
    ui->resize(ev.resize.width, ev.resize.height);
    update_pane_layout();
    needs_redraw = true;
    return;
  }

  if (ev.type == EVENT_KEY) {
    cancel_lsp_mouse_hover();
    int ch = ev.key.key;
    bool is_ctrl = ev.key.ctrl;
    bool is_shift = ev.key.shift;
    bool is_alt = ev.key.alt;
    int original_ch = ch;

    bool ctrl_q_shortcut =
        (is_ctrl && (ch == 'q' || ch == 'Q' || original_ch == 'q' ||
                     original_ch == 'Q')) ||
        ch == 17 || original_ch == 17;
    if (ctrl_q_shortcut) {
      handle_input('q', is_ctrl, is_shift, is_alt, original_ch);
      return;
    }

    bool toggle_terminal_shortcut =
        (is_ctrl && (ch == '`' || ch == '~' || ch == '\\' || ch == '|')) ||
        (is_ctrl && (ch == 'x' || ch == 'X')) ||
        ch == 24 || original_ch == 24 ||
        ch == 28 || original_ch == 28 || ch == 30 || original_ch == 30;
    if (toggle_terminal_shortcut) {
      toggle_integrated_terminal();
      return;
    }

    if (is_ctrl && ch >= 1 && ch <= 26) {
      ch = ch + 96;
    }

    if (show_menu_bar_dropdown) {
      handle_menu_bar_input(ch);
    } else if (show_context_menu) {
      handle_context_menu_input(ch);
    } else if (show_tree_sitter_status_modal) {
      handle_tree_sitter_status_input(ch);
    } else if (show_command_palette) {
      handle_command_palette(ch);
    } else if (show_search) {
      handle_search_panel(ch, is_ctrl, is_shift, is_alt);
    } else if (telescope.is_active()) {
      handle_telescope(ch);
    } else {
      handle_input(ch, is_ctrl, is_shift, is_alt, original_ch);
    }
    return;
  }

  if (ev.type == EVENT_MOUSE) {
    int button = ev.mouse.button;
    bool is_wheel = (button >= 64 && button <= 67);

    if (is_wheel && !telescope.is_active() && !show_command_palette &&
        !show_search) {
      handle_mouse_input(ev.mouse.x, ev.mouse.y, false, button == 64,
                         button == 65);
    } else {
      struct {
        int x, y;
        int bstate;
        bool ctrl;
        bool shift;
        bool alt;
      } mevent;
      mevent.x = ev.mouse.x;
      mevent.y = ev.mouse.y;
      mevent.ctrl = ev.mouse.ctrl;
      mevent.shift = ev.mouse.shift;
      mevent.alt = ev.mouse.alt;
      int bstate = 0;

      int button_code = ev.mouse.button & 0x03;
      bool is_motion = (ev.mouse.button & 0x20) != 0;

      if (is_motion) {
        if (show_context_menu || button_code == 0 || button_code == 3) {
          bstate = 32;
        } else {
          bstate = 0;
        }
      } else if (ev.mouse.pressed) {
        if (button_code == 0)
          bstate = 1;
        else if (button_code == 2)
          bstate = 3;
        else if (button_code == 1)
          bstate = 4;
        else
          bstate = 1;
      } else if (ev.mouse.released) {
        bstate = 2;
      }

      mevent.bstate = bstate;
      handle_mouse(&mevent);
    }
  }
}

void Editor::render_frame() {
  Event rsz = terminal.check_resize_event();
  if (rsz.type == EVENT_RESIZE) {
    ui->invalidate();
    ui->resize(rsz.resize.width, rsz.resize.height);
    update_pane_layout();
    needs_redraw = true;
  }
  if (needs_redraw) {
    render();
  }
}

void Editor::run() {
  task_queue_ = std::make_unique<TaskQueue>(&event_loop_);

  event_loop_.prepare();

  // Re-probe terminal size immediately before the first frame. The size
  // may have changed between Editor construction and run() (e.g. if the
  // window was resized during Python init), or it may still be stuck at
  // the 80x24 fallback if no SIGWINCH has been delivered yet. If the size
  // actually changed, UI::resize() will invalidate (emitting one ESC[2J
  // and resetting last_grid). If it didn't change, we still need the
  // first frame to be a full redraw, which UI's full_redraw_pending flag
  // already covers (set in the constructor). Either way, we never call
  // ui->invalidate() here unconditionally — that would emit a second
  // ESC[2J and a second full-redraw pass on top of the resize path.
  if (terminal.refresh_size()) {
    ui->resize(terminal.get_width(), terminal.get_height());
    update_pane_layout();
  }
  terminal.enable_mouse_hover();
  lsp_mouse_hover_enabled = true;

  needs_redraw = true;

  int stdin_fd = terminal.get_input_fd();

  int stdin_flags = fcntl(stdin_fd, F_GETFL, 0);
  fcntl(stdin_fd, F_SETFL, stdin_flags | O_NONBLOCK);

  event_loop_.watch_fd(stdin_fd, true, false, [this] {
    constexpr int kMaxDrainPerWake = 256;
    int drained = 0;
    for (;;) {
      Event ev = terminal.read_event();
      if (ev.type == EVENT_REDRAW)
        break;
      handle_terminal_event(ev);
      if (++drained >= kMaxDrainPerWake)
        break;
    }
    render_frame();
  });

  int render_ms = std::max(1, 1000 / std::max(1, render_fps));
  event_loop_.set_timer(render_ms, true, [this] { render_frame(); });
  event_loop_.set_timer(50, true, [this] { maybe_fire_lsp_mouse_hover(); });

  // JOT_SAFE_MODE disables every non-essential background timer so
  // we can isolate native crashes from periodic work. The editor
  // stays usable for input, rendering, and undo; only git status,
  // LSP polling, integrated terminal polling, Discord RPC, and
  // auto-save are skipped. Read once on the main thread at start
  // and capture by value into each timer.
  static int safe_mode_cached = -1;
  if (safe_mode_cached < 0) {
    const char *env = std::getenv("JOT_SAFE_MODE");
    safe_mode_cached = (env && env[0] && env[0] != '0') ? 1 : 0;
  }
  const bool safe_mode = safe_mode_cached == 1;

  if (!safe_mode && auto_save_enabled && auto_save_interval_ms > 0) {
    event_loop_.set_timer(auto_save_interval_ms, true, [this] {
      last_auto_save_ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now().time_since_epoch())
              .count();
      auto_save_modified_buffers();
    });
  }

  if (!safe_mode && git_status_active()) {
    event_loop_.set_timer(1500, true, [this] { refresh_git_status(false); });
  }
  if (!safe_mode) {
    event_loop_.set_timer(1000, true, [this] { poll_file_tree_changes(); });
  }
  if (!safe_mode) {
    event_loop_.set_timer(250, true, [this] { poll_lsp_clients(); });
  }
  if (!safe_mode) {
    event_loop_.set_timer(500, true, [this] { poll_debugger_sessions(); });
  }
  if (!safe_mode) {
    event_loop_.set_timer(1000, true, [this] {
      for (auto &term : integrated_terminals) {
        int fd = term ? term->get_master_fd() : -1;
        if (term && term->poll_output() && show_integrated_terminal)
          needs_redraw = true;
        if (term && fd >= 0 && term->get_master_fd() != fd)
          event_loop_.unwatch_fd(fd);
      }
      poll_tree_sitter_installs();
    });
  }
  if (!safe_mode && config.get_bool("discord_rpc", false)) {
    event_loop_.set_timer(1000, true, [this] {
      long long now_ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now().time_since_epoch())
              .count();
      poll_discord_rpc(now_ms);
    });
  }

  event_loop_.set_timer(50, true, [this] {
    if (!running)
      event_loop_.stop();
  });

  event_loop_.run();

  fcntl(stdin_fd, F_SETFL, stdin_flags);
}

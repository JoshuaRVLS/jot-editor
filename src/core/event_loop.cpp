#include "event_loop.h"
#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>

#include "editor.h"

namespace {

int make_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1)
    return -1;
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

} // namespace

EventLoop::EventLoop() { main_thread_id_ = std::this_thread::get_id(); }

EventLoop::~EventLoop() {
  stop();
  if (epoll_fd_ >= 0)
    ::close(epoll_fd_);
  if (wakeup_read_fd_ >= 0)
    ::close(wakeup_read_fd_);
  if (wakeup_write_fd_ >= 0)
    ::close(wakeup_write_fd_);
}

int64_t EventLoop::now_ms() const {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

void EventLoop::prepare() {
  create_epoll();
  create_wakeup_pipe();
}

void EventLoop::create_epoll() {
  if (epoll_fd_ >= 0)
    return;
  epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
  if (epoll_fd_ < 0)
    throw std::runtime_error("epoll_create1 failed: " +
                             std::string(strerror(errno)));
}

void EventLoop::create_wakeup_pipe() {
  if (wakeup_read_fd_ >= 0)
    return;
  int fds[2];
  if (pipe(fds) < 0)
    throw std::runtime_error("pipe failed: " + std::string(strerror(errno)));

  wakeup_read_fd_ = fds[0];
  wakeup_write_fd_ = fds[1];

  make_nonblocking(wakeup_read_fd_);
  make_nonblocking(wakeup_write_fd_);

  epoll_event ev;
  ev.events = EPOLLIN | EPOLLET;
  ev.data.fd = wakeup_read_fd_;
  epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, wakeup_read_fd_, &ev);
}

void EventLoop::watch_fd(int fd, bool read, bool write,
                         std::function<void()> on_ready) {
  assert_main_thread();

  struct epoll_event ev;
  ev.events = 0;
  if (read)
    ev.events |= EPOLLIN;
  if (write)
    ev.events |= EPOLLOUT;
  ev.data.fd = fd;

  if (watchers_.find(fd) != watchers_.end())
    epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev);
  else
    epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev);

  watchers_[fd] = {fd, read ? on_ready : nullptr,
                   write ? on_ready : nullptr, nullptr};
}

void EventLoop::unwatch_fd(int fd) {
  assert_main_thread();
  epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
  watchers_.erase(fd);
}

EventLoop::TimerId EventLoop::set_timer(int interval_ms, bool repeat,
                                        TimerCallback cb) {
  assert_main_thread();
  TimerId id = next_timer_id_++;
  int64_t fire_at = now_ms() + interval_ms;
  timers_.push_back({id, interval_ms, repeat, std::move(cb), fire_at});
  return id;
}

EventLoop::TimerId EventLoop::set_timeout(int delay_ms, TimerCallback cb) {
  return set_timer(delay_ms, false, std::move(cb));
}

void EventLoop::cancel_timer(TimerId id) {
  assert_main_thread();
  timers_.erase(std::remove_if(timers_.begin(), timers_.end(),
                                [id](const TimerEntry &t) { return t.id == id; }),
                timers_.end());
}

void EventLoop::post(PostCallback cb) {
  {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_posts_.push_back(std::move(cb));
  }
  char byte = 1;
  ::write(wakeup_write_fd_, &byte, 1);
}

void EventLoop::handle_wakeup() {
  char buf[16];
  while (::read(wakeup_read_fd_, buf, sizeof(buf)) > 0) {
  }

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
  create_epoll();
  create_wakeup_pipe();

  running_ = true;

  constexpr int kMaxEvents = 64;
  struct epoll_event events[kMaxEvents];

  while (running_) {
    int64_t now = now_ms();
    int timeout_ms = 16;

    for (const auto &timer : timers_) {
      int64_t delta = timer.next_fire_ms - now;
      if (delta < 0)
        delta = 0;
      if (delta < timeout_ms)
        timeout_ms = static_cast<int>(delta);
    }

    int nfds = epoll_wait(epoll_fd_, events, kMaxEvents, timeout_ms);

    now = now_ms();

    for (int i = 0; i < nfds; i++) {
      int fd = events[i].data.fd;

      if (fd == wakeup_read_fd_) {
        handle_wakeup();
        continue;
      }

      auto it = watchers_.find(fd);
      if (it == watchers_.end())
        continue;

      uint32_t revents = events[i].events;
      if ((revents & (EPOLLIN | EPOLLHUP)) && it->second.on_read)
        it->second.on_read();
      if ((revents & EPOLLOUT) && it->second.on_write)
        it->second.on_write();
      if ((revents & EPOLLERR) && it->second.on_error)
        it->second.on_error();
    }

    std::vector<TimerEntry> current_timers;
    current_timers.swap(timers_);
    std::vector<TimerEntry> surviving;
    for (auto &timer : current_timers) {
      if (now >= timer.next_fire_ms) {
        timer.callback();
        if (timer.repeat) {
          timer.next_fire_ms = now + timer.interval_ms;
          surviving.push_back(std::move(timer));
        }
      } else {
        surviving.push_back(std::move(timer));
      }
    }
    for (auto &t : timers_)
      surviving.push_back(std::move(t));
    timers_.swap(surviving);

    handle_wakeup();
  }

  watchers_.clear();
  timers_.clear();
}

void EventLoop::stop() { running_ = false; }

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
    event_loop_.set_timer(50, true, [this] { poll_lsp_clients(); });
  }
  if (!safe_mode) {
    event_loop_.set_timer(50, true, [this] { poll_debugger_sessions(); });
  }
  if (!safe_mode) {
    event_loop_.set_timer(100, true, [this] {
      for (auto &term : integrated_terminals) {
        if (term && term->poll_output() && show_integrated_terminal)
          needs_redraw = true;
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

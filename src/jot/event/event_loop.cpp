#include "event_loop.h"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

#include "jot/keybind_catalog.h"
#include "editor.h"
#include "jot/lua/api.h"


EventLoop::EventLoop()
{
  main_thread_id_ = std::this_thread::get_id();
}

EventLoop::~EventLoop()
{
  stop();
  close_all_handles();
  if (loop_initialized_)
  {
    uv_run(&loop_, UV_RUN_DEFAULT);
    uv_loop_close(&loop_);
    loop_initialized_ = false;
  }
}

void EventLoop::prepare()
{
  if (loop_initialized_)
    return;
  int rc = uv_loop_init(&loop_);
  if (rc != 0)
  {
    throw std::runtime_error("uv_loop_init failed: " + std::string(uv_strerror(rc)));
  }
  loop_initialized_ = true;
  async_.data = this;
  rc = uv_async_init(&loop_,
                     &async_,
                     [](uv_async_t *handle)
                     {
                       auto *loop = static_cast<EventLoop *>(handle->data);
                       if (loop)
                       {
                         loop->drain_posts();
                       }
                     });
  if (rc != 0)
  {
    throw std::runtime_error("uv_async_init failed: " + std::string(uv_strerror(rc)));
  }
  async_initialized_ = true;
}

uv_loop_t *EventLoop::raw_loop()
{
  prepare();
  return &loop_;
}

void EventLoop::watch_fd(int fd, bool read, bool write, std::function<void()> on_ready)
{
  if (fd < 0 || (!read && !write) || !on_ready)
  {
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
  if (rc != 0)
  {
    throw std::runtime_error("uv_poll_init failed: " + std::string(uv_strerror(rc)));
  }

  int events = 0;
  if (read)
    events |= UV_READABLE;
  if (write)
    events |= UV_WRITABLE;
  rc = uv_poll_start(&watcher->poll,
                     events,
                     [](uv_poll_t *handle, int status, int events)
                     {
                       auto *watcher = static_cast<FdWatcherHandle *>(handle->data);
                       if (!watcher)
                       {
                         return;
                       }
                       if (status < 0)
                       {
                         if (watcher->on_error)
                         {
                           watcher->on_error();
                         }
                         return;
                       }
                       if ((events & UV_READABLE) && watcher->on_read)
                       {
                         watcher->on_read();
                       }
                       if ((events & UV_WRITABLE) && watcher->on_write)
                       {
                         watcher->on_write();
                       }
                     });
  if (rc != 0)
  {
    throw std::runtime_error("uv_poll_start failed: " + std::string(uv_strerror(rc)));
  }

  watchers_[fd] = std::move(watcher);
}

void EventLoop::unwatch_fd(int fd)
{
  assert_main_thread();
  auto it = watchers_.find(fd);
  if (it == watchers_.end())
  {
    return;
  }
  uv_poll_stop(&it->second->poll);
  FdWatcherHandle *watcher = it->second.release();
  uv_close(reinterpret_cast<uv_handle_t *>(&watcher->poll), EventLoop::close_delete_watcher);
  watchers_.erase(it);
}

bool EventLoop::is_watching_fd(int fd) const
{
  return watchers_.find(fd) != watchers_.end();
}

EventLoop::TimerId EventLoop::set_timer(int interval_ms, bool repeat, TimerCallback cb)
{
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
  if (rc != 0)
  {
    throw std::runtime_error("uv_timer_init failed: " + std::string(uv_strerror(rc)));
  }
  uint64_t timeout = static_cast<uint64_t>(std::max(0, interval_ms));
  uint64_t repeat_ms = repeat ? timeout : 0;
  rc = uv_timer_start(
      &timer->timer,
      [](uv_timer_t *handle)
      {
        auto *timer = static_cast<TimerHandle *>(handle->data);
        if (!timer || !timer->owner)
        {
          return;
        }
        EventLoop *owner = timer->owner;
        TimerId id = timer->id;
        bool repeat = timer->repeat;
        timer->callback();
        if (!repeat)
        {
          owner->cancel_timer(id);
        }
      },
      timeout,
      repeat_ms);
  if (rc != 0)
  {
    throw std::runtime_error("uv_timer_start failed: " + std::string(uv_strerror(rc)));
  }

  timers_[id] = std::move(timer);
  return id;
}

EventLoop::TimerId EventLoop::set_timeout(int delay_ms, TimerCallback cb)
{
  return set_timer(delay_ms, false, std::move(cb));
}

void EventLoop::cancel_timer(TimerId id)
{
  assert_main_thread();
  auto it = timers_.find(id);
  if (it == timers_.end())
  {
    return;
  }
  uv_timer_stop(&it->second->timer);
  TimerHandle *timer = it->second.release();
  uv_close(reinterpret_cast<uv_handle_t *>(&timer->timer), EventLoop::close_delete_timer);
  timers_.erase(it);
}

bool EventLoop::watch_path(const std::string &path, FsEventCallback cb, bool recursive)
{
  if (path.empty() || !cb)
  {
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
  if (rc != 0)
  {
    return false;
  }

  unsigned int flags = recursive ? UV_FS_EVENT_RECURSIVE : 0;
  rc = uv_fs_event_start(
      &watcher->event,
      [](uv_fs_event_t *handle, const char *filename, int events, int status)
      {
        (void)events;
        auto *watcher = static_cast<FsEventHandle *>(handle->data);
        if (!watcher || !watcher->callback || status < 0)
        {
          return;
        }
        std::string changed = watcher->path;
        if (filename && filename[0])
        {
          changed += "/";
          changed += filename;
        }
        watcher->callback(changed);
      },
      path.c_str(),
      flags);
  if (rc != 0)
  {
    uv_close(reinterpret_cast<uv_handle_t *>(&watcher->event), EventLoop::close_delete_fs_event);
    watcher.release();
    return false;
  }

  fs_events_[path] = std::move(watcher);
  return true;
}

void EventLoop::unwatch_path(const std::string &path)
{
  assert_main_thread();
  auto it = fs_events_.find(path);
  if (it == fs_events_.end())
  {
    return;
  }
  uv_fs_event_stop(&it->second->event);
  FsEventHandle *watcher = it->second.release();
  uv_close(reinterpret_cast<uv_handle_t *>(&watcher->event), EventLoop::close_delete_fs_event);
  fs_events_.erase(it);
}

void EventLoop::post(PostCallback cb)
{
  prepare();
  {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_posts_.push_back(std::move(cb));
  }
  if (async_initialized_)
  {
    uv_async_send(&async_);
  }
}

void EventLoop::drain_posts()
{
  std::vector<PostCallback> posts;
  {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    posts.swap(pending_posts_);
  }
  for (auto &cb : posts)
    cb();
}

bool EventLoop::is_main_thread() const
{
  return std::this_thread::get_id() == main_thread_id_;
}

void EventLoop::assert_main_thread() const
{
  if (!is_main_thread())
    throw std::runtime_error("EventLoop method called from non-main thread");
}

void EventLoop::run()
{
  assert_main_thread();
  prepare();
  running_ = true;
  uv_run(&loop_, UV_RUN_DEFAULT);
  drain_posts();
}

void EventLoop::stop()
{
  running_ = false;
  if (loop_initialized_)
  {
    uv_stop(&loop_);
  }
}

void EventLoop::close_all_handles()
{
  for (auto &entry : watchers_)
  {
    uv_poll_stop(&entry.second->poll);
    if (!uv_is_closing(reinterpret_cast<uv_handle_t *>(&entry.second->poll)))
    {
      FdWatcherHandle *watcher = entry.second.release();
      uv_close(reinterpret_cast<uv_handle_t *>(&watcher->poll), EventLoop::close_delete_watcher);
    }
  }
  watchers_.clear();

  for (auto &entry : timers_)
  {
    uv_timer_stop(&entry.second->timer);
    if (!uv_is_closing(reinterpret_cast<uv_handle_t *>(&entry.second->timer)))
    {
      TimerHandle *timer = entry.second.release();
      uv_close(reinterpret_cast<uv_handle_t *>(&timer->timer), EventLoop::close_delete_timer);
    }
  }
  timers_.clear();

  for (auto &entry : fs_events_)
  {
    uv_fs_event_stop(&entry.second->event);
    if (!uv_is_closing(reinterpret_cast<uv_handle_t *>(&entry.second->event)))
    {
      FsEventHandle *watcher = entry.second.release();
      uv_close(reinterpret_cast<uv_handle_t *>(&watcher->event), EventLoop::close_delete_fs_event);
    }
  }
  fs_events_.clear();

  if (async_initialized_ && !uv_is_closing(reinterpret_cast<uv_handle_t *>(&async_)))
  {
    uv_close(reinterpret_cast<uv_handle_t *>(&async_), nullptr);
  }
  async_initialized_ = false;
}

void EventLoop::close_delete_watcher(uv_handle_t *handle)
{
  auto *watcher = static_cast<FdWatcherHandle *>(handle->data);
  delete watcher;
}

void EventLoop::close_delete_timer(uv_handle_t *handle)
{
  auto *timer = static_cast<TimerHandle *>(handle->data);
  delete timer;
}

void EventLoop::close_delete_fs_event(uv_handle_t *handle)
{
  auto *watcher = static_cast<FsEventHandle *>(handle->data);
  delete watcher;
}

// Editor Event Loop


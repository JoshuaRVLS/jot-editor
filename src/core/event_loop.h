#ifndef EVENT_LOOP_H
#define EVENT_LOOP_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <uv.h>

class EventLoop {
public:
  struct FdWatcher {
    int fd;
    std::function<void()> on_read;
    std::function<void()> on_write;
    std::function<void()> on_error;
  };

  using TimerCallback = std::function<void()>;
  using PostCallback = std::function<void()>;
  using FsEventCallback = std::function<void(const std::string &path)>;
  using TimerId = uint64_t;

  EventLoop();
  ~EventLoop();

  EventLoop(const EventLoop &) = delete;
  EventLoop &operator=(const EventLoop &) = delete;

  void prepare();
  void run();
  void stop();

  void watch_fd(int fd, bool read = true, bool write = false,
                std::function<void()> on_ready = nullptr);
  void unwatch_fd(int fd);
  bool is_watching_fd(int fd) const;

  TimerId set_timer(int interval_ms, bool repeat, TimerCallback cb);
  void cancel_timer(TimerId id);
  TimerId set_timeout(int delay_ms, TimerCallback cb);

  bool watch_path(const std::string &path, FsEventCallback cb,
                  bool recursive = false);
  void unwatch_path(const std::string &path);

  void post(PostCallback cb);

  bool is_main_thread() const;
  void assert_main_thread() const;

private:
  struct FdWatcherHandle {
    EventLoop *owner = nullptr;
    int fd = -1;
    uv_poll_t poll {};
    std::function<void()> on_read;
    std::function<void()> on_write;
    std::function<void()> on_error;
  };

  struct TimerHandle {
    EventLoop *owner = nullptr;
    TimerId id = 0;
    bool repeat = false;
    uv_timer_t timer {};
    TimerCallback callback;
  };

  struct FsEventHandle {
    EventLoop *owner = nullptr;
    std::string path;
    uv_fs_event_t event {};
    FsEventCallback callback;
  };

  void drain_posts();
  void close_all_handles();
  static void close_delete_watcher(uv_handle_t *handle);
  static void close_delete_timer(uv_handle_t *handle);
  static void close_delete_fs_event(uv_handle_t *handle);

  std::atomic<bool> running_{false};
  std::thread::id main_thread_id_;
  uv_loop_t loop_ {};
  bool loop_initialized_ = false;
  uv_async_t async_ {};
  bool async_initialized_ = false;

  std::unordered_map<int, std::unique_ptr<FdWatcherHandle>> watchers_;
  std::unordered_map<TimerId, std::unique_ptr<TimerHandle>> timers_;
  std::unordered_map<std::string, std::unique_ptr<FsEventHandle>> fs_events_;
  TimerId next_timer_id_ = 1;
  std::vector<PostCallback> pending_posts_;
  std::mutex pending_mutex_;
};

#endif

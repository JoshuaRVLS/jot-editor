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

  TimerId set_timer(int interval_ms, bool repeat, TimerCallback cb);
  void cancel_timer(TimerId id);
  TimerId set_timeout(int delay_ms, TimerCallback cb);

  void post(PostCallback cb);

  bool is_main_thread() const;
  void assert_main_thread() const;

  int wakeup_read_fd() const { return wakeup_read_fd_; }

  struct TimerEntry {
    TimerId id;
    int interval_ms;
    bool repeat;
    TimerCallback callback;
    int64_t next_fire_ms;
  };

private:
  void create_epoll();
  void create_wakeup_pipe();
  void handle_wakeup();

  int epoll_fd_ = -1;
  int wakeup_read_fd_ = -1;
  int wakeup_write_fd_ = -1;

  std::atomic<bool> running_{false};
  std::thread::id main_thread_id_;

  std::unordered_map<int, FdWatcher> watchers_;
  std::vector<TimerEntry> timers_;
  TimerId next_timer_id_ = 1;
  std::vector<PostCallback> pending_posts_;
  std::mutex pending_mutex_;

  int64_t now_ms() const;
};

#endif

#ifndef TASK_QUEUE_H
#define TASK_QUEUE_H

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class EventLoop;

struct Task {
  std::function<void()> work;
  std::function<void()> on_complete;
};

class TaskQueue {
public:
  explicit TaskQueue(EventLoop *loop, int num_workers = 0);
  ~TaskQueue();

  TaskQueue(const TaskQueue &) = delete;
  TaskQueue &operator=(const TaskQueue &) = delete;

  void submit(std::function<void()> work, std::function<void()> on_complete);

  template <typename T>
  void submit_val(std::function<T()> work,
                  std::function<void(T)> on_complete) {
    auto shared = std::make_shared<T>();
    submit(
        [work = std::move(work), shared]() mutable { *shared = work(); },
        [shared, on_complete = std::move(on_complete)]() mutable {
          on_complete(std::move(*shared));
        });
  }

  void shutdown();
  int worker_count() const { return num_workers_; }

private:
  void worker_loop();

  EventLoop *loop_;
  int num_workers_;

  std::queue<Task> task_queue_;
  std::mutex queue_mutex_;
  std::condition_variable queue_cv_;
  std::atomic<bool> running_{true};
  std::vector<std::thread> workers_;
};

#endif

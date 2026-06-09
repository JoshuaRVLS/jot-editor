#include "task_queue.h"
#include "event_loop.h"

TaskQueue::TaskQueue(EventLoop *loop, int num_workers)
    : loop_(loop), num_workers_(num_workers) {
  if (num_workers_ <= 0)
    num_workers_ = std::max(1u, std::thread::hardware_concurrency());
  if (num_workers_ > 8)
    num_workers_ = 8;

  for (int i = 0; i < num_workers_; i++)
    workers_.emplace_back(&TaskQueue::worker_loop, this);
}

TaskQueue::~TaskQueue() { shutdown(); }

void TaskQueue::submit(std::function<void()> work,
                       std::function<void()> on_complete) {
  Task task{std::move(work), std::move(on_complete)};
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    task_queue_.push(std::move(task));
  }
  queue_cv_.notify_one();
}

void TaskQueue::shutdown() {
  running_ = false;
  queue_cv_.notify_all();
  for (auto &worker : workers_) {
    if (worker.joinable())
      worker.join();
  }
  workers_.clear();
}

void TaskQueue::worker_loop() {
  while (running_) {
    Task task;
    {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      queue_cv_.wait(lock, [this] {
        return !running_ || !task_queue_.empty();
      });
      if (!running_ && task_queue_.empty())
        return;
      task = std::move(task_queue_.front());
      task_queue_.pop();
    }

    task.work();

    if (task.on_complete && loop_)
      loop_->post(std::move(task.on_complete));
  }
}

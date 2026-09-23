/**
 * @file threading.cpp
 * @brief Implementation of parallel_for() and BackgroundWorker.
 */
#include "core/threading.h"

#include "util/contract.h"

#include <algorithm>
#include <exception>
#include <utility>
#include <vector>

namespace cc {

int worker_thread_count(const ThreadingConfig& cfg)
{
  CC_REQUIRE(cfg.max_workers >= 1, return 1);
  CC_REQUIRE(cfg.fallback_workers >= 1, return 1);
  const unsigned hw = std::thread::hardware_concurrency();
  const int detected = hw == 0 ? cfg.fallback_workers : static_cast<int>(std::min<unsigned>(hw, 1u << 16));
  return std::clamp(detected, 1, cfg.max_workers);
}

void parallel_for(int n, const ThreadingConfig& cfg, const std::function<void(int, int)>& fn)
{
  CC_REQUIRE(static_cast<bool>(fn), return);
  CC_REQUIRE(cfg.min_rows_per_worker >= 1, return);
  if(n <= 0)
  {
    return;
  }
  const int threads = std::min(worker_thread_count(cfg), std::max(1, n / cfg.min_rows_per_worker));
  if(threads <= 1)
  {
    fn(0, n);
    return;
  }
  std::vector<std::thread> pool;
  pool.reserve(static_cast<std::size_t>(threads - 1));
  const int chunk = (n + threads - 1) / threads;
  for(int t = 1; t < threads; ++t)
  {
    const int begin = t * chunk;
    const int end = std::min(n, begin + chunk);
    if(begin >= end)
    {
      break;
    }
    pool.emplace_back([begin, end, &fn]() { fn(begin, end); });
  }
  fn(0, std::min(n, chunk));
  for(std::thread& th : pool)
  {
    th.join();
  }
}

BackgroundWorker::BackgroundWorker() { thread_ = std::thread([this]() { loop(); }); }

BackgroundWorker::~BackgroundWorker()
{
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    quit_ = true;
    progress_.cancel = true;
  }
  cv_.notify_all();
  if(thread_.joinable())
  {
    thread_.join();
  }
}

void BackgroundWorker::submit(const std::string& label, Job job)
{
  CC_REQUIRE(static_cast<bool>(job), return);
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    queue_.push_back(Pending{label, std::move(job)});
    active_++;
  }
  cv_.notify_one();
}

std::string BackgroundWorker::current_label()
{
  const std::lock_guard<std::mutex> lock(mutex_);
  return label_;
}

void BackgroundWorker::cancel_all()
{
  const std::lock_guard<std::mutex> lock(mutex_);
  active_ -= static_cast<int>(queue_.size());
  queue_.clear();
  progress_.cancel = true;
}

void BackgroundWorker::poll()
{
  std::deque<std::function<void()>> done;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    done.swap(done_);
  }
  for(const std::function<void()>& completion : done)
  {
    if(completion)
    {
      completion();
    }
  }
}

bool BackgroundWorker::take_next(std::string& label, Job& job)
{
  std::unique_lock<std::mutex> lock(mutex_);
  cv_.wait(lock, [this]() { return quit_.load() || !queue_.empty(); });
  if(quit_)
  {
    return false;
  }
  label = std::move(queue_.front().label);
  job = std::move(queue_.front().job);
  queue_.pop_front();
  label_ = label;
  progress_.cancel = false;
  progress_.fraction = 0.0f;
  return true;
}

void BackgroundWorker::loop()
{
  /* Intentionally unbounded: this is the worker's scheduler loop; it ends when quit_ is set. */
  for(;;)
  {
    std::string label;
    Job job;
    if(!take_next(label, job))
    {
      return;
    }
    std::function<void()> completion;
    try
    {
      completion = job(progress_);
    }
    catch(const std::exception&)
    {
      completion = nullptr; /* jobs report errors through their own results */
    }
    {
      const std::lock_guard<std::mutex> lock(mutex_);
      done_.push_back(std::move(completion));
      label_.clear();
      active_--;
    }
  }
}

} // namespace cc

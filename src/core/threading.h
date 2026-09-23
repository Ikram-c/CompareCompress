/**
 * @file threading.h
 * @brief A row-parallel loop and a single background worker.
 *
 * The worker keeps the UI thread free while images are decoded, URLs are
 * fetched, ffmpeg runs or metrics are computed.  Results are handed back to
 * the main thread through BackgroundWorker::poll(), so all OpenGL work stays
 * on one thread.
 *
 * Power of 10 note: jobs and completions are std::function objects.  The rule
 * against function pointers is relaxed here because the alternative (a class
 * hierarchy per job) would add far more code than it removes risk; every job
 * is still submitted from one place (App) and runs on one thread.
 */
#pragma once

#include "core/config.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace cc {

/**
 * @brief Number of worker threads parallel_for() may use.
 * @param cfg `threading` section: the cap and the fallback for unknown core counts.
 * @return A count in [1, cfg.max_workers].
 */
[[nodiscard]] int worker_thread_count(const ThreadingConfig& cfg);

/**
 * @brief Calls `fn(begin, end)` on disjoint index ranges that cover [0, n).
 *
 * Ranges are at least `cfg.min_rows_per_worker` long; small `n` runs on the
 * calling thread.  Blocks until every range is done.
 * @param n Number of indices (rows); nothing happens for n <= 0.
 * @param cfg `threading` section.
 * @param fn Callable invoked from several threads at once.
 */
void parallel_for(int n, const ThreadingConfig& cfg, const std::function<void(int, int)>& fn);

/** @brief Progress shared between a job and the UI; every member is thread-safe. */
struct Progress
{
  std::atomic<float> fraction{0.0f}; /**< 0..1, or 0 when unknown */
  std::atomic<bool> cancel{false};   /**< set by the UI; jobs poll it */
  std::mutex mutex;
  std::string message;

  /** @brief Sets fraction and message together. */
  void set(float f, const std::string& msg)
  {
    fraction.store(f);
    const std::lock_guard<std::mutex> lock(mutex);
    message = msg;
  }

  /** @brief Snapshot of the message. */
  std::string text()
  {
    const std::lock_guard<std::mutex> lock(mutex);
    return message;
  }
};

/**
 * @brief One worker thread that runs jobs in submission order.
 *
 * A job runs off-thread and returns a completion that poll() runs on the main
 * thread, so the job can hand results to the UI without any shared state.
 */
class BackgroundWorker
{
public:
  /** @brief A job: runs off-thread, returns the completion to run on the main thread. */
  using Job = std::function<std::function<void()>(Progress&)>;

  BackgroundWorker();
  ~BackgroundWorker();
  BackgroundWorker(const BackgroundWorker&) = delete;
  BackgroundWorker& operator=(const BackgroundWorker&) = delete;

  /**
   * @brief Queues a job.
   * @param label Shown in the status bar while the job runs.
   * @param job The job; an exception escaping it is swallowed and the job yields no completion.
   */
  void submit(const std::string& label, Job job);

  /** @brief Runs the completions of finished jobs.  Call once per frame from the main thread. */
  void poll();

  /** @brief True while jobs are queued or running. */
  [[nodiscard]] bool busy() const { return active_.load() > 0; }

  /** @brief Label of the running job, or "" when idle. */
  [[nodiscard]] std::string current_label();

  /** @brief Progress of the running job. */
  [[nodiscard]] Progress& progress() { return progress_; }

  /** @brief Drops queued jobs and asks the running one to stop. */
  void cancel_all();

private:
  /** @brief Thread body: runs until the destructor sets quit_. */
  void loop();

  /** @brief Takes the next job off the queue; returns false when quitting. */
  bool take_next(std::string& label, Job& job);

  struct Pending
  {
    std::string label;
    Job job;
  };
  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<Pending> queue_;
  std::deque<std::function<void()>> done_;
  std::atomic<int> active_{0};
  std::atomic<bool> quit_{false};
  std::string label_;
  Progress progress_;
  std::thread thread_; /**< last: it must start after every other member is constructed */
};

} // namespace cc

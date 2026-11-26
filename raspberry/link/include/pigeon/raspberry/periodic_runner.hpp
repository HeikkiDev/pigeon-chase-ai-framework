#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace pigeon::raspberry {

/// One thread that calls a piece of work, then waits one period, until it is
/// stopped (ADR-0023). The reader thread of `ConcurrentActuatorLink`.
///
/// The wait is on a condition variable, so `stop()` is immediate even in the
/// middle of a period, and it joins the thread: once it returns, the work is not
/// running and never will again. The destructor stops it.
///
/// The work runs on the runner's thread and must not throw.
class PeriodicRunner {
 public:
  /// Starts the thread at once; the work runs first, then each period after.
  PeriodicRunner(std::chrono::milliseconds period, std::function<void()> work);

  PeriodicRunner(const PeriodicRunner&) = delete;
  PeriodicRunner& operator=(const PeriodicRunner&) = delete;
  PeriodicRunner(PeriodicRunner&&) = delete;
  PeriodicRunner& operator=(PeriodicRunner&&) = delete;

  ~PeriodicRunner();

  /// Stops the thread and waits for it to finish. Harmless if already stopped.
  void stop() noexcept;

 private:
  void run();

  std::chrono::milliseconds period_;
  std::function<void()> work_;
  std::mutex mutex_;
  std::condition_variable wake_;
  bool stopping_{false};
  std::thread thread_;  // last: it starts once everything above exists
};

}  // namespace pigeon::raspberry

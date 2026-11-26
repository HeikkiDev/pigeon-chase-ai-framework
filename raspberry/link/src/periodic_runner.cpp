#include "pigeon/raspberry/periodic_runner.hpp"

#include <chrono>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

namespace pigeon::raspberry {

PeriodicRunner::PeriodicRunner(std::chrono::milliseconds period, std::function<void()> work)
    : period_{period}, work_{std::move(work)}, thread_{[this] { run(); }} {}

PeriodicRunner::~PeriodicRunner() { stop(); }

void PeriodicRunner::stop() noexcept {
  {
    const std::scoped_lock lock{mutex_};
    stopping_ = true;
  }
  wake_.notify_all();
  if (thread_.joinable()) {
    thread_.join();
  }
}

void PeriodicRunner::run() {
  std::unique_lock lock{mutex_};
  while (!stopping_) {
    lock.unlock();
    work_();  // not under the runner's lock: stop() must never wait for the work's
    lock.lock();
    wake_.wait_for(lock, period_, [this] { return stopping_; });
  }
}

}  // namespace pigeon::raspberry

#include "pigeon/raspberry/steady_clock.hpp"

#include <chrono>

namespace pigeon::raspberry {

pigeon::core::MonotonicTimePoint SteadyClock::now() const noexcept {
  // Truncated to milliseconds, the resolution of MonotonicTimePoint. Truncation
  // keeps the order of readings, so the result never goes backwards either.
  const auto since_epoch = std::chrono::steady_clock::now().time_since_epoch();
  pigeon::core::MonotonicTimePoint point;
  point.since_epoch = std::chrono::duration_cast<std::chrono::milliseconds>(since_epoch);
  return point;
}

}  // namespace pigeon::raspberry

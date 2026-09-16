#pragma once

// Verifies: REQ-DEV-002 (support code — a hand-advanced clock so that no test
// ever reads the wall clock), used by REQ-SAF-001, REQ-SAF-005, REQ-SAF-007.

#include <chrono>

#include "pigeon/core/clock.hpp"

namespace pigeon::test_support {

/// A `MonotonicClock` that only ever moves when a test moves it.
///
/// The whole point of `REQ-SAF-005` being driven by an injected clock is that
/// the cool-down and the rate window can be crossed in a test without waiting
/// two seconds and without a timing tolerance (`REQ-DEV-002`, ADR-0005).
class ManualClock final : public pigeon::core::MonotonicClock {
 public:
  ManualClock() = default;

  explicit ManualClock(std::chrono::milliseconds start) : now_{start} {}

  [[nodiscard]] pigeon::core::MonotonicTimePoint now() const noexcept override {
    return pigeon::core::MonotonicTimePoint{now_};
  }

  /// Move the timeline forward. Never backwards: monotonicity is part of the
  /// interface's contract, so the double must honour it too.
  void advance_by(std::chrono::milliseconds delta) {
    if (delta.count() > 0) {
      now_ += delta;
    }
  }

 private:
  std::chrono::milliseconds now_{0};
};

}  // namespace pigeon::test_support

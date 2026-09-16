#pragma once

#include <chrono>

namespace pigeon::core {

/// A point on a monotonic timeline.
///
/// Monotonic means: never decreases, never jumps, unaffected by anyone setting
/// the system's date. The epoch is unspecified and meaningless — only
/// differences between two of these values have any interpretation.
///
/// Milliseconds are the resolution because the only intervals measured in this
/// system are a 2 s cool-down and a one-minute rate window (`REQ-SAF-005`).
struct MonotonicTimePoint {
  /// Time since an unspecified, fixed epoch.
  std::chrono::milliseconds since_epoch{};

  [[nodiscard]] constexpr auto operator<=>(const MonotonicTimePoint&) const = default;
};

/// The one place `core/` is allowed to learn what time it is (ADR-0005).
///
/// Time exists in this system in exactly two places: the firmware's own burst
/// timer, and the engagement rate limiter. The target state machine counts
/// frames and needs none of this — do not inject a clock into it.
///
/// The interface exists so that the rate limiter can be tested by advancing a
/// simulated clock rather than by sleeping. A test that sleeps is a test that
/// is slow when it passes and flaky when it fails (`REQ-DEV-002`).
///
/// Implementations shall be monotonic and shall never throw.
class MonotonicClock {
 public:
  MonotonicClock() = default;
  MonotonicClock(const MonotonicClock&) = delete;
  MonotonicClock& operator=(const MonotonicClock&) = delete;
  MonotonicClock(MonotonicClock&&) = delete;
  MonotonicClock& operator=(MonotonicClock&&) = delete;
  virtual ~MonotonicClock() = default;

  /// The current point on the monotonic timeline.
  ///
  /// Successive calls never return a smaller value than an earlier one.
  [[nodiscard]] virtual MonotonicTimePoint now() const noexcept = 0;
};

}  // namespace pigeon::core

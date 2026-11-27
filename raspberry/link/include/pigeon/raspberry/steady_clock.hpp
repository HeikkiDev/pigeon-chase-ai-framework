#pragma once

#include "pigeon/core/clock.hpp"

namespace pigeon::raspberry {

/// The real `MonotonicClock`, for the Raspberry Pi (ADR-0005, ADR-0021
/// follow-up 3).
///
/// `core/` and the host link never read the time themselves; they are handed a
/// `MonotonicClock`. In tests that is a hand-advanced clock; on the device it is
/// this one, over `std::chrono::steady_clock`, which never goes backwards and is
/// unaffected by anyone setting the date. The epoch is unspecified: only
/// differences between two readings mean anything.
///
/// Holds no state, so any number of instances share one timeline.
class SteadyClock final : public pigeon::core::MonotonicClock {
 public:
  SteadyClock() = default;

  [[nodiscard]] pigeon::core::MonotonicTimePoint now() const noexcept override;
};

}  // namespace pigeon::raspberry

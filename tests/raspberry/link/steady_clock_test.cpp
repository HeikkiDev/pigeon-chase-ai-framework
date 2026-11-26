// Verifies: REQ-DEV-002, REQ-SAF-005, REQ-COM-003
//
// The real `MonotonicClock` the Raspberry Pi runs on (ADR-0021 follow-up 3,
// ADR-0005). `core/` and the link read time only through that interface, so
// every timing in the system (the cool-down, the rate window, the link's reply and
// silence timeouts) is as sound as this clock's one promise: it never goes
// backwards, whatever happens to the date.
//
// These tests read the real clock but assert nothing about how much time passes,
// so their result does not depend on the machine or its load (REQ-DEV-002).

#include <gtest/gtest.h>

#include <chrono>
#include <type_traits>

#include "pigeon/core/clock.hpp"
#include "pigeon/raspberry/steady_clock.hpp"

namespace {

using pigeon::core::MonotonicClock;
using pigeon::core::MonotonicTimePoint;
using pigeon::raspberry::SteadyClock;

TEST(SteadyClock, IsTheMonotonicClockTheRestOfTheSystemTakes) {
  static_assert(std::is_base_of_v<MonotonicClock, SteadyClock>);

  const SteadyClock clock;
  const MonotonicClock& as_core_sees_it = clock;

  static_assert(noexcept(as_core_sees_it.now()));
  EXPECT_GE(as_core_sees_it.now(), MonotonicTimePoint{});
}

TEST(SteadyClock, NeverGoesBackwards) {
  const SteadyClock clock;
  MonotonicTimePoint previous = clock.now();

  for (int i = 0; i < 100000; ++i) {
    const MonotonicTimePoint current = clock.now();
    ASSERT_GE(current, previous) << "after " << i << " readings";
    previous = current;
  }
}

TEST(SteadyClock, MovesForward) {
  // A clock stuck at one value never goes backwards either, so that alone proves
  // too little. This reads until the value changes. How long that takes is not
  // asserted; the bound only stops a stuck clock from hanging the suite.
  const SteadyClock clock;
  const MonotonicTimePoint start = clock.now();
  const auto give_up = std::chrono::steady_clock::now() + std::chrono::seconds{10};

  MonotonicTimePoint latest = start;
  while (latest == start && std::chrono::steady_clock::now() < give_up) {
    latest = clock.now();
  }

  EXPECT_GT(latest, start);
}

TEST(SteadyClock, TwoClocksShareOneTimeline) {
  // The link and core may be given different instances; a reading from one must
  // never be ahead of a later reading from the other.
  const SteadyClock first;
  const SteadyClock second;

  const MonotonicTimePoint earlier = first.now();
  const MonotonicTimePoint later = second.now();

  EXPECT_GE(later, earlier);
}

}  // namespace

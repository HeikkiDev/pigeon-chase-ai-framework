// Verifies: REQ-COM-003, REQ-COM-002, REQ-DEV-002
//
// The reader thread of ADR-0023. It calls one piece of work every period, and
// stopping it is immediate and joins the thread, so that nothing runs beside the
// link while it shuts down. These tests use a real thread, so they assert only
// what holds whatever the machine's load: the work is called, and it stops. The
// time bounds below only stop a failing test from hanging (REQ-DEV-002).

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <memory>
#include <thread>

#include "pigeon/raspberry/periodic_runner.hpp"

namespace {

using pigeon::raspberry::PeriodicRunner;
using std::chrono::milliseconds;

/// Waits until `done()` or a generous bound; true if `done()` came first.
template <typename Condition>
bool eventually(Condition done) {
  const auto give_up = std::chrono::steady_clock::now() + std::chrono::seconds{10};
  while (!done()) {
    if (std::chrono::steady_clock::now() >= give_up) {
      return false;
    }
    std::this_thread::sleep_for(milliseconds{1});
  }
  return true;
}

TEST(PeriodicRunner, CallsItsWorkAgainAndAgain) {
  std::atomic<int> calls{0};
  PeriodicRunner runner{milliseconds{1}, [&] { ++calls; }};

  EXPECT_TRUE(eventually([&] { return calls.load() >= 3; }));
}

TEST(PeriodicRunner, NothingRunsOnceStopHasReturned) {
  std::atomic<int> calls{0};
  PeriodicRunner runner{milliseconds{1}, [&] { ++calls; }};
  ASSERT_TRUE(eventually([&] { return calls.load() >= 1; }));

  runner.stop();
  const int after_stop = calls.load();
  std::this_thread::yield();

  EXPECT_EQ(calls.load(), after_stop);  // the thread has been joined
}

TEST(PeriodicRunner, StopsAtOnceEvenInTheMiddleOfALongPeriod) {
  std::atomic<int> calls{0};
  auto runner = std::make_unique<PeriodicRunner>(std::chrono::hours{1}, [&] { ++calls; });
  ASSERT_TRUE(eventually([&] { return calls.load() >= 1; }));  // now waiting out the hour

  std::promise<void> stopped;
  auto done = stopped.get_future();
  std::thread stopper{[&] {
    runner->stop();
    stopped.set_value();
  }};
  if (done.wait_for(std::chrono::seconds{10}) != std::future_status::ready) {
    std::fputs("FAILED: stop() waited out the period\n", stderr);
    std::fflush(stderr);
    std::_Exit(1);  // the stuck thread cannot be undone
  }
  stopper.join();
  EXPECT_EQ(calls.load(), 1);
}

TEST(PeriodicRunner, StoppingTwiceAndThenDestroyingIsHarmless) {
  PeriodicRunner runner{milliseconds{1}, [] {}};

  runner.stop();
  runner.stop();
}

}  // namespace

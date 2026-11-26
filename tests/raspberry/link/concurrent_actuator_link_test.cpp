// Verifies: REQ-COM-003, REQ-COM-002, REQ-SAF-004, REQ-SAF-008
//
// The wrapper of ADR-0023, without its thread: it owns the host link behind one
// lock, publishes the link's health state after every poll and every command, and
// answers `health()` from that published copy and the time now, never from the
// lock. That last part is what `REQ-COM-003` needs once a reader thread runs:
// asking about the link must not wait for an exchange in progress.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <memory>
#include <optional>
#include <thread>

#include "link_support/simulated_device.hpp"
#include "pigeon/core/actuator_link.hpp"
#include "pigeon/protocol/message.hpp"
#include "pigeon/raspberry/concurrent_actuator_link.hpp"
#include "support/manual_clock.hpp"

namespace {

using pigeon::core::FireCommand;
using pigeon::core::LinkStatus;
using pigeon::protocol::MessageType;
using pigeon::protocol::to_byte;
using pigeon::raspberry::ConcurrentActuatorLink;
using pigeon::test_support::ManualClock;
using pigeon::test_support::SimulatedDevice;
using std::chrono::milliseconds;

class Concurrent : public ::testing::Test {
 protected:
  ManualClock clock;
  SimulatedDevice device{clock};
  ConcurrentActuatorLink link{device, clock};

  void hear_the_device() {
    device.emit_heartbeat();
    link.poll_once();
  }
};

TEST_F(Concurrent, ANewLinkIsUnavailableAndHasSentNothing) {
  EXPECT_EQ(link.health(), LinkStatus::UNAVAILABLE);
  EXPECT_EQ(device.write_calls(), 0U);
}

TEST_F(Concurrent, WhatAPollHearsIsPublished) {
  device.emit_heartbeat();
  ASSERT_EQ(link.health(), LinkStatus::UNAVAILABLE);  // not read yet

  link.poll_once();

  EXPECT_EQ(link.health(), LinkStatus::OK);
}

TEST_F(Concurrent, WhatACommandEndsInIsPublished) {
  hear_the_device();

  device.answer_with_result(1);
  ASSERT_EQ(link.send_fire_command(FireCommand{milliseconds{300}}), LinkStatus::REJECTED);
  EXPECT_EQ(link.health(), LinkStatus::REJECTED);

  device.stay_silent();
  ASSERT_EQ(link.send_safe_state_command(), LinkStatus::TRANSPORT_FAILURE);
  EXPECT_EQ(link.health(), LinkStatus::TRANSPORT_FAILURE);
}

TEST_F(Concurrent, SilenceIsJudgedAgainstTheTimeNowNotTheLastPoll) {
  hear_the_device();

  clock.advance_by(milliseconds{1499});
  EXPECT_EQ(link.health(), LinkStatus::OK);
  clock.advance_by(milliseconds{1});  // no poll in between
  EXPECT_EQ(link.health(), LinkStatus::UNAVAILABLE);
}

TEST_F(Concurrent, StartUpIsForwardedAndItsOutcomePublished) {
  device.start_heartbeats(milliseconds{500});

  EXPECT_EQ(link.start_up(milliseconds{5000}), LinkStatus::OK);

  EXPECT_EQ(link.health(), LinkStatus::OK);
  ASSERT_EQ(device.commands().size(), 1U);
  EXPECT_EQ(device.commands()[0].type, to_byte(MessageType::SAFE_STATE));
}

TEST_F(Concurrent, HealthIsAnsweredWithoutWaitingForAnExchangeInProgress) {
  hear_the_device();
  std::optional<LinkStatus> asked_mid_exchange;
  device.on_command([&] { asked_mid_exchange = link.health(); });

  // The exchange runs on another thread and holds the wrapper's lock while the
  // device's hook asks for health. A health() that took that lock would wait for
  // ever; the bounded wait below turns that into a failure instead of a hang.
  std::promise<LinkStatus> done;
  auto outcome = done.get_future();
  std::thread exchange{[&] { done.set_value(link.send_safe_state_command()); }};

  const bool finished = outcome.wait_for(std::chrono::seconds{5}) == std::future_status::ready;
  if (!finished) {
    // Deadlocked, and nothing can be undone from here: the stuck thread holds the
    // lock, and tearing down the fixture would send SAFE_STATE, run the hook and
    // block this thread on the same lock. So report and end the process now; a
    // hang would hide the failure.
    std::fputs("FAILED: health() waited for the exchange's lock (deadlock)\n", stderr);
    std::fflush(stderr);
    std::_Exit(1);
  }
  exchange.join();
  // The hook refers to this test's locals, and the fixture's teardown sends
  // SAFE_STATE, which would run it after they are gone.
  device.on_command({});

  EXPECT_EQ(outcome.get(), LinkStatus::OK);
  ASSERT_TRUE(asked_mid_exchange.has_value());
  EXPECT_EQ(*asked_mid_exchange, LinkStatus::OK);  // the state from before the exchange
}

TEST(ConcurrentShutdown, DestroyingTheWrapperPutsTheActuatorInItsSafeState) {
  ManualClock clock;
  SimulatedDevice device{clock};

  std::make_unique<ConcurrentActuatorLink>(device, clock).reset();  // destroyed here

  ASSERT_EQ(device.commands().size(), 1U);
  EXPECT_EQ(device.commands()[0].type, to_byte(MessageType::SAFE_STATE));
}

}  // namespace

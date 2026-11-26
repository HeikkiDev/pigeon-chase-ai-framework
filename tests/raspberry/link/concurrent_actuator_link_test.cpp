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

/// A thread is stuck, and nothing can be undone from here: it holds the lock, and
/// tearing down the fixture would send SAFE_STATE and block on the same lock. So
/// report and end the process now; a hang would hide the failure.
[[noreturn]] void abandon(const char* why) {
  std::fprintf(stderr, "FAILED: %s\n", why);
  std::fflush(stderr);
  std::_Exit(1);
}

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

  // The device holds the exchange open, and with it the wrapper's lock, until the
  // test lets it go. Meanwhile a different thread asks for health, so no kind of
  // lock held by the exchange's thread, recursive or not, could let it through.
  std::promise<void> entered;
  auto exchange_open = entered.get_future();
  std::promise<void> release;
  const std::shared_future<void> let_go = release.get_future().share();
  device.on_command([&] {
    entered.set_value();
    let_go.wait();
  });

  std::promise<LinkStatus> done;
  auto outcome = done.get_future();
  std::thread exchange{[&] { done.set_value(link.send_safe_state_command()); }};
  if (exchange_open.wait_for(std::chrono::seconds{5}) != std::future_status::ready) {
    abandon("the exchange never reached the device");
  }

  std::promise<LinkStatus> asked;
  auto answer = asked.get_future();
  std::thread asker{[&] { asked.set_value(link.health()); }};
  if (answer.wait_for(std::chrono::seconds{5}) != std::future_status::ready) {
    abandon("health() waited for the exchange's lock");
  }
  asker.join();

  release.set_value();
  exchange.join();
  // The hook refers to this test's locals, and the fixture's teardown sends
  // SAFE_STATE, which would run it after they are gone.
  device.on_command({});

  EXPECT_EQ(answer.get(), LinkStatus::OK);  // the state from before the exchange
  EXPECT_EQ(outcome.get(), LinkStatus::OK);
}

TEST(ConcurrentShutdown, DestroyingTheWrapperPutsTheActuatorInItsSafeState) {
  ManualClock clock;
  SimulatedDevice device{clock};

  std::make_unique<ConcurrentActuatorLink>(device, clock).reset();  // destroyed here

  ASSERT_EQ(device.commands().size(), 1U);
  EXPECT_EQ(device.commands()[0].type, to_byte(MessageType::SAFE_STATE));
}

}  // namespace

// Verifies: REQ-SAF-004, REQ-COM-001, REQ-COM-002, REQ-COM-003
//
// Startup and shutdown (`docs/architecture/device-protocol.md`, section 8). The
// device powers up with the water actuator inactive and heartbeating. The host
// waits to hear it, because a board that has just reset is not listening and a
// command sent then would fault; once it has, it sends `SAFE_STATE` and repeats
// it, once per heartbeat interval with a new sequence number, until one is
// accepted. If nothing is ever heard, nothing is ever sent. At shutdown the
// host sends `SAFE_STATE` before the line goes away.

#include <gtest/gtest.h>

#include <chrono>
#include <memory>

#include "link_support/simulated_device.hpp"
#include "pigeon/core/actuator_link.hpp"
#include "pigeon/protocol/message.hpp"
#include "pigeon/raspberry/serial_actuator_link.hpp"
#include "support/manual_clock.hpp"

namespace {

using pigeon::core::LinkStatus;
using pigeon::protocol::MessageType;
using pigeon::protocol::to_byte;
using pigeon::raspberry::SerialActuatorLink;
using pigeon::test_support::ManualClock;
using pigeon::test_support::SimulatedDevice;
using std::chrono::milliseconds;

class Startup : public ::testing::Test {
 protected:
  ManualClock clock;
  SimulatedDevice device{clock};
  SerialActuatorLink link{device, clock};

  void SetUp() override { device.start_heartbeats(milliseconds{500}); }
};

TEST_F(Startup, WaitsForTheFirstHeartbeatThenSendsSafeStateAndIsOk) {
  EXPECT_EQ(link.start_up(milliseconds{5000}), LinkStatus::OK);

  ASSERT_EQ(device.commands().size(), 1U);
  EXPECT_EQ(device.commands()[0].type, to_byte(MessageType::SAFE_STATE));
  EXPECT_GE(device.commands()[0].received_at, milliseconds{500});  // not before it heard
  EXPECT_EQ(link.health(), LinkStatus::OK);
}

TEST_F(Startup, SendsNothingAndStaysUnavailableIfTheDeviceIsNeverHeard) {
  SimulatedDevice mute{clock};  // never heartbeats
  SerialActuatorLink link_to_mute{mute, clock};

  EXPECT_EQ(link_to_mute.start_up(milliseconds{2000}), LinkStatus::UNAVAILABLE);

  EXPECT_EQ(mute.write_calls(), 0U);
  EXPECT_EQ(link_to_mute.health(), LinkStatus::UNAVAILABLE);
  EXPECT_EQ(clock.now().since_epoch, milliseconds{2000});  // it gave up, at the deadline
}

TEST_F(Startup, RepeatsOncePerHeartbeatIntervalWithANewSequenceNumberUntilAccepted) {
  device.stay_silent_for(2);  // the first two SAFE_STATE commands go unanswered

  EXPECT_EQ(link.start_up(milliseconds{5000}), LinkStatus::OK);

  ASSERT_EQ(device.commands().size(), 3U);
  EXPECT_EQ(device.commands()[0].seq, 0U);
  EXPECT_EQ(device.commands()[1].seq, 1U);
  EXPECT_EQ(device.commands()[2].seq, 2U);
  const auto first = device.commands()[0].received_at;
  EXPECT_EQ(device.commands()[1].received_at, first + milliseconds{500});
  EXPECT_EQ(device.commands()[2].received_at, first + milliseconds{1000});
  EXPECT_EQ(link.health(), LinkStatus::OK);
}

TEST_F(Startup, GivesUpWithTheLastOutcomeIfNoAttemptIsEverAccepted) {
  device.stay_silent();

  EXPECT_EQ(link.start_up(milliseconds{2000}), LinkStatus::TRANSPORT_FAILURE);

  // First heard at 500 ms, then attempts at 500, 1000 and 1500; none begins at or
  // after the 2000 ms deadline.
  EXPECT_EQ(device.commands().size(), 3U);
  EXPECT_EQ(link.health(), LinkStatus::TRANSPORT_FAILURE);
}

TEST_F(Startup, ALineThatIsNotOpenIsUnavailableAndNothingIsSent) {
  device.close();

  EXPECT_EQ(link.start_up(milliseconds{2000}), LinkStatus::UNAVAILABLE);
  EXPECT_EQ(device.write_calls(), 0U);
}

// --- shutdown ---------------------------------------------------------------

class Shutdown : public ::testing::Test {
 protected:
  ManualClock clock;
  SimulatedDevice device{clock};
};

TEST_F(Shutdown, DestroyingTheLinkPutsTheActuatorInItsSafeState) {
  {
    const SerialActuatorLink link{device, clock};
    ASSERT_TRUE(device.commands().empty());  // construction sends nothing
  }

  ASSERT_EQ(device.commands().size(), 1U);
  EXPECT_EQ(device.commands()[0].type, to_byte(MessageType::SAFE_STATE));
}

TEST_F(Shutdown, ItIsAttemptedEvenIfTheDeviceWasNeverHeard) {
  // SAFE_STATE is attempted whenever the line is open: it is the operation that
  // must work when nothing else does (section 6.1).
  std::make_unique<SerialActuatorLink>(device, clock).reset();  // destroyed here

  EXPECT_EQ(device.commands().size(), 1U);
}

TEST_F(Shutdown, NothingIsSentOnALineThatIsNotOpen) {
  device.close();

  std::make_unique<SerialActuatorLink>(device, clock).reset();  // destroyed here

  EXPECT_EQ(device.write_calls(), 0U);
}

TEST_F(Shutdown, ADeviceThatNeverAnswersCostsAtMostTheReplyTimeout) {
  device.stay_silent();

  std::make_unique<SerialActuatorLink>(device, clock).reset();  // destroyed here

  EXPECT_EQ(clock.now().since_epoch, milliseconds{100});
  EXPECT_EQ(device.commands().size(), 1U);  // and it was not retransmitted
}

}  // namespace

// Verifies: REQ-COM-002, REQ-COM-003, REQ-SAF-007
//
// A line that is lost while the link is using it (`docs/architecture/device-protocol.md`,
// sections 6.1 and 7). A real serial port closes itself when its cable is pulled,
// and a port that is not open is not waited on. So the link must notice that the
// line has gone and stop at once: waiting on it again only spins, and in the
// worst case, a stream whose time passes only while it waits, never ends.

#include <gtest/gtest.h>

#include <chrono>

#include "link_support/simulated_device.hpp"
#include "pigeon/core/actuator_link.hpp"
#include "pigeon/raspberry/serial_actuator_link.hpp"
#include "support/manual_clock.hpp"

namespace {

using pigeon::core::LinkStatus;
using pigeon::raspberry::SerialActuatorLink;
using pigeon::test_support::ManualClock;
using pigeon::test_support::SimulatedDevice;
using std::chrono::milliseconds;

class LostLine : public ::testing::Test {
 protected:
  ManualClock clock;
  SimulatedDevice device{clock};
  SerialActuatorLink link{device, clock};
};

TEST_F(LostLine, AnExchangeWhoseLineIsLostAfterTheCommandWentOutEndsAtOnce) {
  device.close_on_next_command();

  // The command was transmitted, then the line went: a fault, not a refusal, and
  // not "nothing was sent" (REQ-SAF-007 counts it).
  EXPECT_EQ(link.send_safe_state_command(), LinkStatus::TRANSPORT_FAILURE);

  EXPECT_EQ(device.waits_while_closed(), 0U);
  EXPECT_EQ(clock.now().since_epoch, milliseconds{0});  // it did not wait for a reply
  EXPECT_EQ(device.commands().size(), 1U);              // and it did not retransmit
  EXPECT_EQ(link.health(), LinkStatus::UNAVAILABLE);
}

TEST_F(LostLine, StartUpStopsWaitingToHearTheDeviceWhenTheLineIsLost) {
  device.close_at(milliseconds{300});  // no heartbeat ever comes; the line goes at 300 ms

  EXPECT_EQ(link.start_up(milliseconds{5000}), LinkStatus::UNAVAILABLE);

  EXPECT_EQ(device.waits_while_closed(), 0U);
  EXPECT_EQ(clock.now().since_epoch, milliseconds{300});  // not the whole 5 s
  EXPECT_EQ(device.write_calls(), 0U);
}

TEST_F(LostLine, StartUpStopsRetryingWhenTheLineIsLost) {
  device.start_heartbeats(milliseconds{500});
  device.close_on_next_command();  // the first SAFE_STATE goes out, then the line goes

  EXPECT_EQ(link.start_up(milliseconds{5000}), LinkStatus::UNAVAILABLE);

  EXPECT_EQ(device.waits_while_closed(), 0U);
  EXPECT_EQ(device.commands().size(), 1U);  // no further attempt into a closed line
}

}  // namespace

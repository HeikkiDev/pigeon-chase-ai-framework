// Verifies: REQ-COM-002, REQ-COM-001, REQ-COM-003, REQ-SAF-007
//
// What happens when the device answers *late*, twice, in pieces or for something
// else (`docs/architecture/device-protocol.md`, sections 6 and 7). The reply
// timeout is 100 ms, judged as `elapsed >= timeout` (ADR-0014): a reply read at
// the deadline is a reply that did not come in time. A reply that comes after
// the host gave up is unmatched: it ends no exchange, records neither a fault nor
// a rejection, clears nothing, and still shows the device is there.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>

#include "link_support/simulated_device.hpp"
#include "pigeon/core/actuator_link.hpp"
#include "pigeon/core/aiming.hpp"
#include "pigeon/protocol/message.hpp"
#include "pigeon/raspberry/serial_actuator_link.hpp"
#include "support/manual_clock.hpp"

namespace {

using pigeon::core::AimingCommand;
using pigeon::core::Angle;
using pigeon::core::FireCommand;
using pigeon::core::LinkStatus;
using pigeon::core::ServoAngles;
using pigeon::protocol::MessageType;
using pigeon::protocol::to_byte;
using pigeon::raspberry::SerialActuatorLink;
using pigeon::test_support::ManualClock;
using pigeon::test_support::SimulatedDevice;
using std::chrono::milliseconds;

class Timing : public ::testing::Test {
 protected:
  ManualClock clock;
  SimulatedDevice device{clock};
  SerialActuatorLink link{device, clock};

  void hear_the_device() {
    device.emit_heartbeat();
    link.poll();
  }
};

TEST_F(Timing, AReplyJustInsideTheTimeoutIsAccepted) {
  device.delay_replies(milliseconds{99});

  EXPECT_EQ(link.send_safe_state_command(), LinkStatus::OK);
}

TEST_F(Timing, AReplyReadAtTheTimeoutIsTooLate) {
  device.delay_replies(milliseconds{100});

  EXPECT_EQ(link.send_safe_state_command(), LinkStatus::TRANSPORT_FAILURE);
}

TEST_F(Timing, AReplyAfterTheTimeoutIsTooLateForAFireAsWell) {
  device.delay_replies(milliseconds{130});

  EXPECT_EQ(link.send_fire_command(FireCommand{milliseconds{300}}), LinkStatus::TRANSPORT_FAILURE);
  EXPECT_EQ(device.commands().size(), 1U);  // and it was not retransmitted
}

TEST_F(Timing, ALateReplyIsUnmatchedAndChangesNeitherFaultNorRejection) {
  hear_the_device();
  device.delay_replies(milliseconds{130});
  device.answer_with_result(1);  // a refusal, arriving after the host gave up
  ASSERT_EQ(link.send_aiming_command(AimingCommand{ServoAngles{Angle{0.0}, Angle{10.0}}}),
            LinkStatus::TRANSPORT_FAILURE);

  clock.advance_by(milliseconds{50});
  link.poll();  // reads the late REJECTED_ANGLE

  // It faulted at the deadline and is still faulted: not REJECTED, not cleared.
  EXPECT_EQ(link.health(), LinkStatus::TRANSPORT_FAILURE);
}

TEST_F(Timing, ALateReplyStillCountsAsTheDeviceBeingThere) {
  // Nothing has been heard before it, so health is UNAVAILABLE until it arrives.
  device.delay_replies(milliseconds{130});
  ASSERT_EQ(link.send_safe_state_command(), LinkStatus::TRANSPORT_FAILURE);
  ASSERT_EQ(link.health(), LinkStatus::UNAVAILABLE);

  clock.advance_by(milliseconds{50});
  link.poll();

  EXPECT_EQ(link.health(), LinkStatus::TRANSPORT_FAILURE);  // heard, and faulted
}

TEST_F(Timing, ALateReplyToAnAbandonedCommandDoesNotAnswerTheNextOne) {
  hear_the_device();
  device.delay_replies(milliseconds{130});
  device.answer_with_result(1);  // the abandoned command's late reply is a refusal
  ASSERT_EQ(link.send_safe_state_command(), LinkStatus::TRANSPORT_FAILURE);

  device.answer_with_result(0);            // the next command is accepted
  device.delay_replies(milliseconds{50});  // but its own reply comes later than the stale one
  EXPECT_EQ(link.send_aiming_command(AimingCommand{ServoAngles{Angle{0.0}, Angle{10.0}}}),
            LinkStatus::OK);

  EXPECT_EQ(link.health(), LinkStatus::OK);  // the stale refusal set nothing
}

TEST_F(Timing, AReplyThatArrivesInPiecesIsReassembled) {
  device.deliver_one_byte_per_read();

  EXPECT_EQ(link.send_safe_state_command(), LinkStatus::OK);
}

// --- reply matching holds for every command, not only SAFE_STATE -------------

TEST_F(Timing, AnAimNeedsItsOwnSequenceNumberAndType) {
  device.answer_with_seq_offset(1);
  EXPECT_EQ(link.send_aiming_command(AimingCommand{ServoAngles{Angle{0.0}, Angle{10.0}}}),
            LinkStatus::TRANSPORT_FAILURE);

  SimulatedDevice other{clock};
  SerialActuatorLink other_link{other, clock};
  other.answer_naming_type(to_byte(MessageType::FIRE));
  EXPECT_EQ(other_link.send_aiming_command(AimingCommand{ServoAngles{Angle{0.0}, Angle{10.0}}}),
            LinkStatus::TRANSPORT_FAILURE);
}

TEST_F(Timing, AFireNeedsItsOwnSequenceNumberAndType) {
  device.answer_with_seq_offset(1);
  EXPECT_EQ(link.send_fire_command(FireCommand{milliseconds{300}}), LinkStatus::TRANSPORT_FAILURE);

  SimulatedDevice other{clock};
  SerialActuatorLink other_link{other, clock};
  other.answer_naming_type(to_byte(MessageType::AIM));
  EXPECT_EQ(other_link.send_fire_command(FireCommand{milliseconds{300}}),
            LinkStatus::TRANSPORT_FAILURE);
}

TEST_F(Timing, AnUnmatchedReplyWhateverItSaysSetsNeitherFaultNorRejection) {
  hear_the_device();

  for (std::uint8_t result = 0; result <= 9; ++result) {
    device.emit_reply(/*seq=*/200, to_byte(MessageType::FIRE), result);
    link.poll();
    EXPECT_EQ(link.health(), LinkStatus::OK) << "result " << static_cast<int>(result);
  }
}

// --- startup does not trust an old hearing ----------------------------------

TEST_F(Timing, StartUpWaitsToHearTheDeviceAgainIfItWentSilent) {
  hear_the_device();
  for (int i = 0; i < 100; ++i) {  // ten seconds of silence, read every 100 ms
    clock.advance_by(milliseconds{100});
    link.poll();
  }
  ASSERT_EQ(link.health(), LinkStatus::UNAVAILABLE);
  const auto before = device.write_calls();

  EXPECT_EQ(link.start_up(milliseconds{2000}), LinkStatus::UNAVAILABLE);

  EXPECT_EQ(device.write_calls(), before);  // nothing was sent into the silence
}

}  // namespace

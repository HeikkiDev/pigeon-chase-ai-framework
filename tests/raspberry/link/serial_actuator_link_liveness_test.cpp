// Verifies: REQ-COM-003, REQ-COM-001, REQ-COM-002, REQ-SAF-008
//
// Liveness: when the host last heard a valid message from the device, and what
// `health()` makes of it (`docs/architecture/device-protocol.md`, section 7).
// Silence for the link silence timeout, 1.5 s, makes the link `UNAVAILABLE`;
// anything the host discards or finds unsupported is no evidence the device is
// there; and a reader that has stalled must not let a backlog of old messages
// make a stalled host look healthy.

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdint>

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

class Liveness : public ::testing::Test {
 protected:
  ManualClock clock;
  SimulatedDevice device{clock};
  SerialActuatorLink link{device, clock};

  void hear_a_heartbeat() {
    device.emit_heartbeat();
    link.poll();
  }
};

TEST_F(Liveness, AHeartbeatTheHostHasReadMakesTheLinkOk) {
  hear_a_heartbeat();

  EXPECT_EQ(link.health(), LinkStatus::OK);
}

TEST_F(Liveness, AskingDoesNotReadTheLine) {
  device.emit_heartbeat();  // waiting on the line, not yet read

  EXPECT_EQ(link.health(), LinkStatus::UNAVAILABLE);
}

TEST_F(Liveness, SilenceForTheTimeoutMakesTheLinkUnavailableAndNotBefore) {
  hear_a_heartbeat();

  clock.advance_by(milliseconds{1499});
  EXPECT_EQ(link.health(), LinkStatus::OK);

  clock.advance_by(milliseconds{1});  // elapsed >= 1.5 s (ADR-0014)
  EXPECT_EQ(link.health(), LinkStatus::UNAVAILABLE);
}

TEST_F(Liveness, EachHeartbeatRestartsTheSilence) {
  hear_a_heartbeat();
  clock.advance_by(milliseconds{1000});
  hear_a_heartbeat();

  clock.advance_by(milliseconds{1000});
  EXPECT_EQ(link.health(), LinkStatus::OK);

  clock.advance_by(milliseconds{500});
  EXPECT_EQ(link.health(), LinkStatus::UNAVAILABLE);
}

TEST_F(Liveness, ALineThatHasClosedIsUnavailableWhateverWasHeard) {
  hear_a_heartbeat();

  device.close();

  EXPECT_EQ(link.health(), LinkStatus::UNAVAILABLE);
}

TEST_F(Liveness, TheHeartbeatsActuatorFlagDoesNotChangeHealth) {
  device.emit_heartbeat(0x01);  // actuator_active
  link.poll();

  EXPECT_EQ(link.health(), LinkStatus::OK);
}

TEST_F(Liveness, AMatchingReplyIsLivenessToo) {
  ASSERT_EQ(link.send_safe_state_command(), LinkStatus::OK);

  EXPECT_EQ(link.health(), LinkStatus::OK);
}

TEST_F(Liveness, AnUnmatchedReplyIsStillAValidDeviceMessage) {
  // V5: REPLY seq 7 to AIM, ACCEPTED. Nothing is outstanding, so it answers
  // nothing, but the device sent it.
  constexpr std::array<std::uint8_t, 9> v5{0x05, 0x01, 0x81, 0x07, 0x01, 0x03, 0x70, 0xA6, 0x00};
  device.emit_raw(v5);
  link.poll();

  EXPECT_EQ(link.health(), LinkStatus::OK);
}

TEST_F(Liveness, ACorruptedHeartbeatIsNotCounted) {
  // V7 with its last CRC byte changed from A7 to A6.
  constexpr std::array<std::uint8_t, 8> corrupt{0x03, 0x01, 0x82, 0x01, 0x03, 0x4E, 0xA6, 0x00};
  device.emit_raw(corrupt);
  link.poll();

  EXPECT_EQ(link.health(), LinkStatus::UNAVAILABLE);
}

TEST_F(Liveness, AWellFormedMessageTheHostDoesNotSpeakIsNotCounted) {
  // N4: version 2, good CRC. A host ignores it; it is not evidence of a device.
  constexpr std::array<std::uint8_t, 7> version_two{0x06, 0x02, 0x03, 0x04, 0x2B, 0xB7, 0x00};
  // V1: an AIM command. A device never sends one, so a host does not accept it.
  constexpr std::array<std::uint8_t, 11> a_command{0x0A, 0x01, 0x01, 0x07, 0x6C, 0xEE,
                                                   0xDC, 0x05, 0xE4, 0xC6, 0x00};
  device.emit_raw(version_two);
  device.emit_raw(a_command);
  link.poll();

  EXPECT_EQ(link.health(), LinkStatus::UNAVAILABLE);
}

// --- the reader's stall rule (section 7) ------------------------------------

TEST_F(Liveness, AReaderThatRanWithinTheStallBoundStampsWhatItFinds) {
  link.poll();
  clock.advance_by(milliseconds{200});  // not *more than* 200 ms

  hear_a_heartbeat();

  EXPECT_EQ(link.health(), LinkStatus::OK);
}

TEST_F(Liveness, AReaderThatStalledDiscardsWhatTheLineBufferedMeanwhile) {
  link.poll();
  clock.advance_by(milliseconds{201});  // more than 200 ms since it last ran
  device.emit_heartbeat();              // old by the time anyone looks

  link.poll();

  EXPECT_EQ(link.health(), LinkStatus::UNAVAILABLE);
}

TEST_F(Liveness, ARecoveredReaderCountsTheNextHeartbeat) {
  link.poll();
  clock.advance_by(milliseconds{201});
  device.emit_heartbeat();
  link.poll();  // stalled: discards it

  clock.advance_by(milliseconds{100});
  hear_a_heartbeat();

  EXPECT_EQ(link.health(), LinkStatus::OK);
}

TEST_F(Liveness, AnExchangeAfterALongIdleStillSeesItsReply) {
  clock.advance_by(milliseconds{10000});  // nobody polled for ten seconds

  EXPECT_EQ(link.send_safe_state_command(), LinkStatus::OK);
}

}  // namespace

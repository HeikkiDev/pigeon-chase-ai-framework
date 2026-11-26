// Verifies: REQ-COM-002, REQ-COM-003, REQ-COM-001, REQ-SAF-008
//
// The `faulted` flag (`docs/architecture/device-protocol.md`, section 7): an
// exchange that ends in a fault makes `health()` `TRANSPORT_FAILURE` until an
// exchange is answered. A heartbeat does not clear it, because a device whose
// heartbeat timer runs but whose command handling is wedged must not read as
// healthy merely because it is still heartbeating. Any answer, including a
// refusal, disproves it. Silence is judged first: `UNAVAILABLE` outranks it.

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

class Faulted : public ::testing::Test {
 protected:
  ManualClock clock;
  SimulatedDevice device{clock};
  SerialActuatorLink link{device, clock};

  /// A device that has been heard and is healthy: every case below starts here,
  /// so that what changes health is the exchange and not the missing liveness.
  void SetUp() override {
    device.emit_heartbeat();
    link.poll();
    ASSERT_EQ(link.health(), LinkStatus::OK);
  }

  void fault_an_exchange() {
    device.stay_silent();
    ASSERT_EQ(link.send_safe_state_command(), LinkStatus::TRANSPORT_FAILURE);
  }

  void idle_for(milliseconds total) {
    for (milliseconds done{0}; done < total; done += milliseconds{100}) {
      clock.advance_by(milliseconds{100});
      link.poll();
    }
  }
};

TEST_F(Faulted, AnExchangeWithNoReplyMakesHealthATransportFailure) {
  fault_an_exchange();

  EXPECT_EQ(link.health(), LinkStatus::TRANSPORT_FAILURE);
}

TEST_F(Faulted, AWriteErrorMakesHealthATransportFailure) {
  device.fail_writes();
  ASSERT_EQ(link.send_safe_state_command(), LinkStatus::TRANSPORT_FAILURE);

  EXPECT_EQ(link.health(), LinkStatus::TRANSPORT_FAILURE);
}

TEST_F(Faulted, ADeviceThatDidNotUnderstandMakesHealthATransportFailure) {
  device.answer_with_result(4);  // REJECTED_UNSUPPORTED
  ASSERT_EQ(link.send_safe_state_command(), LinkStatus::TRANSPORT_FAILURE);

  EXPECT_EQ(link.health(), LinkStatus::TRANSPORT_FAILURE);
}

TEST_F(Faulted, ACorruptedReplyIsALostReplyAndFaultsTheExchange) {
  device.corrupt_replies();

  EXPECT_EQ(link.send_safe_state_command(), LinkStatus::TRANSPORT_FAILURE);
  EXPECT_EQ(link.health(), LinkStatus::TRANSPORT_FAILURE);
}

TEST_F(Faulted, AHeartbeatDoesNotClearIt) {
  fault_an_exchange();

  device.emit_heartbeat();
  link.poll();

  EXPECT_EQ(link.health(), LinkStatus::TRANSPORT_FAILURE);
}

TEST_F(Faulted, AnUnmatchedReplyDoesNotClearIt) {
  fault_an_exchange();

  // V5: a REPLY seq 7 to AIM, ACCEPTED, answering nothing outstanding.
  constexpr std::array<std::uint8_t, 9> v5{0x05, 0x01, 0x81, 0x07, 0x01, 0x03, 0x70, 0xA6, 0x00};
  device.emit_raw(v5);
  link.poll();

  EXPECT_EQ(link.health(), LinkStatus::TRANSPORT_FAILURE);
}

TEST_F(Faulted, AnAcceptedExchangeClearsIt) {
  fault_an_exchange();
  device.stay_silent(false);

  EXPECT_EQ(link.send_safe_state_command(), LinkStatus::OK);

  EXPECT_EQ(link.health(), LinkStatus::OK);
}

TEST_F(Faulted, ARefusedExchangeAlsoClearsItBecauseAnyAnswerDisprovesIt) {
  fault_an_exchange();
  device.stay_silent(false);
  device.answer_with_result(1);  // REJECTED_ANGLE

  EXPECT_EQ(link.send_safe_state_command(), LinkStatus::REJECTED);

  // SAFE_STATE never sets `rejected` (that is for AIM and FIRE), so nothing is left.
  EXPECT_EQ(link.health(), LinkStatus::OK);
}

TEST_F(Faulted, SilenceOutranksIt) {
  fault_an_exchange();

  idle_for(milliseconds{1500});

  EXPECT_EQ(link.health(), LinkStatus::UNAVAILABLE);
}

}  // namespace

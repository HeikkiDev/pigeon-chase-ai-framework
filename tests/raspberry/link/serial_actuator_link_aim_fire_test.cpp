// Verifies: REQ-COM-001, REQ-COM-002, REQ-AIM-002, REQ-SAF-001, REQ-SAF-007, REQ-SAF-008
//
// The `AIM` and `FIRE` exchanges (`docs/architecture/device-protocol.md`,
// sections 3.2, 6 and 6.1), and the `rejected` flag they alone set and clear
// (section 7). Angles go out as hundredths of a degree rounded towards zero; the
// device refuses what is outside its own limits and the host reports that as
// `REJECTED`, not as a fault. A `FIRE` is transmitted whenever the line is open,
// whatever `health()` says, and is never transmitted twice.

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <limits>

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

AimingCommand aim_at(double x_degrees, double y_degrees) {
  return AimingCommand{ServoAngles{Angle{x_degrees}, Angle{y_degrees}}};
}

FireCommand fire_for(milliseconds duration) { return FireCommand{duration}; }

class AimFire : public ::testing::Test {
 protected:
  ManualClock clock;
  SimulatedDevice device{clock};
  SerialActuatorLink link{device, clock};

  void hear_the_device() {
    device.emit_heartbeat();
    link.poll();
  }
  void idle_for(milliseconds total) {
    for (milliseconds done{0}; done < total; done += milliseconds{100}) {
      clock.advance_by(milliseconds{100});
      link.poll();
    }
  }
};

// --- AIM ---------------------------------------------------------------------

TEST_F(AimFire, AnAimIsSentInHundredthsOfADegree) {
  EXPECT_EQ(link.send_aiming_command(aim_at(-45.0, 15.0)), LinkStatus::OK);  // V1

  ASSERT_EQ(device.commands().size(), 1U);
  EXPECT_EQ(device.commands()[0].type, to_byte(MessageType::AIM));
  EXPECT_EQ(device.commands()[0].x_centidegrees, -4500);
  EXPECT_EQ(device.commands()[0].y_centidegrees, 1500);
}

TEST_F(AimFire, AnAngleThatIsNotAWholeHundredthIsRoundedTowardsZero) {
  EXPECT_EQ(link.send_aiming_command(aim_at(12.375, 7.999)), LinkStatus::OK);
  EXPECT_EQ(link.send_aiming_command(aim_at(-12.375, 0.0)), LinkStatus::OK);

  ASSERT_EQ(device.commands().size(), 2U);
  EXPECT_EQ(device.commands()[0].x_centidegrees, 1237);
  EXPECT_EQ(device.commands()[0].y_centidegrees, 799);
  EXPECT_EQ(device.commands()[1].x_centidegrees, -1237);
}

TEST_F(AimFire, AnAimTheDeviceRefusesIsRejectedNotAFault) {
  EXPECT_EQ(link.send_aiming_command(aim_at(95.0, 10.0)), LinkStatus::REJECTED);
  EXPECT_EQ(link.send_aiming_command(aim_at(0.0, -1.0)), LinkStatus::REJECTED);
}

TEST_F(AimFire, AnAngleThatCannotBeRepresentedIsSentAsOneTheDeviceRefuses) {
  constexpr double nan = std::numeric_limits<double>::quiet_NaN();
  constexpr double inf = std::numeric_limits<double>::infinity();

  EXPECT_EQ(link.send_aiming_command(aim_at(1.0e9, 10.0)), LinkStatus::REJECTED);
  EXPECT_EQ(link.send_aiming_command(aim_at(nan, 10.0)), LinkStatus::REJECTED);
  EXPECT_EQ(link.send_aiming_command(aim_at(0.0, inf)), LinkStatus::REJECTED);
  EXPECT_EQ(link.send_aiming_command(aim_at(-inf, 10.0)), LinkStatus::REJECTED);
}

TEST_F(AimFire, AnAimWithNoReplyIsATransportFailureAndIsNotRetransmitted) {
  device.stay_silent();

  EXPECT_EQ(link.send_aiming_command(aim_at(0.0, 10.0)), LinkStatus::TRANSPORT_FAILURE);
  EXPECT_EQ(device.write_calls(), 1U);
  EXPECT_EQ(clock.now().since_epoch, milliseconds{100});
}

TEST_F(AimFire, AnAimOnAClosedLineIsUnavailableAndNothingIsTransmitted) {
  device.close();

  EXPECT_EQ(link.send_aiming_command(aim_at(0.0, 10.0)), LinkStatus::UNAVAILABLE);
  EXPECT_EQ(device.write_calls(), 0U);
}

// --- FIRE --------------------------------------------------------------------

TEST_F(AimFire, AFireIsSentWithItsDurationInMilliseconds) {
  EXPECT_EQ(link.send_fire_command(fire_for(milliseconds{300})), LinkStatus::OK);

  ASSERT_EQ(device.commands().size(), 1U);
  EXPECT_EQ(device.commands()[0].type, to_byte(MessageType::FIRE));
  EXPECT_EQ(device.commands()[0].duration_ms, 300U);
}

TEST_F(AimFire, AZeroDurationFireIsStillTransmittedBecauseItIsTheDevicesToJudge) {
  EXPECT_EQ(link.send_fire_command(fire_for(milliseconds{0})), LinkStatus::OK);

  ASSERT_EQ(device.commands().size(), 1U);
  EXPECT_EQ(device.commands()[0].duration_ms, 0U);
}

TEST_F(AimFire, AFireLongerThanTheDevicesBoundIsRejectedNotAFault) {
  EXPECT_EQ(link.send_fire_command(fire_for(milliseconds{501})), LinkStatus::REJECTED);
}

TEST_F(AimFire, ACooldownRefusalIsRejected) {
  device.answer_with_result(3);  // REJECTED_COOLDOWN

  EXPECT_EQ(link.send_fire_command(fire_for(milliseconds{300})), LinkStatus::REJECTED);
}

TEST_F(AimFire, ADurationThatCannotBeRepresentedIsSentAsOneTheDeviceRefuses) {
  EXPECT_EQ(link.send_fire_command(fire_for(milliseconds{70000})), LinkStatus::REJECTED);
  EXPECT_EQ(link.send_fire_command(fire_for(milliseconds{-1})), LinkStatus::REJECTED);
}

TEST_F(AimFire, AFireIsTransmittedWheneverTheLineIsOpenWhateverHealthSays) {
  // Nothing has been heard from the device, so health() is UNAVAILABLE. Deciding
  // not to fire on that is core's job (REQ-SAF-008); the link transmits, because
  // "transmitted" is what REQ-SAF-007 counts.
  ASSERT_EQ(link.health(), LinkStatus::UNAVAILABLE);

  EXPECT_EQ(link.send_fire_command(fire_for(milliseconds{300})), LinkStatus::OK);
  EXPECT_EQ(device.commands().size(), 1U);
}

TEST_F(AimFire, AFireWithNoReplyIsATransportFailureAndIsNeverRetransmitted) {
  device.stay_silent();

  EXPECT_EQ(link.send_fire_command(fire_for(milliseconds{300})), LinkStatus::TRANSPORT_FAILURE);

  EXPECT_EQ(device.write_calls(), 1U);  // REQ-SAF-007
  EXPECT_EQ(device.commands().size(), 1U);
}

TEST_F(AimFire, AFireOnAClosedLineIsUnavailableAndNothingIsTransmitted) {
  device.close();

  EXPECT_EQ(link.send_fire_command(fire_for(milliseconds{300})), LinkStatus::UNAVAILABLE);
  EXPECT_EQ(device.write_calls(), 0U);
}

// --- the `rejected` flag (section 7) -----------------------------------------

TEST_F(AimFire, ARejectedAimMakesHealthRejected) {
  hear_the_device();

  ASSERT_EQ(link.send_aiming_command(aim_at(95.0, 10.0)), LinkStatus::REJECTED);

  EXPECT_EQ(link.health(), LinkStatus::REJECTED);
}

TEST_F(AimFire, ARejectedFireMakesHealthRejected) {
  hear_the_device();

  ASSERT_EQ(link.send_fire_command(fire_for(milliseconds{501})), LinkStatus::REJECTED);

  EXPECT_EQ(link.health(), LinkStatus::REJECTED);
}

TEST_F(AimFire, AnAcceptedAimClearsIt) {
  hear_the_device();
  ASSERT_EQ(link.send_aiming_command(aim_at(95.0, 10.0)), LinkStatus::REJECTED);

  ASSERT_EQ(link.send_aiming_command(aim_at(10.0, 10.0)), LinkStatus::OK);

  EXPECT_EQ(link.health(), LinkStatus::OK);
}

TEST_F(AimFire, AnAcceptedFireClearsIt) {
  hear_the_device();
  ASSERT_EQ(link.send_aiming_command(aim_at(95.0, 10.0)), LinkStatus::REJECTED);

  ASSERT_EQ(link.send_fire_command(fire_for(milliseconds{300})), LinkStatus::OK);

  EXPECT_EQ(link.health(), LinkStatus::OK);
}

TEST_F(AimFire, AnAcceptedSafeStateDoesNotClearIt) {
  // The host sends SAFE_STATE whenever an engagement is abandoned, so counting it
  // would erase every rejection the moment it was made.
  hear_the_device();
  ASSERT_EQ(link.send_aiming_command(aim_at(95.0, 10.0)), LinkStatus::REJECTED);

  ASSERT_EQ(link.send_safe_state_command(), LinkStatus::OK);

  EXPECT_EQ(link.health(), LinkStatus::REJECTED);
}

TEST_F(AimFire, ADeviceThatDidNotUnderstandAnAimIsAFaultNotARejection) {
  hear_the_device();
  device.answer_with_result(4);  // REJECTED_UNSUPPORTED

  ASSERT_EQ(link.send_aiming_command(aim_at(10.0, 10.0)), LinkStatus::TRANSPORT_FAILURE);
  EXPECT_EQ(link.health(), LinkStatus::TRANSPORT_FAILURE);

  device.answer_with_result(0);
  ASSERT_EQ(link.send_safe_state_command(), LinkStatus::OK);
  EXPECT_EQ(link.health(), LinkStatus::OK);  // `rejected` was never set
}

TEST_F(AimFire, AFaultDoesNotClearItAndFaultOutranksIt) {
  hear_the_device();
  ASSERT_EQ(link.send_aiming_command(aim_at(95.0, 10.0)), LinkStatus::REJECTED);

  device.stay_silent();
  ASSERT_EQ(link.send_fire_command(fire_for(milliseconds{300})), LinkStatus::TRANSPORT_FAILURE);
  EXPECT_EQ(link.health(), LinkStatus::TRANSPORT_FAILURE);

  device.stay_silent(false);
  ASSERT_EQ(link.send_safe_state_command(), LinkStatus::OK);  // clears `faulted` only
  EXPECT_EQ(link.health(), LinkStatus::REJECTED);
}

TEST_F(AimFire, SilenceOutranksIt) {
  hear_the_device();
  ASSERT_EQ(link.send_aiming_command(aim_at(95.0, 10.0)), LinkStatus::REJECTED);

  idle_for(milliseconds{1500});

  EXPECT_EQ(link.health(), LinkStatus::UNAVAILABLE);
}

}  // namespace

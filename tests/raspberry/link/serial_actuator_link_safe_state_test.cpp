// Verifies: REQ-COM-001, REQ-COM-002, REQ-SAF-004, REQ-SAF-007
//
// One exchange, end to end, against a simulated device that speaks the real
// codec (`docs/architecture/device-protocol.md`, sections 6 and 6.1):
// `SAFE_STATE` is the simplest command and the one that must work when nothing
// else does, so it is where the exchange is built. A command is answered by a
// reply that matches its `seq` and type, is never retransmitted, and every way
// an exchange can end maps to exactly one `LinkStatus`.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>

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

class SafeStateExchange : public ::testing::Test {
 protected:
  ManualClock clock;
  SimulatedDevice device{clock};
  SerialActuatorLink link{device, clock};
};

TEST_F(SafeStateExchange, AnAcceptedReplyIsOkAndTheDeviceSawOneSafeStateCommand) {
  EXPECT_EQ(link.send_safe_state_command(), LinkStatus::OK);

  ASSERT_EQ(device.commands().size(), 1U);
  EXPECT_EQ(device.commands()[0].type, to_byte(MessageType::SAFE_STATE));
  EXPECT_EQ(device.commands()[0].seq, 0U);
}

TEST_F(SafeStateExchange, EachCommandTakesTheNextSequenceNumberAndItWraps) {
  for (int i = 0; i < 257; ++i) {
    ASSERT_EQ(link.send_safe_state_command(), LinkStatus::OK) << "command " << i;
  }

  ASSERT_EQ(device.commands().size(), 257U);
  EXPECT_EQ(device.commands()[1].seq, 1U);
  EXPECT_EQ(device.commands()[255].seq, 255U);
  EXPECT_EQ(device.commands()[256].seq, 0U);
}

TEST_F(SafeStateExchange, NoReplyFaultsAtTheReplyTimeoutAndTheCommandIsNeverRetransmitted) {
  device.stay_silent();

  EXPECT_EQ(link.send_safe_state_command(), LinkStatus::TRANSPORT_FAILURE);

  // Spec section 9: 100 ms, judged as `elapsed >= timeout` (ADR-0014).
  EXPECT_EQ(clock.now().since_epoch, milliseconds{100});
  EXPECT_EQ(device.write_calls(), 1U);  // REQ-SAF-007: no retransmission
  EXPECT_EQ(device.commands().size(), 1U);
}

TEST_F(SafeStateExchange, AReplyForAnotherSequenceNumberDoesNotAnswerTheCommand) {
  device.answer_with_seq_offset(1);

  EXPECT_EQ(link.send_safe_state_command(), LinkStatus::TRANSPORT_FAILURE);
  EXPECT_EQ(clock.now().since_epoch, milliseconds{100});
}

TEST_F(SafeStateExchange, AReplyNamingAnotherCommandTypeDoesNotAnswerTheCommand) {
  device.answer_naming_type(to_byte(MessageType::FIRE));

  EXPECT_EQ(link.send_safe_state_command(), LinkStatus::TRANSPORT_FAILURE);
}

TEST_F(SafeStateExchange, ADeviceThatDidNotUnderstandIsAFaultNotARefusal) {
  device.answer_with_result(4);  // REJECTED_UNSUPPORTED

  EXPECT_EQ(link.send_safe_state_command(), LinkStatus::TRANSPORT_FAILURE);
}

TEST_F(SafeStateExchange, AResultOutsideEveryDefinedCodeIsAFault) {
  device.answer_with_result(9);

  EXPECT_EQ(link.send_safe_state_command(), LinkStatus::TRANSPORT_FAILURE);
}

TEST_F(SafeStateExchange, ARefusalIsRejected) {
  device.answer_with_result(1);  // REJECTED_ANGLE: any of 1, 2, 3 is a refusal

  EXPECT_EQ(link.send_safe_state_command(), LinkStatus::REJECTED);
}

TEST_F(SafeStateExchange, AWriteErrorIsATransportFailure) {
  device.fail_writes();

  EXPECT_EQ(link.send_safe_state_command(), LinkStatus::TRANSPORT_FAILURE);
  EXPECT_EQ(device.write_calls(), 1U);
}

TEST_F(SafeStateExchange, ALineThatIsNotOpenIsUnavailableAndNothingIsTransmitted) {
  device.close();

  EXPECT_EQ(link.send_safe_state_command(), LinkStatus::UNAVAILABLE);
  EXPECT_EQ(device.write_calls(), 0U);
}

}  // namespace

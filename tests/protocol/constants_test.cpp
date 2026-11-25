// Verifies: REQ-SAF-001, REQ-AIM-002, REQ-COM-003
//
// The values both devices must agree on, pinned to the maintainer's rulings
// (docs/requirements/requirements.md, Q26-Q31) and protocol.md §4.1, §4.2, §6.
// A change to any of them is a change to a safety limit or to the protocol,
// and must fail here first so that it is made deliberately.
//
// Added after the constants, in response to code review: not observed failing
// first.

#include <gtest/gtest.h>

#include "pigeon/protocol/protocol.hpp"

namespace {

namespace protocol = pigeon::protocol;

TEST(ProtocolConstants, DeviceFireBoundIsFiveHundredMilliseconds) {
  EXPECT_EQ(protocol::device_fire_bound_ms, 500);  // Q31
}

TEST(ProtocolConstants, DeviceEnvelopeIsTheDeploymentEnvelopeInCentidegrees) {
  // REQ-AIM-002: X in [-90, +90] degrees, Y in [0, +45] degrees; Q27: 0.01 degree.
  EXPECT_EQ(protocol::envelope_x_min_centidegrees, -9000);
  EXPECT_EQ(protocol::envelope_x_max_centidegrees, 9000);
  EXPECT_EQ(protocol::envelope_y_min_centidegrees, 0);
  EXPECT_EQ(protocol::envelope_y_max_centidegrees, 4500);
}

TEST(ProtocolConstants, HeartbeatEveryHundredMillisecondsWithAFiveHundredMillisecondTimeout) {
  EXPECT_EQ(protocol::heartbeat_interval_ms, 100);  // Q29
  EXPECT_EQ(protocol::heartbeat_timeout_ms, 500);   // Q29
}

TEST(ProtocolConstants, ReplyTimeoutIsFiftyMilliseconds) {
  EXPECT_EQ(protocol::reply_timeout_ms, 50);  // Q30
}

// The timeout must outlast several heartbeats, or one late heartbeat would
// make a healthy link look dead.
TEST(ProtocolConstants, HeartbeatTimeoutSpansSeveralIntervals) {
  EXPECT_GE(protocol::heartbeat_timeout_ms, 2 * protocol::heartbeat_interval_ms);
}

}  // namespace

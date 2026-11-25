// Verifies: REQ-COM-001
//
// Decoding valid input (docs/architecture/protocol.md §2-§5): every message
// type is recovered field for field, at the endpoint it is sent to, from a
// byte stream that may also carry other packets.

#include <cstdint>

#include <gtest/gtest.h>

#include "pigeon/protocol/protocol.hpp"
#include "support/decoding.hpp"
#include "support/vectors.hpp"

namespace {

using pigeon::protocol::Decoder;
using pigeon::protocol::DecodeStatus;
using pigeon::protocol::encode;
using pigeon::protocol::Endpoint;
using pigeon::protocol::Message;
using pigeon::protocol::MessageType;
using pigeon::protocol::NakReason;
using pigeon::protocol_test::accepted;
using pigeon::protocol_test::bytes_of;
using pigeon::protocol_test::concat;
using pigeon::protocol_test::push_all;
using pigeon::protocol_test::repeat;
namespace vectors = pigeon::protocol_test;

// ------------------------------------------------------------ device endpoint --

TEST(DecodeAtDevice, Aim) {
  Decoder decoder{Endpoint::DEVICE};
  EXPECT_EQ(push_all(decoder, vectors::aim_example), accepted(vectors::aim_example.size()));

  const Message& aim = decoder.message();
  EXPECT_EQ(aim.type, MessageType::AIM);
  EXPECT_EQ(aim.seq, 0x07);
  EXPECT_EQ(aim.x_centidegrees, 1234);
  EXPECT_EQ(aim.y_centidegrees, 500);
}

TEST(DecodeAtDevice, AimAtTheEnvelopeCorners) {
  Decoder decoder{Endpoint::DEVICE};
  EXPECT_EQ(push_all(decoder, vectors::aim_limits), accepted(vectors::aim_limits.size()));

  const Message& aim = decoder.message();
  EXPECT_EQ(aim.seq, 0xFF);
  EXPECT_EQ(aim.x_centidegrees, -9000);
  EXPECT_EQ(aim.y_centidegrees, 4500);
}

TEST(DecodeAtDevice, Fire) {
  Decoder decoder{Endpoint::DEVICE};
  EXPECT_EQ(push_all(decoder, vectors::fire_example), accepted(vectors::fire_example.size()));

  const Message& fire = decoder.message();
  EXPECT_EQ(fire.type, MessageType::FIRE);
  EXPECT_EQ(fire.seq, 0x08);
  EXPECT_EQ(fire.duration_ms, 500);
}

TEST(DecodeAtDevice, FireOfZeroAndOfTheLargestDuration) {
  Decoder decoder{Endpoint::DEVICE};
  EXPECT_EQ(push_all(decoder, vectors::fire_zero), accepted(vectors::fire_zero.size()));
  EXPECT_EQ(decoder.message().duration_ms, 0);

  EXPECT_EQ(push_all(decoder, vectors::fire_max), accepted(vectors::fire_max.size()));
  EXPECT_EQ(decoder.message().duration_ms, 0xFFFF);
}

TEST(DecodeAtDevice, Safe) {
  Decoder decoder{Endpoint::DEVICE};
  EXPECT_EQ(push_all(decoder, vectors::safe_seq9), accepted(vectors::safe_seq9.size()));

  EXPECT_EQ(decoder.message().type, MessageType::SAFE);
  EXPECT_EQ(decoder.message().seq, 0x09);
}

// ------------------------------------------------------------- host endpoint --

TEST(DecodeAtHost, Ack) {
  Decoder decoder{Endpoint::HOST};
  EXPECT_EQ(push_all(decoder, vectors::ack_example), accepted(vectors::ack_example.size()));

  EXPECT_EQ(decoder.message().type, MessageType::ACK);
  EXPECT_EQ(decoder.message().seq, 0x07);
}

TEST(DecodeAtHost, Nak) {
  Decoder decoder{Endpoint::HOST};
  EXPECT_EQ(push_all(decoder, vectors::nak_seq7), accepted(vectors::nak_seq7.size()));

  const Message& nak = decoder.message();
  EXPECT_EQ(nak.type, MessageType::NAK);
  EXPECT_EQ(nak.seq, 0x07);
  EXPECT_EQ(nak.reason, NakReason::ANGLE_OUT_OF_ENVELOPE);
}

TEST(DecodeAtHost, Heartbeat) {
  Decoder decoder{Endpoint::HOST};
  EXPECT_EQ(push_all(decoder, vectors::heartbeat_example),
            accepted(vectors::heartbeat_example.size()));

  const Message& heartbeat = decoder.message();
  EXPECT_EQ(heartbeat.type, MessageType::HEARTBEAT);
  EXPECT_EQ(heartbeat.seq, 0);
  EXPECT_EQ(heartbeat.counter, 42);
  EXPECT_FALSE(heartbeat.actuator_active);
}

TEST(DecodeAtHost, HeartbeatWithTheActuatorActive) {
  Decoder decoder{Endpoint::HOST};
  EXPECT_EQ(push_all(decoder, vectors::heartbeat_active_max),
            accepted(vectors::heartbeat_active_max.size()));

  EXPECT_EQ(decoder.message().counter, 0xFFFF);
  EXPECT_TRUE(decoder.message().actuator_active);
}

// ----------------------------------------------------------------- streams --

TEST(Decode, AcceptsBackToBackPackets) {
  Decoder decoder{Endpoint::HOST};
  const auto stream =
      concat(concat(vectors::ack_example, vectors::heartbeat_example), vectors::nak_seq7);
  const auto expected = concat(
      concat(accepted(vectors::ack_example.size()), accepted(vectors::heartbeat_example.size())),
      accepted(vectors::nak_seq7.size()));
  EXPECT_EQ(push_all(decoder, stream), expected);
  EXPECT_EQ(decoder.message().type, MessageType::NAK);
}

// The sync byte has no special meaning inside a packet (§2): it is read as
// data because the receiver knows the length.
TEST(Decode, ReadsTheSyncByteInsideAPacketAsData) {
  Decoder decoder{Endpoint::DEVICE};
  EXPECT_EQ(push_all(decoder, vectors::aim_sync_in_payload),
            accepted(vectors::aim_sync_in_payload.size()));

  const Message& aim = decoder.message();
  EXPECT_EQ(aim.seq, 0xA5);
  EXPECT_EQ(aim.x_centidegrees, -23131);
  EXPECT_EQ(aim.y_centidegrees, 165);
}

// Bytes received while looking for SYNC are discarded (§7).
TEST(Decode, SkipsBytesBeforeTheFirstSync) {
  Decoder decoder{Endpoint::DEVICE};
  const vectors::Bytes garbage{0x00, 0xFF, 0x12, 0x5A, 0x01};
  EXPECT_EQ(push_all(decoder, concat(garbage, vectors::aim_example)),
            concat(repeat(DecodeStatus::NEED_MORE, garbage.size()),
                   accepted(vectors::aim_example.size())));
  EXPECT_EQ(decoder.message().x_centidegrees, 1234);
}

// ---------------------------------------------------------------- round trip --

TEST(Decode, RecoversEveryEncodableMessage) {
  const Message to_device[] = {
      {.type = MessageType::AIM, .seq = 0, .x_centidegrees = -1, .y_centidegrees = 0},
      {.type = MessageType::AIM, .seq = 200, .x_centidegrees = 32767, .y_centidegrees = -32768},
      {.type = MessageType::FIRE, .seq = 17, .duration_ms = 1},
      {.type = MessageType::SAFE, .seq = 255},
  };
  const Message to_host[] = {
      {.type = MessageType::ACK, .seq = 128},
      {.type = MessageType::NAK, .seq = 3, .reason = NakReason::ANGLE_OUT_OF_ENVELOPE},
      {.type = MessageType::HEARTBEAT, .seq = 0, .counter = 1, .actuator_active = true},
  };

  Decoder device{Endpoint::DEVICE};
  for (const Message& sent : to_device) {
    const auto packet = bytes_of(encode(sent));
    ASSERT_EQ(push_all(device, packet), accepted(packet.size()));
    const Message& received = device.message();
    EXPECT_EQ(received.type, sent.type);
    EXPECT_EQ(received.seq, sent.seq);
    EXPECT_EQ(received.x_centidegrees, sent.x_centidegrees);
    EXPECT_EQ(received.y_centidegrees, sent.y_centidegrees);
    EXPECT_EQ(received.duration_ms, sent.duration_ms);
  }

  Decoder host{Endpoint::HOST};
  for (const Message& sent : to_host) {
    const auto packet = bytes_of(encode(sent));
    ASSERT_EQ(push_all(host, packet), accepted(packet.size()));
    const Message& received = host.message();
    EXPECT_EQ(received.type, sent.type);
    EXPECT_EQ(received.seq, sent.seq);
    EXPECT_EQ(received.reason, sent.reason);
    EXPECT_EQ(received.counter, sent.counter);
    EXPECT_EQ(received.actuator_active, sent.actuator_active);
  }
}

// A decoded message carries only its own fields; the rest keep their defaults,
// so nothing from an earlier packet leaks into a later one.
TEST(Decode, LeavesFieldsOfOtherTypesAtTheirDefaults) {
  Decoder decoder{Endpoint::DEVICE};
  ASSERT_EQ(push_all(decoder, vectors::aim_example), accepted(vectors::aim_example.size()));
  ASSERT_EQ(push_all(decoder, vectors::safe_seq9), accepted(vectors::safe_seq9.size()));

  const Message& safe = decoder.message();
  const Message defaults{};
  EXPECT_EQ(safe.x_centidegrees, defaults.x_centidegrees);
  EXPECT_EQ(safe.y_centidegrees, defaults.y_centidegrees);
  EXPECT_EQ(safe.duration_ms, defaults.duration_ms);
  EXPECT_EQ(safe.counter, defaults.counter);
  EXPECT_EQ(safe.actuator_active, defaults.actuator_active);
}

}  // namespace

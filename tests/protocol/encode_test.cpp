// Verifies: REQ-COM-001
//
// Encoding (docs/architecture/protocol.md §2-§4): every message becomes the
// exact bytes the specification gives, and the encoder never produces a packet
// a receiver would treat as malformed (§7).

#include <cstdint>

#include <gtest/gtest.h>

#include "pigeon/protocol/protocol.hpp"
#include "support/decoding.hpp"
#include "support/vectors.hpp"

namespace {

using pigeon::protocol::encode;
using pigeon::protocol::max_packet_size;
using pigeon::protocol::Message;
using pigeon::protocol::MessageType;
using pigeon::protocol::min_packet_size;
using pigeon::protocol::NakReason;
using pigeon::protocol_test::bytes_of;
namespace vectors = pigeon::protocol_test;

// ------------------------------------------------- the worked examples (§10) --

TEST(Encode, AimMatchesTheWorkedExample) {
  const Message aim{
      .type = MessageType::AIM, .seq = 0x07, .x_centidegrees = 1234, .y_centidegrees = 500};
  EXPECT_EQ(bytes_of(encode(aim)), vectors::aim_example);
}

TEST(Encode, AckMatchesTheWorkedExample) {
  const Message ack{.type = MessageType::ACK, .seq = 0x07};
  EXPECT_EQ(bytes_of(encode(ack)), vectors::ack_example);
}

TEST(Encode, FireMatchesTheWorkedExample) {
  const Message fire{.type = MessageType::FIRE, .seq = 0x08, .duration_ms = 500};
  EXPECT_EQ(bytes_of(encode(fire)), vectors::fire_example);
}

TEST(Encode, HeartbeatMatchesTheWorkedExample) {
  const Message heartbeat{.type = MessageType::HEARTBEAT, .seq = 0, .counter = 42};
  EXPECT_EQ(bytes_of(encode(heartbeat)), vectors::heartbeat_example);
}

// ------------------------------------------------------- every message type --

TEST(Encode, Safe) {
  const Message safe{.type = MessageType::SAFE, .seq = 0x09};
  EXPECT_EQ(bytes_of(encode(safe)), vectors::safe_seq9);
}

TEST(Encode, Nak) {
  const Message nak{
      .type = MessageType::NAK, .seq = 0x07, .reason = NakReason::ANGLE_OUT_OF_ENVELOPE};
  EXPECT_EQ(bytes_of(encode(nak)), vectors::nak_seq7);
}

TEST(Encode, HeartbeatWithTheActuatorActiveSetsFlagBitZero) {
  const Message heartbeat{
      .type = MessageType::HEARTBEAT, .seq = 0, .counter = 0xFFFF, .actuator_active = true};
  EXPECT_EQ(bytes_of(encode(heartbeat)), vectors::heartbeat_active_max);
}

// Each type has exactly one length (§3): 6 bytes of framing plus its payload.
TEST(Encode, GivesEachTypeItsOneValidLength) {
  EXPECT_EQ(encode(Message{.type = MessageType::AIM}).size, min_packet_size + 4);
  EXPECT_EQ(encode(Message{.type = MessageType::FIRE}).size, min_packet_size + 2);
  EXPECT_EQ(encode(Message{.type = MessageType::SAFE}).size, min_packet_size + 0);
  EXPECT_EQ(encode(Message{.type = MessageType::ACK}).size, min_packet_size + 0);
  EXPECT_EQ(encode(Message{.type = MessageType::NAK}).size, min_packet_size + 1);
  EXPECT_EQ(encode(Message{.type = MessageType::HEARTBEAT}).size, min_packet_size + 3);
}

TEST(Encode, NeverExceedsTheLargestPacket) {
  for (const MessageType type : {MessageType::AIM, MessageType::FIRE, MessageType::SAFE,
                                 MessageType::ACK, MessageType::NAK, MessageType::HEARTBEAT}) {
    EXPECT_LE(encode(Message{.type = type}).size, max_packet_size);
  }
}

// ----------------------------------------------------------- integer layout --

// Big-endian, two's complement (§2), at the envelope's corners (§4.1).
TEST(Encode, WritesNegativeAnglesAsBigEndianTwosComplement) {
  const Message aim{
      .type = MessageType::AIM, .seq = 0xFF, .x_centidegrees = -9000, .y_centidegrees = 4500};
  EXPECT_EQ(bytes_of(encode(aim)), vectors::aim_limits);
}

TEST(Encode, CarriesTheSyncByteInsideAPacketUnescaped) {
  const Message aim{
      .type = MessageType::AIM, .seq = 0xA5, .x_centidegrees = -23131, .y_centidegrees = 165};
  EXPECT_EQ(bytes_of(encode(aim)), vectors::aim_sync_in_payload);
}

TEST(Encode, FireOfZeroMillisecondsIsAValidCommand) {
  const Message fire{.type = MessageType::FIRE, .seq = 0x00, .duration_ms = 0};
  EXPECT_EQ(bytes_of(encode(fire)), vectors::fire_zero);
}

// Truncating a long burst to 500 ms is the firmware's job (§4.2, Q32), not the
// codec's: the request is carried as sent.
TEST(Encode, FireCarriesTheRequestedDurationUntruncated) {
  const Message fire{.type = MessageType::FIRE, .seq = 0x01, .duration_ms = 0xFFFF};
  EXPECT_EQ(bytes_of(encode(fire)), vectors::fire_max);
}

// --------------------------------------------- fields of other types are ignored --

TEST(Encode, IgnoresFieldsThatDoNotBelongToTheType) {
  const Message noisy_safe{.type = MessageType::SAFE,
                           .seq = 0x09,
                           .x_centidegrees = 1234,
                           .y_centidegrees = -1,
                           .duration_ms = 500,
                           .reason = NakReason::ANGLE_OUT_OF_ENVELOPE,
                           .counter = 7,
                           .actuator_active = true};
  EXPECT_EQ(bytes_of(encode(noisy_safe)), vectors::safe_seq9);
}

// ------------------------------------------- never emits a malformed packet (§7) --

TEST(Encode, RefusesAReservedType) {
  const Message reserved{.type = static_cast<MessageType>(0x04)};
  EXPECT_EQ(encode(reserved).size, 0U);
}

TEST(Encode, RefusesAReservedNakReason) {
  const Message nak{.type = MessageType::NAK, .seq = 0x07, .reason = static_cast<NakReason>(0x02)};
  EXPECT_EQ(encode(nak).size, 0U);
}

TEST(Encode, RefusesAHeartbeatWithANonZeroSequenceNumber) {
  const Message heartbeat{.type = MessageType::HEARTBEAT, .seq = 1, .counter = 42};
  EXPECT_EQ(encode(heartbeat).size, 0U);
}

}  // namespace

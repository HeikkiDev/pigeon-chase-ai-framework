// Verifies: REQ-COM-001
//
// "Malformed input is rejected without undefined behaviour."
//
// docs/architecture/protocol.md §7: each malformation is reported at the byte
// that reveals it, the packet is discarded whole, and the decoder then looks
// for the next SYNC starting with the byte after the one reported. The
// randomised tests use a fixed seed and the standardised std::mt19937 output
// (never a distribution, whose output is implementation-defined), so they are
// deterministic on every platform (REQ-DEV-002). Run them under `make asan` to
// show the decoder never reads or writes outside its buffer.

#include <array>
#include <cstdint>
#include <random>
#include <vector>

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
using pigeon::protocol::sync_byte;
using pigeon::protocol_test::accepted;
using pigeon::protocol_test::Bytes;
using pigeon::protocol_test::bytes_of;
using pigeon::protocol_test::concat;
using pigeon::protocol_test::push_all;
using pigeon::protocol_test::repeat;
using pigeon::protocol_test::Statuses;
namespace vectors = pigeon::protocol_test;

constexpr DecodeStatus need_more = DecodeStatus::NEED_MORE;

// `size - 1` NEED_MOREs, then `last`.
Statuses ending_in(DecodeStatus last, std::size_t size) {
  Statuses statuses = repeat(need_more, size - 1);
  statuses.push_back(last);
  return statuses;
}

// ----------------------------------------------------------- the LEN byte --

TEST(DecodeMalformed, RejectsALengthAboveEightAtTheLengthByte) {
  Decoder decoder{Endpoint::DEVICE};
  EXPECT_EQ(push_all(decoder, Bytes{0xA5, 0x01, 0x07, 0x09}),
            ending_in(DecodeStatus::DISCARDED_BAD_LENGTH, 4));
}

TEST(DecodeMalformed, RejectsTheLargestLengthByte) {
  Decoder decoder{Endpoint::HOST};
  EXPECT_EQ(push_all(decoder, Bytes{0xA5, 0x81, 0x07, 0xFF}),
            ending_in(DecodeStatus::DISCARDED_BAD_LENGTH, 4));
}

TEST(DecodeMalformed, RejectsALengthThatIsWrongForTheType) {
  Decoder device{Endpoint::DEVICE};
  EXPECT_EQ(push_all(device, Bytes{0xA5, 0x01, 0x07, 0x02}),  // AIM needs 4
            ending_in(DecodeStatus::DISCARDED_WRONG_LENGTH, 4));
  EXPECT_EQ(push_all(device, Bytes{0xA5, 0x03, 0x09, 0x01}),  // SAFE needs 0
            ending_in(DecodeStatus::DISCARDED_WRONG_LENGTH, 4));

  Decoder host{Endpoint::HOST};
  EXPECT_EQ(push_all(host, Bytes{0xA5, 0x83, 0x00, 0x08}),  // HEARTBEAT needs 3
            ending_in(DecodeStatus::DISCARDED_WRONG_LENGTH, 4));
}

// ---------------------------------------------------------- the TYPE byte --

TEST(DecodeMalformed, RejectsAReservedTypeAtTheTypeByte) {
  constexpr std::array<std::uint8_t, 6> reserved_types{0x00, 0x04, 0x7F, 0x80, 0x84, 0xFF};
  for (const std::uint8_t reserved : reserved_types) {
    Decoder device{Endpoint::DEVICE};
    EXPECT_EQ(push_all(device, Bytes{0xA5, reserved}),
              ending_in(DecodeStatus::DISCARDED_UNKNOWN_TYPE, 2))
        << "type 0x" << std::hex << int{reserved};
    Decoder host{Endpoint::HOST};
    EXPECT_EQ(push_all(host, Bytes{0xA5, reserved}),
              ending_in(DecodeStatus::DISCARDED_UNKNOWN_TYPE, 2))
        << "type 0x" << std::hex << int{reserved};
  }
}

// A device never receives ACK, a host never receives AIM (§7). The rest of the
// packet contains no SYNC, so it is skipped.
TEST(DecodeMalformed, RejectsATypeSentTheOtherWay) {
  Decoder device{Endpoint::DEVICE};
  EXPECT_EQ(push_all(device, vectors::ack_example),
            concat(ending_in(DecodeStatus::DISCARDED_UNKNOWN_TYPE, 2),
                   repeat(need_more, vectors::ack_example.size() - 2)));

  Decoder host{Endpoint::HOST};
  EXPECT_EQ(push_all(host, vectors::aim_example),
            concat(ending_in(DecodeStatus::DISCARDED_UNKNOWN_TYPE, 2),
                   repeat(need_more, vectors::aim_example.size() - 2)));
}

// ------------------------------------------------------------------- the CRC --

TEST(DecodeMalformed, RejectsAWrongCrcAtTheLastCrcByte) {
  // Every byte whose corruption leaves TYPE and LEN intact: SEQ, payload, CRC.
  for (const std::size_t index : {2U, 4U, 5U, 6U, 7U, 8U, 9U}) {
    Bytes corrupted = vectors::aim_example;
    corrupted[index] ^= 0x01U;
    Decoder decoder{Endpoint::DEVICE};
    EXPECT_EQ(push_all(decoder, corrupted),
              ending_in(DecodeStatus::DISCARDED_BAD_CRC, corrupted.size()))
        << "flipped bit 0 of byte " << index;
  }
}

// --------------------------------------------------------- reserved values --

// The CRC matches, so these are reported after it (§7).
TEST(DecodeMalformed, RejectsAReservedNakReason) {
  Decoder decoder{Endpoint::HOST};
  EXPECT_EQ(push_all(decoder, vectors::nak_reserved_reason),
            ending_in(DecodeStatus::DISCARDED_RESERVED_VALUE, vectors::nak_reserved_reason.size()));
}

TEST(DecodeMalformed, RejectsAHeartbeatWithANonZeroSequenceNumber) {
  Decoder decoder{Endpoint::HOST};
  EXPECT_EQ(
      push_all(decoder, vectors::heartbeat_nonzero_seq),
      ending_in(DecodeStatus::DISCARDED_RESERVED_VALUE, vectors::heartbeat_nonzero_seq.size()));
}

TEST(DecodeMalformed, RejectsAHeartbeatWithAReservedFlag) {
  Decoder decoder{Endpoint::HOST};
  EXPECT_EQ(
      push_all(decoder, vectors::heartbeat_reserved_flag),
      ending_in(DecodeStatus::DISCARDED_RESERVED_VALUE, vectors::heartbeat_reserved_flag.size()));
}

// -------------------------------------------------------- resynchronisation --

TEST(DecodeMalformed, AcceptsTheNextPacketAfterADiscardedOne) {
  Bytes corrupted = vectors::aim_example;
  corrupted[5] ^= 0x01U;
  Decoder decoder{Endpoint::DEVICE};
  EXPECT_EQ(push_all(decoder, concat(corrupted, vectors::fire_example)),
            concat(ending_in(DecodeStatus::DISCARDED_BAD_CRC, corrupted.size()),
                   accepted(vectors::fire_example.size())));
  EXPECT_EQ(decoder.message().type, MessageType::FIRE);
}

// The byte that revealed the fault is not reconsidered as SYNC, even when it
// is 0xA5 (§7). The rest of the would-be AIM carries no SYNC, so it is lost;
// the packet after it is not.
TEST(DecodeMalformed, DoesNotReconsiderASyncValuedLengthByte) {
  const Bytes rest_of_aim(vectors::aim_example.begin() + 1, vectors::aim_example.end());
  const Bytes stream =
      concat(concat(Bytes{0xA5, 0x01, 0x07, sync_byte}, rest_of_aim), vectors::fire_example);
  Decoder decoder{Endpoint::DEVICE};
  EXPECT_EQ(push_all(decoder, stream),
            concat(concat(ending_in(DecodeStatus::DISCARDED_BAD_LENGTH, 4),
                          repeat(need_more, rest_of_aim.size())),
                   accepted(vectors::fire_example.size())));
}

TEST(DecodeMalformed, DoesNotReconsiderASyncValuedTypeByte) {
  const Bytes rest_of_aim(vectors::aim_example.begin() + 1, vectors::aim_example.end());
  const Bytes stream = concat(concat(Bytes{0xA5, sync_byte}, rest_of_aim), vectors::fire_example);
  Decoder decoder{Endpoint::DEVICE};
  EXPECT_EQ(push_all(decoder, stream),
            concat(concat(ending_in(DecodeStatus::DISCARDED_UNKNOWN_TYPE, 2),
                          repeat(need_more, rest_of_aim.size())),
                   accepted(vectors::fire_example.size())));
}

// A truncated packet swallows the start of the one after it, which then fails
// its CRC and is lost (§7: "a corrupted packet can hide the start of the
// packet after it"). The third packet is unaffected. CRC of the swallowed
// bytes, 01 07 04 FF A5 01 07, is 0x56F1, not the 0x0404 they are read with.
TEST(DecodeMalformed, LosesAtMostThePacketATruncatedOneOverlaps) {
  const Bytes truncated_aim{0xA5, 0x01, 0x07, 0x04, 0xFF};
  const Bytes stream = concat(concat(truncated_aim, vectors::aim_example), vectors::fire_example);

  Decoder decoder{Endpoint::DEVICE};
  const Statuses expected =
      concat(concat(repeat(need_more, truncated_aim.size() + 4),  // ...FF A5 01 07 04
                    Statuses{DecodeStatus::DISCARDED_BAD_CRC}),   // 04: read as CRC
             concat(repeat(need_more, 5),                         // D2 01 F4 55 47
                    accepted(vectors::fire_example.size())));
  EXPECT_EQ(push_all(decoder, stream), expected);
  EXPECT_EQ(decoder.message().type, MessageType::FIRE);
}

// ------------------------------------------------------- randomised, seeded --

constexpr std::uint32_t seed = 20261125U;

// Any byte sequence: never a status outside the enumeration, and every packet
// the decoder reports is exactly the bytes that produced it.
TEST(DecodeMalformed, SurvivesArbitraryBytesAndReportsOnlyRealPackets) {
  for (const Endpoint endpoint : {Endpoint::DEVICE, Endpoint::HOST}) {
    std::mt19937 engine{seed};
    Decoder decoder{endpoint};
    Bytes history;
    for (int index = 0; index < 200'000; ++index) {
      // One byte in eight is SYNC, so packet starts are frequent.
      const auto draw = static_cast<std::uint32_t>(engine());
      const auto byte = (draw % 8U == 0U) ? sync_byte : static_cast<std::uint8_t>(draw >> 8U);
      history.push_back(byte);

      const DecodeStatus status = decoder.push(byte);
      ASSERT_LE(static_cast<int>(status), static_cast<int>(DecodeStatus::DISCARDED_RESERVED_VALUE));
      if (status == DecodeStatus::PACKET_READY) {
        const Bytes packet = bytes_of(encode(decoder.message()));
        ASSERT_FALSE(packet.empty());
        ASSERT_GE(history.size(), packet.size());
        EXPECT_EQ(Bytes(history.end() - static_cast<std::ptrdiff_t>(packet.size()), history.end()),
                  packet);
      }
    }
  }
}

// Valid packets separated by noise that contains no SYNC: every one of them is
// recovered, in order, and nothing else is.
TEST(DecodeMalformed, RecoversEveryPacketFromNoiseWithoutSync) {
  std::mt19937 engine{seed};
  Bytes stream;
  std::vector<Message> sent;
  for (int index = 0; index < 500; ++index) {
    const std::size_t noise = engine() % 20U;
    for (std::size_t count = 0; count < noise; ++count) {
      auto byte = static_cast<std::uint8_t>(engine());
      if (byte == sync_byte) {
        byte = 0x00;
      }
      stream.push_back(byte);
    }

    Message message{};
    switch (engine() % 3U) {
      case 0U:
        message = {.type = MessageType::AIM,
                   .seq = static_cast<std::uint8_t>(engine()),
                   .x_centidegrees = static_cast<std::int16_t>(engine()),
                   .y_centidegrees = static_cast<std::int16_t>(engine())};
        break;
      case 1U:
        message = {.type = MessageType::FIRE,
                   .seq = static_cast<std::uint8_t>(engine()),
                   .duration_ms = static_cast<std::uint16_t>(engine())};
        break;
      default:
        message = {.type = MessageType::SAFE, .seq = static_cast<std::uint8_t>(engine())};
        break;
    }
    const Bytes packet = bytes_of(encode(message));
    ASSERT_FALSE(packet.empty());
    stream.insert(stream.end(), packet.begin(), packet.end());
    sent.push_back(message);
  }

  Decoder decoder{Endpoint::DEVICE};
  std::vector<Message> received;
  for (const std::uint8_t byte : stream) {
    const DecodeStatus status = decoder.push(byte);
    ASSERT_TRUE(status == DecodeStatus::NEED_MORE || status == DecodeStatus::PACKET_READY);
    if (status == DecodeStatus::PACKET_READY) {
      received.push_back(decoder.message());
    }
  }

  ASSERT_EQ(received.size(), sent.size());
  for (std::size_t index = 0; index < sent.size(); ++index) {
    EXPECT_EQ(bytes_of(encode(received[index])), bytes_of(encode(sent[index])))
        << "packet " << index;
  }
}

// The NAK and HEARTBEAT reserved-value checks must not reject the values that
// are allowed (§4.5, §4.6): checked here so a too-strict decoder fails too.
TEST(DecodeMalformed, AcceptsEveryNonReservedValue) {
  Decoder decoder{Endpoint::HOST};
  for (const bool active : {false, true}) {
    const Message heartbeat{
        .type = MessageType::HEARTBEAT, .seq = 0, .counter = 7, .actuator_active = active};
    const Bytes packet = bytes_of(encode(heartbeat));
    ASSERT_EQ(push_all(decoder, packet), accepted(packet.size()));
    EXPECT_EQ(decoder.message().actuator_active, active);
  }
  const Message nak{.type = MessageType::NAK, .seq = 1, .reason = NakReason::ANGLE_OUT_OF_ENVELOPE};
  const Bytes packet = bytes_of(encode(nak));
  EXPECT_EQ(push_all(decoder, packet), accepted(packet.size()));
}

}  // namespace

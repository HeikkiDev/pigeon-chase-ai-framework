// Verifies: REQ-COM-001
//
// The messages of `docs/architecture/device-protocol.md`: how each command,
// reply and heartbeat is encoded, and the receiving steps of section 5 that
// decide what a receiver does with whatever the line delivers. The normative
// vectors of section 10 are the oracle: each message must encode to exactly the
// bytes the specification gives, and each input a receiver must reject must be
// rejected for the reason the specification gives.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "pigeon/protocol/bytes.hpp"
#include "pigeon/protocol/message.hpp"
#include "support/hex.hpp"
#include "support/vectors.hpp"

namespace {

using pigeon::protocol::AimPayload;
using pigeon::protocol::ByteView;
using pigeon::protocol::encode_aim;
using pigeon::protocol::encode_fire;
using pigeon::protocol::encode_heartbeat;
using pigeon::protocol::encode_reply;
using pigeon::protocol::encode_safe_state;
using pigeon::protocol::FirePayload;
using pigeon::protocol::HeartbeatPayload;
using pigeon::protocol::max_encoded_message_bytes;
using pigeon::protocol::max_wire_message_bytes;
using pigeon::protocol::MessageType;
using pigeon::protocol::MutableBytes;
using pigeon::protocol::protocol_version;
using pigeon::protocol::receive_message;
using pigeon::protocol::Received;
using pigeon::protocol::ReceiveStatus;
using pigeon::protocol::ReplyPayload;
using pigeon::protocol::ResultCode;
using pigeon::protocol::Side;
using pigeon::protocol::to_byte;
using pigeon::protocol::testing::bytes_from_hex;
using pigeon::protocol::testing::to_hex;
using pigeon::protocol::testing::valid_vectors;
using pigeon::protocol::testing::view_of;

using Bytes = std::vector<std::uint8_t>;

constexpr std::uint8_t guard_byte = 0xA5;
constexpr std::size_t guard_size = 8;

// The wire bytes an encoder produced, without its delimiter, in a buffer of
// exactly `capacity` bytes followed by guard bytes no call may touch.
struct Encoded {
  std::size_t length;  // what the encoder returned: 0 on failure
  Bytes wire;          // the first `length` bytes
  bool guard_intact;
};

template <typename Encode>
Encoded encode_with_capacity(std::size_t capacity, Encode&& encode) {
  Bytes storage(capacity + guard_size, guard_byte);
  const std::size_t length = encode(MutableBytes{storage.data(), capacity});
  Encoded result{
      length, Bytes(storage.begin(), storage.begin() + static_cast<std::ptrdiff_t>(length)), true};
  for (std::size_t i = capacity; i < storage.size(); ++i) {
    result.guard_intact = result.guard_intact && storage[i] == guard_byte;
  }
  return result;
}

Bytes wire_of(const char* hex) { return bytes_from_hex(hex); }

// ---------------------------------------------------------------- encoding

// Verifies: REQ-COM-001 — section 10.1, V1 to V8: each message encodes to
// exactly the bytes the specification gives, delimiter included.
TEST(MessageEncode, ProducesEachSpecificationVectorExactly) {
  const auto encoded = [](std::size_t index) {
    switch (index) {
      case 0:
        return encode_with_capacity(max_wire_message_bytes, [](MutableBytes out) {
          return encode_aim(7, AimPayload{-4500, 1500}, out);
        });
      case 1:
        return encode_with_capacity(max_wire_message_bytes, [](MutableBytes out) {
          return encode_aim(0, AimPayload{0, 0}, out);
        });
      case 2:
        return encode_with_capacity(max_wire_message_bytes, [](MutableBytes out) {
          return encode_fire(8, FirePayload{500}, out);
        });
      case 3:
        return encode_with_capacity(max_wire_message_bytes,
                                    [](MutableBytes out) { return encode_safe_state(9, out); });
      case 4:
        return encode_with_capacity(max_wire_message_bytes, [](MutableBytes out) {
          return encode_reply(7, ReplyPayload{MessageType::AIM, ResultCode::ACCEPTED}, out);
        });
      case 5:
        return encode_with_capacity(max_wire_message_bytes, [](MutableBytes out) {
          return encode_reply(8, ReplyPayload{MessageType::FIRE, ResultCode::REJECTED_DURATION},
                              out);
        });
      case 6:
        return encode_with_capacity(max_wire_message_bytes, [](MutableBytes out) {
          return encode_heartbeat(0, HeartbeatPayload{0}, out);
        });
      default:
        return encode_with_capacity(max_wire_message_bytes, [](MutableBytes out) {
          return encode_heartbeat(255, HeartbeatPayload{HeartbeatPayload::actuator_active}, out);
        });
    }
  };

  ASSERT_EQ(valid_vectors.size(), 8U);
  for (std::size_t index = 0; index < valid_vectors.size(); ++index) {
    SCOPED_TRACE(valid_vectors[index].name);
    const Encoded result = encoded(index);

    EXPECT_EQ(to_hex(result.wire), to_hex(wire_of(valid_vectors[index].wire)));
    EXPECT_EQ(result.length, result.wire.size());
    EXPECT_TRUE(result.guard_intact);
  }
}

// Verifies: REQ-COM-001 — the reply a device sends to a message it cannot
// interpret (section 10.2, R2 to R5), built from what the receiver reported.
TEST(MessageEncode, ProducesTheSpecificationRepliesToUnsupportedMessages) {
  struct Case {
    std::uint8_t seq;
    std::uint8_t command_type;
    const char* wire;
  };
  const Case cases[] = {
      {8, 0x02, "08 01 81 08 02 04 96 9F 00"},  // R2
      {3, 0x7F, "08 01 81 03 7F 04 62 11 00"},  // R3
      {4, 0x03, "08 01 81 04 03 04 C6 D9 00"},  // R4
      {5, 0x01, "08 01 81 05 01 04 94 88 00"},  // R5
  };

  for (const Case& c : cases) {
    SCOPED_TRACE(c.wire);
    const Encoded result = encode_with_capacity(max_wire_message_bytes, [&c](MutableBytes out) {
      return encode_reply(
          c.seq, ReplyPayload{c.command_type, to_byte(ResultCode::REJECTED_UNSUPPORTED)}, out);
    });

    EXPECT_EQ(to_hex(result.wire), to_hex(wire_of(c.wire)));
  }
}

// Verifies: REQ-COM-001 — section 3.4: the largest message is 11 bytes on the
// wire and contains exactly one zero, the delimiter at the end, for every value
// of every payload.
TEST(MessageEncode, NeverExceedsTheMaximumSizeAndHasOneDelimiterAtTheEnd) {
  const auto check = [](const Encoded& result) {
    ASSERT_GT(result.length, 0U);
    ASSERT_LE(result.length, max_wire_message_bytes);
    ASSERT_EQ(result.wire.back(), 0x00);
    for (std::size_t i = 0; i + 1 < result.wire.size(); ++i) {
      ASSERT_NE(result.wire[i], 0x00) << "inside the message at " << i;
    }
  };

  const std::int16_t extremes[] = {-32768, -4500, -256, -1, 0, 1, 255, 256, 4500, 32767};
  for (const std::int16_t x : extremes) {
    for (const std::int16_t y : extremes) {
      check(encode_with_capacity(max_wire_message_bytes, [=](MutableBytes out) {
        return encode_aim(0, AimPayload{x, y}, out);
      }));
    }
  }
  for (unsigned seq = 0; seq < 256; ++seq) {
    const auto s = static_cast<std::uint8_t>(seq);
    check(encode_with_capacity(max_wire_message_bytes,
                               [=](MutableBytes out) { return encode_safe_state(s, out); }));
    check(encode_with_capacity(max_wire_message_bytes, [=](MutableBytes out) {
      return encode_fire(s, FirePayload{65535}, out);
    }));
  }
}

// Verifies: REQ-COM-001 — an encoder given a buffer that is too small reports
// failure by returning zero and never writes beyond the buffer, at every
// capacity, for every message type.
TEST(MessageEncode, ReportsAnUndersizedBufferAndNeverWritesPastIt) {
  const std::size_t aim_size = 11;  // V1
  for (std::size_t capacity = 0; capacity < aim_size; ++capacity) {
    const Encoded result = encode_with_capacity(
        capacity, [](MutableBytes out) { return encode_aim(7, AimPayload{-4500, 1500}, out); });
    EXPECT_EQ(result.length, 0U) << "capacity " << capacity;
    EXPECT_TRUE(result.guard_intact) << "capacity " << capacity;
  }
  EXPECT_EQ(
      encode_with_capacity(
          aim_size, [](MutableBytes out) { return encode_aim(7, AimPayload{-4500, 1500}, out); })
          .length,
      aim_size);

  for (std::size_t capacity = 0; capacity < 7; ++capacity) {  // V4 is 7 bytes
    const Encoded result =
        encode_with_capacity(capacity, [](MutableBytes out) { return encode_safe_state(9, out); });
    EXPECT_EQ(result.length, 0U) << "capacity " << capacity;
    EXPECT_TRUE(result.guard_intact) << "capacity " << capacity;
  }
}

// ---------------------------------------------------------------- receiving

// The encoded form of a message, without its delimiter: what a receiver is
// handed once the framer has found the end of it.
Bytes without_delimiter(const char* wire_hex) {
  Bytes wire = bytes_from_hex(wire_hex);
  EXPECT_FALSE(wire.empty());
  EXPECT_EQ(wire.back(), 0x00);
  wire.pop_back();
  return wire;
}

Received receive_hex(const char* wire_hex, Side side) {
  const Bytes encoded = without_delimiter(wire_hex);
  return receive_message(view_of(encoded), side);
}

// Verifies: REQ-COM-001 — section 10.1: the device understands each command the
// host sends, and recovers every field.
TEST(MessageReceive, DeviceRecoversEveryFieldOfEachCommandVector) {
  const Received aim = receive_hex(valid_vectors[0].wire, Side::DEVICE);
  ASSERT_EQ(aim.status, ReceiveStatus::VALID);
  EXPECT_EQ(aim.version, protocol_version);
  EXPECT_EQ(aim.type, to_byte(MessageType::AIM));
  EXPECT_EQ(aim.seq, 7);
  EXPECT_EQ(aim.aim.x_centidegrees, -4500);
  EXPECT_EQ(aim.aim.y_centidegrees, 1500);

  const Received origin = receive_hex(valid_vectors[1].wire, Side::DEVICE);
  ASSERT_EQ(origin.status, ReceiveStatus::VALID);
  EXPECT_EQ(origin.seq, 0);
  EXPECT_EQ(origin.aim.x_centidegrees, 0);
  EXPECT_EQ(origin.aim.y_centidegrees, 0);

  const Received fire = receive_hex(valid_vectors[2].wire, Side::DEVICE);
  ASSERT_EQ(fire.status, ReceiveStatus::VALID);
  EXPECT_EQ(fire.type, to_byte(MessageType::FIRE));
  EXPECT_EQ(fire.seq, 8);
  EXPECT_EQ(fire.fire.duration_ms, 500);

  const Received safe = receive_hex(valid_vectors[3].wire, Side::DEVICE);
  ASSERT_EQ(safe.status, ReceiveStatus::VALID);
  EXPECT_EQ(safe.type, to_byte(MessageType::SAFE_STATE));
  EXPECT_EQ(safe.seq, 9);
}

// Verifies: REQ-COM-001 — section 10.1: the host understands each reply and
// heartbeat the device sends, and recovers every field, including a result code
// and a sequence number at the top of their range.
TEST(MessageReceive, HostRecoversEveryFieldOfEachReplyAndHeartbeatVector) {
  const Received accepted = receive_hex(valid_vectors[4].wire, Side::HOST);
  ASSERT_EQ(accepted.status, ReceiveStatus::VALID);
  EXPECT_EQ(accepted.type, to_byte(MessageType::REPLY));
  EXPECT_EQ(accepted.seq, 7);
  EXPECT_EQ(accepted.reply.command_type, to_byte(MessageType::AIM));
  EXPECT_EQ(accepted.reply.result, to_byte(ResultCode::ACCEPTED));

  const Received refused = receive_hex(valid_vectors[5].wire, Side::HOST);
  ASSERT_EQ(refused.status, ReceiveStatus::VALID);
  EXPECT_EQ(refused.seq, 8);
  EXPECT_EQ(refused.reply.command_type, to_byte(MessageType::FIRE));
  EXPECT_EQ(refused.reply.result, to_byte(ResultCode::REJECTED_DURATION));

  const Received idle = receive_hex(valid_vectors[6].wire, Side::HOST);
  ASSERT_EQ(idle.status, ReceiveStatus::VALID);
  EXPECT_EQ(idle.type, to_byte(MessageType::HEARTBEAT));
  EXPECT_EQ(idle.seq, 0);
  EXPECT_EQ(idle.heartbeat.flags, 0);

  const Received active = receive_hex(valid_vectors[7].wire, Side::HOST);
  ASSERT_EQ(active.status, ReceiveStatus::VALID);
  EXPECT_EQ(active.seq, 255);
  EXPECT_EQ(active.heartbeat.flags, HeartbeatPayload::actuator_active);
}

// Verifies: REQ-COM-001 — section 5, step 4: a type that the other side
// receives is not one this side receives, so each end rejects the other's
// messages rather than acting on them.
TEST(MessageReceive, EachSideRefusesTheTypesOfTheOtherSide) {
  EXPECT_EQ(receive_hex(valid_vectors[4].wire, Side::DEVICE).status,
            ReceiveStatus::UNSUPPORTED_TYPE);
  EXPECT_EQ(receive_hex(valid_vectors[6].wire, Side::DEVICE).status,
            ReceiveStatus::UNSUPPORTED_TYPE);
  EXPECT_EQ(receive_hex(valid_vectors[0].wire, Side::HOST).status, ReceiveStatus::UNSUPPORTED_TYPE);
  EXPECT_EQ(receive_hex(valid_vectors[2].wire, Side::HOST).status, ReceiveStatus::UNSUPPORTED_TYPE);
  EXPECT_EQ(receive_hex(valid_vectors[3].wire, Side::HOST).status, ReceiveStatus::UNSUPPORTED_TYPE);
}

// Verifies: REQ-COM-001 — section 10.2: a message whose CRC fails is discarded
// before anything in it is believed (N1), and the codec says so.
TEST(MessageReceive, DiscardsAMessageWhoseCrcFailsN1) {
  const Received received = receive_hex("0A 01 01 06 6C EE DC 05 E4 C6 00", Side::DEVICE);

  EXPECT_EQ(received.status, ReceiveStatus::DISCARDED_CHECK);
}

// Verifies: REQ-COM-001 — section 5, steps 3 to 5 and section 10.2, N2 to N5: a
// message with a valid CRC that the device cannot interpret is reported with the
// sequence number and type it carried, so the device can answer it with
// REJECTED_UNSUPPORTED (R2 to R5).
TEST(MessageReceive, ReportsWhyAMessageIsUnsupportedAndWhatToAnswerN2ToN5) {
  struct Case {
    const char* wire;
    ReceiveStatus status;
    std::uint8_t seq;
    std::uint8_t type;
  };
  const Case cases[] = {
      {"06 01 02 08 F4 01 03 53 86 00", ReceiveStatus::UNSUPPORTED_LENGTH, 8, 0x02},  // N2
      {"06 01 7F 03 A8 D3 00", ReceiveStatus::UNSUPPORTED_TYPE, 3, 0x7F},             // N3
      {"06 02 03 04 2B B7 00", ReceiveStatus::UNSUPPORTED_VERSION, 4, 0x03},          // N4
      {"08 01 01 05 01 02 6A 35 00", ReceiveStatus::UNSUPPORTED_LENGTH, 5, 0x01},     // N5
  };

  for (const Case& c : cases) {
    SCOPED_TRACE(c.wire);
    const Received received = receive_hex(c.wire, Side::DEVICE);

    EXPECT_EQ(received.status, c.status);
    EXPECT_EQ(received.seq, c.seq);
    EXPECT_EQ(received.type, c.type);
  }
}

// Verifies: REQ-COM-001 — section 10.2, N6 and N7: more than 16 bytes before the
// delimiter, and a COBS code byte that overruns, are discarded as framing
// errors with nothing believed.
TEST(MessageReceive, DiscardsAnOversizeMessageAndABrokenEncodingN6N7) {
  const Bytes oversize(17, 0x11);
  EXPECT_EQ(receive_message(view_of(oversize), Side::DEVICE).status,
            ReceiveStatus::DISCARDED_FRAMING);

  const Bytes overrun = bytes_from_hex("05 11 22");
  EXPECT_EQ(receive_message(view_of(overrun), Side::DEVICE).status,
            ReceiveStatus::DISCARDED_FRAMING);

  EXPECT_EQ(max_encoded_message_bytes, 16U);
}

// Verifies: REQ-COM-001 — section 10.2, N9: a well-formed FIRE whose duration is
// over the device's bound is *valid* to the codec, which enforces no limit of
// the device's (section 5, step 6); the device refuses it with V6.
TEST(MessageReceive, CarriesAnOverlongFireDurationThroughForTheDeviceToRefuseN9) {
  const Received received = receive_hex("08 01 02 08 F5 01 81 03 00", Side::DEVICE);

  ASSERT_EQ(received.status, ReceiveStatus::VALID);
  EXPECT_EQ(received.fire.duration_ms, 501);
}

// Verifies: REQ-COM-001 — the empty message is not a message.
TEST(MessageReceive, DiscardsAnEmptyMessage) {
  EXPECT_EQ(receive_message(ByteView{}, Side::DEVICE).status, ReceiveStatus::DISCARDED_FRAMING);
}

// Verifies: REQ-COM-001 — fewer than three header bytes and a two-byte CRC
// cannot be a message, so a short one is discarded however its bytes read.
TEST(MessageReceive, DiscardsAnythingTooShortToHoldAHeaderAndACrc) {
  for (const char* encoded : {"01", "02 11", "03 11 22", "04 11 22 33", "05 11 22 33 44"}) {
    SCOPED_TRACE(encoded);
    const Bytes bytes = bytes_from_hex(encoded);
    const Received received = receive_message(view_of(bytes), Side::DEVICE);
    EXPECT_NE(received.status, ReceiveStatus::VALID);
  }
}

// ---------------------------------------------------------------- round trips

Bytes encode_for_round_trip(std::size_t index, std::uint8_t seq, unsigned a, unsigned b) {
  Bytes storage(max_wire_message_bytes, 0);
  MutableBytes out{storage.data(), storage.size()};
  std::size_t length = 0;
  switch (index) {
    case 0:
      length = encode_aim(
          seq, AimPayload{static_cast<std::int16_t>(a), static_cast<std::int16_t>(b)}, out);
      break;
    case 1:
      length = encode_fire(seq, FirePayload{static_cast<std::uint16_t>(a)}, out);
      break;
    case 2:
      length = encode_safe_state(seq, out);
      break;
    case 3:
      length = encode_reply(
          seq, ReplyPayload{static_cast<std::uint8_t>(a), static_cast<std::uint8_t>(b)}, out);
      break;
    default:
      length = encode_heartbeat(seq, HeartbeatPayload{static_cast<std::uint8_t>(a)}, out);
      break;
  }
  EXPECT_GT(length, 1U);
  storage.resize(length - 1);  // the framer strips the delimiter
  return storage;
}

// Verifies: REQ-COM-001 — encode then receive is the identity, across every
// sequence number and a spread of payloads that includes zero bytes in every
// position, so no value of any field is corrupted by the encoding.
TEST(Message, EncodeThenReceiveRecoversEveryField) {
  const unsigned values[] = {0x0000, 0x0001, 0x00FF, 0x0100, 0x0101,
                             0x7FFF, 0x8000, 0xFFFF, 0x1234, 0xFF00};

  for (unsigned seq = 0; seq < 256; ++seq) {
    const auto s = static_cast<std::uint8_t>(seq);
    for (const unsigned a : values) {
      for (const unsigned b : values) {
        const Bytes aim = encode_for_round_trip(0, s, a, b);
        const Received r_aim = receive_message(view_of(aim), Side::DEVICE);
        ASSERT_EQ(r_aim.status, ReceiveStatus::VALID);
        ASSERT_EQ(r_aim.seq, s);
        ASSERT_EQ(r_aim.aim.x_centidegrees, static_cast<std::int16_t>(a));
        ASSERT_EQ(r_aim.aim.y_centidegrees, static_cast<std::int16_t>(b));

        const Bytes reply = encode_for_round_trip(3, s, a, b);
        const Received r_reply = receive_message(view_of(reply), Side::HOST);
        ASSERT_EQ(r_reply.status, ReceiveStatus::VALID);
        ASSERT_EQ(r_reply.seq, s);
        ASSERT_EQ(r_reply.reply.command_type, static_cast<std::uint8_t>(a));
        ASSERT_EQ(r_reply.reply.result, static_cast<std::uint8_t>(b));
      }
      const Bytes fire = encode_for_round_trip(1, s, a, 0);
      const Received r_fire = receive_message(view_of(fire), Side::DEVICE);
      ASSERT_EQ(r_fire.status, ReceiveStatus::VALID);
      ASSERT_EQ(r_fire.fire.duration_ms, static_cast<std::uint16_t>(a));

      const Bytes beat = encode_for_round_trip(4, s, a, 0);
      const Received r_beat = receive_message(view_of(beat), Side::HOST);
      ASSERT_EQ(r_beat.status, ReceiveStatus::VALID);
      ASSERT_EQ(r_beat.heartbeat.flags, static_cast<std::uint8_t>(a));
    }
    const Bytes safe = encode_for_round_trip(2, s, 0, 0);
    const Received r_safe = receive_message(view_of(safe), Side::DEVICE);
    ASSERT_EQ(r_safe.status, ReceiveStatus::VALID);
    ASSERT_EQ(r_safe.type, to_byte(MessageType::SAFE_STATE));
    ASSERT_EQ(r_safe.seq, s);
  }
}

// Verifies: REQ-COM-001 — "malformed input is rejected without undefined
// behaviour": the receiver answers every input of up to two bytes, exhaustively,
// and 30000 seeded longer ones, on both sides, with a status. Whatever it calls
// valid must be a message the encoder could have produced: re-encoding the
// fields it reports is accepted and reports the same fields. Under the
// sanitizers (`make asan`) this is also the evidence that no read leaves the
// input.
TEST(MessageReceive, AnswersEveryShortInputAndSurvivesArbitraryBytes) {
  const auto check = [](const Bytes& input) {
    for (const Side side : {Side::DEVICE, Side::HOST}) {
      const Received received = receive_message(view_of(input), side);
      if (received.status != ReceiveStatus::VALID) {
        continue;
      }
      ASSERT_GE(input.size(), 4U) << to_hex(input);  // a header, a CRC and a COBS code byte
      Bytes storage(max_wire_message_bytes, 0);
      MutableBytes out{storage.data(), storage.size()};
      std::size_t length = 0;
      if (received.type == to_byte(MessageType::AIM)) {
        length = encode_aim(received.seq, received.aim, out);
      } else if (received.type == to_byte(MessageType::FIRE)) {
        length = encode_fire(received.seq, received.fire, out);
      } else if (received.type == to_byte(MessageType::SAFE_STATE)) {
        length = encode_safe_state(received.seq, out);
      } else if (received.type == to_byte(MessageType::REPLY)) {
        length = encode_reply(received.seq, received.reply, out);
      } else {
        ASSERT_EQ(received.type, to_byte(MessageType::HEARTBEAT)) << to_hex(input);
        length = encode_heartbeat(received.seq, received.heartbeat, out);
      }
      ASSERT_GT(length, 1U) << to_hex(input);
      storage.resize(length - 1);
      const Received again = receive_message(view_of(storage), side);
      ASSERT_EQ(again.status, ReceiveStatus::VALID) << to_hex(input);
      ASSERT_EQ(again.type, received.type);
      ASSERT_EQ(again.seq, received.seq);
    }
  };

  check({});
  for (unsigned first = 0; first < 256; ++first) {
    check({static_cast<std::uint8_t>(first)});
    for (unsigned second = 0; second < 256; ++second) {
      check({static_cast<std::uint8_t>(first), static_cast<std::uint8_t>(second)});
    }
  }

  std::uint32_t state = 4242;  // a fixed seed (REQ-DEV-002)
  for (int round = 0; round < 30000; ++round) {
    Bytes input;
    state = state * 1664525U + 1013904223U;
    const std::size_t length = 3 + (state >> 24U) % 20;
    for (std::size_t i = 0; i < length; ++i) {
      state = state * 1664525U + 1013904223U;
      input.push_back(static_cast<std::uint8_t>(state >> 24U));
    }
    check(input);
  }
}

}  // namespace

// Verifies: REQ-COM-001
//
// Finding the messages in a stream of bytes (`docs/architecture/device-protocol.md`,
// sections 3 and 3.4): a message ends at the delimiter, a receiver holds at most
// 16 bytes before one, and after an overflow it discards everything up to and
// including the next delimiter and parses nothing of it. The line can drop,
// corrupt and duplicate bytes, so the framer must find its feet again after any
// damage, and must never let the tail of an over-long message be read as the
// start of a new one.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "pigeon/protocol/bytes.hpp"
#include "pigeon/protocol/framer.hpp"
#include "pigeon/protocol/message.hpp"
#include "support/hex.hpp"
#include "support/vectors.hpp"

namespace {

using pigeon::protocol::ByteView;
using pigeon::protocol::FramerEvent;
using pigeon::protocol::max_encoded_message_bytes;
using pigeon::protocol::MessageFramer;
using pigeon::protocol::receive_message;
using pigeon::protocol::Received;
using pigeon::protocol::ReceiveStatus;
using pigeon::protocol::Side;
using pigeon::protocol::testing::bytes_from_hex;
using pigeon::protocol::testing::to_hex;
using pigeon::protocol::testing::valid_vectors;

using Bytes = std::vector<std::uint8_t>;

Bytes copy_of(ByteView view) {
  Bytes out;
  for (std::size_t i = 0; i < view.size(); ++i) {
    out.push_back(view.at(i));
  }
  return out;
}

// Feeds `bytes` to the framer one at a time and records every event that is
// not "nothing yet", with the message that came with it.
struct Heard {
  FramerEvent event;
  Bytes message;  // empty unless the event is MESSAGE
};

std::vector<Heard> feed(MessageFramer& framer, const Bytes& bytes) {
  std::vector<Heard> heard;
  for (const std::uint8_t byte : bytes) {
    const FramerEvent event = framer.push(byte);
    if (event == FramerEvent::NONE) {
      continue;
    }
    heard.push_back(
        Heard{event, event == FramerEvent::MESSAGE ? copy_of(framer.message()) : Bytes{}});
  }
  return heard;
}

Bytes concatenated(const Bytes& a, const Bytes& b) {
  Bytes out = a;
  out.insert(out.end(), b.begin(), b.end());
  return out;
}

// Verifies: REQ-COM-001 — a message is complete at its delimiter and not before,
// and what the framer hands on is the encoded message without it, ready for
// `receive_message`.
TEST(MessageFramer, CompletesEachSpecificationVectorAtItsDelimiterAndNotBefore) {
  for (const auto& vector : valid_vectors) {
    SCOPED_TRACE(vector.name);
    const Bytes wire = bytes_from_hex(vector.wire);
    MessageFramer framer;

    for (std::size_t i = 0; i + 1 < wire.size(); ++i) {
      ASSERT_EQ(framer.push(wire[i]), FramerEvent::NONE) << "byte " << i;
    }
    ASSERT_EQ(framer.push(wire.back()), FramerEvent::MESSAGE);

    EXPECT_EQ(to_hex(copy_of(framer.message())), to_hex(Bytes(wire.begin(), wire.end() - 1)));
  }
}

// Verifies: REQ-COM-001 — what the framer completes, the receiver understands:
// the two layers meet at exactly the bytes between the delimiters.
TEST(MessageFramer, HandsTheReceiverExactlyWhatItNeeds) {
  MessageFramer framer;
  const Bytes wire = bytes_from_hex(valid_vectors[0].wire);  // V1

  const std::vector<Heard> heard = feed(framer, wire);

  ASSERT_EQ(heard.size(), 1U);
  const Received received = receive_message(framer.message(), Side::DEVICE);
  EXPECT_EQ(received.status, ReceiveStatus::VALID);
  EXPECT_EQ(received.seq, 7);
  EXPECT_EQ(received.aim.x_centidegrees, -4500);
}

// Verifies: REQ-COM-001 — messages that follow one another are found one by one,
// each starting fresh.
TEST(MessageFramer, FindsMessagesThatFollowOneAnotherInOneStream) {
  Bytes stream;
  for (const auto& vector : valid_vectors) {
    stream = concatenated(stream, bytes_from_hex(vector.wire));
  }
  MessageFramer framer;

  const std::vector<Heard> heard = feed(framer, stream);

  ASSERT_EQ(heard.size(), valid_vectors.size());
  for (std::size_t i = 0; i < heard.size(); ++i) {
    EXPECT_EQ(heard[i].event, FramerEvent::MESSAGE);
    Bytes expected = bytes_from_hex(valid_vectors[i].wire);
    expected.pop_back();
    EXPECT_EQ(to_hex(heard[i].message), to_hex(expected)) << valid_vectors[i].name;
  }
}

// Verifies: REQ-COM-001 — section 10.2, N8, and section 5: an empty message,
// two delimiters in a row, is reported as empty and is not a message.
TEST(MessageFramer, ReportsALoneDelimiterAsEmptyAndNotAsAMessage) {
  MessageFramer framer;

  EXPECT_EQ(framer.push(0x00), FramerEvent::EMPTY);
  EXPECT_EQ(framer.push(0x00), FramerEvent::EMPTY);
  EXPECT_TRUE(framer.message().empty());
}

// Verifies: REQ-COM-001 — section 3.4: exactly 16 bytes before the delimiter is
// still a message.
TEST(MessageFramer, AcceptsAMessageOfExactlyTheReceiveBound) {
  MessageFramer framer;
  const Bytes bytes(max_encoded_message_bytes, 0x11);

  for (const std::uint8_t byte : bytes) {
    ASSERT_EQ(framer.push(byte), FramerEvent::NONE);
  }
  ASSERT_EQ(framer.push(0x00), FramerEvent::MESSAGE);

  EXPECT_EQ(framer.message().size(), max_encoded_message_bytes);
}

// Verifies: REQ-COM-001 — section 10.2, N6: one byte more than the bound is an
// over-long message, discarded when its delimiter arrives, with nothing of it
// kept.
TEST(MessageFramer, DiscardsAMessageOneByteOverTheBoundAtItsDelimiterN6) {
  MessageFramer framer;
  const Bytes bytes(max_encoded_message_bytes + 1, 0x11);

  for (const std::uint8_t byte : bytes) {
    ASSERT_EQ(framer.push(byte), FramerEvent::NONE);
  }

  EXPECT_EQ(framer.push(0x00), FramerEvent::OVERSIZE);
  EXPECT_TRUE(framer.message().empty());
}

// Verifies: REQ-COM-001 — section 3.4: after an overflow the receiver discards
// everything up to and including the next delimiter and "parses nothing of what
// it threw away": a valid message glued on to the end of an over-long one, with
// no delimiter between, is part of the junk and is not found; the one after the
// delimiter is.
TEST(MessageFramer, NeverReadsTheTailOfAnOverlongMessageAsANewOne) {
  Bytes junk(max_encoded_message_bytes, 0x11);
  Bytes valid_body = bytes_from_hex(valid_vectors[0].wire);  // V1
  const Bytes valid_wire = valid_body;
  valid_body.pop_back();                  // its delimiter is not part of the junk
  junk = concatenated(junk, valid_body);  // glued on: 16 + 10 bytes, no delimiter
  junk.push_back(0x00);
  const Bytes stream = concatenated(junk, valid_wire);  // then a real message
  MessageFramer framer;

  const std::vector<Heard> heard = feed(framer, stream);

  ASSERT_EQ(heard.size(), 2U);
  EXPECT_EQ(heard[0].event, FramerEvent::OVERSIZE);
  EXPECT_EQ(heard[1].event, FramerEvent::MESSAGE);
  Bytes expected = valid_wire;
  expected.pop_back();
  EXPECT_EQ(to_hex(heard[1].message), to_hex(expected));
}

// Verifies: REQ-COM-001 — the line drops bytes: a message cut short is lost, and
// the one after it is found, because the delimiter between them resynchronises
// the framer.
TEST(MessageFramer, ResynchronisesAtTheNextDelimiterAfterACutShortMessage) {
  Bytes wire = bytes_from_hex(valid_vectors[2].wire);  // V3
  Bytes damaged = wire;
  damaged.erase(damaged.begin() + 3, damaged.begin() + 5);  // two bytes lost mid-message
  const Bytes stream = concatenated(damaged, bytes_from_hex(valid_vectors[3].wire));  // then V4
  MessageFramer framer;

  const std::vector<Heard> heard = feed(framer, stream);

  ASSERT_EQ(heard.size(), 2U);
  // The damaged one is still *a message* to the framer; rejecting it is the
  // receiver's job, by its CRC.
  const Received damaged_received =
      receive_message(ByteView{heard[0].message.data(), heard[0].message.size()}, Side::DEVICE);
  EXPECT_NE(damaged_received.status, ReceiveStatus::VALID);
  const Received intact =
      receive_message(ByteView{heard[1].message.data(), heard[1].message.size()}, Side::DEVICE);
  EXPECT_EQ(intact.status, ReceiveStatus::VALID);
  EXPECT_EQ(intact.seq, 9);
}

// Verifies: REQ-COM-001 — a reconnection starts clean: whatever half-received
// message was in the framer is dropped by `reset()`, and the next message is
// not contaminated by it.
TEST(MessageFramer, ResetDropsAHalfReceivedMessage) {
  MessageFramer framer;
  const Bytes wire = bytes_from_hex(valid_vectors[0].wire);
  for (std::size_t i = 0; i < 5; ++i) {
    ASSERT_EQ(framer.push(wire[i]), FramerEvent::NONE);
  }

  framer.reset();
  const std::vector<Heard> heard = feed(framer, bytes_from_hex(valid_vectors[3].wire));  // V4

  ASSERT_EQ(heard.size(), 1U);
  Bytes expected = bytes_from_hex(valid_vectors[3].wire);
  expected.pop_back();
  EXPECT_EQ(to_hex(heard[0].message), to_hex(expected));
}

// Verifies: REQ-COM-001 — a reset also ends an overflow in progress: the junk
// before it is not carried into the next message.
TEST(MessageFramer, ResetEndsAnOverflowInProgress) {
  MessageFramer framer;
  for (std::size_t i = 0; i < max_encoded_message_bytes + 5; ++i) {
    ASSERT_EQ(framer.push(0x11), FramerEvent::NONE);
  }

  framer.reset();
  const std::vector<Heard> heard = feed(framer, bytes_from_hex(valid_vectors[3].wire));

  ASSERT_EQ(heard.size(), 1U);
  EXPECT_EQ(heard[0].event, FramerEvent::MESSAGE);
}

// Verifies: REQ-COM-001 — "malformed input is rejected without undefined
// behaviour": over 200000 seeded bytes of arbitrary content, every delimiter
// produces exactly one event, no other byte produces one, and no message is
// ever longer than the bound or contains the delimiter. Under the sanitizers
// (`make asan`) this is also the evidence that the framer's buffer is never
// overrun.
TEST(MessageFramer, EveryDelimiterProducesExactlyOneEventWhateverTheBytes) {
  MessageFramer framer;
  std::uint32_t state = 777;  // a fixed seed (REQ-DEV-002)
  std::size_t delimiters = 0;
  std::size_t events = 0;

  for (int i = 0; i < 200000; ++i) {
    state = state * 1664525U + 1013904223U;
    // Bias towards zero so that short messages, empty ones and overflows all occur.
    const std::uint32_t pick = (state >> 24U) % 40;
    const auto byte =
        pick < 3 ? std::uint8_t{0x00} : static_cast<std::uint8_t>(1 + (state >> 8U) % 255);

    const FramerEvent event = framer.push(byte);

    if (byte == 0x00) {
      ++delimiters;
    }
    if (event != FramerEvent::NONE) {
      ++events;
      ASSERT_EQ(byte, 0x00) << "an event on a byte that is not the delimiter";
    }
    if (event == FramerEvent::MESSAGE) {
      const ByteView message = framer.message();
      ASSERT_GE(message.size(), 1U);
      ASSERT_LE(message.size(), max_encoded_message_bytes);
      for (std::size_t j = 0; j < message.size(); ++j) {
        ASSERT_NE(message.at(j), 0x00);
      }
    }
  }

  EXPECT_EQ(events, delimiters);
  EXPECT_GT(delimiters, 1000U);
}

}  // namespace

// Verifies: REQ-COM-001
//
// Every value of every byte the decoder judges (docs/architecture/protocol.md
// §3, §4.5, §4.6, §7), rather than a sample: all 256 TYPE bytes at each
// endpoint, all 256 LEN bytes for each accepted type, and all 256 values of
// each reserved-value field. Also the §7 ordering rule that a reserved value is
// only reported once the CRC has matched.
//
// Added after the implementation, in response to code review: these tests were
// not observed failing before the code existed. Their failure was demonstrated
// instead by mutation, described in the commit that adds them.
//
// Packets with arbitrary field values are built with `crc16`, which
// crc16_test.cpp verifies independently against the catalogue check value and
// Python-computed vectors.

#include <cstdint>
#include <initializer_list>

#include <gtest/gtest.h>

#include "pigeon/protocol/protocol.hpp"
#include "support/decoding.hpp"
#include "support/vectors.hpp"

namespace {

using pigeon::protocol::crc16;
using pigeon::protocol::Decoder;
using pigeon::protocol::DecodeStatus;
using pigeon::protocol::Endpoint;
using pigeon::protocol::max_payload_size;
using pigeon::protocol::sync_byte;
using pigeon::protocol_test::accepted;
using pigeon::protocol_test::Bytes;
using pigeon::protocol_test::push_all;
using pigeon::protocol_test::repeat;
using pigeon::protocol_test::Statuses;
namespace vectors = pigeon::protocol_test;

// A packet with a correct CRC for whatever TYPE, SEQ and payload it is given.
Bytes packet(std::uint8_t type, std::uint8_t seq, const Bytes& payload) {
  Bytes covered{type, seq, static_cast<std::uint8_t>(payload.size())};
  covered.insert(covered.end(), payload.begin(), payload.end());
  const std::uint16_t crc = crc16(covered.data(), covered.size());
  Bytes bytes{sync_byte};
  bytes.insert(bytes.end(), covered.begin(), covered.end());
  bytes.push_back(static_cast<std::uint8_t>(crc >> 8U));
  bytes.push_back(static_cast<std::uint8_t>(crc & 0xFFU));
  return bytes;
}

Statuses ending_in(DecodeStatus last, std::size_t size) {
  Statuses statuses = repeat(DecodeStatus::NEED_MORE, size - 1);
  statuses.push_back(last);
  return statuses;
}

struct Accepted {
  std::uint8_t type;
  std::size_t length;
};

// The types each endpoint accepts, and their one valid length (§3).
constexpr std::initializer_list<Accepted> device_types{{0x01, 4}, {0x02, 2}, {0x03, 0}};
constexpr std::initializer_list<Accepted> host_types{{0x81, 0}, {0x82, 1}, {0x83, 3}};

bool accepts(std::initializer_list<Accepted> types, std::uint8_t type) {
  for (const Accepted& candidate : types) {
    if (candidate.type == type) {
      return true;
    }
  }
  return false;
}

// ------------------------------------------------------------- every TYPE --

TEST(DecodeExhaustive, AcceptsExactlyTheTypesSentTowardsEachEndpoint) {
  for (int value = 0; value <= 0xFF; ++value) {
    const auto type = static_cast<std::uint8_t>(value);

    Decoder device{Endpoint::DEVICE};
    EXPECT_EQ(device.push(sync_byte), DecodeStatus::NEED_MORE);
    EXPECT_EQ(device.push(type), accepts(device_types, type) ? DecodeStatus::NEED_MORE
                                                             : DecodeStatus::DISCARDED_UNKNOWN_TYPE)
        << "device, type 0x" << std::hex << value;

    Decoder host{Endpoint::HOST};
    EXPECT_EQ(host.push(sync_byte), DecodeStatus::NEED_MORE);
    EXPECT_EQ(host.push(type), accepts(host_types, type) ? DecodeStatus::NEED_MORE
                                                         : DecodeStatus::DISCARDED_UNKNOWN_TYPE)
        << "host, type 0x" << std::hex << value;
  }
}

// -------------------------------------------------------------- every LEN --

void expect_every_length(Endpoint endpoint, std::initializer_list<Accepted> types) {
  for (const Accepted& accepted_type : types) {
    for (int value = 0; value <= 0xFF; ++value) {
      const auto length = static_cast<std::size_t>(value);
      DecodeStatus expected = DecodeStatus::NEED_MORE;
      if (length > max_payload_size) {
        expected = DecodeStatus::DISCARDED_BAD_LENGTH;
      } else if (length != accepted_type.length) {
        expected = DecodeStatus::DISCARDED_WRONG_LENGTH;
      }

      Decoder decoder{endpoint};
      const Bytes header{sync_byte, accepted_type.type, 0x00, static_cast<std::uint8_t>(value)};
      EXPECT_EQ(push_all(decoder, header), ending_in(expected, header.size()))
          << "type 0x" << std::hex << int{accepted_type.type} << ", length " << std::dec << value;
    }
  }
}

TEST(DecodeExhaustive, JudgesEveryLengthByteAtTheDevice) {
  expect_every_length(Endpoint::DEVICE, device_types);
}

TEST(DecodeExhaustive, JudgesEveryLengthByteAtTheHost) {
  expect_every_length(Endpoint::HOST, host_types);
}

// ------------------------------------------------- every reserved-value field --

TEST(DecodeExhaustive, AcceptsOnlyTheDefinedNakReason) {
  for (int value = 0; value <= 0xFF; ++value) {
    const Bytes nak = packet(0x82, 0x07, Bytes{static_cast<std::uint8_t>(value)});
    Decoder decoder{Endpoint::HOST};
    EXPECT_EQ(push_all(decoder, nak),
              value == 0x01 ? accepted(nak.size())
                            : ending_in(DecodeStatus::DISCARDED_RESERVED_VALUE, nak.size()))
        << "reason 0x" << std::hex << value;
  }
}

TEST(DecodeExhaustive, AcceptsOnlyFlagBitZeroInAHeartbeat) {
  for (int value = 0; value <= 0xFF; ++value) {
    const Bytes heartbeat = packet(0x83, 0x00, Bytes{0x00, 0x2A, static_cast<std::uint8_t>(value)});
    Decoder decoder{Endpoint::HOST};
    EXPECT_EQ(push_all(decoder, heartbeat),
              value <= 0x01 ? accepted(heartbeat.size())
                            : ending_in(DecodeStatus::DISCARDED_RESERVED_VALUE, heartbeat.size()))
        << "flags 0x" << std::hex << value;
  }
}

TEST(DecodeExhaustive, AcceptsOnlySequenceNumberZeroInAHeartbeat) {
  for (int value = 0; value <= 0xFF; ++value) {
    const Bytes heartbeat = packet(0x83, static_cast<std::uint8_t>(value), Bytes{0x00, 0x2A, 0x00});
    Decoder decoder{Endpoint::HOST};
    EXPECT_EQ(push_all(decoder, heartbeat),
              value == 0x00 ? accepted(heartbeat.size())
                            : ending_in(DecodeStatus::DISCARDED_RESERVED_VALUE, heartbeat.size()))
        << "seq 0x" << std::hex << value;
  }
}

// ---------------------------------------------- the CRC is judged first (§7) --

// "Reserved field value: detected at the last CRC byte, only once the CRC has
// matched." A packet with both faults is a bad-CRC packet.
TEST(DecodeExhaustive, ReportsABadCrcBeforeAReservedValue) {
  for (const Bytes* reserved : {&vectors::nak_reserved_reason, &vectors::heartbeat_nonzero_seq,
                                &vectors::heartbeat_reserved_flag}) {
    Bytes corrupted = *reserved;
    corrupted.back() ^= 0x01U;
    Decoder decoder{Endpoint::HOST};
    EXPECT_EQ(push_all(decoder, corrupted),
              ending_in(DecodeStatus::DISCARDED_BAD_CRC, corrupted.size()));
  }
}

TEST(DecodeExhaustive, RejectsAWrongCrcAtTheHost) {
  for (const Bytes* valid :
       {&vectors::ack_example, &vectors::nak_seq7, &vectors::heartbeat_example}) {
    for (const std::size_t from_end : {1U, 2U}) {
      Bytes corrupted = *valid;
      corrupted[corrupted.size() - from_end] ^= 0x80U;
      Decoder decoder{Endpoint::HOST};
      EXPECT_EQ(push_all(decoder, corrupted),
                ending_in(DecodeStatus::DISCARDED_BAD_CRC, corrupted.size()));
    }
  }
}

}  // namespace

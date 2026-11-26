// Verifies: REQ-COM-001
//
// The checksum of `docs/architecture/device-protocol.md`, section 3.3:
// CRC-16/CCITT-FALSE, polynomial 0x1021, initial value 0xFFFF, no reflection,
// no final XOR. "Malformed input is rejected" (`REQ-COM-001`) rests on this
// check: a corrupted message must not be mistaken for a good one.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "pigeon/protocol/bytes.hpp"
#include "pigeon/protocol/crc16.hpp"
#include "support/hex.hpp"
#include "support/vectors.hpp"

namespace {

using pigeon::protocol::ByteView;
using pigeon::protocol::crc16;
using pigeon::protocol::crc16_initial;
using pigeon::protocol::testing::bytes_from_hex;
using pigeon::protocol::testing::valid_vectors;
using pigeon::protocol::testing::view_of;

// Verifies: REQ-COM-001 — the published check value for this algorithm: the
// CRC of the ASCII string "123456789" is 0x29B1. It is what distinguishes
// CCITT-FALSE from the many other CRC-16s that differ in one parameter.
TEST(Crc16, MatchesThePublishedCheckValue) {
  const std::vector<std::uint8_t> text{'1', '2', '3', '4', '5', '6', '7', '8', '9'};

  EXPECT_EQ(crc16(view_of(text)), 0x29B1);
}

// Verifies: REQ-COM-001
TEST(Crc16, OfNothingIsTheInitialValue) {
  EXPECT_EQ(crc16(ByteView{}), crc16_initial);
  EXPECT_EQ(crc16_initial, 0xFFFF);
}

// Verifies: REQ-COM-001 — a CRC can be continued across two runs, which is how
// a receiver that sees the header before the payload computes it.
TEST(Crc16, ContinuesAcrossTwoRunsAsIfOneRun) {
  const std::vector<std::uint8_t> whole = bytes_from_hex("01 01 07 6C EE DC 05");
  const std::vector<std::uint8_t> head = bytes_from_hex("01 01 07");
  const std::vector<std::uint8_t> tail = bytes_from_hex("6C EE DC 05");

  EXPECT_EQ(crc16(view_of(tail), crc16(view_of(head))), crc16(view_of(whole)));
}

// Verifies: REQ-COM-001 — each vector of section 10.1 carries the CRC of its own
// body, little-endian, in its last two bytes.
TEST(Crc16, EverySpecificationVectorCarriesItsOwnCrcLittleEndian) {
  for (const auto& vector : valid_vectors) {
    SCOPED_TRACE(vector.name);
    const std::vector<std::uint8_t> bytes = bytes_from_hex(vector.body_and_crc);
    ASSERT_GE(bytes.size(), 2U);
    const std::size_t body_size = bytes.size() - 2;

    const std::uint16_t carried =
        static_cast<std::uint16_t>(bytes[body_size] | (bytes[body_size + 1] << 8));

    EXPECT_EQ(crc16(ByteView{bytes.data(), body_size}), carried);
  }
}

// Verifies: REQ-COM-001 — "malformed input is rejected": flipping any single
// bit of any specified body changes its CRC, so no one-bit corruption passes.
TEST(Crc16, DetectsEverySingleBitFlipInEverySpecificationBody) {
  for (const auto& vector : valid_vectors) {
    SCOPED_TRACE(vector.name);
    std::vector<std::uint8_t> bytes = bytes_from_hex(vector.body_and_crc);
    const std::size_t body_size = bytes.size() - 2;
    const std::uint16_t original = crc16(ByteView{bytes.data(), body_size});

    for (std::size_t index = 0; index < body_size; ++index) {
      for (int bit = 0; bit < 8; ++bit) {
        bytes[index] = static_cast<std::uint8_t>(bytes[index] ^ (1U << bit));
        EXPECT_NE(crc16(ByteView{bytes.data(), body_size}), original)
            << "byte " << index << " bit " << bit;
        bytes[index] = static_cast<std::uint8_t>(bytes[index] ^ (1U << bit));
      }
    }
  }
}

}  // namespace

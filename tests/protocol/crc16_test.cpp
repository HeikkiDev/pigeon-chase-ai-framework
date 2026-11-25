// Verifies: REQ-COM-001
//
// The packet checksum (docs/architecture/protocol.md §2): CRC-16/CCITT-FALSE.
// Expected values come from the catalogued check value and from an independent
// Python implementation, never from the code under test.

#include <cstdint>
#include <string_view>

#include <gtest/gtest.h>

#include "pigeon/protocol/protocol.hpp"
#include "support/vectors.hpp"

namespace {

using pigeon::protocol::crc16;
using pigeon::protocol_test::Bytes;

std::uint16_t crc_of(const Bytes& bytes) { return crc16(bytes.data(), bytes.size()); }

// The CRC a packet carries in its last two bytes, big-endian.
std::uint16_t trailing_crc(const Bytes& packet) {
  const auto high = static_cast<std::uint16_t>(packet[packet.size() - 2]);
  const auto low = static_cast<std::uint16_t>(packet[packet.size() - 1]);
  return static_cast<std::uint16_t>((high << 8U) | low);
}

// The bytes a packet's CRC covers: TYPE through PAYLOAD (protocol.md §2).
Bytes covered_bytes(const Bytes& packet) { return {packet.begin() + 1, packet.end() - 2}; }

TEST(Crc16, MatchesTheCatalogueCheckValue) {
  constexpr std::string_view check = "123456789";
  Bytes bytes(check.begin(), check.end());
  EXPECT_EQ(crc_of(bytes), 0x29B1U);
}

TEST(Crc16, OfNoBytesIsTheInitialValue) { EXPECT_EQ(crc16(nullptr, 0), 0xFFFFU); }

TEST(Crc16, OfASingleZeroByte) { EXPECT_EQ(crc_of(Bytes{0x00}), 0xE1F0U); }

TEST(Crc16, CoversTypeThroughPayloadOfEveryWorkedExample) {
  for (const Bytes* packet :
       {&pigeon::protocol_test::aim_example, &pigeon::protocol_test::ack_example,
        &pigeon::protocol_test::fire_example, &pigeon::protocol_test::heartbeat_example}) {
    EXPECT_EQ(crc_of(covered_bytes(*packet)), trailing_crc(*packet));
  }
}

TEST(Crc16, ChangesWhenAnyCoveredByteChanges) {
  const Bytes covered = covered_bytes(pigeon::protocol_test::aim_example);
  const std::uint16_t original = crc_of(covered);
  for (std::size_t index = 0; index < covered.size(); ++index) {
    Bytes corrupted = covered;
    corrupted[index] ^= 0x01U;
    EXPECT_NE(crc_of(corrupted), original) << "flipped bit 0 of byte " << index;
  }
}

}  // namespace

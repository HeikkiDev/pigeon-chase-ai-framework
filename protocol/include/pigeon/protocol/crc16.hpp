#pragma once

#include <cstddef>
#include <cstdint>

#include "pigeon/protocol/bytes.hpp"

namespace pigeon::protocol {

/// The initial value of the checksum, and the CRC of no bytes at all.
inline constexpr std::uint16_t crc16_initial = 0xFFFF;

/// The generator polynomial, x^16 + x^12 + x^5 + 1.
inline constexpr std::uint16_t crc16_polynomial = 0x1021;

/// CRC-16/CCITT-FALSE of `data`: polynomial 0x1021, initial value 0xFFFF, no
/// input or output reflection, no final XOR (`device-protocol.md`, section 3.3).
/// The CRC of the ASCII string `123456789` is 0x29B1.
///
/// Pass the CRC of the bytes so far as `crc` to continue across two runs: a
/// receiver that sees the header before the payload checks the whole message
/// without joining them.
///
/// Computed a bit at a time, with no table. A 256-entry table is half a
/// kilobyte on a device with two, and the messages are at most seven bytes.
///
/// The arithmetic is done in `std::uint32_t`, which is unsigned and wide enough
/// on every target. Doing it in `std::uint16_t` would be promoted to *signed*
/// `int`, which is 16 bits on many AVR boards and 32 on the host, so the same
/// source would mean different things at the two ends of the link.
[[nodiscard]] constexpr std::uint16_t crc16(ByteView data,
                                            std::uint16_t crc = crc16_initial) noexcept {
  constexpr std::uint32_t polynomial = crc16_polynomial;
  constexpr std::uint32_t top_bit = 0x8000U;
  constexpr std::uint32_t mask = 0xFFFFU;

  std::uint32_t value = crc;
  for (std::size_t index = 0; index < data.size(); ++index) {
    value ^= static_cast<std::uint32_t>(data.at(index)) << 8U;
    for (unsigned int bit = 0; bit < 8U; ++bit) {
      value =
          (value & top_bit) != 0U ? (((value << 1U) ^ polynomial) & mask) : ((value << 1U) & mask);
    }
  }
  return static_cast<std::uint16_t>(value);
}

}  // namespace pigeon::protocol

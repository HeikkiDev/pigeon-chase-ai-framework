#pragma once

// The normative test vectors of `docs/architecture/device-protocol.md`,
// section 10, copied exactly. The specification is the authority: if this file
// and the document disagree, the document is right and this file is a defect.
//
// Every byte was produced by a reference implementation whose COBS encoder was
// checked against the published COBS examples and whose CRC against the check
// value 0x29B1, and then recomputed independently by a second implementation
// during review.

#include <array>

namespace pigeon::protocol::testing {

/// A message that must encode and decode exactly (section 10.1).
struct ValidVector {
  const char* name;
  /// The body followed by its little-endian CRC, before COBS.
  const char* body_and_crc;
  /// The whole message on the wire, including the 0x00 delimiter.
  const char* wire;
};

inline constexpr std::array<ValidVector, 8> valid_vectors{{
    {"V1 AIM seq 7 X -45.00 Y 15.00", "01 01 07 6C EE DC 05 E4 C6",
     "0A 01 01 07 6C EE DC 05 E4 C6 00"},
    {"V2 AIM seq 0 X 0 Y 0", "01 01 00 00 00 00 00 0F 0C", "03 01 01 01 01 01 01 03 0F 0C 00"},
    {"V3 FIRE seq 8 500 ms", "01 02 08 F4 01 B0 30", "08 01 02 08 F4 01 B0 30 00"},
    {"V4 SAFE_STATE seq 9", "01 03 09 D6 3F", "06 01 03 09 D6 3F 00"},
    {"V5 REPLY seq 7 to AIM ACCEPTED", "01 81 07 01 00 70 A6", "05 01 81 07 01 03 70 A6 00"},
    {"V6 REPLY seq 8 to FIRE REJECTED_DURATION", "01 81 08 02 02 50 FF",
     "08 01 81 08 02 02 50 FF 00"},
    {"V7 HEARTBEAT seq 0 flags 0", "01 82 00 00 4E A7", "03 01 82 01 03 4E A7 00"},
    {"V8 HEARTBEAT seq 255 actuator_active", "01 82 FF 01 90 B4", "07 01 82 FF 01 90 B4 00"},
}};

}  // namespace pigeon::protocol::testing

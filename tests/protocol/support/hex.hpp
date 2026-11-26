#pragma once

// Test support: bytes written the way `docs/architecture/device-protocol.md`
// writes them, as hexadecimal text, so a vector in a test can be compared with
// the specification by eye.

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "pigeon/protocol/bytes.hpp"

namespace pigeon::protocol::testing {

/// Parses "0A 01 ff" into bytes. Whitespace separates digits and is ignored.
/// Throws on anything that is not a pair of hexadecimal digits: a typo in a
/// vector must fail loudly, not become a different vector.
inline std::vector<std::uint8_t> bytes_from_hex(std::string_view text) {
  std::vector<std::uint8_t> out;
  int high_nibble = -1;
  for (const char c : text) {
    int digit = -1;
    if (c >= '0' && c <= '9') {
      digit = c - '0';
    } else if (c >= 'a' && c <= 'f') {
      digit = c - 'a' + 10;
    } else if (c >= 'A' && c <= 'F') {
      digit = c - 'A' + 10;
    } else if (c == ' ' || c == '\t' || c == '\n') {
      if (high_nibble >= 0) {
        throw std::invalid_argument("odd number of hex digits");
      }
      continue;
    } else {
      throw std::invalid_argument(std::string("not a hex digit: ") + c);
    }
    if (high_nibble < 0) {
      high_nibble = digit;
    } else {
      out.push_back(static_cast<std::uint8_t>((high_nibble << 4) | digit));
      high_nibble = -1;
    }
  }
  if (high_nibble >= 0) {
    throw std::invalid_argument("odd number of hex digits");
  }
  return out;
}

/// A read-only view of a vector's bytes. The vector must outlive the view.
inline ByteView view_of(const std::vector<std::uint8_t>& bytes) {
  return ByteView{bytes.data(), bytes.size()};
}

/// The same text form the specification uses, for readable failure messages.
inline std::string to_hex(const std::vector<std::uint8_t>& bytes) {
  static constexpr char digits[] = "0123456789ABCDEF";
  std::string out;
  for (const std::uint8_t b : bytes) {
    if (!out.empty()) {
      out += ' ';
    }
    out += digits[b >> 4];
    out += digits[b & 0x0F];
  }
  return out;
}

}  // namespace pigeon::protocol::testing

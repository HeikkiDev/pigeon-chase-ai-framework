#pragma once

#include <cstddef>
#include <cstdint>

#include "pigeon/protocol/bytes.hpp"

namespace pigeon::protocol {

/// How a COBS operation ended.
enum class CobsStatus : std::uint8_t {
  /// It succeeded; `CobsResult::length` bytes were written.
  OK,
  /// The input is not a COBS encoding: it contains a zero byte, or a code byte
  /// promises more bytes than the input holds, or it is empty.
  MALFORMED,
  /// The output buffer is smaller than the result needs. Nothing was written
  /// beyond it.
  OUTPUT_TOO_SMALL,
};

/// The outcome of a COBS operation. `length` is meaningful only when `status`
/// is `OK`, and is zero otherwise. What a failed call left in the output buffer
/// is unspecified, but it never lies outside the buffer.
///
/// It has a constructor rather than being an aggregate: designated initialisers
/// are C++20, which the firmware toolchain may not have, and positional ones
/// are what clang-tidy rightly objects to.
struct CobsResult {
  constexpr CobsResult() noexcept = default;
  constexpr CobsResult(CobsStatus outcome, std::size_t produced) noexcept
      : status(outcome), length(produced) {}

  CobsStatus status{CobsStatus::MALFORMED};
  std::size_t length{0};
};

/// The most bytes `cobs_encode` can produce for `input_size` bytes of input:
/// one code byte, plus one more for each full block of 254. For the protocol's
/// messages, at most nine bytes, that is ten (`device-protocol.md`, section 3.4).
/// Meant for message-sized inputs: it wraps for a size near the largest
/// `std::size_t`, which is only 65535 on a 16-bit target.
[[nodiscard]] constexpr std::size_t cobs_max_encoded_size(std::size_t input_size) noexcept {
  return input_size + (input_size / 254U) + 1U;
}

/// Consistent Overhead Byte Stuffing: encodes `input` into `output` so that the
/// result contains no zero byte, which can then delimit a message
/// (`device-protocol.md`, section 3). The delimiter itself is not written.
///
/// Returns `OUTPUT_TOO_SMALL` if `output` cannot hold the encoding; never
/// writes past it. `cobs_max_encoded_size(input.size())` bytes always suffice.
[[nodiscard]] constexpr CobsResult cobs_encode(ByteView input, MutableBytes output) noexcept {
  constexpr std::uint8_t full_block = 0xFF;

  // Each block of up to 254 non-zero bytes is preceded by a code byte, written
  // last because it holds the block's length. `code_index` is where it goes.
  std::size_t code_index = 0;
  std::size_t next = 1;
  std::uint8_t code = 1;
  bool fits = output.set(0, 0);

  for (std::size_t index = 0; fits && index < input.size(); ++index) {
    const std::uint8_t byte = input.at(index);
    if (byte != 0) {
      fits = output.set(next, byte);
      ++next;
      ++code;
    }
    if (byte == 0 || code == full_block) {
      fits = fits && output.set(code_index, code);
      code_index = next;
      fits = fits && output.set(next, 0);
      ++next;
      code = 1;
    }
  }
  fits = fits && output.set(code_index, code);

  if (!fits) {
    return CobsResult{CobsStatus::OUTPUT_TOO_SMALL, 0};
  }
  return CobsResult{CobsStatus::OK, next};
}

/// Decodes a COBS encoding (without its delimiter) into `output`.
///
/// Total on every input: anything that is not an encoding is `MALFORMED`, and a
/// buffer that is too small is `OUTPUT_TOO_SMALL`. It never reads outside
/// `encoded` and never writes outside `output`, whatever the bytes say.
///
/// It does not insist on the canonical form: an encoding that a conforming
/// encoder would not produce, but that decodes unambiguously, is accepted. The
/// CRC the protocol puts inside the encoding is what rejects a corrupted one.
[[nodiscard]] constexpr CobsResult cobs_decode(ByteView encoded, MutableBytes output) noexcept {
  constexpr std::size_t full_block = 0xFF;

  if (encoded.empty()) {
    return CobsResult{CobsStatus::MALFORMED, 0};
  }

  std::size_t read = 0;
  std::size_t written = 0;
  while (read < encoded.size()) {
    const std::size_t code = encoded.at(read);
    if (code == 0) {
      return CobsResult{CobsStatus::MALFORMED, 0};
    }
    ++read;

    const std::size_t data_bytes = code - 1;
    if (data_bytes > encoded.size() - read) {
      return CobsResult{CobsStatus::MALFORMED, 0};
    }
    for (std::size_t offset = 0; offset < data_bytes; ++offset) {
      const std::uint8_t byte = encoded.at(read + offset);
      if (byte == 0) {
        return CobsResult{CobsStatus::MALFORMED, 0};
      }
      if (!output.set(written, byte)) {
        return CobsResult{CobsStatus::OUTPUT_TOO_SMALL, 0};
      }
      ++written;
    }
    read += data_bytes;

    // A block implies a zero after it, except a full block (which says "more
    // non-zero bytes follow") and the last block (whose zero is the delimiter).
    if (code != full_block && read < encoded.size()) {
      if (!output.set(written, 0)) {
        return CobsResult{CobsStatus::OUTPUT_TOO_SMALL, 0};
      }
      ++written;
    }
  }
  return CobsResult{CobsStatus::OK, written};
}

}  // namespace pigeon::protocol

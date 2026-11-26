// Verifies: REQ-COM-001
//
// Consistent Overhead Byte Stuffing, which is what makes `0x00` an unambiguous
// message delimiter (`docs/architecture/device-protocol.md`, section 3). The
// receiver runs this on whatever the line delivers, so the properties that
// matter are the safety ones: every input is answered with a status, no output
// is written past the buffer it was given, and an encoding never contains the
// delimiter.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "pigeon/protocol/bytes.hpp"
#include "pigeon/protocol/cobs.hpp"
#include "support/hex.hpp"
#include "support/vectors.hpp"

namespace {

using pigeon::protocol::ByteView;
using pigeon::protocol::cobs_decode;
using pigeon::protocol::cobs_encode;
using pigeon::protocol::cobs_max_encoded_size;
using pigeon::protocol::CobsResult;
using pigeon::protocol::CobsStatus;
using pigeon::protocol::MutableBytes;
using pigeon::protocol::testing::bytes_from_hex;
using pigeon::protocol::testing::to_hex;
using pigeon::protocol::testing::valid_vectors;
using pigeon::protocol::testing::view_of;

using Bytes = std::vector<std::uint8_t>;

constexpr std::uint8_t guard_byte = 0xA5;
constexpr std::size_t guard_size = 8;

// A buffer of `capacity` writable bytes followed by guard bytes that no call may
// touch, so that a write past the end is observed instead of merely corrupting
// memory somewhere else.
class GuardedBuffer {
 public:
  explicit GuardedBuffer(std::size_t capacity)
      : capacity_(capacity), storage_(capacity + guard_size, guard_byte) {}

  MutableBytes writable() { return MutableBytes{storage_.data(), capacity_}; }

  Bytes written(std::size_t length) const {
    return Bytes(storage_.begin(), storage_.begin() + static_cast<std::ptrdiff_t>(length));
  }

  bool guard_is_intact() const {
    for (std::size_t i = capacity_; i < storage_.size(); ++i) {
      if (storage_[i] != guard_byte) {
        return false;
      }
    }
    return true;
  }

 private:
  std::size_t capacity_;
  Bytes storage_;
};

Bytes encode_or_fail(const Bytes& input) {
  GuardedBuffer out{cobs_max_encoded_size(input.size())};
  const CobsResult result = cobs_encode(view_of(input), out.writable());
  EXPECT_EQ(result.status, CobsStatus::OK);
  EXPECT_TRUE(out.guard_is_intact());
  return out.written(result.length);
}

// Verifies: REQ-COM-001 — the published COBS examples (Cheshire and Baker). The
// reference implementation that produced the specification's vectors was
// checked against these before it was trusted.
TEST(CobsEncode, ReproducesThePublishedExamples) {
  struct Example {
    const char* raw;
    const char* encoded;
  };
  const Example examples[] = {
      {"", "01"},
      {"00", "01 01"},
      {"00 00", "01 01 01"},
      {"11 22 33 44", "05 11 22 33 44"},
      {"11 22 00 33", "03 11 22 02 33"},
      {"11 22 33 00", "04 11 22 33 01"},
      {"11 00 00 00", "02 11 01 01 01"},
  };

  for (const Example& example : examples) {
    SCOPED_TRACE(example.raw);
    EXPECT_EQ(to_hex(encode_or_fail(bytes_from_hex(example.raw))), example.encoded);
  }
}

// Verifies: REQ-COM-001 — section 10.1: each specification vector's body and
// CRC encode to exactly the bytes on the wire, less the delimiter.
TEST(CobsEncode, ProducesTheSpecificationVectorsOnTheWire) {
  for (const auto& vector : valid_vectors) {
    SCOPED_TRACE(vector.name);
    Bytes wire = bytes_from_hex(vector.wire);
    ASSERT_FALSE(wire.empty());
    ASSERT_EQ(wire.back(), 0x00);
    wire.pop_back();

    EXPECT_EQ(to_hex(encode_or_fail(bytes_from_hex(vector.body_and_crc))), to_hex(wire));
  }
}

// Verifies: REQ-COM-001 — section 10.1, the other way.
TEST(CobsDecode, RecoversTheSpecificationVectorsFromTheWire) {
  for (const auto& vector : valid_vectors) {
    SCOPED_TRACE(vector.name);
    Bytes wire = bytes_from_hex(vector.wire);
    wire.pop_back();
    GuardedBuffer out{16};

    const CobsResult result = cobs_decode(view_of(wire), out.writable());

    ASSERT_EQ(result.status, CobsStatus::OK);
    EXPECT_EQ(to_hex(out.written(result.length)), to_hex(bytes_from_hex(vector.body_and_crc)));
    EXPECT_TRUE(out.guard_is_intact());
  }
}

// The inputs the properties below are run over: every length from 0 to 600,
// which crosses the 254-byte block boundary twice, in five shapes.
std::vector<Bytes> sample_inputs() {
  std::vector<Bytes> inputs;
  std::uint32_t state = 12345;  // a fixed seed (REQ-DEV-002)
  for (std::size_t length = 0; length <= 600; ++length) {
    Bytes zeros(length, 0x00);
    Bytes nonzero(length, 0x11);
    Bytes ascending;
    Bytes alternating;
    Bytes pseudo_random;
    for (std::size_t i = 0; i < length; ++i) {
      ascending.push_back(static_cast<std::uint8_t>(1 + (i % 255)));
      alternating.push_back(i % 2 == 0 ? std::uint8_t{0x00} : std::uint8_t{0x7F});
      state = state * 1664525U + 1013904223U;
      pseudo_random.push_back(static_cast<std::uint8_t>(state >> 24U));
    }
    inputs.push_back(zeros);
    inputs.push_back(nonzero);
    inputs.push_back(ascending);
    inputs.push_back(alternating);
    inputs.push_back(pseudo_random);
  }
  return inputs;
}

// Verifies: REQ-COM-001 — the delimiter is unambiguous only if no encoding ever
// contains it, whatever it encodes.
TEST(CobsEncode, NeverProducesTheDelimiterInsideAnEncoding) {
  for (const Bytes& input : sample_inputs()) {
    for (const std::uint8_t byte : encode_or_fail(input)) {
      ASSERT_NE(byte, 0x00) << "input length " << input.size();
    }
  }
}

// Verifies: REQ-COM-001 — decoding undoes encoding for every sampled input,
// including those that straddle a 254-byte block.
TEST(Cobs, RoundTripsEverySampledInput) {
  for (const Bytes& input : sample_inputs()) {
    const Bytes encoded = encode_or_fail(input);
    GuardedBuffer out{input.size()};

    const CobsResult result = cobs_decode(view_of(encoded), out.writable());

    ASSERT_EQ(result.status, CobsStatus::OK) << "input length " << input.size();
    EXPECT_EQ(out.written(result.length), input);
    EXPECT_TRUE(out.guard_is_intact());
  }
}

// Verifies: REQ-COM-001 — the overhead bound the receive buffer is sized by.
TEST(CobsEncode, NeverExceedsTheStatedMaximumSize) {
  for (const Bytes& input : sample_inputs()) {
    EXPECT_LE(encode_or_fail(input).size(), cobs_max_encoded_size(input.size()));
  }
  EXPECT_EQ(cobs_max_encoded_size(0), 1U);
  EXPECT_EQ(cobs_max_encoded_size(9), 10U);
}

// Verifies: REQ-COM-001 — an encoder given a buffer that is too small reports
// it and never writes beyond what it was given, at every capacity.
TEST(CobsEncode, ReportsAnUndersizedBufferAndNeverWritesPastIt) {
  const Bytes input = bytes_from_hex("11 22 00 33 00 00 44");
  const std::size_t needed = encode_or_fail(input).size();

  for (std::size_t capacity = 0; capacity < needed; ++capacity) {
    GuardedBuffer out{capacity};

    const CobsResult result = cobs_encode(view_of(input), out.writable());

    EXPECT_EQ(result.status, CobsStatus::OUTPUT_TOO_SMALL) << "capacity " << capacity;
    EXPECT_EQ(result.length, 0U);
    EXPECT_TRUE(out.guard_is_intact()) << "capacity " << capacity;
  }

  GuardedBuffer exact{needed};
  EXPECT_EQ(cobs_encode(view_of(input), exact.writable()).status, CobsStatus::OK);
  EXPECT_TRUE(exact.guard_is_intact());
}

// Verifies: REQ-COM-001 — "malformed input is rejected": a zero inside an
// encoding cannot occur in one, so it is refused.
TEST(CobsDecode, RejectsAZeroByteInsideTheEncoding) {
  GuardedBuffer out{16};

  EXPECT_EQ(cobs_decode(view_of(bytes_from_hex("03 11 00 22")), out.writable()).status,
            CobsStatus::MALFORMED);
  EXPECT_EQ(cobs_decode(view_of(bytes_from_hex("00")), out.writable()).status,
            CobsStatus::MALFORMED);
  EXPECT_TRUE(out.guard_is_intact());
}

// Verifies: REQ-COM-001 — specification section 10.2, N7: a code byte that
// promises more bytes than the message holds.
TEST(CobsDecode, RejectsACodeByteThatOverrunsTheMessage) {
  GuardedBuffer out{16};

  EXPECT_EQ(cobs_decode(view_of(bytes_from_hex("05 11 22")), out.writable()).status,
            CobsStatus::MALFORMED);
  EXPECT_EQ(cobs_decode(view_of(bytes_from_hex("FF 01 02")), out.writable()).status,
            CobsStatus::MALFORMED);
  EXPECT_TRUE(out.guard_is_intact());
}

// Verifies: REQ-COM-001 — an encoding has at least one byte, even for no data,
// so nothing at all is not an encoding.
TEST(CobsDecode, RejectsAnEmptyEncoding) {
  GuardedBuffer out{16};

  EXPECT_EQ(cobs_decode(ByteView{}, out.writable()).status, CobsStatus::MALFORMED);
}

// Verifies: REQ-COM-001 — a decoder given too small an output reports it and
// never writes beyond it, at every capacity.
TEST(CobsDecode, ReportsAnUndersizedBufferAndNeverWritesPastIt) {
  const Bytes encoded = bytes_from_hex("03 11 22 02 33");  // decodes to 11 22 00 33
  const std::size_t needed = 4;

  for (std::size_t capacity = 0; capacity < needed; ++capacity) {
    GuardedBuffer out{capacity};

    const CobsResult result = cobs_decode(view_of(encoded), out.writable());

    EXPECT_EQ(result.status, CobsStatus::OUTPUT_TOO_SMALL) << "capacity " << capacity;
    EXPECT_TRUE(out.guard_is_intact()) << "capacity " << capacity;
  }
}

// Verifies: REQ-COM-001 — "malformed input is rejected without undefined
// behaviour": the decoder answers every input of up to two bytes, exhaustively,
// and a sample of longer ones, with one of its three statuses, never writing
// beyond its buffer and never claiming more output than it was given. Run under
// the sanitizers (`make asan`), this is also the evidence that no read leaves
// the input.
TEST(CobsDecode, AnswersEveryShortInputWithoutOverrunningAnything) {
  const auto check = [](const Bytes& input) {
    GuardedBuffer out{6};
    const CobsResult result = cobs_decode(view_of(input), out.writable());
    ASSERT_TRUE(out.guard_is_intact()) << to_hex(input);
    ASSERT_LE(result.length, 6U) << to_hex(input);
    if (result.status == CobsStatus::OK) {
      // Whatever decoded cleanly must encode back to something that decodes to
      // the same bytes: the decoder never accepts what it cannot represent.
      const Bytes decoded = out.written(result.length);
      GuardedBuffer again{decoded.size()};
      const CobsResult redone = cobs_decode(view_of(encode_or_fail(decoded)), again.writable());
      ASSERT_EQ(redone.status, CobsStatus::OK) << to_hex(input);
      ASSERT_EQ(again.written(redone.length), decoded) << to_hex(input);
    }
  };

  check({});
  for (unsigned first = 0; first < 256; ++first) {
    check({static_cast<std::uint8_t>(first)});
    for (unsigned second = 0; second < 256; ++second) {
      check({static_cast<std::uint8_t>(first), static_cast<std::uint8_t>(second)});
    }
  }

  std::uint32_t state = 2024;  // a fixed seed (REQ-DEV-002)
  for (int round = 0; round < 20000; ++round) {
    Bytes input;
    state = state * 1664525U + 1013904223U;
    const std::size_t length = 3 + (state >> 24U) % 18;
    for (std::size_t i = 0; i < length; ++i) {
      state = state * 1664525U + 1013904223U;
      input.push_back(static_cast<std::uint8_t>(state >> 24U));
    }
    check(input);
  }
}

}  // namespace

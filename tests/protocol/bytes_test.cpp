// Verifies: REQ-COM-001
//
// The two byte views every other part of the codec is written in terms of.
// `REQ-COM-001` requires that "malformed input is rejected without undefined
// behaviour", and the codec reads and writes untrusted bytes through nothing
// else, so the property worth proving here is that an out-of-range access is
// answered, never performed.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "pigeon/protocol/bytes.hpp"

namespace {

using pigeon::protocol::ByteView;
using pigeon::protocol::MutableBytes;

constexpr std::size_t largest_index = std::numeric_limits<std::size_t>::max();

// Verifies: REQ-COM-001 — a view that was never given bytes is empty and safe
// to query, so a default-constructed one cannot be mistaken for a buffer.
TEST(ByteView, IsEmptyByDefault) {
  const ByteView view;

  EXPECT_EQ(view.size(), 0U);
  EXPECT_TRUE(view.empty());
  EXPECT_EQ(view.at(0), 0);
}

// Verifies: REQ-COM-001
TEST(ByteView, ReadsTheByteAtAnInRangeIndex) {
  const std::array<std::uint8_t, 3> bytes{0x11, 0x22, 0x33};
  const ByteView view{bytes.data(), bytes.size()};

  EXPECT_EQ(view.size(), 3U);
  EXPECT_FALSE(view.empty());
  EXPECT_EQ(view.at(0), 0x11);
  EXPECT_EQ(view.at(2), 0x33);
}

// Verifies: REQ-COM-001 — "malformed input is rejected without undefined
// behaviour": an index past the end yields zero rather than reading beyond the
// buffer, whatever the index.
TEST(ByteView, AnswersZeroForAnyIndexPastTheEnd) {
  const std::array<std::uint8_t, 2> bytes{0xAA, 0xBB};
  const ByteView view{bytes.data(), bytes.size()};

  EXPECT_EQ(view.at(2), 0);
  EXPECT_EQ(view.at(3), 0);
  EXPECT_EQ(view.at(largest_index), 0);
}

// Verifies: REQ-COM-001 — a null pointer with a zero size is an empty view and
// is never dereferenced.
TEST(ByteView, TreatsANullPointerWithZeroSizeAsEmpty) {
  const ByteView view{nullptr, 0};

  EXPECT_TRUE(view.empty());
  EXPECT_EQ(view.at(0), 0);
  EXPECT_TRUE(view.subview(0, 4).empty());
}

// Verifies: REQ-COM-001
TEST(ByteView, SubviewIsTheRequestedRun) {
  const std::array<std::uint8_t, 5> bytes{1, 2, 3, 4, 5};
  const ByteView view{bytes.data(), bytes.size()};

  const ByteView middle = view.subview(1, 3);

  EXPECT_EQ(middle.size(), 3U);
  EXPECT_EQ(middle.at(0), 2);
  EXPECT_EQ(middle.at(2), 4);
}

// Verifies: REQ-COM-001 — a run that overshoots is clipped to what exists, and
// one that starts past the end is empty; neither can name bytes outside the view.
TEST(ByteView, SubviewIsClippedToTheView) {
  const std::array<std::uint8_t, 4> bytes{1, 2, 3, 4};
  const ByteView view{bytes.data(), bytes.size()};

  EXPECT_EQ(view.subview(2, 100).size(), 2U);
  EXPECT_EQ(view.subview(2, largest_index).size(), 2U);
  EXPECT_TRUE(view.subview(4, 1).empty());
  EXPECT_TRUE(view.subview(100, 1).empty());
  EXPECT_TRUE(view.subview(largest_index, largest_index).empty());
}

// Verifies: REQ-COM-001
TEST(MutableBytes, WritesAnInRangeIndexAndReadsItBack) {
  std::array<std::uint8_t, 3> bytes{};
  MutableBytes out{bytes.data(), bytes.size()};

  EXPECT_TRUE(out.set(1, 0x5A));

  EXPECT_EQ(out.at(1), 0x5A);
  EXPECT_EQ(bytes[1], 0x5A);
  EXPECT_EQ(bytes[0], 0);
  EXPECT_EQ(bytes[2], 0);
}

// Verifies: REQ-COM-001 — a write past the end is refused and changes nothing,
// so an encoder given too small a buffer cannot overrun it.
TEST(MutableBytes, RefusesAWritePastTheEndAndWritesNothing) {
  std::array<std::uint8_t, 2> bytes{0x01, 0x02};
  MutableBytes out{bytes.data(), bytes.size()};

  EXPECT_FALSE(out.set(2, 0xFF));
  EXPECT_FALSE(out.set(largest_index, 0xFF));

  EXPECT_EQ(bytes[0], 0x01);
  EXPECT_EQ(bytes[1], 0x02);
  EXPECT_EQ(out.at(2), 0);
}

// Verifies: REQ-COM-001
TEST(MutableBytes, IsEmptyByDefaultAndRefusesEveryWrite) {
  MutableBytes out;

  EXPECT_EQ(out.size(), 0U);
  EXPECT_FALSE(out.set(0, 1));
}

// Verifies: REQ-COM-001 — the written prefix can be handed on as a read-only
// view, clipped to the buffer.
TEST(MutableBytes, ViewsItsWrittenPrefixReadOnly) {
  std::array<std::uint8_t, 4> bytes{9, 8, 7, 6};
  const MutableBytes out{bytes.data(), bytes.size()};

  const ByteView prefix = out.view(2);

  EXPECT_EQ(prefix.size(), 2U);
  EXPECT_EQ(prefix.at(0), 9);
  EXPECT_EQ(prefix.at(1), 8);
  EXPECT_EQ(out.view(100).size(), 4U);
}

}  // namespace

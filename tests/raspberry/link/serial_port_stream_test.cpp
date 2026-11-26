// Verifies: REQ-COM-001, REQ-COM-002, REQ-DEV-003
//
// The host's serial port (`docs/architecture/device-protocol.md`, section 2) as a
// `ByteStream`, tested through a pseudo-terminal pair: software, not hardware
// (maintainer ruling Q27), so these run on every machine. What a pseudo-terminal
// cannot show is the line speed, parity and stop bits, which have no meaning
// without a real port; those are for a hand-run check on the board (ADR-0021,
// follow-up 3). What it can show is the part that corrupts binary data when it is
// wrong: the port must be in **raw mode**, so that no byte value is translated,
// echoed, interpreted as a signal or swallowed as flow control.

#include <gtest/gtest.h>

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>

#include "link_support/pseudo_terminal.hpp"
#include "pigeon/raspberry/serial_port_stream.hpp"

namespace {

using pigeon::raspberry::SerialPortStream;
using pigeon::raspberry::SerialSettings;
using pigeon::test_support::PseudoTerminal;
using std::chrono::milliseconds;

constexpr int kFailureBoundMs = 5000;  // only bounds how long a failing test hangs

std::vector<std::uint8_t> every_byte_value() {
  std::vector<std::uint8_t> all(256);
  std::iota(all.begin(), all.end(), std::uint8_t{0});
  return all;
}

/// Reads from the stream until `count` bytes arrive, waiting on it as the link does.
std::vector<std::uint8_t> read_from(SerialPortStream& stream, std::size_t count) {
  std::vector<std::uint8_t> got;
  for (int attempt = 0; attempt < 100 && got.size() < count; ++attempt) {
    std::uint8_t chunk[64];
    const std::size_t n = stream.read(chunk);
    if (n > 0) {
      got.insert(got.end(), chunk, chunk + n);
    } else {
      stream.wait_readable(milliseconds{100});
    }
  }
  return got;
}

class SerialPort : public ::testing::Test {
 protected:
  PseudoTerminal line;
  void SetUp() override { ASSERT_TRUE(line.ok()) << "this machine cannot make a pseudo-terminal"; }
};

// --- opening ----------------------------------------------------------------

TEST_F(SerialPort, OpensAPortByPath) {
  SerialPortStream stream{line.slave_path()};

  EXPECT_TRUE(stream.is_open());
  EXPECT_EQ(stream.open_error(), 0);
}

TEST_F(SerialPort, ThereIsNothingToOpenAtAPathThatDoesNotExist) {
  SerialPortStream stream{"/nonexistent-directory/pigeon-port"};

  EXPECT_FALSE(stream.is_open());
  EXPECT_EQ(stream.open_error(), ENOENT);
}

TEST_F(SerialPort, RefusesSomethingThatIsNotATerminal) {
  SerialPortStream stream{"/dev/null"};

  EXPECT_FALSE(stream.is_open());
  EXPECT_EQ(stream.open_error(), ENOTTY);
}

TEST_F(SerialPort, RefusesALineSpeedItDoesNotKnow) {
  SerialPortStream stream{line.slave_path(), SerialSettings{.baud_rate = 12345}};

  EXPECT_FALSE(stream.is_open());
  EXPECT_EQ(stream.open_error(), EINVAL);
}

TEST_F(SerialPort, AcceptsTheSpeedsThePiAndTheArduinoShare) {
  for (const unsigned baud : {9600U, 19200U, 38400U, 57600U, 115200U}) {
    SerialPortStream stream{line.slave_path(), SerialSettings{.baud_rate = baud}};
    EXPECT_TRUE(stream.is_open()) << baud;
  }
}

// --- raw binary, both ways --------------------------------------------------

TEST_F(SerialPort, EveryByteValueReachesTheStreamUnchanged) {
  SerialPortStream stream{line.slave_path()};
  ASSERT_TRUE(stream.is_open());
  const auto all = every_byte_value();

  line.send(all);

  // 0x00 (the delimiter), 0x03 and 0x1A (signals), 0x11 and 0x13 (flow control),
  // 0x0D and 0x0A (line endings) and 0xFF all have to arrive as sent.
  EXPECT_EQ(read_from(stream, all.size()), all);
}

TEST_F(SerialPort, EveryByteValueReachesTheDeviceUnchanged) {
  SerialPortStream stream{line.slave_path()};
  ASSERT_TRUE(stream.is_open());
  const auto all = every_byte_value();

  EXPECT_TRUE(stream.write(all));

  EXPECT_EQ(line.receive(all.size(), kFailureBoundMs), all);
}

TEST_F(SerialPort, NothingIsEchoedBackToTheDevice) {
  SerialPortStream stream{line.slave_path()};
  ASSERT_TRUE(stream.is_open());
  const auto all = every_byte_value();
  line.send(all);
  ASSERT_EQ(read_from(stream, all.size()).size(), all.size());

  EXPECT_TRUE(line.receive_now().empty());
}

// --- never waiting, except where asked --------------------------------------

TEST_F(SerialPort, ReadReturnsAtOnceWhenNothingHasArrived) {
  SerialPortStream stream{line.slave_path()};
  ASSERT_TRUE(stream.is_open());
  std::uint8_t chunk[16];

  EXPECT_EQ(stream.read(chunk), 0U);
  EXPECT_TRUE(stream.is_open());  // nothing yet is not a lost line
}

TEST_F(SerialPort, WaitReturnsWhenABytePasses) {
  SerialPortStream stream{line.slave_path()};
  ASSERT_TRUE(stream.is_open());
  const std::vector<std::uint8_t> one{0x42};
  line.send(one);

  stream.wait_readable(milliseconds{kFailureBoundMs});  // would hang if it ignored the byte

  std::uint8_t chunk[16];
  EXPECT_EQ(stream.read(chunk), 1U);
  EXPECT_EQ(chunk[0], 0x42);
}

TEST_F(SerialPort, WaitGivesUpAfterTheLimitWhenNothingComes) {
  SerialPortStream stream{line.slave_path()};
  ASSERT_TRUE(stream.is_open());

  stream.wait_readable(milliseconds{20});  // returns; how long it took is not asserted

  std::uint8_t chunk[16];
  EXPECT_EQ(stream.read(chunk), 0U);
}

// --- closing and losing the line --------------------------------------------

TEST_F(SerialPort, AClosedStreamReadsNothingWritesNothingAndDoesNotWait) {
  SerialPortStream stream{line.slave_path()};
  ASSERT_TRUE(stream.is_open());

  stream.close();

  std::uint8_t chunk[16];
  const std::vector<std::uint8_t> one{0x01};
  EXPECT_FALSE(stream.is_open());
  EXPECT_EQ(stream.read(chunk), 0U);
  EXPECT_FALSE(stream.write(one));
  stream.wait_readable(milliseconds{10000});  // returns at once: there is nothing to wait for
}

TEST_F(SerialPort, ClosingTwiceIsHarmless) {
  SerialPortStream stream{line.slave_path()};

  stream.close();
  stream.close();

  EXPECT_FALSE(stream.is_open());
}

#if defined(__linux__)
// How a vanished peer shows on the port differs between systems, so this is
// asserted where it has been observed. Elsewhere a lost line is still caught by
// the silence timeout (section 7).
TEST_F(SerialPort, ALineWhoseOtherEndWentAwayIsNoLongerOpen) {
  SerialPortStream stream{line.slave_path()};
  ASSERT_TRUE(stream.is_open());

  line.close_master();

  std::uint8_t chunk[16];
  EXPECT_EQ(stream.read(chunk), 0U);
  EXPECT_FALSE(stream.is_open());
}

TEST_F(SerialPort, WritingToALineWhoseOtherEndWentAwayFailsAndClosesIt) {
  SerialPortStream stream{line.slave_path()};
  ASSERT_TRUE(stream.is_open());
  line.close_master();
  const std::vector<std::uint8_t> one{0x01};

  EXPECT_FALSE(stream.write(one));
  EXPECT_FALSE(stream.is_open());
}
#endif

}  // namespace

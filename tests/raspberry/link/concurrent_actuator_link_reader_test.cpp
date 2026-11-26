// Verifies: REQ-COM-003, REQ-COM-001, REQ-SAF-004
//
// ADR-0023 end to end, with nothing simulated but the device: a real port (a
// pseudo-terminal, Q27), the real clock and the reader thread. Once reading has
// started, a heartbeat sent by the device makes `health()` read OK with no caller
// polling, and shutting down stops the reader and sends SAFE_STATE. The bounds only
// stop a failing test from hanging (REQ-DEV-002).

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include "link_support/pseudo_terminal.hpp"
#include "pigeon/core/actuator_link.hpp"
#include "pigeon/protocol/bytes.hpp"
#include "pigeon/protocol/message.hpp"
#include "pigeon/raspberry/concurrent_actuator_link.hpp"
#include "pigeon/raspberry/serial_port_stream.hpp"
#include "pigeon/raspberry/steady_clock.hpp"

namespace {

using pigeon::core::LinkStatus;
using pigeon::raspberry::ConcurrentActuatorLink;
using pigeon::raspberry::SerialPortStream;
using pigeon::raspberry::SteadyClock;
using pigeon::test_support::PseudoTerminal;

// V7: HEARTBEAT seq 0, flags 0 (device-protocol.md section 10).
constexpr std::array<std::uint8_t, 8> kHeartbeat{0x03, 0x01, 0x82, 0x01, 0x03, 0x4E, 0xA7, 0x00};

TEST(ReaderThread, AHeartbeatMakesTheLinkHealthyWithNoCallerPolling) {
  PseudoTerminal device;
  ASSERT_TRUE(device.ok());
  SerialPortStream port{device.slave_path()};
  ASSERT_TRUE(port.is_open());
  const SteadyClock clock;
  ConcurrentActuatorLink link{port, clock};
  ASSERT_EQ(link.health(), LinkStatus::UNAVAILABLE);

  link.start_reading();

  // Sent again until it is heard, as the device does every 500 ms. A single one can
  // rightly be discarded: if more than the 200 ms stall bound passes between
  // building the link and the reader's first poll (a loaded machine), that poll
  // treats what the line buffered as stale and drops it (section 7).
  const auto give_up = std::chrono::steady_clock::now() + std::chrono::seconds{10};
  while (link.health() != LinkStatus::OK && std::chrono::steady_clock::now() < give_up) {
    device.send(kHeartbeat);
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
  }
  EXPECT_EQ(link.health(), LinkStatus::OK);
}

TEST(ReaderThread, ShuttingDownStopsTheReaderAndSendsSafeState) {
  PseudoTerminal device;
  ASSERT_TRUE(device.ok());
  SerialPortStream port{device.slave_path()};
  ASSERT_TRUE(port.is_open());
  const SteadyClock clock;

  {
    ConcurrentActuatorLink link{port, clock};
    link.start_reading();
  }  // the reader is stopped and joined, then the link sends SAFE_STATE

  std::array<std::uint8_t, pigeon::protocol::max_wire_message_bytes> expected{};
  const auto length = pigeon::protocol::encode_safe_state(
      0, pigeon::protocol::MutableBytes{expected.data(), expected.size()});
  const std::vector<std::uint8_t> want(expected.begin(),
                                       expected.begin() + static_cast<std::ptrdiff_t>(length));
  EXPECT_EQ(device.receive(want.size(), 5000), want);
}

}  // namespace

// Verifies: REQ-COM-001, REQ-COM-002, REQ-COM-003, REQ-SAF-004, REQ-DEV-003
//
// The wiring (ADR-0021 follow-up 3, ADR-0023): one object that opens a serial
// port by path and owns the real clock and the reader-driven host link built on
// them, in the right order, so that the application has one thing to construct. The
// device on the far end is a pseudo-terminal with the real codec behind it (Q27); the
// clock and the reader thread are real, so only outcomes are asserted, never how
// long anything took (REQ-DEV-002).

#include <gtest/gtest.h>

#include <cerrno>
#include <chrono>
#include <memory>
#include <vector>

#include "link_support/pseudo_terminal.hpp"
#include "link_support/pty_device.hpp"
#include "pigeon/core/actuator_link.hpp"
#include "pigeon/protocol/message.hpp"
#include "pigeon/raspberry/device_link.hpp"

namespace {

using pigeon::core::LinkStatus;
using pigeon::protocol::MessageType;
using pigeon::protocol::to_byte;
using pigeon::raspberry::DeviceLink;
using pigeon::test_support::PseudoTerminal;
using pigeon::test_support::PtyDevice;
using std::chrono::seconds;

constexpr seconds kFailureBound{10};  // only bounds how long a failing test may hang

TEST(DeviceLink, OpensThePortItIsGiven) {
  PseudoTerminal line;
  ASSERT_TRUE(line.ok());

  DeviceLink device{line.slave_path()};

  EXPECT_TRUE(device.is_open());
  EXPECT_EQ(device.open_error(), 0);
}

TEST(DeviceLink, APortThatCannotBeOpenedIsReportedAndTheLinkIsUnavailable) {
  DeviceLink device{"/nonexistent-directory/pigeon-port"};

  EXPECT_FALSE(device.is_open());
  EXPECT_EQ(device.open_error(), ENOENT);
  EXPECT_EQ(device.link().health(), LinkStatus::UNAVAILABLE);
  EXPECT_EQ(device.start(kFailureBound), LinkStatus::UNAVAILABLE);
}

TEST(DeviceLink, ANewLinkIsUnavailableUntilTheDeviceHasBeenHeard) {
  PseudoTerminal line;
  ASSERT_TRUE(line.ok());

  DeviceLink device{line.slave_path()};

  EXPECT_EQ(device.link().health(), LinkStatus::UNAVAILABLE);
}

TEST(DeviceLink, StartBringsTheLinkUpAgainstARealPortAndTheRealClock) {
  PseudoTerminal line;
  ASSERT_TRUE(line.ok());
  PtyDevice far_end{line};
  DeviceLink device{line.slave_path()};

  EXPECT_EQ(device.start(kFailureBound), LinkStatus::OK);

  EXPECT_EQ(device.link().health(), LinkStatus::OK);
  EXPECT_EQ(device.link().send_safe_state_command(), LinkStatus::OK);
}

TEST(DeviceLink, DestroyingItPutsTheActuatorInItsSafeStateLast) {
  PseudoTerminal line;
  ASSERT_TRUE(line.ok());
  PtyDevice far_end{line};

  {
    DeviceLink device{line.slave_path()};
    ASSERT_EQ(device.start(kFailureBound), LinkStatus::OK);
  }  // the reader is stopped, then SAFE_STATE is sent, then the port is closed

  far_end.stop();
  const std::vector<std::uint8_t> seen = far_end.commands();
  ASSERT_GE(seen.size(), 2U);  // one from start-up, one at shutdown
  EXPECT_EQ(seen.back(), to_byte(MessageType::SAFE_STATE));
}

}  // namespace

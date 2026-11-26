// Verifies: REQ-COM-003, REQ-COM-001, REQ-SAF-008
//
// What `health()` says before the device has been heard from, and that asking
// costs nothing (`docs/architecture/device-protocol.md`, section 7): a line that
// has just opened is `UNAVAILABLE`, not `OK`, and the query transmits nothing,
// however often it is asked and however much time passes.

#include <gtest/gtest.h>

#include <chrono>

#include "link_support/recording_stream.hpp"
#include "pigeon/core/actuator_link.hpp"
#include "pigeon/raspberry/serial_actuator_link.hpp"
#include "support/manual_clock.hpp"

namespace {

using pigeon::core::LinkStatus;
using pigeon::raspberry::SerialActuatorLink;
using pigeon::test_support::ManualClock;
using pigeon::test_support::RecordingStream;
using std::chrono::milliseconds;

TEST(SerialActuatorLinkHealth, ALineThatHasJustOpenedIsUnavailableAndNothingHasBeenSent) {
  RecordingStream line;
  ManualClock clock;

  const SerialActuatorLink link{line, clock};

  EXPECT_EQ(link.health(), LinkStatus::UNAVAILABLE);
  EXPECT_EQ(line.bytes_written(), 0U);
}

TEST(SerialActuatorLinkHealth, AClosedLineIsUnavailable) {
  RecordingStream line{/*open=*/false};
  ManualClock clock;

  const SerialActuatorLink link{line, clock};

  EXPECT_EQ(link.health(), LinkStatus::UNAVAILABLE);
}

TEST(SerialActuatorLinkHealth, AskingRepeatedlyAsTimePassesTransmitsNothing) {
  RecordingStream line;
  ManualClock clock;
  const SerialActuatorLink link{line, clock};

  for (int i = 0; i < 20; ++i) {
    clock.advance_by(milliseconds{500});
    EXPECT_EQ(link.health(), LinkStatus::UNAVAILABLE);
  }

  EXPECT_EQ(line.bytes_written(), 0U);
}

}  // namespace

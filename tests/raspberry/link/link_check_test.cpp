// Verifies: REQ-COM-001, REQ-COM-002, REQ-SAF-001, REQ-SAF-004, REQ-DEV-003
//
// The logic of the hand-run check against a real Arduino (ADR-0021 follow-up 3).
// The tool itself opens a real port and so never runs in the default suite
// (REQ-DEV-003); what it does once the link is up is a plain function of the link
// and its options, and is tested here against the simulated device.
//
// The check is run by a person at a bench, on hardware that may carry a water
// actuator. So it is conservative by construction: it sends `SAFE_STATE` and, only
// if asked, one aiming command. It has no way to send `FIRE`, and the tests below
// hold it to that over every option.

#include <gtest/gtest.h>

#include <chrono>
#include <string>

#include "link_support/simulated_device.hpp"
#include "pigeon/core/actuator_link.hpp"
#include "pigeon/core/aiming.hpp"
#include "pigeon/protocol/message.hpp"
#include "pigeon/raspberry/concurrent_actuator_link.hpp"
#include "pigeon/raspberry/link_check.hpp"
#include "support/manual_clock.hpp"

namespace {

using pigeon::core::Angle;
using pigeon::core::LinkStatus;
using pigeon::core::ServoAngles;
using pigeon::protocol::MessageType;
using pigeon::protocol::to_byte;
using pigeon::raspberry::ConcurrentActuatorLink;
using pigeon::raspberry::describe;
using pigeon::raspberry::LinkCheckOptions;
using pigeon::raspberry::LinkCheckReport;
using pigeon::raspberry::run_link_check;
using pigeon::test_support::ManualClock;
using pigeon::test_support::SimulatedDevice;
using std::chrono::milliseconds;

class LinkCheck : public ::testing::Test {
 protected:
  ManualClock clock;
  SimulatedDevice device{clock};
  ConcurrentActuatorLink link{device, clock};

  LinkStatus bring_up() {
    device.start_heartbeats(milliseconds{500});
    return link.start_up(milliseconds{5000});
  }
};

TEST_F(LinkCheck, AHealthyDevicePasses) {
  const LinkStatus started = bring_up();
  ASSERT_EQ(started, LinkStatus::OK);

  const LinkCheckReport report = run_link_check(link, started, LinkCheckOptions{});

  EXPECT_TRUE(report.passed());
  EXPECT_EQ(report.start_up, LinkStatus::OK);
  EXPECT_EQ(report.health_after_start, LinkStatus::OK);
  ASSERT_EQ(report.safe_state_outcomes.size(), 3U);  // the default
  for (const LinkStatus outcome : report.safe_state_outcomes) {
    EXPECT_EQ(outcome, LinkStatus::OK);
  }
  EXPECT_FALSE(report.aim_outcome.has_value());  // not asked for, not sent
  EXPECT_EQ(report.health_at_end, LinkStatus::OK);
}

TEST_F(LinkCheck, TheNumberOfSafeStateExchangesIsTheCallers) {
  const LinkStatus started = bring_up();
  LinkCheckOptions options;
  options.safe_state_exchanges = 5;

  const LinkCheckReport report = run_link_check(link, started, options);

  EXPECT_EQ(report.safe_state_outcomes.size(), 5U);
}

TEST_F(LinkCheck, ADeviceNeverHeardIsFailedAndNothingIsSentIntoTheSilence) {
  // No heartbeat ever: start-up gives up, and section 8 says nothing is sent.
  const LinkStatus started = link.start_up(milliseconds{2000});
  ASSERT_EQ(started, LinkStatus::UNAVAILABLE);

  const LinkCheckReport report = run_link_check(link, started, LinkCheckOptions{});

  EXPECT_FALSE(report.passed());
  EXPECT_EQ(report.start_up, LinkStatus::UNAVAILABLE);
  EXPECT_TRUE(report.safe_state_outcomes.empty());
  EXPECT_FALSE(report.aim_outcome.has_value());
  EXPECT_EQ(device.write_calls(), 0U);
}

TEST_F(LinkCheck, AnExchangeThatFailsFailsTheCheckAndEveryExchangeIsStillReported) {
  const LinkStatus started = bring_up();
  device.stay_silent();

  const LinkCheckReport report = run_link_check(link, started, LinkCheckOptions{});

  EXPECT_FALSE(report.passed());
  ASSERT_EQ(report.safe_state_outcomes.size(), 3U);
  for (const LinkStatus outcome : report.safe_state_outcomes) {
    EXPECT_EQ(outcome, LinkStatus::TRANSPORT_FAILURE);
  }
}

TEST_F(LinkCheck, AnAimIsSentOnlyWhenAskedForAndReportedWithItsOutcome) {
  const LinkStatus started = bring_up();
  LinkCheckOptions options;
  options.aim = ServoAngles{Angle{10.0}, Angle{20.0}};

  const LinkCheckReport report = run_link_check(link, started, options);

  ASSERT_TRUE(report.aim_outcome.has_value());
  EXPECT_EQ(*report.aim_outcome, LinkStatus::OK);
  EXPECT_TRUE(report.passed());
}

TEST_F(LinkCheck, ARefusedAimIsAWorkingLimitNotAFailureOfTheLink) {
  const LinkStatus started = bring_up();
  LinkCheckOptions options;
  options.aim = ServoAngles{Angle{95.0}, Angle{10.0}};  // outside the device's envelope

  const LinkCheckReport report = run_link_check(link, started, options);

  ASSERT_TRUE(report.aim_outcome.has_value());
  EXPECT_EQ(*report.aim_outcome, LinkStatus::REJECTED);
  EXPECT_TRUE(report.passed());  // the device did what it should
}

TEST_F(LinkCheck, AnAimThatGetsNoAnswerFailsTheCheck) {
  const LinkStatus started = bring_up();
  device.stay_silent();
  LinkCheckOptions options;
  options.safe_state_exchanges = 0;
  options.aim = ServoAngles{Angle{10.0}, Angle{20.0}};

  const LinkCheckReport report = run_link_check(link, started, options);

  ASSERT_TRUE(report.aim_outcome.has_value());
  EXPECT_EQ(*report.aim_outcome, LinkStatus::TRANSPORT_FAILURE);
  EXPECT_FALSE(report.passed());
}

TEST_F(LinkCheck, ItNeverSendsFireWhateverItIsAskedFor) {
  const LinkStatus started = bring_up();
  LinkCheckOptions options;
  options.safe_state_exchanges = 4;
  options.aim = ServoAngles{Angle{0.0}, Angle{10.0}};

  static_cast<void>(run_link_check(link, started, options));

  ASSERT_FALSE(device.commands().empty());
  for (const auto& command : device.commands()) {
    EXPECT_NE(command.type, to_byte(MessageType::FIRE));
  }
}

TEST_F(LinkCheck, TheReportReadsAsOneLinePerStepAndSaysPassedOrFailed) {
  const LinkStatus started = bring_up();
  LinkCheckOptions options;
  options.safe_state_exchanges = 2;
  options.aim = ServoAngles{Angle{10.0}, Angle{20.0}};

  const std::string text = describe(run_link_check(link, started, options));

  EXPECT_NE(text.find("start-up: OK"), std::string::npos) << text;
  EXPECT_NE(text.find("SAFE_STATE 1: OK"), std::string::npos) << text;
  EXPECT_NE(text.find("SAFE_STATE 2: OK"), std::string::npos) << text;
  EXPECT_NE(text.find("AIM"), std::string::npos) << text;
  EXPECT_NE(text.find("PASSED"), std::string::npos) << text;
  EXPECT_EQ(text.find("FAILED"), std::string::npos) << text;
}

TEST_F(LinkCheck, AFailedReportNamesWhatFailed) {
  const LinkStatus started = link.start_up(milliseconds{2000});  // never heard

  const std::string text = describe(run_link_check(link, started, LinkCheckOptions{}));

  EXPECT_NE(text.find("start-up: UNAVAILABLE"), std::string::npos) << text;
  EXPECT_NE(text.find("FAILED"), std::string::npos) << text;
  EXPECT_EQ(text.find("PASSED"), std::string::npos) << text;
}

}  // namespace

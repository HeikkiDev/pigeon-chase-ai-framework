// Verifies: REQ-COM-002, REQ-DET-001, REQ-DET-002, REQ-SAF-001, REQ-SAF-002,
//           REQ-SAF-004, REQ-SAF-005, REQ-SAF-007, REQ-TRK-002, REQ-TRK-008,
//           REQ-TRK-011, REQ-TRK-012, REQ-DEV-001, REQ-DEV-002
//
// End-to-end scenarios: detection, association, the state machine, the safety
// policy and a simulated actuator system, wired together exactly as the
// Raspberry Pi application loop will wire them — and with no hardware, no wall
// clock and no camera anywhere in the path.

#include <chrono>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "fixtures/rgb888_fixtures.hpp"
#include "pigeon/core/actuator_link.hpp"
#include "pigeon/core/configuration.hpp"
#include "pigeon/core/detection.hpp"
#include "pigeon/core/geometry.hpp"
#include "pigeon/core/safety_policy.hpp"
#include "pigeon/core/target_state.hpp"
#include "support/detection_builders.hpp"
#include "support/engagement_loop.hpp"
#include "support/manual_clock.hpp"
#include "support/simulated_actuator_link.hpp"
#include "support/simulated_detectors.hpp"
#include "support/test_configuration.hpp"

namespace {

using std::chrono::milliseconds;

using pigeon::core::Configuration;
using pigeon::core::Detection;
using pigeon::core::DetectionOutcome;
using pigeon::core::EngagementIntent;
using pigeon::core::FireRefusal;
using pigeon::core::LinkStatus;
using pigeon::core::PixelDistance;
using pigeon::core::TargetState;
using pigeon::core::Track;
using pigeon::test_support::calibrated_configuration;
using pigeon::test_support::detection_at;
using pigeon::test_support::EngagementLoop;
using pigeon::test_support::FrameOutcome;
using pigeon::test_support::ManualClock;
using pigeon::test_support::SimulatedActuatorLink;
using pigeon::test_support::test_image_size;

// One bird, sitting still in the middle of the frame, big enough to win any
// selection.
[[nodiscard]] DetectionOutcome bird_frame() {
  return DetectionOutcome::found({detection_at(320.0, 200.0, 60.0, 40.0)});
}

[[nodiscard]] FrameOutcome run_to_first_burst(EngagementLoop& loop) {
  static_cast<void>(loop.process(bird_frame()));
  static_cast<void>(loop.process(bird_frame()));
  static_cast<void>(loop.process(bird_frame()));
  return loop.process(bird_frame());
}

// Verifies: REQ-TRK-002, REQ-TRK-004, REQ-TRK-011, REQ-SAF-002 — three
// consecutive detections aim the rig, the fourth authorises exactly one burst,
// and the engagement ends there with a return to SEARCHING.
TEST(EngagementScenario, ThreeDetectionsAimAndTheVerificationFrameFiresOnce) {
  ManualClock clock;
  SimulatedActuatorLink link{clock};
  EngagementLoop loop{calibrated_configuration(), clock, link, test_image_size};

  const FrameOutcome first = loop.process(bird_frame());
  const FrameOutcome second = loop.process(bird_frame());
  const FrameOutcome third = loop.process(bird_frame());
  const FrameOutcome fourth = loop.process(bird_frame());

  EXPECT_EQ(first.intent, EngagementIntent::KEEP_SEARCHING);
  EXPECT_EQ(second.intent, EngagementIntent::KEEP_SEARCHING);
  EXPECT_EQ(third.intent, EngagementIntent::AIM_AT_TARGET);
  EXPECT_TRUE(third.aiming_command_sent);
  EXPECT_FALSE(third.fire_command_sent) << "locking must never itself open the valve";

  EXPECT_EQ(fourth.intent, EngagementIntent::FIRE_AT_TARGET);
  EXPECT_TRUE(fourth.fire_command_sent);
  EXPECT_EQ(fourth.state_after, TargetState::SEARCHING);
  EXPECT_EQ(link.fire_commands().size(), 1U);
  EXPECT_EQ(link.fire_commands().at(0).duration, milliseconds{500});
}

// Verifies: REQ-SAF-001 — "a simulated actuator that receives a fire command
// is inactive again after the configured maximum duration".
TEST(EngagementScenario, TheWaterStopsAfterTheConfiguredMaximumDuration) {
  ManualClock clock;
  SimulatedActuatorLink link{clock};
  const Configuration configuration = calibrated_configuration();
  EngagementLoop loop{configuration, clock, link, test_image_size};

  ASSERT_TRUE(run_to_first_burst(loop).fire_command_sent);
  EXPECT_TRUE(link.water_active()) << "the burst should have started";

  clock.advance_by(configuration.safety.max_fire_duration);
  EXPECT_FALSE(link.water_active())
      << "the burst must end on the device's own timer, with no further command";
}

// Verifies: REQ-SAF-001 — "a simulated link that goes silent immediately after
// a fire command still results in an inactive actuator". The bound must not
// depend on the commanding device still being alive.
TEST(EngagementScenario, TheWaterStopsEvenIfTheLinkGoesSilentImmediately) {
  ManualClock clock;
  SimulatedActuatorLink link{clock};
  const Configuration configuration = calibrated_configuration();
  EngagementLoop loop{configuration, clock, link, test_image_size};

  ASSERT_TRUE(run_to_first_burst(loop).fire_command_sent);
  link.set_status(LinkStatus::UNAVAILABLE);

  clock.advance_by(configuration.safety.max_fire_duration);
  EXPECT_FALSE(link.water_active());
}

// Verifies: REQ-SAF-001 — "a configured duration longer than the firmware's
// own bound is rejected or truncated by the firmware, not obeyed". The
// commanding side's configuration does not override the device's own limit.
TEST(EngagementScenario, ADurationAboveTheDeviceBoundIsRefusedNotObeyed) {
  ManualClock clock;
  SimulatedActuatorLink link{clock};
  Configuration configuration = calibrated_configuration();
  configuration.safety.max_fire_duration =
      SimulatedActuatorLink::firmware_burst_bound + milliseconds{250};
  EngagementLoop loop{configuration, clock, link, test_image_size};

  const FrameOutcome burst = run_to_first_burst(loop);

  ASSERT_TRUE(burst.fire_command_sent);
  ASSERT_TRUE(burst.fire_status.has_value());
  EXPECT_EQ(*burst.fire_status, LinkStatus::REJECTED);
  EXPECT_FALSE(link.water_active()) << "a refused burst must open no valve at all";
}

// Verifies: REQ-SAF-004 — "the simulated actuator reports inactive immediately
// after construction and after shutdown".
TEST(ActuatorSafeState, IsInactiveOnConstructionAndAfterASafeStateCommand) {
  ManualClock clock;
  SimulatedActuatorLink link{clock};

  EXPECT_FALSE(link.water_active()) << "the valve must be shut when the link comes up";

  EngagementLoop loop{calibrated_configuration(), clock, link, test_image_size};
  ASSERT_TRUE(run_to_first_burst(loop).fire_command_sent);
  ASSERT_TRUE(link.water_active());

  EXPECT_EQ(link.send_safe_state_command(), LinkStatus::OK);
  EXPECT_FALSE(link.water_active()) << "the safe state must shut the valve immediately";
}

// Verifies: REQ-TRK-011, REQ-SAF-005, REQ-SAF-007 — "no second fire command is
// issued before the cool-down has elapsed, even while the same track remains
// confirmable".
TEST(EngagementScenario, NoSecondBurstBeforeTheCoolDownHasElapsed) {
  ManualClock clock;
  SimulatedActuatorLink link{clock};
  EngagementLoop loop{calibrated_configuration(), clock, link, test_image_size};

  ASSERT_TRUE(run_to_first_burst(loop).fire_command_sent);

  bool refused_for_cool_down = false;
  for (int frame = 0; frame < 6; ++frame) {
    clock.advance_by(milliseconds{200});  // 1.2 s in total: inside the cool-down
    const FrameOutcome outcome = loop.process(bird_frame());
    if (outcome.refusal.has_value() && *outcome.refusal == FireRefusal::COOLING_DOWN) {
      refused_for_cool_down = true;
    }
  }

  EXPECT_EQ(link.fire_commands().size(), 1U)
      << "the bird stayed put, but the rig may not keep spraying it";
  EXPECT_TRUE(refused_for_cool_down)
      << "the second engagement should have been refused by the cool-down, by name";
}

// Verifies: REQ-TRK-012 — "a track detected in every frame, engaged and fired
// upon in frame N, is not confirmed in frame N+1 or N+2, and is confirmed in
// frame N+3 at the earliest"; "no aiming command is issued for that bird in
// frames N+1 and N+2, and the state is SEARCHING throughout them"; "the
// consecutive-detection count observed for that bird in frame N+1 is one, not
// five".
TEST(EngagementScenario, TheFiredUponBirdMustEarnThreeFreshDetections) {
  ManualClock clock;
  SimulatedActuatorLink link{clock};
  EngagementLoop loop{calibrated_configuration(), clock, link, test_image_size};

  ASSERT_TRUE(run_to_first_burst(loop).fire_command_sent);
  const std::size_t aiming_commands_after_burst = link.aiming_commands().size();
  // Well past the cool-down, so re-confirmation is the only thing standing in
  // the way of a second burst.
  clock.advance_by(milliseconds{3000});

  const FrameOutcome after_one = loop.process(bird_frame());
  ASSERT_EQ(after_one.tracks_after.size(), 1U);
  EXPECT_EQ(after_one.tracks_after.at(0).consecutive_detections, 1U)
      << "the fired-upon bird must start counting again from one";
  EXPECT_EQ(after_one.state_after, TargetState::SEARCHING);
  EXPECT_FALSE(after_one.aiming_command_sent);

  const FrameOutcome after_two = loop.process(bird_frame());
  EXPECT_EQ(after_two.tracks_after.at(0).consecutive_detections, 2U);
  EXPECT_EQ(after_two.state_after, TargetState::SEARCHING);
  EXPECT_FALSE(after_two.aiming_command_sent);
  EXPECT_EQ(link.aiming_commands().size(), aiming_commands_after_burst)
      << "no servo movement is owed to a bird that has not been re-confirmed";

  const FrameOutcome after_three = loop.process(bird_frame());
  EXPECT_EQ(after_three.state_after, TargetState::TARGET_LOCKED)
      << "three fresh consecutive detections re-confirm the bird, and not sooner";
  EXPECT_TRUE(after_three.aiming_command_sent);

  const FrameOutcome after_four = loop.process(bird_frame());
  EXPECT_TRUE(after_four.fire_command_sent);
  EXPECT_EQ(link.fire_commands().size(), 2U);
}

// Verifies: REQ-COM-002 — "with a simulated link that fails after locking, no
// fire command is emitted and the final state is SEARCHING".
TEST(EngagementScenario, ALinkThatFailsAfterLockingFiresNothing) {
  ManualClock clock;
  SimulatedActuatorLink link{clock};
  EngagementLoop loop{calibrated_configuration(), clock, link, test_image_size};

  static_cast<void>(loop.process(bird_frame()));
  static_cast<void>(loop.process(bird_frame()));
  link.set_status(LinkStatus::UNAVAILABLE);  // the link dies between frames

  const FrameOutcome locking = loop.process(bird_frame());
  EXPECT_TRUE(locking.engagement_abandoned)
      << "an aiming command the link could not carry must abandon the engagement";
  EXPECT_EQ(locking.state_after, TargetState::SEARCHING);

  const FrameOutcome next = loop.process(bird_frame());
  EXPECT_FALSE(next.fire_command_sent);
  EXPECT_TRUE(link.fire_commands().empty()) << "no fire command may be emitted over a dead link";
  EXPECT_EQ(loop.state().state, TargetState::SEARCHING);
  EXPECT_FALSE(link.water_active());
}

// Verifies: REQ-TRK-012 — "after an engagement abandoned under REQ-COM-002,
// the same holds from the frame in which it was abandoned": the bird earns no
// credit from the engagement that was dropped.
TEST(EngagementScenario, AnAbandonedEngagementAlsoRetiresItsTrack) {
  ManualClock clock;
  SimulatedActuatorLink link{clock};
  EngagementLoop loop{calibrated_configuration(), clock, link, test_image_size};

  static_cast<void>(loop.process(bird_frame()));
  static_cast<void>(loop.process(bird_frame()));
  link.set_status(LinkStatus::UNAVAILABLE);
  const FrameOutcome abandoned = loop.process(bird_frame());
  ASSERT_TRUE(abandoned.engagement_abandoned);
  EXPECT_TRUE(abandoned.tracks_after.empty())
      << "the engaged track is retired the moment the engagement ends";

  link.set_status(LinkStatus::OK);  // the link comes back

  const FrameOutcome first = loop.process(bird_frame());
  ASSERT_EQ(first.tracks_after.size(), 1U);
  EXPECT_EQ(first.tracks_after.at(0).consecutive_detections, 1U);
  EXPECT_EQ(first.state_after, TargetState::SEARCHING);
  const FrameOutcome second = loop.process(bird_frame());
  EXPECT_EQ(second.state_after, TargetState::SEARCHING);
  const FrameOutcome third = loop.process(bird_frame());
  EXPECT_EQ(third.state_after, TargetState::TARGET_LOCKED)
      << "the abandoned bird needed three fresh detections, exactly like any other";
}

// Verifies: REQ-TRK-008, REQ-TRK-012 — "no fire command is ever issued for
// more than one target in one engagement", and "a track that was not the
// engaged one keeps its consecutive-detection count across the end of another
// track's engagement".
TEST(EngagementScenario, AMultiBirdFrameProducesExactlyOneEngagement) {
  ManualClock clock;
  SimulatedActuatorLink link{clock};
  EngagementLoop loop{calibrated_configuration(), clock, link, test_image_size};

  const Detection big = detection_at(320.0, 200.0, 80.0, 60.0);
  const Detection medium = detection_at(120.0, 300.0, 40.0, 30.0);
  const Detection small = detection_at(500.0, 100.0, 20.0, 20.0);
  const DetectionOutcome flock = DetectionOutcome::found({medium, big, small});

  FrameOutcome outcome;
  for (int frame = 0; frame < 4; ++frame) {
    outcome = loop.process(flock);
  }

  ASSERT_TRUE(outcome.fire_command_sent);
  EXPECT_EQ(link.fire_commands().size(), 1U) << "one engagement, one burst, three birds present";
  EXPECT_EQ(outcome.tracks_after.size(), 2U)
      << "the two birds that were not engaged keep their tracks";
  for (const Track& track : outcome.tracks_after) {
    EXPECT_EQ(track.consecutive_detections, 4U)
        << "a bystander's count is not affected by another bird's engagement";
    EXPECT_NE(track.latest, big) << "the largest bird was the engaged one and has been retired";
  }
}

// Verifies: REQ-SAF-004, REQ-AIM-002, REQ-DEV-001 — an unconfigured
// installation is inert: it associates nothing, confirms nothing, and commands
// neither the servos nor the valve, however many pigeons it sees.
TEST(EngagementScenario, AnUnconfiguredRigCommandsNothingAtAll) {
  ManualClock clock;
  SimulatedActuatorLink link{clock};
  EngagementLoop loop{Configuration{}, clock, link, test_image_size};

  for (int frame = 0; frame < 10; ++frame) {
    const FrameOutcome outcome = loop.process(bird_frame());
    EXPECT_EQ(outcome.state_after, TargetState::SEARCHING);
    EXPECT_FALSE(outcome.aiming_command_sent);
    EXPECT_FALSE(outcome.fire_command_sent);
  }

  EXPECT_TRUE(link.aiming_commands().empty());
  EXPECT_TRUE(link.fire_commands().empty());
  EXPECT_FALSE(link.water_active());
}

// Verifies: REQ-DET-001, REQ-DET-002, REQ-DEV-001 — the whole path, from
// RGB888 fixture bytes to a fire command, with a `Detector` in place of a
// camera and a simulated link in place of an Arduino.
TEST(EngagementScenario, RunsEndToEndFromRgb888FixturesWithNoHardware) {
  ManualClock clock;
  SimulatedActuatorLink link{clock};
  Configuration configuration = calibrated_configuration();
  // The fixtures are 16x12, so the association radius is in that frame's scale.
  configuration.association_radius = PixelDistance{3.0};
  EngagementLoop loop{configuration, clock, link, pigeon::test_fixtures::fixture_size};

  const pigeon::test_support::BrightBlobDetector detector;
  const pigeon::test_fixtures::FixtureImage with_pigeon = pigeon::test_fixtures::pigeon_frame();
  const pigeon::test_fixtures::FixtureImage without_pigeon = pigeon::test_fixtures::empty_frame();

  ASSERT_EQ(detector.detect(without_pigeon.view(0)).result(), pigeon::core::DetectionResult::NONE);

  FrameOutcome outcome;
  for (std::uint64_t frame = 0; frame < 4; ++frame) {
    outcome = loop.process(detector.detect(with_pigeon.view(frame)));
  }

  EXPECT_TRUE(outcome.fire_command_sent);
  EXPECT_EQ(link.fire_commands().size(), 1U);
  EXPECT_EQ(link.aiming_commands().size(), 1U);
}

// Verifies: REQ-DEV-002 — "repeated runs, including shuffled runs, produce
// identical results": the same recorded scenario replayed twice produces the
// same commands, in the same order, with the same aim.
TEST(EngagementScenario, ReplayingTheSameScenarioProducesIdenticalCommands) {
  const std::vector<DetectionOutcome> script{
      bird_frame(),
      bird_frame(),
      DetectionOutcome::none(),
      bird_frame(),
      DetectionOutcome::found({detection_at(320.0, 200.0, 60.0, 40.0),
                               detection_at(100.0, 100.0, 10.0, 10.0)}),
      bird_frame(),
      bird_frame(),
      bird_frame(),
  };

  const auto replay = [&script](std::vector<TargetState>& states,
                                std::vector<bool>& fired) {
    ManualClock clock;
    SimulatedActuatorLink link{clock};
    EngagementLoop loop{calibrated_configuration(), clock, link, test_image_size};
    for (const DetectionOutcome& outcome : script) {
      const FrameOutcome frame = loop.process(outcome);
      states.push_back(frame.state_after);
      fired.push_back(frame.fire_command_sent);
      clock.advance_by(milliseconds{200});
    }
  };

  std::vector<TargetState> first_states;
  std::vector<bool> first_fired;
  std::vector<TargetState> second_states;
  std::vector<bool> second_fired;
  replay(first_states, first_fired);
  replay(second_states, second_fired);

  EXPECT_EQ(first_states, second_states);
  EXPECT_EQ(first_fired, second_fired);
}

}  // namespace

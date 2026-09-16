// Verifies: REQ-TRK-001, REQ-TRK-002, REQ-TRK-003, REQ-TRK-004, REQ-TRK-005,
//           REQ-TRK-006, REQ-TRK-010, REQ-TRK-011, REQ-TRK-012, REQ-DEV-002
//
// The target state machine, one transition at a time. `advance` is pure, so
// every case below is a statement about arguments and results and nothing
// else.

#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "pigeon/core/detection.hpp"
#include "pigeon/core/geometry.hpp"
#include "pigeon/core/target_state.hpp"
#include "pigeon/core/track.hpp"
#include "support/detection_builders.hpp"
#include "support/frame_driver.hpp"

namespace {

using pigeon::core::advance;
using pigeon::core::confirmation_frame_count;
using pigeon::core::DetectionResult;
using pigeon::core::EngagementIntent;
using pigeon::core::FrameInput;
using pigeon::core::PixelDistance;
using pigeon::core::TargetMachineState;
using pigeon::core::TargetState;
using pigeon::core::TargetTransition;
using pigeon::core::Track;
using pigeon::core::TrackId;
using pigeon::core::TrackUpdate;
using pigeon::test_support::detection_at;
using pigeon::test_support::frame_with;
using pigeon::test_support::track_with;
using pigeon::test_support::TrackingDriver;

constexpr PixelDistance radius{30.0};

FrameInput found_with(std::vector<Track> tracks, std::uint32_t next_id = 9) {
  return FrameInput::found(
      TrackUpdate{.tracks = std::move(tracks), .next_id = static_cast<TrackId>(next_id)});
}

TargetMachineState locked_on(const Track& engaged, std::vector<Track> tracks = {}) {
  if (tracks.empty()) {
    tracks.push_back(engaged);
  }
  return TargetMachineState{.state = TargetState::TARGET_LOCKED,
                            .engaged_track = engaged.id,
                            .tracks = std::move(tracks),
                            .next_id = static_cast<TrackId>(9)};
}

// ---------------------------------------------------------------------------
// REQ-TRK-001 — three states, SEARCHING first.
// ---------------------------------------------------------------------------

// Verifies: REQ-TRK-001 — "a freshly constructed state machine reports
// SEARCHING", with nothing engaged and nothing remembered.
TEST(TargetStateMachine, StartsInSearchingWithNothingEngaged) {
  const TargetMachineState fresh;

  EXPECT_EQ(fresh.state, TargetState::SEARCHING);
  EXPECT_FALSE(fresh.engaged_track.has_value());
  EXPECT_TRUE(fresh.tracks.empty());
}

// Verifies: REQ-TRK-001, REQ-TRK-003 — a NONE frame while searching changes
// nothing and asks for nothing.
TEST(TargetStateMachine, ANoneFrameWhileSearchingKeepsSearching) {
  const TargetTransition transition = advance(TargetMachineState{}, FrameInput::none());

  EXPECT_EQ(transition.next.state, TargetState::SEARCHING);
  EXPECT_EQ(transition.intent, EngagementIntent::KEEP_SEARCHING);
  EXPECT_FALSE(transition.aim_at.has_value());
  EXPECT_FALSE(transition.next.engaged_track.has_value());
}

// Verifies: REQ-DET-001, REQ-TRK-010 — the frame input carries identity, not
// mere presence, and an update with no tracks is a NONE frame.
TEST(FrameInputContract, ClassifiesByWhatWasDetected) {
  EXPECT_EQ(FrameInput::none().result(), DetectionResult::NONE);
  EXPECT_TRUE(FrameInput::none().detected_tracks().empty());

  const FrameInput empty_update = FrameInput::found(TrackUpdate{});
  EXPECT_EQ(empty_update.result(), DetectionResult::NONE);

  const Track bird = track_with(4, detection_at(120.0, 90.0), 2);
  const FrameInput found = found_with({bird});
  EXPECT_EQ(found.result(), DetectionResult::FOUND);
  ASSERT_EQ(found.detected_tracks().size(), 1U);
  EXPECT_EQ(found.detected_tracks()[0], bird);
}

// ---------------------------------------------------------------------------
// REQ-TRK-002 / REQ-TRK-003 — confirmation, and the reset that guards it.
// ---------------------------------------------------------------------------

// Verifies: REQ-TRK-002 — "FOUND, FOUND on one track → still SEARCHING".
TEST(TargetStateMachine, TwoConsecutiveDetectionsDoNotLock) {
  TrackingDriver driver{radius};

  const TargetTransition first = driver.step(frame_with({detection_at(200.0, 150.0)}));
  const TargetTransition second = driver.step(frame_with({detection_at(205.0, 150.0)}));

  EXPECT_EQ(first.next.state, TargetState::SEARCHING);
  EXPECT_EQ(first.intent, EngagementIntent::KEEP_SEARCHING);
  EXPECT_EQ(second.next.state, TargetState::SEARCHING);
  EXPECT_EQ(second.intent, EngagementIntent::KEEP_SEARCHING);
  ASSERT_EQ(second.next.tracks.size(), 1U);
  EXPECT_EQ(second.next.tracks.at(0).consecutive_detections, 2U);
}

// Verifies: REQ-TRK-002, REQ-AIM-001 — "FOUND, FOUND, FOUND on one track →
// TARGET_LOCKED", and the transition carries the position to aim at.
TEST(TargetStateMachine, ThreeConsecutiveDetectionsOfOneTrackLock) {
  TrackingDriver driver{radius};

  static_cast<void>(driver.step(frame_with({detection_at(200.0, 150.0)})));
  static_cast<void>(driver.step(frame_with({detection_at(205.0, 150.0)})));
  const TargetTransition third = driver.step(frame_with({detection_at(210.0, 152.0)}));

  EXPECT_EQ(third.next.state, TargetState::TARGET_LOCKED);
  EXPECT_EQ(third.intent, EngagementIntent::AIM_AT_TARGET);
  ASSERT_TRUE(third.aim_at.has_value());
  EXPECT_EQ(*third.aim_at, detection_at(210.0, 152.0))
      << "the rig must be aimed at where the bird was seen in this frame";
  ASSERT_TRUE(third.next.engaged_track.has_value());
  EXPECT_EQ(*third.next.engaged_track, third.next.tracks.at(0).id);
}

// Verifies: REQ-TRK-002, REQ-TRK-003 — "FOUND, FOUND, NONE, FOUND, FOUND →
// still SEARCHING", and "after any interleaved NONE, three further consecutive
// FOUND frames are required".
TEST(TargetStateMachine, AnInterleavedNoneCostsTheWholeConfirmation) {
  TrackingDriver driver{radius};

  static_cast<void>(driver.step(frame_with({detection_at(200.0, 150.0)})));
  static_cast<void>(driver.step(frame_with({detection_at(202.0, 150.0)})));
  const TargetTransition gap = driver.step(pigeon::core::DetectionOutcome::none());
  const TargetTransition fourth = driver.step(frame_with({detection_at(204.0, 150.0)}));
  const TargetTransition fifth = driver.step(frame_with({detection_at(206.0, 150.0)}));

  EXPECT_EQ(gap.next.state, TargetState::SEARCHING);
  EXPECT_TRUE(gap.next.tracks.empty()) << "the NONE frame must clear the counters";
  EXPECT_EQ(fourth.next.state, TargetState::SEARCHING);
  EXPECT_EQ(fifth.next.state, TargetState::SEARCHING)
      << "two detections after the gap are two, not five";
  EXPECT_EQ(fifth.intent, EngagementIntent::KEEP_SEARCHING);

  const TargetTransition sixth = driver.step(frame_with({detection_at(208.0, 150.0)}));
  EXPECT_EQ(sixth.next.state, TargetState::TARGET_LOCKED)
      << "three fresh consecutive detections, and only then, confirm";
}

// Verifies: REQ-TRK-002 — "three consecutive frames each containing a
// detection, but too far apart to associate into one track, do not confirm a
// target". Three different birds must not confirm a bird that was never there.
TEST(TargetStateMachine, ThreeUnassociableDetectionsDoNotConfirm) {
  TrackingDriver driver{radius};

  const TargetTransition first = driver.step(frame_with({detection_at(50.0, 50.0)}));
  const TargetTransition second = driver.step(frame_with({detection_at(400.0, 50.0)}));
  const TargetTransition third = driver.step(frame_with({detection_at(120.0, 400.0)}));

  EXPECT_EQ(first.next.state, TargetState::SEARCHING);
  EXPECT_EQ(second.next.state, TargetState::SEARCHING);
  EXPECT_EQ(third.next.state, TargetState::SEARCHING);
  EXPECT_EQ(third.intent, EngagementIntent::KEEP_SEARCHING);
  ASSERT_EQ(third.next.tracks.size(), 1U);
  EXPECT_EQ(third.next.tracks.at(0).consecutive_detections, 1U);
}

// Verifies: REQ-TRK-002, REQ-SAF-002 — a track carrying a count below the
// confirmation threshold never locks, whatever else is in the frame.
TEST(TargetStateMachine, DoesNotLockOnACountBelowTheThreshold) {
  for (std::uint32_t count = 1; count < confirmation_frame_count; ++count) {
    const TargetTransition transition =
        advance(TargetMachineState{}, found_with({track_with(1, detection_at(100.0, 100.0), count)}));

    EXPECT_EQ(transition.next.state, TargetState::SEARCHING) << "count " << count;
    EXPECT_EQ(transition.intent, EngagementIntent::KEEP_SEARCHING) << "count " << count;
    EXPECT_FALSE(transition.aim_at.has_value()) << "count " << count;
  }
}

// ---------------------------------------------------------------------------
// REQ-TRK-004 / REQ-TRK-010 — re-verification before firing.
// ---------------------------------------------------------------------------

// Verifies: REQ-TRK-004, REQ-TRK-010 — "verification frame contains the locked
// track → a fire command is issued", aimed at that track's latest detection.
TEST(TargetStateMachine, AVerificationFrameContainingTheLockedTrackFires) {
  const Track engaged = track_with(2, detection_at(300.0, 200.0), 3);
  const Track re_detected = track_with(2, detection_at(305.0, 205.0), 4);

  const TargetTransition transition = advance(locked_on(engaged), found_with({re_detected}));

  EXPECT_EQ(transition.intent, EngagementIntent::FIRE_AT_TARGET);
  ASSERT_TRUE(transition.aim_at.has_value());
  EXPECT_EQ(*transition.aim_at, re_detected.latest);
}

// Verifies: REQ-TRK-004, REQ-TRK-005, REQ-TRK-010 — "verification frame NONE →
// no fire command", and the state becomes TARGET_LOST.
TEST(TargetStateMachine, AVerificationFrameOfNoneLosesTheTargetAndFiresNothing) {
  const Track engaged = track_with(2, detection_at(300.0, 200.0), 3);

  const TargetTransition transition = advance(locked_on(engaged), FrameInput::none());

  EXPECT_EQ(transition.next.state, TargetState::TARGET_LOST);
  EXPECT_EQ(transition.intent, EngagementIntent::ABANDON);
  EXPECT_FALSE(transition.aim_at.has_value());
  EXPECT_FALSE(transition.next.engaged_track.has_value());
}

// Verifies: REQ-TRK-010 — "verification frame is FOUND but contains only other
// tracks → no fire command, and the state becomes TARGET_LOST". One bird must
// not confirm an engagement that a different bird then authorises (ADR-0009).
TEST(TargetStateMachine, AVerificationFrameWithOnlyOtherTracksLosesTheTarget) {
  const Track engaged = track_with(2, detection_at(300.0, 200.0), 3);
  const Track another = track_with(7, detection_at(120.0, 400.0), 6);

  const TargetTransition transition = advance(locked_on(engaged), found_with({another}));

  EXPECT_EQ(transition.next.state, TargetState::TARGET_LOST);
  EXPECT_EQ(transition.intent, EngagementIntent::ABANDON)
      << "a confirmed bird elsewhere in the frame is not this engagement's bird";
  EXPECT_FALSE(transition.aim_at.has_value());
}

// Verifies: REQ-TRK-004 — "the state machine reads no clock on this path".
// Structural: the transition function's whole input is the state and the
// frame, so there is nothing for a clock to be passed through
// (`scripts/arch-check.sh` rejects a wall-clock read inside `core/`).
TEST(TargetStateMachine, TakesNoClockOnAnyPath) {
  static_assert(
      std::is_same_v<decltype(&advance),
                     TargetTransition (*)(const TargetMachineState&, const FrameInput&)>,
      "advance must remain a pure function of (state, frame) with no clock (REQ-TRK-004)");
  SUCCEED();
}

// Verifies: REQ-TRK-004 — "if no further frame arrives, the engagement remains
// unresolved and no fire command is issued": the lock transition asks only for
// aim, and nothing further happens until another frame is fed in.
TEST(TargetStateMachine, LeavesAnEngagementUnresolvedUntilTheNextFrameArrives) {
  TrackingDriver driver{radius};
  static_cast<void>(driver.step(frame_with({detection_at(200.0, 150.0)})));
  static_cast<void>(driver.step(frame_with({detection_at(202.0, 150.0)})));
  const TargetTransition lock = driver.step(frame_with({detection_at(204.0, 150.0)}));

  ASSERT_EQ(lock.next.state, TargetState::TARGET_LOCKED);
  EXPECT_EQ(lock.intent, EngagementIntent::AIM_AT_TARGET)
      << "locking must never itself authorise water";
  EXPECT_EQ(driver.state().state, TargetState::TARGET_LOCKED)
      << "with no further frame the engagement simply never resolves";
}

// Verifies: REQ-DEV-002 — `advance` is pure: the same state and the same frame
// give the same transition, every time.
TEST(TargetStateMachine, IsAPureFunctionOfStateAndFrame) {
  const Track engaged = track_with(2, detection_at(300.0, 200.0), 3);
  const TargetMachineState state = locked_on(engaged);

  const TargetTransition first = advance(state, found_with({track_with(2, detection_at(301.0, 201.0), 4)}));
  const TargetTransition second = advance(state, found_with({track_with(2, detection_at(301.0, 201.0), 4)}));

  EXPECT_EQ(first, second);
}

// ---------------------------------------------------------------------------
// REQ-TRK-005 / REQ-TRK-006 — loss, and where it can be entered from.
// ---------------------------------------------------------------------------

// Verifies: REQ-TRK-005 — "the sequence FOUND ×3 then NONE yields states
// TARGET_LOCKED → TARGET_LOST → SEARCHING", and "no fire command is emitted
// anywhere in that sequence".
TEST(TargetStateMachine, LosesThenReturnsToSearchingWithoutFiring) {
  TrackingDriver driver{radius};
  std::vector<TargetState> states;
  std::vector<EngagementIntent> intents;

  for (const double x_px : {200.0, 203.0, 206.0}) {
    const TargetTransition transition = driver.step(frame_with({detection_at(x_px, 150.0)}));
    states.push_back(transition.next.state);
    intents.push_back(transition.intent);
  }
  const TargetTransition lost = driver.step(pigeon::core::DetectionOutcome::none());
  states.push_back(lost.next.state);
  intents.push_back(lost.intent);

  const TargetTransition recovered = driver.step(pigeon::core::DetectionOutcome::none());
  states.push_back(recovered.next.state);
  intents.push_back(recovered.intent);

  ASSERT_EQ(states.size(), 5U);
  EXPECT_EQ(states[2], TargetState::TARGET_LOCKED);
  EXPECT_EQ(states[3], TargetState::TARGET_LOST);
  EXPECT_EQ(states[4], TargetState::SEARCHING);
  for (const EngagementIntent intent : intents) {
    EXPECT_NE(intent, EngagementIntent::FIRE_AT_TARGET)
        << "a lost target must never produce a fire command";
  }
}

// Verifies: REQ-TRK-005, REQ-TRK-006 — TARGET_LOST is left on the following
// transition, and leaving it returns to SEARCHING rather than re-locking.
TEST(TargetStateMachine, LeavesTargetLostForSearchingOnTheNextFrame) {
  const TargetMachineState lost{.state = TargetState::TARGET_LOST,
                                .engaged_track = std::nullopt,
                                .tracks = {},
                                .next_id = static_cast<TrackId>(4)};

  const TargetTransition after_none = advance(lost, FrameInput::none());
  const TargetTransition after_found =
      advance(lost, found_with({track_with(1, detection_at(100.0, 100.0), 5)}));

  EXPECT_EQ(after_none.next.state, TargetState::SEARCHING);
  EXPECT_EQ(after_none.intent, EngagementIntent::KEEP_SEARCHING);
  EXPECT_EQ(after_found.next.state, TargetState::SEARCHING);
  EXPECT_EQ(after_found.intent, EngagementIntent::KEEP_SEARCHING);
  EXPECT_NE(after_found.intent, EngagementIntent::FIRE_AT_TARGET);
}

// Verifies: REQ-TRK-006 — "no sequence of frames starting in SEARCHING reaches
// TARGET_LOST without passing through TARGET_LOCKED". Here directly for the
// searching state; exhaustively in `target_state_exhaustive_test.cpp`.
TEST(TargetStateMachine, NeverEntersTargetLostDirectlyFromSearching) {
  const std::vector<FrameInput> inputs = [] {
    std::vector<FrameInput> built;
    built.push_back(FrameInput::none());
    for (std::uint32_t count = 1; count <= confirmation_frame_count + 1; ++count) {
      built.push_back(found_with({track_with(1, detection_at(100.0, 100.0), count)}));
      built.push_back(found_with({track_with(1, detection_at(100.0, 100.0), count),
                                  track_with(2, detection_at(400.0, 300.0), count)}));
    }
    return built;
  }();

  for (const FrameInput& input : inputs) {
    const TargetTransition transition = advance(TargetMachineState{}, input);
    EXPECT_NE(transition.next.state, TargetState::TARGET_LOST)
        << "TARGET_LOST is reachable only from TARGET_LOCKED";
  }
}

// ---------------------------------------------------------------------------
// REQ-TRK-011 / REQ-TRK-012 — the engagement ends at the burst, and earns
// nothing towards the next one.
// ---------------------------------------------------------------------------

// Verifies: REQ-TRK-011 — "after a fire command has been issued the system
// SHALL return to SEARCHING", in the same transition and with nothing engaged.
TEST(TargetStateMachine, ReturnsToSearchingInTheSameTransitionAsTheBurst) {
  const Track engaged = track_with(2, detection_at(300.0, 200.0), 3);

  const TargetTransition transition =
      advance(locked_on(engaged), found_with({track_with(2, detection_at(300.0, 200.0), 4)}));

  ASSERT_EQ(transition.intent, EngagementIntent::FIRE_AT_TARGET);
  EXPECT_EQ(transition.next.state, TargetState::SEARCHING);
  EXPECT_FALSE(transition.next.engaged_track.has_value());
}

// Verifies: REQ-TRK-012 — "the engaged track's consecutive-detection count
// SHALL be reset" when the burst ends the engagement. Retirement is how the
// reset is realised (ADR-0013), so the fired-upon track must not be carried
// into the next frame with its count intact.
TEST(TargetStateMachine, RetiresTheEngagedTrackWhenTheBurstEndsTheEngagement) {
  const Track engaged = track_with(2, detection_at(300.0, 200.0), 3);
  const Track bystander = track_with(7, detection_at(80.0, 400.0), 5);

  const TargetTransition transition =
      advance(locked_on(engaged, {engaged, bystander}),
              found_with({track_with(2, detection_at(300.0, 200.0), 4), bystander}));

  ASSERT_EQ(transition.intent, EngagementIntent::FIRE_AT_TARGET);
  for (const Track& track : transition.next.tracks) {
    EXPECT_NE(track.id, engaged.id)
        << "the fired-upon bird must earn three fresh detections (REQ-TRK-012)";
  }
  // Verifies: REQ-TRK-012 — "a track that was not the engaged one keeps its
  // consecutive-detection count across the end of another track's engagement."
  ASSERT_EQ(transition.next.tracks.size(), 1U);
  EXPECT_EQ(transition.next.tracks.at(0), bystander);
}

// Verifies: REQ-COM-002, REQ-TRK-012 — abandoning drops the engagement, fires
// nothing, returns to SEARCHING, and retires the engaged track exactly as a
// burst does.
TEST(TargetStateMachine, AbandoningAnEngagementReturnsToSearchingAndRetiresTheTrack) {
  const Track engaged = track_with(2, detection_at(300.0, 200.0), 4);
  const Track bystander = track_with(7, detection_at(80.0, 400.0), 2);

  const TargetMachineState after = pigeon::core::abandon_engagement(locked_on(engaged, {engaged, bystander}));

  EXPECT_EQ(after.state, TargetState::SEARCHING);
  EXPECT_FALSE(after.engaged_track.has_value());
  ASSERT_EQ(after.tracks.size(), 1U);
  EXPECT_EQ(after.tracks.at(0), bystander);
}

// Verifies: REQ-DEV-002 — the identity allocator never moves backwards, so a
// track identifier is never reused within a run.
TEST(TargetStateMachine, NeverMovesTheIdentityAllocatorBackwards) {
  const TargetMachineState state{.state = TargetState::SEARCHING,
                                 .engaged_track = std::nullopt,
                                 .tracks = {},
                                 .next_id = static_cast<TrackId>(12)};

  const TargetTransition with_older_input =
      advance(state, found_with({track_with(1, detection_at(10.0, 10.0), 1)}, 3));
  const TargetTransition with_newer_input =
      advance(state, found_with({track_with(1, detection_at(10.0, 10.0), 1)}, 20));

  EXPECT_GE(with_older_input.next.next_id, state.next_id);
  EXPECT_EQ(with_newer_input.next.next_id, static_cast<TrackId>(20));
}

}  // namespace

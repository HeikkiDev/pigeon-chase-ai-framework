// Verifies: REQ-SAF-002, REQ-TRK-001, REQ-TRK-002, REQ-TRK-004, REQ-TRK-006,
//           REQ-TRK-008, REQ-TRK-009, REQ-TRK-010, REQ-TRK-011, REQ-TRK-012
//
// `REQ-SAF-002` asks for "exhaustive state-machine tests [that] show no
// reachable path to a fire command that skips either condition". This file is
// that test, twice over and from two directions:
//
//   1. An exhaustive breadth-first search over every state reachable from the
//      initial one under every constructible frame input drawn from a small
//      universe of tracks. Nothing is sampled; the whole reachable graph is
//      walked and every transition in it is checked against the safety
//      invariants.
//   2. An exhaustive enumeration of every frame *sequence* of bounded length
//      over a two-bird world, driven through real association, asserting that
//      any burst is preceded by four consecutive frames containing the bird
//      that was fired at — three confirmations (`REQ-TRK-002`) and one
//      verification (`REQ-TRK-004`, `REQ-TRK-010`).
//
// Both are pure-function searches: no clock, no randomness, no ordering
// dependence (`REQ-DEV-002`).

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <set>
#include <span>
#include <string>
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
using pigeon::core::Detection;
using pigeon::core::DetectionOutcome;
using pigeon::core::EngagementIntent;
using pigeon::core::FrameInput;
using pigeon::core::PixelDistance;
using pigeon::core::select_confirmed_target;
using pigeon::core::TargetMachineState;
using pigeon::core::TargetState;
using pigeon::core::TargetTransition;
using pigeon::core::Track;
using pigeon::core::TrackId;
using pigeon::core::TrackUpdate;
using pigeon::test_support::detection_at;
using pigeon::test_support::track_with;
using pigeon::test_support::TrackingDriver;

// --- The universe the search is conducted over ------------------------------
//
// Two identities is the smallest universe that can express every case the
// requirements distinguish: the engaged bird, a different bird, both at once,
// and neither (`REQ-TRK-010`). Counts run one past the confirmation threshold
// so that "not yet confirmed", "just confirmed" and "long confirmed" are all
// represented.

constexpr std::uint32_t first_bird = 1;
constexpr std::uint32_t second_bird = 2;
constexpr std::uint32_t max_count = confirmation_frame_count + 1;

[[nodiscard]] Detection position_of(std::uint32_t bird) {
  return bird == first_bird ? detection_at(100.0, 100.0, 40.0, 40.0)
                            : detection_at(400.0, 300.0, 20.0, 20.0);
}

[[nodiscard]] FrameInput frame_containing(std::uint32_t first_count, std::uint32_t second_count) {
  std::vector<Track> tracks;
  if (first_count > 0) {
    tracks.push_back(track_with(first_bird, position_of(first_bird), first_count));
  }
  if (second_count > 0) {
    tracks.push_back(track_with(second_bird, position_of(second_bird), second_count));
  }
  if (tracks.empty()) {
    return FrameInput::none();
  }
  return FrameInput::found(
      TrackUpdate{.tracks = std::move(tracks), .next_id = static_cast<TrackId>(9)});
}

[[nodiscard]] std::vector<FrameInput> every_frame_input() {
  std::vector<FrameInput> inputs;
  for (std::uint32_t first_count = 0; first_count <= max_count; ++first_count) {
    for (std::uint32_t second_count = 0; second_count <= max_count; ++second_count) {
      inputs.push_back(frame_containing(first_count, second_count));
    }
  }
  return inputs;
}

[[nodiscard]] std::string state_key(const TargetMachineState& state) {
  std::string key = std::to_string(static_cast<int>(state.state));
  key += state.engaged_track.has_value()
             ? "/engaged:" + std::to_string(static_cast<std::uint32_t>(*state.engaged_track))
             : "/engaged:none";
  std::vector<Track> sorted{state.tracks.begin(), state.tracks.end()};
  std::ranges::sort(sorted, [](const Track& lhs, const Track& rhs) { return lhs.id < rhs.id; });
  for (const Track& track : sorted) {
    key += "/" + std::to_string(static_cast<std::uint32_t>(track.id)) + ":" +
           std::to_string(track.consecutive_detections);
  }
  key += "/next:" + std::to_string(static_cast<std::uint32_t>(state.next_id));
  return key;
}

[[nodiscard]] std::string input_description(const FrameInput& input) {
  std::string description = "frame{";
  for (const Track& track : input.detected_tracks()) {
    description += "id " + std::to_string(static_cast<std::uint32_t>(track.id)) + " seen " +
                   std::to_string(track.consecutive_detections) + "x; ";
  }
  return description + "}";
}

[[nodiscard]] bool contains_track(std::span<const Track> tracks, TrackId id) {
  return std::ranges::any_of(tracks, [id](const Track& track) { return track.id == id; });
}

// Verifies: REQ-SAF-002 — the whole reachable state graph, every transition in
// it checked. Also REQ-TRK-001 (no fourth state), REQ-TRK-006 (TARGET_LOST
// only from TARGET_LOCKED), REQ-TRK-008 (one target), REQ-TRK-009 (no track
// survives a frame it was not detected in), REQ-TRK-011 and REQ-TRK-012 (the
// burst ends the engagement and retires its track).
TEST(TargetStateMachineExhaustively, HasNoReachablePathToAFireCommandThatSkipsConfirmation) {
  const std::vector<FrameInput> inputs = every_frame_input();

  std::set<std::string> visited;
  std::deque<TargetMachineState> frontier;
  frontier.push_back(TargetMachineState{});
  visited.insert(state_key(TargetMachineState{}));

  std::set<TargetState> observed_states;
  std::set<EngagementIntent> observed_intents;
  int transitions_checked = 0;

  while (!frontier.empty()) {
    const TargetMachineState current = frontier.front();
    frontier.pop_front();

    // Invariant of the state itself: an engaged track exists only while locked
    // (`REQ-TRK-008`, target_state.hpp).
    if (current.engaged_track.has_value()) {
      ASSERT_EQ(current.state, TargetState::TARGET_LOCKED)
          << "reachable state " << state_key(current) << " engages a track without being locked";
    }

    for (const FrameInput& input : inputs) {
      const TargetTransition transition = advance(current, input);
      ++transitions_checked;
      observed_states.insert(transition.next.state);
      observed_intents.insert(transition.intent);

      const std::string context = "from " + state_key(current) + " on " + input_description(input);

      // REQ-TRK-001: exactly three states, and no transition produces another.
      ASSERT_TRUE(transition.next.state == TargetState::SEARCHING ||
                  transition.next.state == TargetState::TARGET_LOCKED ||
                  transition.next.state == TargetState::TARGET_LOST)
          << context << ": produced a state outside the three of REQ-TRK-001";

      // REQ-TRK-006: TARGET_LOST is entered only from TARGET_LOCKED.
      if (transition.next.state == TargetState::TARGET_LOST) {
        ASSERT_EQ(current.state, TargetState::TARGET_LOCKED)
            << context << ": entered TARGET_LOST from somewhere other than TARGET_LOCKED";
      }

      // REQ-AIM-001: an intent to act always carries something to act on.
      const bool acts = transition.intent == EngagementIntent::AIM_AT_TARGET ||
                        transition.intent == EngagementIntent::FIRE_AT_TARGET;
      ASSERT_EQ(transition.aim_at.has_value(), acts)
          << context << ": aim point and intent disagree";

      // REQ-SAF-002, REQ-TRK-004, REQ-TRK-010: a burst requires a live
      // engagement whose track was re-detected in this very frame.
      if (transition.intent == EngagementIntent::FIRE_AT_TARGET) {
        ASSERT_EQ(current.state, TargetState::TARGET_LOCKED)
            << context << ": fired without a confirmed engagement";
        ASSERT_TRUE(current.engaged_track.has_value())
            << context << ": fired with no engaged track";
        ASSERT_TRUE(contains_track(input.detected_tracks(), *current.engaged_track))
            << context << ": fired on a frame that did not re-detect the engaged track";

        // REQ-TRK-011, REQ-TRK-012: the engagement ends here, and its track is
        // retired rather than carried forward with its count.
        ASSERT_EQ(transition.next.state, TargetState::SEARCHING)
            << context << ": did not return to SEARCHING after firing";
        ASSERT_FALSE(transition.next.engaged_track.has_value())
            << context << ": kept an engaged track after firing";
        ASSERT_FALSE(contains_track(transition.next.tracks, *current.engaged_track))
            << context << ": carried the fired-upon track forward (REQ-TRK-012)";
      }

      // REQ-TRK-002, REQ-TRK-008: locking requires a confirmed track in this
      // frame, and the engaged track is the one selection chooses.
      if (transition.next.state == TargetState::TARGET_LOCKED) {
        ASSERT_TRUE(transition.next.engaged_track.has_value())
            << context << ": locked on nothing";
        const std::optional<Track> expected = select_confirmed_target(input.detected_tracks());
        ASSERT_TRUE(expected.has_value())
            << context << ": locked although no track in the frame was confirmed";
        ASSERT_EQ(*transition.next.engaged_track, expected->id)
            << context << ": engaged a track other than the selected one";
        ASSERT_GE(expected->consecutive_detections, confirmation_frame_count)
            << context << ": engaged a track with fewer than three consecutive detections";
      }

      // REQ-TRK-009: every carried track was detected in this frame.
      for (const Track& carried : transition.next.tracks) {
        ASSERT_TRUE(contains_track(input.detected_tracks(), carried.id))
            << context << ": carried a track that was not detected in the frame";
      }

      const std::string key = state_key(transition.next);
      if (visited.insert(key).second) {
        frontier.push_back(transition.next);
      }
    }
  }

  // Guards against a vacuous pass: a search that reached nothing proves
  // nothing.
  EXPECT_GT(transitions_checked, 0);
  EXPECT_EQ(observed_states.size(), 3U) << "the search did not reach all three states";
  EXPECT_TRUE(observed_intents.contains(EngagementIntent::FIRE_AT_TARGET))
      << "the search never reached a fire command, so it proved nothing about firing";
  EXPECT_TRUE(observed_intents.contains(EngagementIntent::AIM_AT_TARGET));
  EXPECT_TRUE(observed_intents.contains(EngagementIntent::ABANDON));
  EXPECT_TRUE(observed_intents.contains(EngagementIntent::KEEP_SEARCHING));
}

// --- Exhaustive over frame sequences, through real association --------------

enum class FrameKind : std::uint8_t { NONE, FIRST_BIRD, SECOND_BIRD, BOTH };

constexpr std::array<FrameKind, 4> frame_alphabet{FrameKind::NONE, FrameKind::FIRST_BIRD,
                                                  FrameKind::SECOND_BIRD, FrameKind::BOTH};

[[nodiscard]] DetectionOutcome outcome_for(FrameKind kind) {
  switch (kind) {
    case FrameKind::NONE:
      return DetectionOutcome::none();
    case FrameKind::FIRST_BIRD:
      return DetectionOutcome::found({position_of(first_bird)});
    case FrameKind::SECOND_BIRD:
      return DetectionOutcome::found({position_of(second_bird)});
    case FrameKind::BOTH:
      return DetectionOutcome::found({position_of(first_bird), position_of(second_bird)});
  }
  return DetectionOutcome::none();
}

[[nodiscard]] bool frame_contains(FrameKind kind, std::uint32_t bird) {
  if (kind == FrameKind::BOTH) {
    return true;
  }
  return bird == first_bird ? kind == FrameKind::FIRST_BIRD : kind == FrameKind::SECOND_BIRD;
}

[[nodiscard]] std::string describe(const std::vector<FrameKind>& sequence) {
  std::string description;
  for (const FrameKind kind : sequence) {
    switch (kind) {
      case FrameKind::NONE:
        description += "N";
        break;
      case FrameKind::FIRST_BIRD:
        description += "A";
        break;
      case FrameKind::SECOND_BIRD:
        description += "B";
        break;
      case FrameKind::BOTH:
        description += "*";
        break;
    }
  }
  return description;
}

// Verifies: REQ-SAF-002, REQ-TRK-002, REQ-TRK-003, REQ-TRK-004, REQ-TRK-010 —
// every frame sequence of length six over a two-bird world, run through real
// association. A burst may only ever appear after four consecutive frames
// containing the bird it was aimed at.
TEST(TargetStateMachineExhaustively, NeverFiresWithoutFourConsecutiveFramesOfTheSameBird) {
  constexpr std::size_t sequence_length = 6;
  constexpr PixelDistance radius{30.0};

  std::vector<FrameKind> sequence(sequence_length, FrameKind::NONE);
  const std::size_t total_sequences = [] {
    std::size_t total = 1;
    for (std::size_t index = 0; index < sequence_length; ++index) {
      total *= frame_alphabet.size();
    }
    return total;
  }();

  int bursts_seen = 0;

  for (std::size_t encoded = 0; encoded < total_sequences; ++encoded) {
    std::size_t remainder = encoded;
    for (std::size_t index = 0; index < sequence_length; ++index) {
      sequence[index] = frame_alphabet[remainder % frame_alphabet.size()];
      remainder /= frame_alphabet.size();
    }

    TrackingDriver driver{radius};
    for (std::size_t index = 0; index < sequence_length; ++index) {
      const TargetTransition transition = driver.step(outcome_for(sequence[index]));
      if (transition.intent != EngagementIntent::FIRE_AT_TARGET) {
        continue;
      }
      ++bursts_seen;

      ASSERT_TRUE(transition.aim_at.has_value()) << describe(sequence);
      const std::uint32_t bird =
          transition.aim_at->centroid_px == position_of(first_bird).centroid_px ? first_bird
                                                                               : second_bird;

      ASSERT_GE(index, confirmation_frame_count)
          << describe(sequence) << ": fired at frame " << index
          << ", too early for three confirmations and a verification";
      for (std::size_t back = 0; back <= confirmation_frame_count; ++back) {
        ASSERT_TRUE(frame_contains(sequence[index - back], bird))
            << describe(sequence) << ": fired at frame " << index << " on bird " << bird
            << ", which was absent from frame " << (index - back);
      }
    }
  }

  EXPECT_GT(bursts_seen, 0)
      << "no sequence produced a burst, so this search proved nothing about firing";
}

}  // namespace

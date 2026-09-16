#pragma once

// Verifies: support code for REQ-TRK-002, REQ-TRK-003, REQ-TRK-007,
// REQ-TRK-009 — association plus one transition, with no safety policy, no
// clock and no link in the way.

#include <utility>
#include <vector>

#include "pigeon/core/configuration.hpp"
#include "pigeon/core/detection.hpp"
#include "pigeon/core/geometry.hpp"
#include "pigeon/core/target_state.hpp"
#include "pigeon/core/track.hpp"

namespace pigeon::test_support {

/// Drives the tracking half of the pipeline: associate this frame's
/// detections, then advance the state machine by one frame.
///
/// Exists so that a frame-sequence test reads as a sequence of frames rather
/// than as a sequence of hand-built `TrackUpdate`s, and so that the counters
/// under test are the ones association actually produces.
class TrackingDriver {
 public:
  explicit TrackingDriver(pigeon::core::PixelDistance association_radius)
      : association_radius_{association_radius} {}

  /// One frame in, one transition out. The carried state is updated to the
  /// transition's `next`, which is the only set with the `REQ-TRK-012`
  /// retirement applied.
  pigeon::core::TargetTransition step(const pigeon::core::DetectionOutcome& outcome) {
    pigeon::core::TrackUpdate update = pigeon::core::associate_detections(
        state_.tracks, outcome, association_radius_, state_.next_id);
    const pigeon::core::FrameInput input =
        update.tracks.empty() ? pigeon::core::FrameInput::none()
                              : pigeon::core::FrameInput::found(std::move(update));
    pigeon::core::TargetTransition transition = pigeon::core::advance(state_, input);
    state_ = transition.next;
    return transition;
  }

  [[nodiscard]] const pigeon::core::TargetMachineState& state() const noexcept { return state_; }

 private:
  pigeon::core::PixelDistance association_radius_{};
  pigeon::core::TargetMachineState state_{};
};

}  // namespace pigeon::test_support

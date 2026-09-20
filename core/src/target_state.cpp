#include "pigeon/core/target_state.hpp"

#include <algorithm>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "pigeon/core/detection.hpp"
#include "pigeon/core/track.hpp"

namespace pigeon::core {
namespace {

/// The track with this identity in `tracks`, or `nullptr`. Verification is
/// about identity, not presence (`REQ-TRK-010`).
[[nodiscard]] const Track* track_with_id(std::span<const Track> tracks, TrackId id) noexcept {
  const auto found =
      std::ranges::find_if(tracks, [id](const Track& track) { return track.id == id; });
  return found == tracks.end() ? nullptr : &*found;
}

/// Drop the engaged track from a carried set, which is how the confirmation
/// reset of `REQ-TRK-012` is realised (ADR-0013).
void retire(std::vector<Track>& tracks, TrackId id) {
  const auto removed =
      std::ranges::remove_if(tracks, [id](const Track& track) { return track.id == id; });
  tracks.erase(removed.begin(), removed.end());
}

}  // namespace

FrameInput FrameInput::none() { return {}; }

FrameInput FrameInput::found(TrackUpdate update) {
  FrameInput input;
  // An update with no tracks is a NONE frame, and a NONE frame allocated
  // nothing, so it carries no identifier either.
  if (!update.tracks.empty()) {
    input.update_ = std::move(update);
  }
  return input;
}

DetectionResult FrameInput::result() const noexcept {
  return update_.tracks.empty() ? DetectionResult::NONE : DetectionResult::FOUND;
}

std::span<const Track> FrameInput::detected_tracks() const noexcept { return update_.tracks; }

TrackId FrameInput::next_id() const noexcept { return update_.next_id; }

TargetTransition advance(const TargetMachineState& current, const FrameInput& input) {
  TargetTransition transition;
  transition.next.tracks.assign(input.detected_tracks().begin(), input.detected_tracks().end());
  // The allocator never moves backwards, so an identifier is never reused
  // within a run (`REQ-DEV-002`).
  transition.next.next_id = std::max(current.next_id, input.next_id());

  switch (current.state) {
    case TargetState::SEARCHING:
    // TARGET_LOST is stored for exactly one transition and is then left
    // immediately: the frame that leaves it is judged as any other searching
    // frame, so it may lock directly (`REQ-TRK-005`, ADR-0018). Nothing needs
    // clearing on the way out — a lost track was not detected, so
    // `REQ-TRK-009` discarded it when the loss was observed.
    case TargetState::TARGET_LOST: {
      const std::optional<Track> selected = select_confirmed_target(input.detected_tracks());
      if (!selected.has_value()) {
        transition.next.state = TargetState::SEARCHING;
        transition.intent = EngagementIntent::KEEP_SEARCHING;
        break;
      }
      // Three consecutive detections of one track, and exactly one target
      // (`REQ-TRK-002`, `REQ-TRK-008`). Locking here still owes a verification
      // frame before anything may fire (`REQ-TRK-004`, `REQ-SAF-002`).
      transition.next.state = TargetState::TARGET_LOCKED;
      transition.next.engaged_track = selected->id;
      transition.intent = EngagementIntent::AIM_AT_TARGET;
      transition.aim_at = selected->latest;
      break;
    }

    case TargetState::TARGET_LOCKED: {
      const Track* re_detected = nullptr;
      if (current.engaged_track.has_value()) {
        re_detected = track_with_id(input.detected_tracks(), *current.engaged_track);
      }
      if (re_detected == nullptr) {
        // The verification frame did not re-detect the engaged track, whether
        // it was NONE or FOUND on other birds (`REQ-TRK-005`, `REQ-TRK-010`).
        transition.next.state = TargetState::TARGET_LOST;
        transition.intent = EngagementIntent::ABANDON;
        break;
      }
      // The burst and the return to searching are one step: there is no state
      // left to sit in (`REQ-TRK-004`, `REQ-TRK-011`).
      transition.intent = EngagementIntent::FIRE_AT_TARGET;
      transition.aim_at = re_detected->latest;
      transition.next.state = TargetState::SEARCHING;
      retire(transition.next.tracks, re_detected->id);
      break;
    }
  }

  return transition;
}

TargetMachineState abandon_engagement(const TargetMachineState& current) {
  TargetMachineState next = current;
  next.state = TargetState::SEARCHING;
  if (current.engaged_track.has_value()) {
    // An abandoned engagement earns no credit either (`REQ-TRK-012`).
    retire(next.tracks, *current.engaged_track);
  }
  next.engaged_track.reset();
  return next;
}

}  // namespace pigeon::core

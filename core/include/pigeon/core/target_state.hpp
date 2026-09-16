#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "pigeon/core/detection.hpp"
#include "pigeon/core/track.hpp"

/// The target state machine: the safety-critical heart of the system.
///
/// Everything in this header is a **pure function of its arguments**. No
/// clock, no randomness, no I/O, no hidden state, no allocation-dependent
/// behaviour. That is not a stylistic preference: `REQ-SAF-002` requires that
/// no reachable path produces a fire command without confirmation, and
/// exhaustive path testing is only feasible for a function whose inputs are
/// fully enumerable. ADR-0005 keeps time out of it by counting frames.
namespace pigeon::core {

/// The three target states, and no others (`REQ-TRK-001`).
///
/// `TARGET_LOST` is reachable only from `TARGET_LOCKED` (`REQ-TRK-006`); the
/// transition function is the only thing that may produce it.
enum class TargetState : std::uint8_t {
  /// No confirmed target. The initial state (`REQ-TRK-001`).
  SEARCHING,
  /// A confirmed target exists and is being engaged (`REQ-TRK-002`).
  TARGET_LOCKED,
  /// The verification frame after locking did not re-detect the engaged track
  /// (`REQ-TRK-005`, `REQ-TRK-010`).
  TARGET_LOST,
};

/// What the transition asks the rest of the system to do about this frame.
///
/// An *intent*, not a command. The state machine may not decide to fire on its
/// own: an intent still has to survive the envelope clamp (`REQ-AIM-002`), the
/// exclusion zone (`REQ-SAF-003`), the rate limiter (`REQ-SAF-005`) and the
/// state of the actuator link (`REQ-COM-002`) before it becomes a command.
/// Keeping the decision and the authorisation in different components is what
/// lets both be tested exhaustively.
enum class EngagementIntent : std::uint8_t {
  /// Nothing to do this frame.
  KEEP_SEARCHING,
  /// A target was confirmed: point the deterrent at `TargetTransition::aim_at`
  /// (`REQ-TRK-002`, `REQ-AIM-001`).
  AIM_AT_TARGET,
  /// The verification frame re-detected the engaged track: the fire command
  /// may now be authorised (`REQ-TRK-004`, `REQ-TRK-010`, `REQ-SAF-002`). The
  /// engagement ends with this intent (`REQ-TRK-011`).
  FIRE_AT_TARGET,
  /// The engagement ends without firing (`REQ-TRK-005`, `REQ-COM-002`).
  ABANDON,
};

/// The complete state of the machine: the single value a caller carries from
/// one frame to the next.
///
/// It carries the **track set** as well as the state and the engaged track,
/// and that is a safety decision rather than a convenience. The confirmation
/// counters live on the tracks (`REQ-TRK-007`); `REQ-TRK-012` requires the
/// engaged track's confirmation to be retired the moment an engagement ends;
/// and the state machine is the only component that knows when that happened.
/// Were the counters held in a variable beside this one, applying the
/// retirement would be a step the application loop could omit, and a safety
/// property that depends on remembering to apply it is not a safety property
/// (ADR-0013).
///
/// The cycle therefore runs one way, and through here:
///
/// ```text
/// current.tracks ─▶ associate_detections ─▶ TrackUpdate ─▶ FrameInput
///        ▲                                                      │
///        └──────────── advance(current, input).next ◀───────────┘
/// ```
///
/// The only track set a caller has to feed into the next frame's association
/// is the one `advance` — or `abandon_engagement` — handed back, and that set
/// already has the retirement applied. A caller that wanted the un-retired set
/// would have to keep a second copy of the state deliberately; it cannot get
/// there by forgetting a step.
///
/// It is still a plain value, so a test can construct any state directly
/// rather than having to drive the machine into it.
struct TargetMachineState {
  /// The current state. `SEARCHING` on construction (`REQ-TRK-001`).
  TargetState state{TargetState::SEARCHING};

  /// The track being engaged. Present only while `TARGET_LOCKED`; exactly one
  /// target is engaged at a time (`REQ-TRK-008`).
  std::optional<TrackId> engaged_track;

  /// The tracks to carry into the next frame's association: the tracks
  /// detected in the frame just processed, less any track retired because its
  /// engagement ended (`REQ-TRK-009`, `REQ-TRK-012`). Empty on construction —
  /// a new run has seen nothing.
  std::vector<Track> tracks;

  /// The first track identifier not yet handed out. Carried here so that the
  /// caller holds one value rather than two, and never moves backwards, which
  /// is what keeps identifiers unique within a run (`REQ-DEV-002`).
  TrackId next_id{};

  [[nodiscard]] bool operator==(const TargetMachineState&) const = default;
};

/// One frame as the state machine sees it.
///
/// The confirmation counters live on the tracks (`REQ-TRK-007`), so the
/// machine needs no counter of its own: it is handed the tracks that received
/// a detection in this frame and reads their counts. It is handed the tracks
/// rather than a bare classification because verification is about identity,
/// not presence — the locked track itself must be re-detected
/// (`REQ-TRK-010`).
///
/// The invariant "`FOUND` if and only if at least one track was detected" is
/// maintained by construction, so the contradictory input — `FOUND` with
/// nothing detected — cannot be built and need not be tested for.
class FrameInput {
 public:
  /// A frame classified `NONE`. The default, because the absence of evidence
  /// is the safe input (`REQ-TRK-003`).
  FrameInput() = default;

  /// An explicit `NONE` frame, for readability at call sites.
  [[nodiscard]] static FrameInput none();

  /// A frame in which the tracks of `update` each received a detection.
  ///
  /// Takes the whole `TrackUpdate` returned by `associate_detections` rather
  /// than just its tracks, because the identity allocator has to reach the
  /// next state too. Its tracks are exactly the tracks detected in this frame:
  /// association keeps no others (`REQ-TRK-009`). An update with no tracks
  /// yields a `NONE` frame.
  [[nodiscard]] static FrameInput found(TrackUpdate update);

  /// The frame classification (`REQ-DET-001`).
  [[nodiscard]] DetectionResult result() const noexcept;

  /// The tracks that received a detection in this frame.
  [[nodiscard]] std::span<const Track> detected_tracks() const noexcept;

  /// The first identifier not yet handed out, as reported by association.
  /// Zero for a `NONE` frame, which allocates nothing.
  [[nodiscard]] TrackId next_id() const noexcept;

 private:
  TrackUpdate update_;
};

/// The result of one transition: the next state and what to do about it.
struct TargetTransition {
  /// The state to carry into the next frame — including the track set, with
  /// any retirement already applied (`REQ-TRK-012`).
  TargetMachineState next;

  /// What the caller should attempt this frame.
  EngagementIntent intent{EngagementIntent::KEEP_SEARCHING};

  /// The detection to aim at, in the image frame. Present for
  /// `AIM_AT_TARGET` and `FIRE_AT_TARGET`, absent otherwise, so an intent to
  /// act always carries something to act on (`REQ-AIM-001`).
  std::optional<Detection> aim_at;

  [[nodiscard]] bool operator==(const TargetTransition&) const = default;
};

/// Advance the target state machine by exactly one frame.
///
/// The single transition function. Pure: same arguments, same result, always.
/// It reads no clock, because the `REQ-TRK-004` deadline is "the immediately
/// following frame" rather than a duration (ADR-0005).
///
/// The transitions it implements:
/// * `SEARCHING` + a confirmed selected track → `TARGET_LOCKED`,
///   `AIM_AT_TARGET` (`REQ-TRK-002`, `REQ-TRK-008`). Confirmation requires
///   `confirmation_frame_count` consecutive detections of **one** track, which
///   is a property of the track, so three different birds in three frames
///   cannot confirm anything.
/// * `SEARCHING` + `NONE` → `SEARCHING`, `KEEP_SEARCHING`. The counters are
///   reset by association discarding the tracks, not here (`REQ-TRK-003`,
///   `REQ-TRK-009`).
/// * `TARGET_LOCKED` + a verification frame **containing the engaged track** →
///   all of the following in **one** transition: intent `FIRE_AT_TARGET`,
///   aimed at that track's latest detection; next state `SEARCHING` with no
///   engaged track; and the engaged track retired from the carried set
///   (`REQ-TRK-004`, `REQ-TRK-010`, `REQ-TRK-011`, `REQ-TRK-012`). The burst
///   and the return to searching are the same step because there is no state
///   left to sit in: a fourth state would breach `REQ-TRK-001`, and remaining
///   `TARGET_LOCKED` for another frame would re-verify and fire again. The
///   same bird may be engaged again, but only after three fresh consecutive
///   detections and no sooner than the `REQ-SAF-005` cool-down allows.
/// * `TARGET_LOCKED` + a verification frame **without** the engaged track →
///   `TARGET_LOST`, `ABANDON`, no fire — whether the frame is `NONE` or
///   `FOUND` on other birds (`REQ-TRK-005`, `REQ-TRK-010`, ADR-0009). This is
///   why the input carries tracks rather than a bare classification: one bird
///   must not confirm an engagement that another bird then authorises.
/// * `TARGET_LOST` → `SEARCHING`, `KEEP_SEARCHING`. `TARGET_LOST` is entered
///   from `TARGET_LOCKED` and from nowhere else (`REQ-TRK-006`), and is left on
///   the following transition so that the sequence `TARGET_LOCKED` →
///   `TARGET_LOST` → `SEARCHING` of `REQ-TRK-005` is observable rather than
///   collapsed into one step.
/// * If no further frame arrives, no transition happens, the engagement never
///   resolves, and no fire command is issued. Inaction is the safe failure.
///
/// What it does with the track set, which is the other half of its job:
/// * `next.tracks` is the tracks of `input`, **less the engaged track when
///   this transition ends an engagement** (`REQ-TRK-012`, ADR-0013). The
///   retired bird's next detection starts a new track at a count of one, so it
///   must earn three fresh consecutive detections before it can be confirmed
///   again, exactly as `REQ-SAF-002` requires of "the current engagement".
///   Retirement is how the reset is realised, for the same reason a missed
///   frame discards a track rather than zeroing it (`REQ-TRK-009`, ADR-0010):
///   it keeps the invariant that a live track's count is never zero.
/// * Only `FIRE_AT_TARGET` retires a track here. `TARGET_LOST` needs no rule
///   of its own — the engaged track was not detected, so association has
///   already discarded it — and the link-failure path is served by
///   `abandon_engagement`.
/// * `next.next_id` is the larger of `current.next_id` and `input.next_id()`,
///   so the allocator never moves backwards and an identifier is never reused
///   within a run (`REQ-DEV-002`).
///
/// **Derived, not specified:** an engagement ends when this function emits
/// `FIRE_AT_TARGET`, whether or not `SafetyPolicy` then authorises the command
/// and whether or not the link accepts it. The machine is pure and hears
/// nothing back (ADR-0007), so it cannot behave otherwise; the effect is that
/// a refused burst also retires the track, which is the conservative direction.
[[nodiscard]] TargetTransition advance(const TargetMachineState& current, const FrameInput& input);

/// Abandon the current engagement without firing (`REQ-COM-002`).
///
/// The fail-safe entry point, for when the actuator link becomes unavailable
/// between locking and firing: the engagement is dropped, no fire command is
/// issued, and the machine returns to `SEARCHING` with no engaged track. It is
/// a separate pure function rather than an input to `advance` because the
/// trigger is a property of the link, not of a frame.
///
/// It retires the engaged track from `current.tracks` exactly as a burst does,
/// so an abandoned engagement earns no credit either: the same bird must be
/// detected in three fresh consecutive frames before it can be confirmed again
/// (`REQ-TRK-012`, ADR-0013). Returning the whole state rather than only the
/// state enum is what makes that retirement unavoidable — the caller has no
/// separate track set to keep.
[[nodiscard]] TargetMachineState abandon_engagement(const TargetMachineState& current);

}  // namespace pigeon::core

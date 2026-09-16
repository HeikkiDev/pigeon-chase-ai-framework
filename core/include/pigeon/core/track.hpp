#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "pigeon/core/detection.hpp"
#include "pigeon/core/geometry.hpp"

namespace pigeon::core {

/// Identity of a track: one pigeon followed across frames (`REQ-TRK-007`).
///
/// Opaque by design — the numeric value carries no meaning beyond identity and
/// must never be interpreted as an index, a count or a priority. It is a
/// scoped enumeration with no enumerators so that it compares and orders like
/// an integer without converting to one implicitly.
///
/// Identifiers are assigned deterministically in ascending order and are never
/// reused within a run, which is what makes a scenario replay reproducible
/// (`REQ-DEV-002`).
enum class TrackId : std::uint32_t {};

/// A pigeon followed across frames.
///
/// Tracks live only as long as they are seen and only until their engagement
/// ends. A track that receives no detection in a frame is **discarded** rather
/// than kept with a zeroed count (`REQ-TRK-009`, ADR-0010), and the engaged
/// track is **retired** when its engagement ends (`REQ-TRK-012`, ADR-0013).
/// Two invariants follow, and both simplify everything downstream:
///
/// * every track in a track set was detected in the frame just processed;
/// * `consecutive_detections` is therefore always at least one.
struct Track {
  /// Stable identity of this track for as long as it survives.
  TrackId id{};

  /// The most recent detection associated with this track, in the image
  /// frame. This is the position aiming uses (`REQ-AIM-001`) and the position
  /// selection orders by (`REQ-TRK-008`).
  Detection latest{};

  /// Number of consecutive frames in which this track was detected, counting
  /// the frame just processed. Never zero for a live track.
  ///
  /// Both counter resets the specification requires — the one for a missed
  /// frame (`REQ-TRK-003`) and the one for an ended engagement
  /// (`REQ-TRK-012`) — happen by the track ceasing to exist, which is the
  /// strongest form of reset available: there is no stale count left to read.
  /// A bird that reappears afterwards is a new track at a count of one and
  /// must earn `confirmation_frame_count` fresh consecutive detections.
  std::uint32_t consecutive_detections{};

  [[nodiscard]] constexpr bool operator==(const Track&) const = default;
};

/// Consecutive detections of **one track** required before a target is
/// confirmed (`REQ-TRK-002`).
///
/// A compile-time constant rather than a configured parameter: it is part of
/// the specification, not of the physical rig, and `REQ-AIM-003` lists only
/// rig properties as configuration.
inline constexpr std::uint32_t confirmation_frame_count = 3;

/// Whether this track has been detected in enough consecutive frames to be
/// confirmed (`REQ-TRK-002`).
[[nodiscard]] bool is_confirmed(const Track& track) noexcept;

/// The track set after one frame has been associated, plus the identity
/// counter, on their way to the state machine.
///
/// The counter is threaded through the caller rather than held inside the
/// tracker, so association stays a pure function with no hidden state
/// (ADR-0003).
///
/// This is *this frame's observation*, not the set to carry forward. It is
/// handed to `FrameInput::found` and reaches the next frame only as
/// `TargetTransition::next`, because a retirement may still be applied to it
/// (`REQ-TRK-012`, ADR-0013).
struct TrackUpdate {
  /// Every track that survives the frame — which is exactly the set of tracks
  /// detected in it (`REQ-TRK-009`) — in a deterministic order that does not
  /// depend on the detector's ordering (`REQ-DEV-002`).
  std::vector<Track> tracks;

  /// The first identifier not yet handed out. Pass it to the next call.
  TrackId next_id{};
};

/// Associate one frame's detections with the existing tracks (`REQ-TRK-007`,
/// `REQ-TRK-009`, ADR-0003, ADR-0010).
///
/// A pure function: no clock, no randomness, no I/O, no member state. The same
/// arguments always produce the same result.
///
/// Rules:
/// * each detection joins the **nearest** existing track whose centroid lies
///   within `association_radius` of it. The radius is **inclusive**: a
///   centroid exactly `association_radius` away associates, so the test is
///   `distance <= association_radius` (`REQ-TRK-007`). Exact equality is
///   vanishingly rare in floating point; the rule exists so that the tracker
///   and its tests cannot each guess differently;
/// * a detection that matches no track starts a new track, taking `next_id`;
/// * a track that received a detection has its count incremented and its
///   `latest` replaced;
/// * a track that received no detection is **discarded**, which is how the
///   counter reset of `REQ-TRK-003` is realised (`REQ-TRK-009`). A `NONE`
///   frame therefore empties the track set entirely.
///
/// A zero `association_radius` matches nothing but a detection exactly on a
/// track's centroid, which no real detector produces twice, so the default of
/// an uncalibrated configuration starts a new track for every detection and
/// confirms nothing. That is the inert behaviour `REQ-SAF-004` asks for.
///
/// There is no track-retention or track-decay parameter, and adding one would
/// change no confirmation outcome: a retained track with a zeroed count
/// re-associates at one, exactly as a new track would (`REQ-TRK-009`,
/// ADR-0010).
/// The `tracks` argument is the carried set, which comes from
/// `TargetMachineState::tracks` and from nowhere else: it is the only set with
/// the `REQ-TRK-012` retirement already applied.
[[nodiscard]] TrackUpdate associate_detections(std::span<const Track> tracks,
                                               const DetectionOutcome& outcome,
                                               PixelDistance association_radius, TrackId next_id);

/// Choose the single track to engage (`REQ-TRK-008`, ADR-0003).
///
/// Considers only confirmed tracks (`is_confirmed`). Among those it returns
/// the one with the largest bounding-box area; ties are broken deterministically
/// by **ascending centroid X**, then by **ascending centroid Y**. Returns
/// `std::nullopt` when no track is confirmed.
///
/// Determinism matters more than the particular rule here: pigeons are
/// gregarious, so ties are ordinary, and a scenario test must produce the same
/// engagement on every run (`REQ-DEV-002`).
[[nodiscard]] std::optional<Track> select_confirmed_target(std::span<const Track> tracks);

}  // namespace pigeon::core

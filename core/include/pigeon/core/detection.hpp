#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "pigeon/core/geometry.hpp"

namespace pigeon::core {

/// The classification of one frame. Exactly two values, and never any other
/// (`REQ-DET-001`).
///
/// The set is closed on purpose: a downstream state machine that must handle
/// "unknown", "error" or "maybe" is a state machine whose safety cannot be
/// established by enumeration.
enum class DetectionResult : std::uint8_t {
  /// No pigeon was located in the frame.
  NONE,
  /// At least one pigeon was located in the frame.
  FOUND,
};

/// One pigeon located within a single frame.
///
/// Both members are in the image frame, in pixels (see `geometry.hpp`). The
/// centroid is what aiming consumes (`REQ-AIM-001`) and what association
/// measures distance against (`REQ-TRK-007`); the bounding box is what target
/// selection orders by (`REQ-TRK-008`).
///
/// Detection confidence is deliberately absent: it is a property of whichever
/// model is loaded, and ADR-0003 rejected it as a selection key for exactly
/// that reason.
struct Detection {
  /// Centre of the detected pigeon, in the image frame.
  PixelPoint centroid_px{};
  /// Extent of the detected pigeon, in the image frame.
  BoundingBox bounding_box_px{};

  [[nodiscard]] constexpr bool operator==(const Detection&) const = default;
};

/// The complete result of classifying one frame: the `REQ-DET-001` verdict
/// together with the detections that justify it.
///
/// The invariant "`FOUND` if and only if at least one detection is present" is
/// maintained by construction, so a `FOUND` outcome carrying nothing to aim at
/// — a state the rest of the pipeline has no sensible response to — cannot be
/// built.
class DetectionOutcome {
 public:
  /// A `NONE` outcome. The default is the empty one, so a
  /// default-constructed outcome can never trigger an engagement.
  DetectionOutcome() = default;

  /// An explicit `NONE` outcome, for readability at call sites.
  [[nodiscard]] static DetectionOutcome none();

  /// A `FOUND` outcome carrying every detection in the frame.
  ///
  /// Passing an empty vector yields a `NONE` outcome: the classification
  /// follows from the evidence rather than from the caller's claim.
  [[nodiscard]] static DetectionOutcome found(std::vector<Detection> detections);

  /// The frame classification (`REQ-DET-001`). Total: never throws.
  [[nodiscard]] DetectionResult result() const noexcept;

  /// The detections in this frame, in the detector's own order. Empty when the
  /// result is `NONE`.
  ///
  /// Association must not depend on this order (`REQ-TRK-007`,
  /// `REQ-DEV-002`).
  [[nodiscard]] std::span<const Detection> detections() const noexcept;

 private:
  std::vector<Detection> detections_;
};

}  // namespace pigeon::core

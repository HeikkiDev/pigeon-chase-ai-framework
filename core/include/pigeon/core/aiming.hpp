#pragma once

#include <optional>

#include "pigeon/core/geometry.hpp"

/// Targeting maths: image position to servo angles, and the mechanical
/// envelope that bounds them.
///
/// Servo coordinate frame, stated once so nothing downstream has to guess
/// (ADR-0004, `REQ-AIM-002`):
///
/// | Axis | Datum                  | Positive direction |
/// | ---- | ---------------------- | ------------------ |
/// | X    | 0° is straight ahead   | to the right       |
/// | Y    | 0° is **the horizon**  | elevated           |
///
/// Y = 0° at the horizon is a safety property of the datum itself: "never point
/// at the ground" becomes "never go negative", which the deployment envelope
/// Y ∈ [0°, +45°] expresses structurally rather than as a rule someone has to
/// remember.
namespace pigeon::core {

/// An angle in degrees.
///
/// A distinct type so that an angle cannot be passed where a pixel distance, a
/// duration or a bare count is expected. The frame an angle belongs to is
/// carried by the aggregate that holds it, never by this type alone.
struct Angle {
  /// Magnitude in degrees.
  double degrees{};

  [[nodiscard]] constexpr auto operator<=>(const Angle&) const = default;
};

/// A pair of angles in the servo frame: where the deterrent points.
///
/// X = 0° straight ahead with positive to the right; Y = 0° at the horizon
/// with positive elevated (`REQ-AIM-002`, ADR-0004).
struct ServoAngles {
  /// Pan angle, in the servo frame.
  Angle x{};
  /// Tilt angle, in the servo frame. Never negative in a configured envelope.
  Angle y{};

  [[nodiscard]] constexpr bool operator==(const ServoAngles&) const = default;
};

/// The camera's angular coverage.
///
/// The only optical calibration the boresighted design needs: no focal length,
/// no principal point, no distortion coefficients, and above all no range
/// estimate (ADR-0004).
struct FieldOfView {
  /// Total horizontal coverage of the frame, in degrees.
  Angle horizontal{};
  /// Total vertical coverage of the frame, in degrees.
  Angle vertical{};
};

/// Everything the aiming transform knows about the physical rig
/// (`REQ-AIM-001`, `REQ-AIM-003`).
///
/// Defaults are all zero, which is "uncalibrated": with a zero field of view
/// every target resolves to the neutral angles. That is harmless because the
/// default envelope is empty and therefore permits nothing to be commanded at
/// all (`REQ-AIM-002`, `REQ-SAF-004`).
struct CameraCalibration {
  /// Angular coverage of the camera. Measured during bring-up.
  FieldOfView field_of_view{};

  /// Correction for imperfect camera/nozzle alignment, in the servo frame.
  /// Added to every computed angle (`REQ-AIM-001`).
  ServoAngles boresight_offset{};

  /// The angles at which the rig points straight down the camera's optical
  /// axis, in the servo frame. The origin the image offset is measured from.
  ServoAngles neutral{};
};

/// A closed interval of angles, **empty by default**.
///
/// Emptiness is structural, not a comment: the default member initialisers put
/// the minimum above the maximum, so a default-constructed range contains no
/// angle and there is no way to obtain a non-empty one without stating both
/// limits. An uncalibrated system is therefore inert rather than dangerous
/// (`REQ-AIM-002`, `REQ-SAF-004`).
class AngleRange {
 public:
  /// The empty range. Contains no angle; clamps nothing.
  AngleRange() = default;

  /// The two limits of a closed interval, grouped so that they cannot be
  /// transposed silently at a call site.
  struct ClosedInterval {
    Angle minimum{};
    Angle maximum{};
  };

  /// The closed interval [`limits.minimum`, `limits.maximum`].
  ///
  /// A minimum above the maximum yields the empty range rather than an error
  /// or an exception: the caller gets the safe outcome without having to
  /// handle one.
  [[nodiscard]] static AngleRange inclusive(ClosedInterval limits);

  /// Whether this range admits no angle at all.
  [[nodiscard]] bool is_empty() const noexcept;

  /// Whether `angle` lies within the range, limits included. Always false for
  /// an empty range.
  [[nodiscard]] bool contains(Angle angle) const noexcept;

  /// `angle` moved to the nearest point of the range, or `std::nullopt` when
  /// the range is empty.
  ///
  /// The optional is the whole point: an unconfigured axis has no nearest
  /// legal angle, and returning one anyway would invent an aim point that no
  /// one authorised (`REQ-AIM-002`).
  [[nodiscard]] std::optional<Angle> clamp(Angle angle) const noexcept;

 private:
  // Minimum above maximum: empty unless explicitly constructed otherwise.
  Angle minimum_{1.0};
  Angle maximum_{-1.0};
};

/// The configured travel of the two servo axes (`REQ-AIM-002`).
///
/// Both axes default to the empty range, so a default-constructed envelope
/// permits no aiming command and therefore no firing (`REQ-SAF-004`). The
/// deployment envelope is X ∈ [−90°, +90°], Y ∈ [0°, +45°].
struct MechanicalEnvelope {
  /// Permitted pan travel. Empty until configured.
  AngleRange x;
  /// Permitted tilt travel. Empty until configured; never extends below 0°,
  /// the horizon.
  AngleRange y;
};

/// Convert a detection centroid into servo angles (`REQ-AIM-001`, ADR-0004).
///
/// The boresighted transform, and the whole of it:
///
/// ```text
/// angle_x = neutral_x + boresight_x + ((cx - width/2)  / width)  * HFOV
/// angle_y = neutral_y + boresight_y - ((cy - height/2) / height) * VFOV
/// ```
///
/// The sign flip on Y is the image-to-servo handedness change: image +y points
/// down, servo +y points up.
///
/// There is deliberately **no range, depth or scale term**. The camera rides
/// the pan/tilt rig boresighted with the nozzle, so a target's angular offset
/// from the image centre *is* the correction the servos must apply, whatever
/// the distance to the bird.
///
/// Returns `std::nullopt` for a degenerate `image_size` (zero width or
/// height), because the transform divides by both. The result is **not**
/// clamped; pass it through `clamp_to_envelope` before it reaches any actuator.
[[nodiscard]] std::optional<ServoAngles> aim_at_centroid(
    PixelPoint centroid_px, ImageSize image_size, const CameraCalibration& calibration) noexcept;

/// Clamp computed angles to the mechanical envelope (`REQ-AIM-002`).
///
/// Each axis is clamped independently to its configured limits. Returns
/// `std::nullopt` if either axis range is empty — an unconfigured rig is
/// commanded nothing at all, which is what makes a default-constructed
/// configuration incapable of firing (`REQ-SAF-004`).
///
/// A target below the horizon in the image clamps to the envelope minimum,
/// which is 0° in the deployment envelope. It never goes below.
///
/// Clamping here is necessary but not sufficient: the Arduino enforces its own
/// limits on receipt, because a bound that only the commanding device enforces
/// fails exactly when the commanding device fails.
[[nodiscard]] std::optional<ServoAngles> clamp_to_envelope(
    ServoAngles angles, const MechanicalEnvelope& envelope) noexcept;

}  // namespace pigeon::core

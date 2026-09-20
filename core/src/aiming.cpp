#include "pigeon/core/aiming.hpp"

#include <optional>

#include "pigeon/core/geometry.hpp"

namespace pigeon::core {

AngleRange AngleRange::inclusive(ClosedInterval limits) {
  AngleRange range;
  // Transposed limits yield the empty range rather than an error: the caller
  // gets the safe outcome without having to handle one (`REQ-SAF-004`).
  if (limits.minimum <= limits.maximum) {
    range.minimum_ = limits.minimum;
    range.maximum_ = limits.maximum;
  }
  return range;
}

bool AngleRange::is_empty() const noexcept { return minimum_ > maximum_; }

bool AngleRange::contains(Angle angle) const noexcept {
  return !is_empty() && angle >= minimum_ && angle <= maximum_;
}

std::optional<Angle> AngleRange::clamp(Angle angle) const noexcept {
  // An unconfigured axis has no nearest legal angle. Inventing one would be an
  // aim point nobody authorised (`REQ-AIM-002`).
  if (is_empty()) {
    return std::nullopt;
  }
  if (angle < minimum_) {
    return minimum_;
  }
  if (angle > maximum_) {
    return maximum_;
  }
  return angle;
}

std::optional<ServoAngles> aim_at_centroid(PixelPoint centroid_px, ImageSize image_size,
                                           const CameraCalibration& calibration) noexcept {
  // The transform divides by both dimensions (`REQ-AIM-001`).
  if (image_size.width_px == 0 || image_size.height_px == 0) {
    return std::nullopt;
  }

  const auto width_px = static_cast<double>(image_size.width_px);
  const auto height_px = static_cast<double>(image_size.height_px);
  const double x_fraction = (centroid_px.x_px - (width_px / 2.0)) / width_px;
  const double y_fraction = (centroid_px.y_px - (height_px / 2.0)) / height_px;

  // The sign flip on Y is the image-to-servo handedness change: image +y points
  // down, servo +y points up (ADR-0004).
  const double x_degrees = calibration.neutral.x.degrees + calibration.boresight_offset.x.degrees +
                           (x_fraction * calibration.field_of_view.horizontal.degrees);
  const double y_degrees = calibration.neutral.y.degrees + calibration.boresight_offset.y.degrees -
                           (y_fraction * calibration.field_of_view.vertical.degrees);

  return ServoAngles{.x = Angle{x_degrees}, .y = Angle{y_degrees}};
}

std::optional<ServoAngles> clamp_to_envelope(ServoAngles angles,
                                             const MechanicalEnvelope& envelope) noexcept {
  const std::optional<Angle> x = envelope.x.clamp(angles.x);
  const std::optional<Angle> y = envelope.y.clamp(angles.y);
  // Either axis unconfigured means nothing at all is commanded (`REQ-SAF-004`).
  if (!x.has_value() || !y.has_value()) {
    return std::nullopt;
  }
  return ServoAngles{.x = *x, .y = *y};
}

}  // namespace pigeon::core

#pragma once

// Verifies: support code for REQ-AIM-002, REQ-AIM-003, REQ-SAF-003,
// REQ-SAF-005, REQ-SAF-006 — one calibrated configuration for the tests that
// need a rig that is actually allowed to do something.

#include <chrono>
#include <cstdint>

#include "pigeon/core/aiming.hpp"
#include "pigeon/core/configuration.hpp"
#include "pigeon/core/geometry.hpp"

namespace pigeon::test_support {

/// The frame size every aiming and scenario test uses.
inline constexpr pigeon::core::ImageSize test_image_size{.width_px = 640, .height_px = 480};

/// Field of view and association radius are *uncalibrated* in the
/// specification — they are properties of a physical rig recorded at bring-up
/// (`REQ-AIM-003`), not constants the specification fixes. These values are
/// therefore a **test fixture**, chosen to make the arithmetic checkable by
/// hand (a 60°/40° camera, a 30 px radius), and no test asserts that the
/// system ships with them.
inline constexpr double test_horizontal_fov_degrees = 60.0;
inline constexpr double test_vertical_fov_degrees = 40.0;
inline constexpr double test_association_radius_px = 30.0;

/// The deployment mechanical envelope, which the specification *does* fix:
/// X ∈ [−90°, +90°], Y ∈ [0°, +45°], Y = 0° being the horizon (`REQ-AIM-002`).
[[nodiscard]] inline pigeon::core::MechanicalEnvelope deployment_envelope() {
  pigeon::core::MechanicalEnvelope envelope;
  envelope.x = pigeon::core::AngleRange::inclusive(
      pigeon::core::AngleRange::ClosedInterval{.minimum = pigeon::core::Angle{-90.0},
                                               .maximum = pigeon::core::Angle{90.0}});
  envelope.y = pigeon::core::AngleRange::inclusive(
      pigeon::core::AngleRange::ClosedInterval{.minimum = pigeon::core::Angle{0.0},
                                               .maximum = pigeon::core::Angle{45.0}});
  return envelope;
}

/// A camera boresighted with the nozzle, neutral straight ahead at the
/// horizon, with no boresight correction (`REQ-AIM-001`, ADR-0004).
[[nodiscard]] inline pigeon::core::CameraCalibration test_camera() {
  return pigeon::core::CameraCalibration{
      .field_of_view =
          pigeon::core::FieldOfView{
              .horizontal = pigeon::core::Angle{test_horizontal_fov_degrees},
              .vertical = pigeon::core::Angle{test_vertical_fov_degrees},
          },
      .boresight_offset = pigeon::core::ServoAngles{},
      .neutral = pigeon::core::ServoAngles{},
  };
}

/// A fully calibrated installation with no exclusion zone — the reference
/// installation of `REQ-SAF-003` (Q9) — and the shipped safety defaults.
[[nodiscard]] inline pigeon::core::Configuration calibrated_configuration() {
  pigeon::core::Configuration configuration;
  configuration.camera = test_camera();
  configuration.envelope = deployment_envelope();
  configuration.association_radius = pigeon::core::PixelDistance{test_association_radius_px};
  return configuration;
}

/// An exclusion zone covering a rectangle of angle space, for the tests that
/// keep the mechanism verified even though the reference installation
/// configures none (`REQ-SAF-003`, `REQ-SAF-006`).
[[nodiscard]] inline pigeon::core::ExclusionZone exclusion_zone(double x_min_degrees,
                                                                double x_max_degrees,
                                                                double y_min_degrees,
                                                                double y_max_degrees) {
  pigeon::core::ExclusionZone zone;
  zone.x = pigeon::core::AngleRange::inclusive(
      pigeon::core::AngleRange::ClosedInterval{.minimum = pigeon::core::Angle{x_min_degrees},
                                               .maximum = pigeon::core::Angle{x_max_degrees}});
  zone.y = pigeon::core::AngleRange::inclusive(
      pigeon::core::AngleRange::ClosedInterval{.minimum = pigeon::core::Angle{y_min_degrees},
                                               .maximum = pigeon::core::Angle{y_max_degrees}});
  return zone;
}

}  // namespace pigeon::test_support

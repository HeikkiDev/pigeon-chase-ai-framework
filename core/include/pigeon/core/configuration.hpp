#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

#include "pigeon/core/aiming.hpp"
#include "pigeon/core/geometry.hpp"

namespace pigeon::core {

/// The region of servo-angle space in which firing is prohibited
/// (`REQ-SAF-003`, `REQ-SAF-006`).
///
/// An axis-aligned rectangle in angle space: a zone is the **conjunction** of
/// its two axis ranges, so firing is prohibited only when the aim lies inside
/// the X range *and* inside the Y range. Both ranges are empty by default, so
/// a default-constructed zone excludes nothing.
///
/// An installation configures **at most one** zone (`REQ-SAF-006`), which is
/// why `SafetyLimits` holds a `std::optional` rather than a container: "none"
/// and "one" are the only representable configurations, and a second zone
/// cannot be expressed at all.
///
/// The reference installation configures no zone (Q9), which is why the
/// mechanism has to be exercised by tests that configure one explicitly: an
/// untested safety mechanism is a safety mechanism that does not work.
struct ExclusionZone {
  /// Prohibited pan interval.
  AngleRange x;
  /// Prohibited tilt interval.
  AngleRange y;
};

/// The limits that make autonomous firing acceptable.
///
/// Defaults are the values the system ships with, per the "Configured
/// parameters" table in `docs/requirements/requirements.md`.
struct SafetyLimits {
  /// Longest single burst (`REQ-SAF-001`). The Arduino enforces its own bound
  /// independently, so this value is a request, not a guarantee: a value above
  /// the firmware's bound is truncated or rejected by the firmware.
  std::chrono::milliseconds max_fire_duration{500};

  /// Minimum interval between the start of one engagement and the next
  /// (`REQ-SAF-005`). Two seconds by default.
  std::chrono::milliseconds cool_down{2000};

  /// Ceiling on engagements within any one-minute window (`REQ-SAF-005`).
  /// Deterrence, not harassment.
  std::uint32_t max_engagements_per_minute{6};

  /// The region of angle space in which firing is prohibited (`REQ-SAF-003`,
  /// `REQ-SAF-006`). Absent means no zone is configured, which is both the
  /// default and the reference installation.
  std::optional<ExclusionZone> exclusion_zone;
};

/// Everything `core/` needs to know about the installation it is running on.
///
/// A plain value type. `core/` never reads a file, never parses anything and
/// never knows where these numbers came from: `raspberry/` parses the
/// version-controlled key/value calibration file and passes this struct in
/// (`REQ-AIM-003`, `REQ-DEV-001`).
///
/// **A default-constructed `Configuration` permits no firing**, and does so
/// structurally rather than by convention (`REQ-AIM-002`, `REQ-SAF-004`):
///
/// * `envelope` is empty on both axes, so `clamp_to_envelope` yields nothing
///   and no aiming or fire command can be authorised;
/// * `association_radius` is zero, so no detection ever joins an existing
///   track and nothing is ever confirmed;
/// * `camera` is uncalibrated, so every target resolves to the neutral angles.
///
/// A malformed or missing configuration file must therefore leave this struct
/// at its defaults rather than half-populated: the safe failure is the default
/// one.
struct Configuration {
  /// Optical and alignment calibration of the rig (`REQ-AIM-001`).
  CameraCalibration camera{};

  /// Permitted servo travel (`REQ-AIM-002`). Empty until configured.
  MechanicalEnvelope envelope{};

  /// Maximum centroid distance at which a detection joins an existing track
  /// (`REQ-TRK-007`). Zero until configured, which associates nothing.
  PixelDistance association_radius{};

  /// Firing limits (`REQ-SAF-001`, `REQ-SAF-003`, `REQ-SAF-005`,
  /// `REQ-SAF-006`).
  SafetyLimits safety{};
};

}  // namespace pigeon::core

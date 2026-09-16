// Verifies: REQ-AIM-002, REQ-AIM-003, REQ-SAF-001, REQ-SAF-003, REQ-SAF-004,
//           REQ-SAF-005, REQ-SAF-006
//
// The configuration is the system's safety datum: what it says when nobody has
// configured anything decides what an uncalibrated or mis-parsed installation
// does.

#include <chrono>
#include <optional>
#include <type_traits>

#include <gtest/gtest.h>

#include "pigeon/core/aiming.hpp"
#include "pigeon/core/configuration.hpp"
#include "pigeon/core/geometry.hpp"

namespace {

using pigeon::core::Configuration;
using pigeon::core::ExclusionZone;
using pigeon::core::SafetyLimits;

// Verifies: REQ-AIM-002, REQ-SAF-004 — "a default-constructed configuration
// SHALL have an empty envelope and SHALL therefore permit no firing until
// explicitly configured".
TEST(DefaultConfiguration, HasAnEmptyEnvelopeOnBothAxes) {
  const Configuration configuration;

  EXPECT_TRUE(configuration.envelope.x.is_empty());
  EXPECT_TRUE(configuration.envelope.y.is_empty());
}

// Verifies: REQ-SAF-004, REQ-TRK-007 — an uncalibrated association radius
// associates nothing, so an unconfigured system confirms nothing either.
TEST(DefaultConfiguration, HasAZeroAssociationRadius) {
  const Configuration configuration;

  EXPECT_DOUBLE_EQ(configuration.association_radius.pixels, 0.0);
}

// Verifies: REQ-SAF-004 — an uncalibrated camera has no field of view, so
// every target resolves to the neutral angles rather than to something
// invented.
TEST(DefaultConfiguration, HasAnUncalibratedCamera) {
  const Configuration configuration;

  EXPECT_DOUBLE_EQ(configuration.camera.field_of_view.horizontal.degrees, 0.0);
  EXPECT_DOUBLE_EQ(configuration.camera.field_of_view.vertical.degrees, 0.0);
  EXPECT_EQ(configuration.camera.boresight_offset, pigeon::core::ServoAngles{});
  EXPECT_EQ(configuration.camera.neutral, pigeon::core::ServoAngles{});
}

// Verifies: REQ-SAF-001, REQ-SAF-005 — the shipped defaults of the "Configured
// parameters" table: a 500 ms maximum burst, a 2 s cool-down and 6 engagements
// per minute.
TEST(DefaultConfiguration, ShipsTheSpecifiedSafetyDefaults) {
  const SafetyLimits limits;

  EXPECT_EQ(limits.max_fire_duration, std::chrono::milliseconds{500});
  EXPECT_EQ(limits.cool_down, std::chrono::milliseconds{2000});
  EXPECT_EQ(limits.max_engagements_per_minute, 6U);
}

// Verifies: REQ-SAF-003, REQ-SAF-006 — "the reference installation configures
// no exclusion zone", and that is also the default.
TEST(DefaultConfiguration, ConfiguresNoExclusionZone) {
  const Configuration configuration;

  EXPECT_FALSE(configuration.safety.exclusion_zone.has_value());
}

// Verifies: REQ-SAF-006 — "the configuration cannot represent more than one
// zone". Structural, because that is how the requirement is met: an optional
// has no room for a second zone, so no undocumented behaviour is needed for a
// case that cannot be expressed.
TEST(DefaultConfiguration, CannotRepresentMoreThanOneExclusionZone) {
  static_assert(std::is_same_v<decltype(SafetyLimits::exclusion_zone), std::optional<ExclusionZone>>,
                "an installation configures at most one exclusion zone (REQ-SAF-006)");
  SUCCEED();
}

// Verifies: REQ-SAF-006 — "a default-constructed zone excludes nothing":
// both axis intervals are empty, and a zone is the conjunction of the two.
TEST(DefaultExclusionZone, ExcludesNothing) {
  const ExclusionZone zone;

  EXPECT_TRUE(zone.x.is_empty());
  EXPECT_TRUE(zone.y.is_empty());
}

// Verifies: REQ-AIM-003 — "core/ contains no file access; the configuration
// arrives as a struct". Structural: `Configuration` is a plain value that can
// be built, copied and handed in, and `core/` offers no entry point that takes
// a path. The absence of file access in `core/` is enforced mechanically by
// `scripts/arch-check.sh`.
TEST(Configuration, IsAPlainValueTypeHandedInFromOutsideCore) {
  static_assert(std::is_default_constructible_v<Configuration>);
  static_assert(std::is_copy_constructible_v<Configuration>);
  static_assert(std::is_copy_assignable_v<Configuration>);
  static_assert(std::is_aggregate_v<Configuration>,
                "the configuration is data, not an object with behaviour (REQ-AIM-003)");
  SUCCEED();
}

}  // namespace

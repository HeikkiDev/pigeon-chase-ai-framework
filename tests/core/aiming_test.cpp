// Verifies: REQ-AIM-001, REQ-AIM-002, REQ-SAF-004
//
// The targeting maths, and the envelope that bounds it. Y = 0° is the horizon,
// so "never point at the ground" is a property of the datum and is asserted as
// one: no clamped angle is ever negative on Y.

#include <optional>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "pigeon/core/aiming.hpp"
#include "pigeon/core/geometry.hpp"
#include "support/test_configuration.hpp"

namespace {

using pigeon::core::aim_at_centroid;
using pigeon::core::Angle;
using pigeon::core::AngleRange;
using pigeon::core::CameraCalibration;
using pigeon::core::clamp_to_envelope;
using pigeon::core::FieldOfView;
using pigeon::core::ImageSize;
using pigeon::core::MechanicalEnvelope;
using pigeon::core::PixelPoint;
using pigeon::core::ServoAngles;
using pigeon::test_support::deployment_envelope;
using pigeon::test_support::test_camera;
using pigeon::test_support::test_horizontal_fov_degrees;
using pigeon::test_support::test_image_size;
using pigeon::test_support::test_vertical_fov_degrees;

// The tolerance every angle comparison in this file uses. The transform is a
// handful of double multiplications with no iteration, so anything beyond
// floating-point representation error is a defect, not noise
// (`REQ-AIM-001`: "within a documented tolerance").
constexpr double angle_tolerance_degrees = 1e-9;

// Verifies: REQ-AIM-001 — "a target at the image centre yields the configured
// neutral angles plus the boresight offset".
TEST(Aiming, ATargetAtTheImageCentreYieldsNeutralPlusBoresight) {
  CameraCalibration calibration = test_camera();
  calibration.neutral = ServoAngles{.x = Angle{12.0}, .y = Angle{30.0}};
  calibration.boresight_offset = ServoAngles{.x = Angle{-1.5}, .y = Angle{2.25}};
  const PixelPoint centre{static_cast<double>(test_image_size.width_px) / 2.0,
                          static_cast<double>(test_image_size.height_px) / 2.0};

  const std::optional<ServoAngles> angles = aim_at_centroid(centre, test_image_size, calibration);

  ASSERT_TRUE(angles.has_value());
  EXPECT_NEAR(angles->x.degrees, 12.0 - 1.5, angle_tolerance_degrees);
  EXPECT_NEAR(angles->y.degrees, 30.0 + 2.25, angle_tolerance_degrees);
}

// Verifies: REQ-AIM-001 — "a target at the horizontal image edge yields an
// offset of half the horizontal field of view", on both edges.
TEST(Aiming, ATargetAtAHorizontalEdgeYieldsHalfTheHorizontalFieldOfView) {
  const CameraCalibration calibration = test_camera();
  const double width = static_cast<double>(test_image_size.width_px);
  const double centre_y = static_cast<double>(test_image_size.height_px) / 2.0;

  const std::optional<ServoAngles> right =
      aim_at_centroid(PixelPoint{width, centre_y}, test_image_size, calibration);
  const std::optional<ServoAngles> left =
      aim_at_centroid(PixelPoint{0.0, centre_y}, test_image_size, calibration);

  ASSERT_TRUE(right.has_value());
  ASSERT_TRUE(left.has_value());
  EXPECT_NEAR(right->x.degrees, test_horizontal_fov_degrees / 2.0, angle_tolerance_degrees)
      << "positive X is to the right (ADR-0004)";
  EXPECT_NEAR(left->x.degrees, -test_horizontal_fov_degrees / 2.0, angle_tolerance_degrees);
}

// Verifies: REQ-AIM-001, REQ-AIM-002 — the image-to-servo handedness change:
// image +y points down, servo +y points up. Getting this backwards aims water
// at the ground, so it is asserted by sign as well as by magnitude.
TEST(Aiming, ATargetAboveTheImageCentreElevatesTheNozzle) {
  const CameraCalibration calibration = test_camera();
  const double centre_x = static_cast<double>(test_image_size.width_px) / 2.0;
  const double height = static_cast<double>(test_image_size.height_px);

  const std::optional<ServoAngles> top =
      aim_at_centroid(PixelPoint{centre_x, 0.0}, test_image_size, calibration);
  const std::optional<ServoAngles> bottom =
      aim_at_centroid(PixelPoint{centre_x, height}, test_image_size, calibration);

  ASSERT_TRUE(top.has_value());
  ASSERT_TRUE(bottom.has_value());
  EXPECT_NEAR(top->y.degrees, test_vertical_fov_degrees / 2.0, angle_tolerance_degrees)
      << "a bird at the top of the image is above the horizon";
  EXPECT_NEAR(bottom->y.degrees, -test_vertical_fov_degrees / 2.0, angle_tolerance_degrees)
      << "a bird at the bottom of the image is below the horizon, before clamping";
}

// Verifies: REQ-AIM-001 — "known fixture positions yield the documented
// expected angles within a documented tolerance". The expectations are the
// transform of ADR-0004 evaluated by hand.
TEST(Aiming, KnownFixturePositionsYieldTheDocumentedAngles) {
  const CameraCalibration calibration = test_camera();
  const double width = static_cast<double>(test_image_size.width_px);
  const double height = static_cast<double>(test_image_size.height_px);

  struct Case {
    PixelPoint centroid;
    double expected_x_degrees;
    double expected_y_degrees;
  };

  const std::vector<Case> cases{
      {PixelPoint{width * 0.75, height * 0.5}, test_horizontal_fov_degrees * 0.25, 0.0},
      {PixelPoint{width * 0.25, height * 0.5}, -test_horizontal_fov_degrees * 0.25, 0.0},
      {PixelPoint{width * 0.5, height * 0.25}, 0.0, test_vertical_fov_degrees * 0.25},
      {PixelPoint{width * 0.5, height * 0.75}, 0.0, -test_vertical_fov_degrees * 0.25},
      {PixelPoint{width * 0.75, height * 0.25}, test_horizontal_fov_degrees * 0.25,
       test_vertical_fov_degrees * 0.25},
  };

  for (const Case& test_case : cases) {
    const std::optional<ServoAngles> angles =
        aim_at_centroid(test_case.centroid, test_image_size, calibration);
    ASSERT_TRUE(angles.has_value());
    EXPECT_NEAR(angles->x.degrees, test_case.expected_x_degrees, angle_tolerance_degrees)
        << "centroid (" << test_case.centroid.x_px << ", " << test_case.centroid.y_px << ")";
    EXPECT_NEAR(angles->y.degrees, test_case.expected_y_degrees, angle_tolerance_degrees)
        << "centroid (" << test_case.centroid.x_px << ", " << test_case.centroid.y_px << ")";
  }
}

// Verifies: REQ-AIM-001 — "the computation uses no range, depth or scale
// estimate". Structural: the transform's entire input is a centroid, a frame
// size and the rig's calibration, so there is nothing for a range estimate to
// arrive through (ADR-0004).
TEST(Aiming, TakesNoRangeDepthOrScaleTerm) {
  static_assert(
      std::is_same_v<decltype(&aim_at_centroid),
                     std::optional<ServoAngles> (*)(PixelPoint, ImageSize,
                                                    const CameraCalibration&) noexcept>,
      "aiming must depend only on the centroid, the frame size and the calibration (REQ-AIM-001)");
  SUCCEED();
}

// Verifies: REQ-AIM-001 — "returns nullopt for a degenerate image_size",
// rather than dividing by zero and aiming somewhere arbitrary.
TEST(Aiming, YieldsNothingForADegenerateImageSize) {
  const CameraCalibration calibration = test_camera();

  EXPECT_FALSE(
      aim_at_centroid(PixelPoint{1.0, 1.0}, ImageSize{.width_px = 0, .height_px = 480}, calibration)
          .has_value());
  EXPECT_FALSE(
      aim_at_centroid(PixelPoint{1.0, 1.0}, ImageSize{.width_px = 640, .height_px = 0}, calibration)
          .has_value());
  EXPECT_FALSE(aim_at_centroid(PixelPoint{1.0, 1.0}, ImageSize{}, calibration).has_value());
}

// ---------------------------------------------------------------------------
// REQ-AIM-002 — the mechanical envelope.
// ---------------------------------------------------------------------------

// Verifies: REQ-AIM-002, REQ-SAF-004 — "a default-constructed configuration
// SHALL have an empty envelope and SHALL therefore permit no firing".
TEST(AngleRangeContract, IsEmptyByDefaultAndAdmitsNothing) {
  const AngleRange range;

  EXPECT_TRUE(range.is_empty());
  for (const double degrees : {-180.0, -1.0, 0.0, 1.0, 180.0}) {
    EXPECT_FALSE(range.contains(Angle{degrees})) << degrees << "° in an empty range";
    EXPECT_FALSE(range.clamp(Angle{degrees}).has_value())
        << "an unconfigured axis has no nearest legal angle; inventing one is the defect";
  }
}

// Verifies: REQ-AIM-002 — a configured range is closed: both limits are inside
// it.
TEST(AngleRangeContract, ContainsBothOfItsLimits) {
  const AngleRange range = AngleRange::inclusive(
      AngleRange::ClosedInterval{.minimum = Angle{0.0}, .maximum = Angle{45.0}});

  EXPECT_FALSE(range.is_empty());
  EXPECT_TRUE(range.contains(Angle{0.0}));
  EXPECT_TRUE(range.contains(Angle{45.0}));
  EXPECT_TRUE(range.contains(Angle{22.5}));
  EXPECT_FALSE(range.contains(Angle{-0.000001}));
  EXPECT_FALSE(range.contains(Angle{45.000001}));
}

// Verifies: REQ-AIM-002, REQ-SAF-004 — "a minimum above the maximum yields the
// empty range rather than an error": the caller gets the safe outcome without
// having to handle one.
TEST(AngleRangeContract, TransposedLimitsYieldTheEmptyRange) {
  const AngleRange range = AngleRange::inclusive(
      AngleRange::ClosedInterval{.minimum = Angle{45.0}, .maximum = Angle{0.0}});

  EXPECT_TRUE(range.is_empty());
  EXPECT_FALSE(range.contains(Angle{22.5}));
  EXPECT_FALSE(range.clamp(Angle{22.5}).has_value());
}

// Verifies: REQ-AIM-002 — "an out-of-range computed angle is emitted as the
// corresponding limit", at and beyond both ends.
TEST(AngleRangeContract, ClampsToTheNearestLimit) {
  const AngleRange range = AngleRange::inclusive(
      AngleRange::ClosedInterval{.minimum = Angle{-90.0}, .maximum = Angle{90.0}});

  EXPECT_EQ(range.clamp(Angle{-1000.0}), Angle{-90.0});
  EXPECT_EQ(range.clamp(Angle{-90.0}), Angle{-90.0});
  EXPECT_EQ(range.clamp(Angle{-89.999}), Angle{-89.999});
  EXPECT_EQ(range.clamp(Angle{0.0}), Angle{0.0});
  EXPECT_EQ(range.clamp(Angle{89.999}), Angle{89.999});
  EXPECT_EQ(range.clamp(Angle{90.0}), Angle{90.0});
  EXPECT_EQ(range.clamp(Angle{1000.0}), Angle{90.0});
}

// Verifies: REQ-AIM-002, REQ-SAF-004 — "a default-constructed configuration
// emits no fire command for any input": with either axis unconfigured, nothing
// at all is commanded.
TEST(EnvelopeClamp, YieldsNothingWhenEitherAxisIsUnconfigured) {
  const MechanicalEnvelope unconfigured;
  MechanicalEnvelope half_configured;
  half_configured.x = AngleRange::inclusive(
      AngleRange::ClosedInterval{.minimum = Angle{-90.0}, .maximum = Angle{90.0}});

  EXPECT_FALSE(clamp_to_envelope(ServoAngles{}, unconfigured).has_value());
  EXPECT_FALSE(clamp_to_envelope(ServoAngles{.x = Angle{10.0}, .y = Angle{10.0}}, half_configured)
                   .has_value())
      << "an envelope with one unconfigured axis must command nothing at all";
}

// Verifies: REQ-AIM-002 — angles already inside the envelope pass through
// untouched, including exactly at the limits.
TEST(EnvelopeClamp, LeavesAnglesInsideTheEnvelopeUnchanged) {
  const MechanicalEnvelope envelope = deployment_envelope();

  const std::vector<ServoAngles> inside{
      ServoAngles{.x = Angle{0.0}, .y = Angle{0.0}},
      ServoAngles{.x = Angle{-90.0}, .y = Angle{0.0}},
      ServoAngles{.x = Angle{90.0}, .y = Angle{45.0}},
      ServoAngles{.x = Angle{-33.5}, .y = Angle{12.25}},
  };

  for (const ServoAngles& angles : inside) {
    const std::optional<ServoAngles> clamped = clamp_to_envelope(angles, envelope);
    ASSERT_TRUE(clamped.has_value());
    EXPECT_EQ(*clamped, angles) << "angle (" << angles.x.degrees << ", " << angles.y.degrees
                                << ") was inside the envelope and should not have moved";
  }
}

// Verifies: REQ-AIM-002 — "an out-of-range computed angle is emitted as the
// corresponding limit", per axis and independently.
TEST(EnvelopeClamp, ClampsEachAxisToItsOwnLimits) {
  const MechanicalEnvelope envelope = deployment_envelope();

  const std::optional<ServoAngles> beyond_right =
      clamp_to_envelope(ServoAngles{.x = Angle{140.0}, .y = Angle{60.0}}, envelope);
  const std::optional<ServoAngles> beyond_left =
      clamp_to_envelope(ServoAngles{.x = Angle{-140.0}, .y = Angle{-60.0}}, envelope);

  ASSERT_TRUE(beyond_right.has_value());
  EXPECT_EQ(beyond_right->x, Angle{90.0});
  EXPECT_EQ(beyond_right->y, Angle{45.0});
  ASSERT_TRUE(beyond_left.has_value());
  EXPECT_EQ(beyond_left->x, Angle{-90.0});
  EXPECT_EQ(beyond_left->y, Angle{0.0});
}

// Verifies: REQ-AIM-002 — "a target below the horizon in the image clamps to
// Y = 0°, never below", and "the nozzle is never commanded below the
// horizontal". This is the safety-critical direction of the clamp.
TEST(EnvelopeClamp, NeverCommandsAnElevationBelowTheHorizon) {
  const MechanicalEnvelope envelope = deployment_envelope();
  const CameraCalibration calibration = test_camera();
  const double width = static_cast<double>(test_image_size.width_px);
  const double height = static_cast<double>(test_image_size.height_px);

  // A sweep over the whole image plane and beyond it, in deterministic steps.
  for (int x_step = -2; x_step <= 12; ++x_step) {
    for (int y_step = -2; y_step <= 12; ++y_step) {
      const PixelPoint centroid{width * static_cast<double>(x_step) / 10.0,
                                height * static_cast<double>(y_step) / 10.0};
      const std::optional<ServoAngles> computed =
          aim_at_centroid(centroid, test_image_size, calibration);
      ASSERT_TRUE(computed.has_value());

      const std::optional<ServoAngles> clamped = clamp_to_envelope(*computed, envelope);
      ASSERT_TRUE(clamped.has_value());
      EXPECT_GE(clamped->y.degrees, 0.0) << "centroid (" << centroid.x_px << ", " << centroid.y_px
                                         << ") produced an elevation below the horizon";
      EXPECT_LE(clamped->y.degrees, 45.0);
      EXPECT_GE(clamped->x.degrees, -90.0);
      EXPECT_LE(clamped->x.degrees, 90.0);
    }
  }
}

// Verifies: REQ-AIM-002 — "no angle outside the configured envelope is ever
// emitted", for an envelope that is not the deployment one either.
TEST(EnvelopeClamp, NeverEmitsAnAngleOutsideAnArbitraryConfiguredEnvelope) {
  MechanicalEnvelope envelope;
  envelope.x = AngleRange::inclusive(
      AngleRange::ClosedInterval{.minimum = Angle{-20.0}, .maximum = Angle{35.0}});
  envelope.y = AngleRange::inclusive(
      AngleRange::ClosedInterval{.minimum = Angle{5.0}, .maximum = Angle{10.0}});

  for (int x_degrees = -200; x_degrees <= 200; x_degrees += 7) {
    for (int y_degrees = -200; y_degrees <= 200; y_degrees += 11) {
      const ServoAngles requested{.x = Angle{static_cast<double>(x_degrees)},
                                  .y = Angle{static_cast<double>(y_degrees)}};
      const std::optional<ServoAngles> clamped = clamp_to_envelope(requested, envelope);

      ASSERT_TRUE(clamped.has_value());
      EXPECT_TRUE(envelope.x.contains(clamped->x))
          << "emitted X " << clamped->x.degrees << "° for requested " << x_degrees << "°";
      EXPECT_TRUE(envelope.y.contains(clamped->y))
          << "emitted Y " << clamped->y.degrees << "° for requested " << y_degrees << "°";
    }
  }
}

}  // namespace

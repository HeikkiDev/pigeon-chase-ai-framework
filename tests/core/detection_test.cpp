// Verifies: REQ-DET-001, REQ-DET-002, REQ-DET-004, REQ-DEV-002
//
// The detection contract: a closed two-value classification, an interface that
// knows nothing about cameras, and one declared pixel format.

#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "fixtures/rgb888_fixtures.hpp"
#include "pigeon/core/detection.hpp"
#include "pigeon/core/detector.hpp"
#include "pigeon/core/frame.hpp"
#include "pigeon/core/geometry.hpp"
#include "support/detection_builders.hpp"
#include "support/simulated_detectors.hpp"

namespace {

using pigeon::core::Detection;
using pigeon::core::DetectionOutcome;
using pigeon::core::DetectionResult;
using pigeon::core::FrameView;
using pigeon::core::ImageSize;
using pigeon::test_fixtures::FixtureImage;
using pigeon::test_support::BrightBlobDetector;
using pigeon::test_support::detection_at;

// ---------------------------------------------------------------------------
// REQ-DET-001 — every frame is classified as exactly one of NONE or FOUND.
// ---------------------------------------------------------------------------

// Verifies: REQ-DET-001 — "the detector never returns any other value".
// A default-constructed outcome is the empty one, so nothing can be triggered
// by a value nobody filled in.
TEST(DetectionOutcome, DefaultsToNoneWithNoDetections) {
  const DetectionOutcome outcome;

  EXPECT_EQ(outcome.result(), DetectionResult::NONE);
  EXPECT_TRUE(outcome.detections().empty());
}

// Verifies: REQ-DET-001 — "given a fixture frame containing no pigeon, the
// detector returns NONE".
TEST(DetectionOutcome, ExplicitNoneCarriesNoDetections) {
  const DetectionOutcome outcome = DetectionOutcome::none();

  EXPECT_EQ(outcome.result(), DetectionResult::NONE);
  EXPECT_TRUE(outcome.detections().empty());
}

// Verifies: REQ-DET-001 — "given a fixture frame containing a pigeon, the
// detector returns FOUND", and the detections that justify the verdict travel
// with it.
TEST(DetectionOutcome, FoundCarriesEveryDetectionInTheFrame) {
  const DetectionOutcome outcome =
      DetectionOutcome::found({detection_at(10.0, 20.0), detection_at(100.0, 200.0)});

  EXPECT_EQ(outcome.result(), DetectionResult::FOUND);
  ASSERT_EQ(outcome.detections().size(), 2U);
  EXPECT_EQ(outcome.detections()[0], detection_at(10.0, 20.0));
  EXPECT_EQ(outcome.detections()[1], detection_at(100.0, 200.0));
}

// Verifies: REQ-DET-001 — the invariant "FOUND if and only if at least one
// detection is present". A caller claiming FOUND with nothing to aim at is a
// state the state machine has no safe answer to, so the classification follows
// the evidence.
TEST(DetectionOutcome, FoundWithNoDetectionsIsNone) {
  const DetectionOutcome outcome = DetectionOutcome::found({});

  EXPECT_EQ(outcome.result(), DetectionResult::NONE);
  EXPECT_TRUE(outcome.detections().empty());
}

// Verifies: REQ-DET-001 — the result set is closed: whatever the frame, the
// verdict is one of exactly two values and reading it never throws.
TEST(SimulatedDetector, ClassifiesEveryFrameAsExactlyOneOfTwoValues) {
  const BrightBlobDetector detector;
  const FixtureImage with_pigeon = pigeon::test_fixtures::pigeon_frame();
  const FixtureImage without_pigeon = pigeon::test_fixtures::empty_frame();
  const FixtureImage two_pigeons = pigeon::test_fixtures::two_pigeon_frame();
  const FrameView empty_view;

  for (const FrameView& frame :
       {with_pigeon.view(), without_pigeon.view(), two_pigeons.view(), empty_view}) {
    DetectionResult result{};
    ASSERT_NO_THROW(result = detector.detect(frame).result());
    EXPECT_TRUE(result == DetectionResult::NONE || result == DetectionResult::FOUND)
        << "a frame was classified as something other than NONE or FOUND";
  }
}

// Verifies: REQ-DET-001, REQ-DET-002 — a fixture frame containing a pigeon is
// classified FOUND, through the `Detector` interface and with no camera
// anywhere in the path.
TEST(SimulatedDetector, ReportsFoundForAFrameContainingAPigeon) {
  const BrightBlobDetector detector;
  const FixtureImage image = pigeon::test_fixtures::pigeon_frame();

  const DetectionOutcome outcome = detector.detect(image.view());

  EXPECT_EQ(outcome.result(), DetectionResult::FOUND);
  ASSERT_EQ(outcome.detections().size(), 1U);
  const Detection& detection = outcome.detections()[0];
  EXPECT_DOUBLE_EQ(detection.bounding_box_px.top_left_px.x_px,
                   static_cast<double>(pigeon::test_fixtures::pigeon_blob.x0_px));
  EXPECT_DOUBLE_EQ(detection.bounding_box_px.top_left_px.y_px,
                   static_cast<double>(pigeon::test_fixtures::pigeon_blob.y0_px));
  EXPECT_DOUBLE_EQ(detection.bounding_box_px.width_px,
                   static_cast<double>(pigeon::test_fixtures::pigeon_blob.width_px));
  EXPECT_DOUBLE_EQ(detection.bounding_box_px.height_px,
                   static_cast<double>(pigeon::test_fixtures::pigeon_blob.height_px));
  EXPECT_DOUBLE_EQ(detection.centroid_px.x_px, 6.0);
  EXPECT_DOUBLE_EQ(detection.centroid_px.y_px, 5.0);
}

// Verifies: REQ-DET-001 — a fixture frame with no pigeon is classified NONE.
TEST(SimulatedDetector, ReportsNoneForAFrameContainingNoPigeon) {
  const BrightBlobDetector detector;
  const FixtureImage image = pigeon::test_fixtures::empty_frame();

  EXPECT_EQ(detector.detect(image.view()).result(), DetectionResult::NONE);
}

// Verifies: REQ-DET-001 — "never throws for a well-formed frame", and an
// empty or malformed frame is NONE rather than an error, because inaction is
// the safe response (detector.hpp).
TEST(SimulatedDetector, TreatsAMalformedFrameAsNoneRatherThanAnError) {
  const BrightBlobDetector detector;
  const FixtureImage image = pigeon::test_fixtures::pigeon_frame();

  const FrameView no_pixels{
      .size = image.size, .stride_bytes = image.stride_bytes, .pixels = {}, .sequence_number = 1};
  const FrameView zero_size{.size = ImageSize{},
                            .stride_bytes = 0,
                            .pixels = std::span<const std::byte>{image.bytes},
                            .sequence_number = 2};
  const FrameView stride_too_small{.size = image.size,
                                   .stride_bytes = 1,
                                   .pixels = std::span<const std::byte>{image.bytes},
                                   .sequence_number = 3};

  for (const FrameView& frame : {no_pixels, zero_size, stride_too_small}) {
    DetectionResult result{};
    ASSERT_NO_THROW(result = detector.detect(frame).result());
    EXPECT_EQ(result, DetectionResult::NONE);
  }
}

// Verifies: REQ-DEV-002 — "the same frame yields the same outcome"
// (detector.hpp), which is what makes a recorded fixture a reproducible run.
TEST(SimulatedDetector, IsDeterministicForTheSameFrame) {
  const BrightBlobDetector detector;
  const FixtureImage image = pigeon::test_fixtures::two_pigeon_frame();

  const DetectionOutcome first = detector.detect(image.view());
  const DetectionOutcome second = detector.detect(image.view());

  ASSERT_EQ(first.result(), second.result());
  ASSERT_EQ(first.detections().size(), second.detections().size());
  for (std::size_t index = 0; index < first.detections().size(); ++index) {
    EXPECT_EQ(first.detections()[index], second.detections()[index])
        << "detection " << index << " differed between two runs over the same frame";
  }
}

// ---------------------------------------------------------------------------
// REQ-DET-004 — packed RGB888, and the addressing that follows from it.
// ---------------------------------------------------------------------------

// Verifies: REQ-DET-004 — three 8-bit channels per pixel, declared once and
// not configurable.
TEST(FrameFormat, IsThreeBytesPerPixel) {
  EXPECT_EQ(pigeon::core::bytes_per_pixel, 3U);
}

// Verifies: REQ-DET-004 — "the frame type carries width, height and stride,
// and the buffer it views is at least stride × height bytes".
TEST(FrameFormat, CarriesSizeAndStrideAndAtLeastStrideTimesHeightBytes) {
  const FixtureImage image = pigeon::test_fixtures::padded_pigeon_frame();
  const FrameView frame = image.view();

  EXPECT_EQ(frame.size.width_px, pigeon::test_fixtures::fixture_size.width_px);
  EXPECT_EQ(frame.size.height_px, pigeon::test_fixtures::fixture_size.height_px);
  EXPECT_GE(frame.stride_bytes,
            static_cast<std::size_t>(frame.size.width_px) * pigeon::core::bytes_per_pixel);
  EXPECT_GE(frame.pixels.size(),
            frame.stride_bytes * static_cast<std::size_t>(frame.size.height_px));
}

// Verifies: REQ-DET-004 — "a pixel at (x, y) begins at byte
// y × stride + x × 3", with the channels in red, green, blue order.
TEST(FrameFormat, PixelAtXYBeginsAtYTimesStridePlusXTimesThree) {
  const FixtureImage image = pigeon::test_fixtures::padded_pigeon_frame();
  const FrameView frame = image.view();

  for (std::uint32_t y_px = 0; y_px < frame.size.height_px; ++y_px) {
    for (std::uint32_t x_px = 0; x_px < frame.size.width_px; ++x_px) {
      const std::size_t offset =
          (static_cast<std::size_t>(y_px) * frame.stride_bytes) +
          (static_cast<std::size_t>(x_px) * pigeon::core::bytes_per_pixel);
      const bool inside_blob =
          x_px >= pigeon::test_fixtures::pigeon_blob.x0_px &&
          x_px < pigeon::test_fixtures::pigeon_blob.x0_px +
                     pigeon::test_fixtures::pigeon_blob.width_px &&
          y_px >= pigeon::test_fixtures::pigeon_blob.y0_px &&
          y_px < pigeon::test_fixtures::pigeon_blob.y0_px +
                     pigeon::test_fixtures::pigeon_blob.height_px;
      const std::uint8_t expected =
          inside_blob ? pigeon::test_fixtures::pigeon_level : pigeon::test_fixtures::background_level;

      for (std::size_t channel = 0; channel < pigeon::core::bytes_per_pixel; ++channel) {
        ASSERT_EQ(std::to_integer<std::uint8_t>(frame.pixels[offset + channel]), expected)
            << "pixel (" << x_px << "," << y_px << ") channel " << channel
            << " was not at byte y*stride + x*3";
      }
    }
  }
}

// Verifies: REQ-DET-004 — a stride larger than width × 3 is honoured: the row
// padding is not pixel data, and a reader that ignores the stride reads it as
// if it were.
TEST(FrameFormat, RowPaddingIsNotPixelData) {
  const FixtureImage image = pigeon::test_fixtures::padded_pigeon_frame();
  const std::size_t row_pixel_bytes =
      static_cast<std::size_t>(image.size.width_px) * pigeon::core::bytes_per_pixel;
  ASSERT_GT(image.stride_bytes, row_pixel_bytes) << "this fixture is supposed to be padded";

  for (std::uint32_t y_px = 0; y_px < image.size.height_px; ++y_px) {
    for (std::size_t byte = row_pixel_bytes; byte < image.stride_bytes; ++byte) {
      ASSERT_EQ(std::to_integer<std::uint8_t>(
                    image.bytes[(static_cast<std::size_t>(y_px) * image.stride_bytes) + byte]),
                pigeon::test_fixtures::padding_level);
    }
  }
}

// Verifies: REQ-DET-004, REQ-DEV-002 — "fixtures are stored as RGB888 and are
// byte-identical between runs".
TEST(FrameFixtures, AreByteIdenticalBetweenRuns) {
  const FixtureImage first = pigeon::test_fixtures::two_pigeon_frame();
  const FixtureImage second = pigeon::test_fixtures::two_pigeon_frame();

  EXPECT_EQ(first.size, second.size);
  EXPECT_EQ(first.stride_bytes, second.stride_bytes);
  EXPECT_EQ(first.bytes, second.bytes);
}

// Verifies: REQ-DET-004, REQ-DET-002 — the same pigeon in a padded frame and
// in an unpadded frame is the same detection: the detector addresses pixels by
// stride, and no colour conversion happens anywhere in the path.
TEST(SimulatedDetector, FindsTheSamePigeonInAPaddedFrame) {
  const BrightBlobDetector detector;
  const FixtureImage unpadded = pigeon::test_fixtures::pigeon_frame();
  const FixtureImage padded = pigeon::test_fixtures::padded_pigeon_frame();

  const DetectionOutcome from_unpadded = detector.detect(unpadded.view());
  const DetectionOutcome from_padded = detector.detect(padded.view());

  ASSERT_EQ(from_unpadded.detections().size(), 1U);
  ASSERT_EQ(from_padded.detections().size(), 1U);
  EXPECT_EQ(from_unpadded.detections()[0], from_padded.detections()[0]);
}

// Verifies: REQ-DET-002 — "the full detection path can be exercised" without a
// camera: the detector is reached through the interface, and the frame is a
// borrowed view of a buffer a non-camera producer owns.
TEST(SimulatedDetector, IsReachedThroughTheInterfaceWithNoCameraInvolved) {
  const BrightBlobDetector concrete;
  const pigeon::core::Detector& detector = concrete;
  const FixtureImage image = pigeon::test_fixtures::two_pigeon_frame();
  const FrameView frame = image.view();

  const DetectionOutcome outcome = detector.detect(frame);

  EXPECT_EQ(outcome.result(), DetectionResult::FOUND);
  EXPECT_EQ(outcome.detections().size(), 2U);
  // The view borrows; nothing was copied across the boundary.
  EXPECT_EQ(frame.pixels.data(), image.bytes.data());
}

}  // namespace

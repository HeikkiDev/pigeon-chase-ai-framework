// Verifies: REQ-DET-001, REQ-DET-002, REQ-DET-004, REQ-DEV-001, REQ-DEV-002
//
// The `core::Detector` contract, applied to the real model (ADR-0019 §9): two
// values and only two, total over malformed input, deterministic, stateless
// across frames, and `bird`-only at the seam.
//
// No assertion here names a box coordinate. ADR-0019 records that determinism
// is per-binary: the thread count and bf16 storage change numerics, so a test
// that pinned a coordinate would be a test of this Mac rather than of the
// contract.

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "pigeon/core/detection.hpp"
#include "pigeon/core/frame.hpp"
#include "pigeon/core/geometry.hpp"
#include "pigeon/raspberry/ncnn_detector.hpp"
#include "support/model_fixture.hpp"
#include "support/ppm_fixture.hpp"

namespace {

using pigeon::core::Detection;
using pigeon::core::DetectionOutcome;
using pigeon::core::DetectionResult;
using pigeon::core::FrameView;
using pigeon::core::ImageSize;
using pigeon::raspberry::ModelLoadStatus;
using pigeon::raspberry::NcnnDetector;
using pigeon::raspberry::NcnnDetectorSettings;
using pigeon::test_support::calibrated_settings;
using pigeon::test_support::fixture_path;
using pigeon::test_support::load_ppm;
using pigeon::test_support::PpmImage;

/// One loaded detector per test, built from the version-controlled bundle.
/// Nothing is shared between tests, so no test can depend on another having
/// run first (`REQ-DEV-002`).
class RealDetectorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    result_ = NcnnDetector::load(calibrated_settings());
    ASSERT_EQ(result_.status, ModelLoadStatus::OK)
        << "The vendored model bundle must load before behaviour can be verified.";
    ASSERT_NE(result_.detector, nullptr);
  }

  [[nodiscard]] const NcnnDetector& detector() const { return *result_.detector; }

  [[nodiscard]] static PpmImage fixture(const std::string& name,
                                        std::size_t row_padding_bytes = 0) {
    PpmImage image = load_ppm(fixture_path(name), row_padding_bytes);
    EXPECT_TRUE(image.loaded()) << "fixture missing: " << name;
    return image;
  }

 private:
  NcnnDetector::LoadResult result_;
};

// ---------------------------------------------------------------------------
// REQ-DET-001 — a pigeon is FOUND, everything else is NONE.
// ---------------------------------------------------------------------------

// Encodes REQ-DET-001: "given a fixture frame containing a pigeon, the
// detector returns FOUND", on a real photograph rather than a synthetic blob.
TEST_F(RealDetectorTest, RealPigeonPhotographIsFound) {
  const PpmImage bird = fixture("pigeon_640x480.ppm");
  ASSERT_TRUE(bird.loaded());

  const DetectionOutcome outcome = detector().detect(bird.view());

  EXPECT_EQ(outcome.result(), DetectionResult::FOUND)
      << "A rock dove filling most of a 640x480 frame must be detected.";
  EXPECT_FALSE(outcome.detections().empty()) << "FOUND must carry the detections that justify it.";
}

// Encodes REQ-DET-001: the centroid is what aiming consumes, so a FOUND
// outcome whose centroid is not on the bird is a FOUND outcome that aims water
// at the wrong place. The window is the bird's hand-measured extent in this
// fixture, not the model's reported box, so the assertion is about where the
// bird is and not about what this binary happened to output.
TEST_F(RealDetectorTest, PigeonCentroidLiesOnTheBird) {
  const PpmImage bird = fixture("pigeon_640x480.ppm");
  ASSERT_TRUE(bird.loaded());

  const DetectionOutcome outcome = detector().detect(bird.view());
  ASSERT_EQ(outcome.result(), DetectionResult::FOUND);
  ASSERT_FALSE(outcome.detections().empty());

  const Detection& detection = outcome.detections().front();

  EXPECT_GT(detection.centroid_px.x_px, 192.0) << "centroid is left of the bird";
  EXPECT_LT(detection.centroid_px.x_px, 576.0) << "centroid is right of the bird";
  EXPECT_GT(detection.centroid_px.y_px, 96.0) << "centroid is above the bird";
  EXPECT_LT(detection.centroid_px.y_px, 432.0) << "centroid is below the bird";
}

// Encodes REQ-DET-001: "given a fixture frame containing no pigeon, the
// detector returns NONE" — the scene a balcony camera sees most of the time.
TEST_F(RealDetectorTest, SceneWithNoBirdIsNone) {
  const PpmImage wall = fixture("brick_wall_320x240.ppm");
  ASSERT_TRUE(wall.loaded());

  const DetectionOutcome outcome = detector().detect(wall.view());

  EXPECT_EQ(outcome.result(), DetectionResult::NONE);
  EXPECT_TRUE(outcome.detections().empty());
}

// Encodes ADR-0019 §4: `bird` is the proxy class and every other COCO class is
// discarded. The model is confidently right about this frame — it contains a
// car — and the detector must still answer NONE. Without this case, "FOUND
// means a bird" is indistinguishable from "FOUND means the model saw
// something". Verifies: REQ-DET-001.
TEST_F(RealDetectorTest, ConfidentNonBirdClassIsNone) {
  const PpmImage car = fixture("car_320x240.ppm");
  ASSERT_TRUE(car.loaded());

  const DetectionOutcome outcome = detector().detect(car.view());

  EXPECT_EQ(outcome.result(), DetectionResult::NONE)
      << "A car detected at high confidence is not a bird and must not be reported.";
  EXPECT_TRUE(outcome.detections().empty());
}

// Encodes REQ-DET-001: "the detector never returns any other value and never
// throws for a well-formed frame", over every real image in the suite.
TEST_F(RealDetectorTest, EveryFixtureIsClassifiedAsExactlyOneOfTwoValues) {
  for (const std::string& name :
       {"pigeon_640x480.ppm", "brick_wall_320x240.ppm", "car_320x240.ppm"}) {
    const PpmImage image = fixture(name);
    ASSERT_TRUE(image.loaded());

    DetectionOutcome outcome;
    ASSERT_NO_THROW({ outcome = detector().detect(image.view()); }) << name;

    const bool is_one_of_two =
        outcome.result() == DetectionResult::NONE || outcome.result() == DetectionResult::FOUND;
    EXPECT_TRUE(is_one_of_two) << name;
    EXPECT_EQ(outcome.result() == DetectionResult::FOUND, !outcome.detections().empty())
        << "FOUND if and only if at least one detection is present, for " << name;
  }
}

// ---------------------------------------------------------------------------
// REQ-DET-004 / REQ-DET-001 — malformed frames are NONE, never a crash.
// ---------------------------------------------------------------------------

// Encodes the `core::Detector` contract: "treat an empty or malformed frame as
// NONE rather than as an error, because inaction is the safe response".
// Verifies: REQ-DET-001, REQ-DET-004.
TEST_F(RealDetectorTest, EmptyFrameIsNone) {
  const FrameView empty{};

  DetectionOutcome outcome;
  ASSERT_NO_THROW({ outcome = detector().detect(empty); });
  EXPECT_EQ(outcome.result(), DetectionResult::NONE);
}

// Encodes REQ-DET-001 and REQ-DET-004: dimensions that describe no image.
TEST_F(RealDetectorTest, ZeroSizedFrameWithABufferIsNone) {
  const PpmImage bird = fixture("pigeon_640x480.ppm");
  ASSERT_TRUE(bird.loaded());

  FrameView frame = bird.view();
  frame.size = ImageSize{.width_px = 0, .height_px = 0};

  DetectionOutcome outcome;
  ASSERT_NO_THROW({ outcome = detector().detect(frame); });
  EXPECT_EQ(outcome.result(), DetectionResult::NONE);
}

// Encodes REQ-DET-004: the stride is "at least width x 3". A smaller one
// describes rows that overlap, which is not an image. Verifies: REQ-DET-001.
TEST_F(RealDetectorTest, StrideNarrowerThanTheRowIsNone) {
  const PpmImage bird = fixture("pigeon_640x480.ppm");
  ASSERT_TRUE(bird.loaded());

  FrameView frame = bird.view();
  frame.stride_bytes = frame.stride_bytes - 1;

  DetectionOutcome outcome;
  ASSERT_NO_THROW({ outcome = detector().detect(frame); });
  EXPECT_EQ(outcome.result(), DetectionResult::NONE)
      << "A stride below width x 3 is malformed, not an image to classify.";
}

// Encodes REQ-DET-004: the buffer must hold at least stride x height bytes.
// Reading past it is the defect this case exists to catch, so the suite is
// also run under AddressSanitizer (`make asan`). Verifies: REQ-DET-001.
TEST_F(RealDetectorTest, BufferShorterThanStrideTimesHeightIsNone) {
  const PpmImage bird = fixture("pigeon_640x480.ppm");
  ASSERT_TRUE(bird.loaded());

  FrameView frame = bird.view();
  frame.pixels = frame.pixels.first(frame.pixels.size() / 2);

  DetectionOutcome outcome;
  ASSERT_NO_THROW({ outcome = detector().detect(frame); });
  EXPECT_EQ(outcome.result(), DetectionResult::NONE)
      << "A truncated buffer must be refused, not read past.";
}

// Encodes REQ-DET-004: "separated by a stride in bytes that is at least three
// times the frame width". A padded frame is the same picture, so it must be
// classified the same way — a reader that ignores the stride sees a sheared
// image instead. Verifies: REQ-DET-001, REQ-DET-004.
TEST_F(RealDetectorTest, PaddedRowsAreHonouredAndChangeNothing) {
  const PpmImage packed = fixture("pigeon_640x480.ppm");
  const PpmImage padded = fixture("pigeon_640x480.ppm", 7);
  ASSERT_TRUE(packed.loaded());
  ASSERT_TRUE(padded.loaded());
  ASSERT_GT(padded.stride_bytes, packed.stride_bytes);

  const DetectionOutcome from_packed = detector().detect(packed.view());
  const DetectionOutcome from_padded = detector().detect(padded.view());

  ASSERT_EQ(from_packed.result(), DetectionResult::FOUND);
  EXPECT_EQ(from_padded.result(), DetectionResult::FOUND)
      << "Row padding must be skipped, not fed to the model.";

  const std::vector<Detection> packed_detections(from_packed.detections().begin(),
                                                 from_packed.detections().end());
  const std::vector<Detection> padded_detections(from_padded.detections().begin(),
                                                 from_padded.detections().end());
  EXPECT_EQ(packed_detections, padded_detections)
      << "The same picture with a wider stride is the same picture.";
}

// ---------------------------------------------------------------------------
// REQ-DEV-002 — determinism.
// ---------------------------------------------------------------------------

// Encodes ADR-0019's verification clause: "a test in tests/raspberry/ detects
// the same fixture frame twice and asserts the two DetectionOutcomes are
// equal". Verifies: REQ-DEV-002.
TEST_F(RealDetectorTest, TheSameFrameDetectedRepeatedlyGivesTheSameOutcome) {
  const PpmImage bird = fixture("pigeon_640x480.ppm");
  ASSERT_TRUE(bird.loaded());

  const DetectionOutcome first = detector().detect(bird.view());
  const std::vector<Detection> expected(first.detections().begin(), first.detections().end());
  ASSERT_EQ(first.result(), DetectionResult::FOUND);

  for (int repeat = 0; repeat < 4; ++repeat) {
    const DetectionOutcome again = detector().detect(bird.view());
    const std::vector<Detection> actual(again.detections().begin(), again.detections().end());

    EXPECT_EQ(again.result(), first.result()) << "repeat " << repeat;
    EXPECT_EQ(actual, expected) << "repeat " << repeat;
  }
}

// Encodes REQ-DEV-002 and the `core::Detector` contract's "stateless across
// frames": what the detector saw in the previous frame must not change what it
// reports for this one, so recorded fixtures replay reproducibly in any order.
TEST_F(RealDetectorTest, InterleavedFramesDoNotInfluenceOneAnother) {
  const PpmImage bird = fixture("pigeon_640x480.ppm");
  const PpmImage wall = fixture("brick_wall_320x240.ppm");
  ASSERT_TRUE(bird.loaded());
  ASSERT_TRUE(wall.loaded());

  const DetectionOutcome bird_alone = detector().detect(bird.view(1));
  const std::vector<Detection> expected(bird_alone.detections().begin(),
                                        bird_alone.detections().end());
  ASSERT_EQ(bird_alone.result(), DetectionResult::FOUND);

  EXPECT_EQ(detector().detect(wall.view(2)).result(), DetectionResult::NONE);

  const DetectionOutcome bird_after_wall = detector().detect(bird.view(3));
  const std::vector<Detection> actual(bird_after_wall.detections().begin(),
                                      bird_after_wall.detections().end());

  EXPECT_EQ(bird_after_wall.result(), DetectionResult::FOUND);
  EXPECT_EQ(actual, expected) << "An intervening frame changed the verdict.";
}

// Encodes REQ-DEV-002: the sequence number describes *which* frame this is and
// nothing else. `core/` never uses it to measure an interval, and the detector
// must not use it at all.
TEST_F(RealDetectorTest, SequenceNumberDoesNotAffectTheOutcome) {
  const PpmImage bird = fixture("pigeon_640x480.ppm");
  ASSERT_TRUE(bird.loaded());

  const DetectionOutcome first = detector().detect(bird.view(0));
  const DetectionOutcome later = detector().detect(bird.view(4'000'000'000ULL));
  const std::vector<Detection> expected(first.detections().begin(), first.detections().end());
  const std::vector<Detection> actual(later.detections().begin(), later.detections().end());

  EXPECT_EQ(later.result(), first.result());
  EXPECT_EQ(actual, expected);
}

// Encodes REQ-DEV-002: two detectors built from the same settings in the same
// binary are the same detector. Loading is not allowed to pick up anything
// from the machine or the environment that the settings do not name.
TEST_F(RealDetectorTest, TwoDetectorsFromTheSameSettingsAgree) {
  const PpmImage bird = fixture("pigeon_640x480.ppm");
  ASSERT_TRUE(bird.loaded());

  const NcnnDetector::LoadResult other = NcnnDetector::load(calibrated_settings());
  ASSERT_EQ(other.status, ModelLoadStatus::OK);
  ASSERT_NE(other.detector, nullptr);

  const DetectionOutcome mine = detector().detect(bird.view());
  const DetectionOutcome theirs = other.detector->detect(bird.view());
  const std::vector<Detection> expected(mine.detections().begin(), mine.detections().end());
  const std::vector<Detection> actual(theirs.detections().begin(), theirs.detections().end());

  EXPECT_EQ(theirs.result(), mine.result());
  EXPECT_EQ(actual, expected);
}

// Encodes ADR-0019: the thread count is a configured value and changes
// numerics, so only the *verdict* is asserted across thread counts — never a
// coordinate. A rig that stopped seeing the bird when it was given more
// threads would be a rig whose behaviour depends on its hardware.
// Verifies: REQ-DET-001, REQ-DEV-002.
TEST_F(RealDetectorTest, ThreadCountDoesNotChangeTheVerdict) {
  const PpmImage bird = fixture("pigeon_640x480.ppm");
  const PpmImage wall = fixture("brick_wall_320x240.ppm");
  ASSERT_TRUE(bird.loaded());
  ASSERT_TRUE(wall.loaded());

  NcnnDetectorSettings four_threads = calibrated_settings();
  four_threads.num_threads = 4;

  const NcnnDetector::LoadResult loaded = NcnnDetector::load(four_threads);
  ASSERT_EQ(loaded.status, ModelLoadStatus::OK);
  ASSERT_NE(loaded.detector, nullptr);

  EXPECT_EQ(loaded.detector->detect(bird.view()).result(), DetectionResult::FOUND);
  EXPECT_EQ(loaded.detector->detect(wall.view()).result(), DetectionResult::NONE);
}

}  // namespace

// Verifies: REQ-TRK-003, REQ-TRK-007, REQ-TRK-009, REQ-DEV-002
//
// Association is what makes confirmation a statement about a bird rather than
// about a frame, so every rule in `REQ-TRK-007` is a safety rule once removed.

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

#include <gtest/gtest.h>

#include "pigeon/core/detection.hpp"
#include "pigeon/core/geometry.hpp"
#include "pigeon/core/track.hpp"
#include "support/detection_builders.hpp"

namespace {

using pigeon::core::associate_detections;
using pigeon::core::Detection;
using pigeon::core::DetectionOutcome;
using pigeon::core::PixelDistance;
using pigeon::core::Track;
using pigeon::core::TrackId;
using pigeon::core::TrackUpdate;
using pigeon::test_support::detection_at;
using pigeon::test_support::frame_with;

constexpr PixelDistance radius{30.0};
constexpr TrackId first_id{0};

// Verifies: REQ-TRK-007 — "a detection that matches no track starts a new
// track", taking the next identifier.
TEST(Association, AnUnmatchedDetectionStartsANewTrack) {
  const TrackUpdate update =
      associate_detections({}, frame_with({detection_at(100.0, 100.0)}), radius, first_id);

  ASSERT_EQ(update.tracks.size(), 1U);
  EXPECT_EQ(update.tracks.at(0).id, first_id);
  EXPECT_EQ(update.tracks.at(0).consecutive_detections, 1U);
  EXPECT_EQ(update.tracks.at(0).latest, detection_at(100.0, 100.0));
  EXPECT_GT(update.next_id, first_id) << "the identity allocator must move forward";
}

// Verifies: REQ-TRK-007 — "two detections in successive frames within the
// association radius produce one track with a count of two".
TEST(Association, TwoDetectionsWithinTheRadiusProduceOneTrackWithCountTwo) {
  const TrackUpdate first =
      associate_detections({}, frame_with({detection_at(100.0, 100.0)}), radius, first_id);
  const TrackUpdate second = associate_detections(
      first.tracks, frame_with({detection_at(110.0, 100.0)}), radius, first.next_id);

  ASSERT_EQ(second.tracks.size(), 1U);
  EXPECT_EQ(second.tracks.at(0).id, first.tracks.at(0).id) << "the same bird must keep its identity";
  EXPECT_EQ(second.tracks.at(0).consecutive_detections, 2U);
  EXPECT_EQ(second.tracks.at(0).latest, detection_at(110.0, 100.0))
      << "the track must carry its most recent position, which is what aiming uses";
}

// Verifies: REQ-TRK-007 — "two detections in successive frames beyond the
// association radius produce two tracks, each with a count of one".
//
// The two tracks exist across the run rather than simultaneously: the first is
// discarded the moment it misses a frame (`REQ-TRK-009`, ADR-0010). The
// observable consequence — a second, distinct track that starts again at one,
// so nothing is ever confirmed by two different birds — is what is asserted.
TEST(Association, TwoDetectionsBeyondTheRadiusProduceTwoDistinctTracksEachStartingAtOne) {
  const TrackUpdate first =
      associate_detections({}, frame_with({detection_at(100.0, 100.0)}), radius, first_id);
  const TrackUpdate second = associate_detections(
      first.tracks, frame_with({detection_at(300.0, 100.0)}), radius, first.next_id);

  ASSERT_EQ(second.tracks.size(), 1U);
  EXPECT_NE(second.tracks.at(0).id, first.tracks.at(0).id)
      << "a detection outside the radius is a different bird and must not inherit a count";
  EXPECT_EQ(second.tracks.at(0).consecutive_detections, 1U);
}

// Verifies: REQ-TRK-007 — just inside the radius associates, just outside does
// not. The exact-equality boundary is deliberately not asserted: neither the
// requirement nor `track.hpp` says whether "within" includes the radius
// itself, and choosing is the architect's call, not the test's.
TEST(Association, AssociatesJustInsideTheRadiusAndNotJustOutside) {
  const TrackUpdate first =
      associate_detections({}, frame_with({detection_at(100.0, 100.0)}), radius, first_id);

  const TrackUpdate just_inside = associate_detections(
      first.tracks, frame_with({detection_at(100.0 + radius.pixels - 0.5, 100.0)}), radius,
      first.next_id);
  const TrackUpdate just_outside = associate_detections(
      first.tracks, frame_with({detection_at(100.0 + radius.pixels + 0.5, 100.0)}), radius,
      first.next_id);

  ASSERT_EQ(just_inside.tracks.size(), 1U);
  EXPECT_EQ(just_inside.tracks.at(0).consecutive_detections, 2U);
  ASSERT_EQ(just_outside.tracks.size(), 1U);
  EXPECT_EQ(just_outside.tracks.at(0).consecutive_detections, 1U);
}

// Verifies: REQ-TRK-007 — "each detection joins the **nearest** existing track
// whose centroid lies within the radius", not merely the first one in range.
TEST(Association, ADetectionJoinsTheNearestTrackInRange) {
  const TrackUpdate seed = associate_detections(
      {}, frame_with({detection_at(100.0, 100.0), detection_at(120.0, 100.0)}), radius, first_id);
  ASSERT_EQ(seed.tracks.size(), 2U);

  const auto nearer = std::ranges::find_if(seed.tracks, [](const Track& track) {
    return track.latest.centroid_px.x_px == 120.0;
  });
  ASSERT_NE(nearer, seed.tracks.end());
  const TrackId expected_id = nearer->id;

  const TrackUpdate update = associate_detections(
      seed.tracks, frame_with({detection_at(115.0, 100.0)}), radius, seed.next_id);

  ASSERT_EQ(update.tracks.size(), 1U);
  EXPECT_EQ(update.tracks.at(0).id, expected_id)
      << "the detection was 5 px from one track and 15 px from the other";
  EXPECT_EQ(update.tracks.at(0).consecutive_detections, 2U);
}

// Verifies: REQ-TRK-003, REQ-TRK-009 — "a NONE frame empties the track set
// entirely" (track.hpp), which is how the counter reset is realised.
TEST(Association, ANoneFrameEmptiesTheTrackSet) {
  const TrackUpdate first =
      associate_detections({}, frame_with({detection_at(100.0, 100.0)}), radius, first_id);

  const TrackUpdate second =
      associate_detections(first.tracks, DetectionOutcome::none(), radius, first.next_id);

  EXPECT_TRUE(second.tracks.empty());
}

// Verifies: REQ-TRK-009 — "a track absent for one frame and detected again in
// the next has a consecutive-detection count of one, not two".
TEST(Association, ATrackAbsentForOneFrameRestartsAtOne) {
  const TrackUpdate first =
      associate_detections({}, frame_with({detection_at(100.0, 100.0)}), radius, first_id);
  const TrackUpdate second =
      associate_detections(first.tracks, frame_with({detection_at(105.0, 100.0)}), radius,
                           first.next_id);
  ASSERT_EQ(second.tracks.at(0).consecutive_detections, 2U);

  const TrackUpdate missed =
      associate_detections(second.tracks, DetectionOutcome::none(), radius, second.next_id);
  const TrackUpdate returned = associate_detections(
      missed.tracks, frame_with({detection_at(105.0, 100.0)}), radius, missed.next_id);

  ASSERT_EQ(returned.tracks.size(), 1U);
  EXPECT_EQ(returned.tracks.at(0).consecutive_detections, 1U)
      << "a bird that vanished for a frame must earn its confirmation again";
  EXPECT_NE(returned.tracks.at(0).id, second.tracks.at(0).id)
      << "there is no retention: the returning bird is a new track (ADR-0010)";
}

// Verifies: REQ-TRK-009 — "every track in the track set has a
// consecutive-detection count of at least one".
TEST(Association, EveryTrackInTheSetHasBeenDetectedInTheFrameJustProcessed) {
  TrackUpdate update;
  update.next_id = first_id;
  const std::vector<std::vector<Detection>> frames{
      {detection_at(10.0, 10.0), detection_at(200.0, 200.0)},
      {detection_at(12.0, 10.0)},
      {detection_at(14.0, 10.0), detection_at(400.0, 50.0), detection_at(200.0, 205.0)},
      {detection_at(600.0, 400.0)},
  };

  for (const std::vector<Detection>& detections : frames) {
    update = associate_detections(update.tracks, frame_with(detections), radius, update.next_id);
    EXPECT_EQ(update.tracks.size(), detections.size())
        << "the surviving set must be exactly the tracks detected in this frame";
    for (const Track& track : update.tracks) {
      EXPECT_GE(track.consecutive_detections, 1U);
    }
  }
}

// Verifies: REQ-SAF-004, REQ-TRK-007 — "a zero association_radius matches
// nothing, so every detection starts a new track and nothing is ever
// confirmed" (track.hpp): the inert behaviour of an uncalibrated rig.
TEST(Association, AZeroRadiusAssociatesNothing) {
  TrackUpdate update;
  update.next_id = first_id;

  for (int frame = 0; frame < 5; ++frame) {
    update = associate_detections(update.tracks, frame_with({detection_at(100.0, 100.0)}),
                                  PixelDistance{0.0}, update.next_id);
    ASSERT_EQ(update.tracks.size(), 1U);
    EXPECT_EQ(update.tracks.at(0).consecutive_detections, 1U)
        << "an uncalibrated radius must never accumulate a confirmation";
  }
}

// Verifies: REQ-TRK-007, REQ-DEV-002 — "association is deterministic:
// identical frame sequences produce identical tracks, independent of iteration
// order". The detector's ordering is its own business (`detection.hpp`), so a
// permutation of the same detections must produce the same track set,
// identifiers included.
//
// The permutations are drawn from a generator seeded with a fixed constant, so
// the case set is the same on every run.
TEST(Association, IsIndependentOfTheOrderTheDetectorReportsDetectionsIn) {
  constexpr std::uint_fast32_t seed = 20260916;
  const std::vector<Detection> detections{
      detection_at(50.0, 50.0), detection_at(300.0, 120.0), detection_at(80.0, 400.0),
      detection_at(500.0, 300.0), detection_at(55.0, 240.0)};

  const TrackUpdate reference = associate_detections({}, frame_with(detections), radius, first_id);

  std::mt19937 generator{seed};
  std::vector<Detection> permuted = detections;
  for (int attempt = 0; attempt < 20; ++attempt) {
    std::shuffle(permuted.begin(), permuted.end(), generator);
    const TrackUpdate update = associate_detections({}, frame_with(permuted), radius, first_id);

    ASSERT_EQ(update.tracks.size(), reference.tracks.size());
    EXPECT_EQ(update.tracks, reference.tracks)
        << "attempt " << attempt << " (seed " << seed
        << "): the track set depended on the detector's ordering";
    EXPECT_EQ(update.next_id, reference.next_id);
  }
}

// Verifies: REQ-DEV-002 — association is a pure function: same arguments, same
// result, always.
TEST(Association, IsAPureFunctionOfItsArguments) {
  const TrackUpdate seed = associate_detections(
      {}, frame_with({detection_at(100.0, 100.0), detection_at(400.0, 300.0)}), radius, first_id);

  const TrackUpdate first = associate_detections(
      seed.tracks, frame_with({detection_at(105.0, 100.0)}), radius, seed.next_id);
  const TrackUpdate second = associate_detections(
      seed.tracks, frame_with({detection_at(105.0, 100.0)}), radius, seed.next_id);

  EXPECT_EQ(first.tracks, second.tracks);
  EXPECT_EQ(first.next_id, second.next_id);
}

// Verifies: REQ-DEV-002 — "identifiers are assigned deterministically in
// ascending order and are never reused within a run" (track.hpp), which is
// what makes a replayed scenario reproducible.
TEST(Association, IdentifiersAreAscendingAndNeverReusedWithinARun) {
  TrackUpdate update;
  update.next_id = first_id;
  std::vector<TrackId> seen;
  TrackId previous_next = first_id;

  for (int frame = 0; frame < 6; ++frame) {
    // Every frame is a new bird somewhere else, so every frame allocates.
    const double x_px = 20.0 + (static_cast<double>(frame) * 100.0);
    update = associate_detections(update.tracks, frame_with({detection_at(x_px, 30.0)}), radius,
                                  update.next_id);
    ASSERT_EQ(update.tracks.size(), 1U);
    EXPECT_EQ(std::ranges::count(seen, update.tracks.at(0).id), 0)
        << "identifier reused within a run at frame " << frame;
    seen.push_back(update.tracks.at(0).id);
    EXPECT_GE(update.next_id, previous_next) << "the allocator moved backwards";
    previous_next = update.next_id;
  }
}

}  // namespace

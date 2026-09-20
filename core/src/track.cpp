#include "pigeon/core/track.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <tuple>
#include <vector>

#include "pigeon/core/detection.hpp"
#include "pigeon/core/geometry.hpp"

namespace pigeon::core {
namespace {

/// A total order on detections that depends only on where they are.
///
/// Association allocates identifiers in this order rather than in the
/// detector's, so a permuted frame yields the same tracks with the same
/// identifiers (`REQ-TRK-007`, `REQ-DEV-002`). Position alone would order any
/// real frame; the extent is included so that two detections sharing a centroid
/// still have a defined order.
[[nodiscard]] bool precedes(const Detection& lhs, const Detection& rhs) noexcept {
  return std::tie(lhs.centroid_px.x_px, lhs.centroid_px.y_px, lhs.bounding_box_px.top_left_px.x_px,
                  lhs.bounding_box_px.top_left_px.y_px, lhs.bounding_box_px.width_px,
                  lhs.bounding_box_px.height_px) <
         std::tie(rhs.centroid_px.x_px, rhs.centroid_px.y_px, rhs.bounding_box_px.top_left_px.x_px,
                  rhs.bounding_box_px.top_left_px.y_px, rhs.bounding_box_px.width_px,
                  rhs.bounding_box_px.height_px);
}

/// The nearest track this detection may join, or `nullptr` if there is none.
///
/// The radius is **inclusive**: the test is `distance <= association_radius`
/// (`REQ-TRK-007`, Q17). A track already joined by an earlier detection in this
/// frame is skipped, because a track is one bird and cannot be two. Ties in
/// distance are broken by track identity, never by the order the tracks happen
/// to be held in (`REQ-DEV-002`).
[[nodiscard]] const Track* nearest_track_in_range(std::span<const Track> tracks,
                                                  std::span<const TrackId> claimed,
                                                  const Detection& detection,
                                                  PixelDistance association_radius) {
  const Track* nearest = nullptr;
  PixelDistance nearest_distance{};

  for (const Track& track : tracks) {
    if (std::ranges::find(claimed, track.id) != claimed.end()) {
      continue;
    }
    const PixelDistance distance =
        distance_between(detection.centroid_px, track.latest.centroid_px);
    if (distance <= association_radius &&
        (nearest == nullptr || distance < nearest_distance ||
         (distance == nearest_distance && track.id < nearest->id))) {
      nearest = &track;
      nearest_distance = distance;
    }
  }
  return nearest;
}

/// Ordered by bounding-box area, ties broken by ascending centroid X and then
/// ascending centroid Y (`REQ-TRK-008`).
[[nodiscard]] bool outranks(const Track& candidate, const Track& incumbent) noexcept {
  const double candidate_area = area_px2(candidate.latest.bounding_box_px);
  const double incumbent_area = area_px2(incumbent.latest.bounding_box_px);
  if (candidate_area != incumbent_area) {
    return candidate_area > incumbent_area;
  }
  return std::tie(candidate.latest.centroid_px.x_px, candidate.latest.centroid_px.y_px) <
         std::tie(incumbent.latest.centroid_px.x_px, incumbent.latest.centroid_px.y_px);
}

}  // namespace

bool is_confirmed(const Track& track) noexcept {
  return track.consecutive_detections >= confirmation_frame_count;
}

TrackUpdate associate_detections(std::span<const Track> tracks, const DetectionOutcome& outcome,
                                 PixelDistance association_radius, TrackId next_id) {
  std::vector<Detection> detections(outcome.detections().begin(), outcome.detections().end());
  std::ranges::sort(detections, precedes);

  std::vector<TrackId> claimed;
  claimed.reserve(detections.size());

  TrackUpdate update;
  update.next_id = next_id;
  update.tracks.reserve(detections.size());

  for (const Detection& detection : detections) {
    const Track* nearest = nearest_track_in_range(tracks, claimed, detection, association_radius);

    if (nearest == nullptr) {
      // A detection that matches no track starts a new one, taking the next
      // identifier. Identifiers are handed out in the canonical order above,
      // never in the detector's (`REQ-TRK-007`, `REQ-DEV-002`).
      update.tracks.push_back(Track{
          .id = update.next_id,
          .latest = detection,
          .consecutive_detections = 1,
      });
      update.next_id = static_cast<TrackId>(static_cast<std::uint32_t>(update.next_id) + 1U);
      continue;
    }

    claimed.push_back(nearest->id);
    update.tracks.push_back(Track{
        .id = nearest->id,
        .latest = detection,
        .consecutive_detections = nearest->consecutive_detections + 1,
    });
  }

  // A track that received no detection is simply absent from the result: that
  // is the counter reset of `REQ-TRK-003`, realised by discard (`REQ-TRK-009`,
  // ADR-0010).
  return update;
}

std::optional<Track> select_confirmed_target(std::span<const Track> tracks) {
  std::optional<Track> selected;
  for (const Track& track : tracks) {
    if (!is_confirmed(track)) {
      continue;
    }
    if (!selected.has_value() || outranks(track, *selected)) {
      selected = track;
    }
  }
  return selected;
}

}  // namespace pigeon::core

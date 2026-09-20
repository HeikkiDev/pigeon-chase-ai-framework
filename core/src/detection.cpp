#include "pigeon/core/detection.hpp"

#include <span>
#include <utility>
#include <vector>

namespace pigeon::core {

DetectionOutcome DetectionOutcome::none() { return {}; }

DetectionOutcome DetectionOutcome::found(std::vector<Detection> detections) {
  DetectionOutcome outcome;
  outcome.detections_ = std::move(detections);
  return outcome;
}

DetectionResult DetectionOutcome::result() const noexcept {
  // FOUND if and only if there is something to aim at: the classification
  // follows the evidence rather than the caller's claim (`REQ-DET-001`).
  return detections_.empty() ? DetectionResult::NONE : DetectionResult::FOUND;
}

std::span<const Detection> DetectionOutcome::detections() const noexcept { return detections_; }

}  // namespace pigeon::core

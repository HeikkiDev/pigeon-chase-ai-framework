#pragma once

// Verifies: REQ-DET-001, REQ-DEV-002 (support code — the paths and settings
// every tests/raspberry/ suite is driven from).
//
// Paths arrive as compile definitions from tests/raspberry/CMakeLists.txt so
// that no test depends on the working directory it happens to be run from
// (`REQ-DEV-002`: no dependence on execution context).

#include <string>

#include "pigeon/raspberry/ncnn_detector.hpp"

namespace pigeon::test_support {

/// The vendored model bundle (ADR-0019 §8). Its layout is fixed by the ADR;
/// the file names are the upstream ones.
[[nodiscard]] inline std::string model_param_path() { return PIGEON_TEST_MODEL_PARAM; }
[[nodiscard]] inline std::string model_weights_path() { return PIGEON_TEST_MODEL_WEIGHTS; }

/// A version-controlled fixture, by file name.
[[nodiscard]] inline std::string fixture_path(const std::string& name) {
  return std::string{PIGEON_TEST_FIXTURE_DIR} + "/" + name;
}

/// The confidence threshold the suite configures.
///
/// **Test data, not deployment calibration.** ADR-0019 §5 leaves the
/// deployment value uncalibrated and in the configuration file; this is the
/// model's own baked-in floor, chosen so the suite neither raises nor lowers a
/// value nobody has decided. It is not a tuning parameter: the bird fixture
/// scores 0.967 and the no-bird fixtures score nothing at all, so every
/// assertion here holds across the whole usable range.
inline constexpr float test_confidence_threshold = 0.55F;

/// Threads are fixed, never derived from the machine: the thread count changes
/// numerics, and a detector whose numerics depend on the host CPU is not
/// reproducible (`REQ-DEV-002`, ADR-0019).
inline constexpr int test_thread_count = 1;

/// Settings that name the real model and are calibrated to detect.
[[nodiscard]] inline raspberry::NcnnDetectorSettings calibrated_settings() {
  raspberry::NcnnDetectorSettings settings;
  settings.param_path = model_param_path();
  settings.weights_path = model_weights_path();
  settings.confidence_threshold = test_confidence_threshold;
  settings.num_threads = test_thread_count;
  return settings;
}

/// Settings that name the real model but leave the threshold at its inert
/// default (ADR-0008, ADR-0019 §5).
[[nodiscard]] inline raspberry::NcnnDetectorSettings uncalibrated_settings() {
  raspberry::NcnnDetectorSettings settings;
  settings.param_path = model_param_path();
  settings.weights_path = model_weights_path();
  settings.num_threads = test_thread_count;
  return settings;
}

}  // namespace pigeon::test_support

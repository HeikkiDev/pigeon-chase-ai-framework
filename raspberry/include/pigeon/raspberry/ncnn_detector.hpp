#pragma once

/// The real detector: ncnn running Yolo-Fastest-1.1 behind `core::Detector`
/// (ADR-0019).
///
/// Declarations only. This header is **standard-library only**: the
/// `ncnn::Net` lives behind a pimpl so that no translation unit outside
/// `raspberry/src/` ever sees an ncnn header, and `core/` neither includes nor
/// links the runtime (`REQ-DET-002`).
///
/// Bodies belong to the implementation-engineer. This file exists so the
/// acceptance criteria of ADR-0019 can be written as compiled tests against a
/// declared interface, exactly as `core/`'s interfaces were declared before
/// `tests/core/` was written.

#include <cstdint>
#include <memory>
#include <string>

#include "pigeon/core/detection.hpp"
#include "pigeon/core/detector.hpp"
#include "pigeon/core/frame.hpp"

namespace pigeon::raspberry {

/// Everything the detector needs that is not a frame.
///
/// Calibration, not code (`REQ-AIM-003` by analogy): a plain value type parsed
/// from the version-controlled key/value file in `raspberry/` and passed in.
struct NcnnDetectorSettings {
  /// Path to the model's `.param` topology file. Empty means unconfigured.
  std::string param_path;
  /// Path to the model's `.bin` weights file. Empty means unconfigured.
  std::string weights_path;
  /// Minimum confidence a detection must carry to be reported.
  ///
  /// Defaults **above 1.0**, which no model output can reach, so a
  /// default-constructed settings value yields a detector that reports `NONE`
  /// for every frame (ADR-0008, ADR-0019 §5). The model's own baked-in
  /// threshold is a floor; this value may only raise it.
  float confidence_threshold{2.0F};
  /// Inference threads. Fixed by configuration, never derived from the
  /// machine, because the thread count changes numerics (`REQ-DEV-002`).
  int num_threads{1};
  /// bf16 storage. A deployment setting; off by default.
  bool use_bf16_storage{false};
};

/// Why a load attempt did not produce a detector. Reported as a value: no
/// exception crosses this module boundary (ADR-0007).
enum class ModelLoadStatus : std::uint8_t {
  /// No model paths were configured.
  UNCONFIGURED,
  /// The model was loaded and the detector is usable.
  OK,
  /// The `.param` file does not exist or cannot be read.
  PARAM_NOT_FOUND,
  /// The `.bin` file does not exist or cannot be read.
  WEIGHTS_NOT_FOUND,
  /// A file exists but is not a model this runtime can parse.
  MALFORMED,
};

/// A `core::Detector` backed by ncnn and the Yolo-Fastest-1.1 COCO model.
///
/// Reports the COCO `bird` class as the proxy for a pigeon (maintainer ruling
/// Q23) and discards every other class. Confidence is applied here and dropped
/// here: `core::Detection` carries none (ADR-0003).
///
/// `detect` honours the `core::Detector` contract: total, `const`, stateless
/// across frames, never throwing, and treating an empty or malformed frame as
/// `NONE` (`REQ-DET-001`, `REQ-DET-004`).
class NcnnDetector final : public core::Detector {
 public:
  /// The outcome of a load attempt. Default-constructs to **failure**: a
  /// result nobody filled in is not a working detector (ADR-0008).
  struct LoadResult {
    ModelLoadStatus status{ModelLoadStatus::UNCONFIGURED};
    /// Null unless `status == ModelLoadStatus::OK`.
    std::unique_ptr<NcnnDetector> detector;
  };

  /// Load the model named by `settings`. The only fallible operation here.
  [[nodiscard]] static LoadResult load(const NcnnDetectorSettings& settings);

  NcnnDetector(const NcnnDetector&) = delete;
  NcnnDetector& operator=(const NcnnDetector&) = delete;
  NcnnDetector(NcnnDetector&&) = delete;
  NcnnDetector& operator=(NcnnDetector&&) = delete;
  ~NcnnDetector() override;

  /// Classify one RGB888 frame (`REQ-DET-001`, `REQ-DET-004`).
  ///
  /// The frame is stretched to the model's 320x320 input honouring
  /// `stride_bytes` and normalised by 1/255; the normalised boxes the model
  /// returns are clamped to `[0, 1]` before being scaled back to pixels, so no
  /// centroid can fall outside the frame (ADR-0019 section 6).
  [[nodiscard]] core::DetectionOutcome detect(const core::FrameView& frame) const override;

 private:
  struct Impl;

  explicit NcnnDetector(std::unique_ptr<Impl> impl);

  std::unique_ptr<Impl> impl_;
};

}  // namespace pigeon::raspberry

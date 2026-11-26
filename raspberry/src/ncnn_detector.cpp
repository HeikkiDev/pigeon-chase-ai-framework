#include "pigeon/raspberry/ncnn_detector.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

#include <ncnn/mat.h>
#include <ncnn/net.h>

#include "pigeon/core/detection.hpp"
#include "pigeon/core/frame.hpp"
#include "pigeon/core/geometry.hpp"

/// The one translation unit in the repository permitted to include
/// `<ncnn/...>` (ADR-0019 §2, `REQ-DET-002`). Everything ncnn-shaped stops
/// here: callers see `core::DetectionOutcome` and nothing else.

namespace pigeon::raspberry {
namespace {

/// The model's input, fixed by `yolo-fastest-1.1.param` (ADR-0019 §6).
constexpr int model_input_width = 320;
constexpr int model_input_height = 320;

/// Blob names, likewise fixed by the vendored topology file.
constexpr const char* input_blob = "data";
constexpr const char* output_blob = "output";

/// COCO `bird` in the ncnn `Yolov3DetectionOutput` convention, where label 0
/// is background, so the COCO index 14 becomes 15 (ADR-0019 §4). Every other
/// class is discarded at this seam: a car detected at 0.96 is not a target.
constexpr int coco_bird_label = 15;

/// One `Yolov3DetectionOutput` row: label, probability, then the box as
/// `x1, y1, x2, y2` normalised to the model's input.
constexpr std::size_t detection_row_width = 6;

/// 1/255 on every channel, no mean subtraction (ADR-0019 §6).
constexpr float channel_normalisation = 1.0F / 255.0F;

/// A frame that is not an image is `NONE`, never an error: inaction is the
/// safe response (`core::Detector`'s contract, `REQ-DET-001`, `REQ-DET-004`).
[[nodiscard]] bool frame_is_usable(const core::FrameView& frame) {
  if (frame.size.width_px == 0 || frame.size.height_px == 0) {
    return false;
  }

  const auto width = static_cast<std::size_t>(frame.size.width_px);
  const auto height = static_cast<std::size_t>(frame.size.height_px);
  const std::size_t row_bytes = width * core::bytes_per_pixel;

  // A stride below width x 3 describes rows that overlap, which is not an
  // image; a buffer shorter than stride x height is one the producer never
  // filled (`REQ-DET-004`).
  if (frame.stride_bytes < row_bytes) {
    return false;
  }
  return frame.pixels.size() >= frame.stride_bytes * height;
}

/// Normalised model output, clamped before scaling so that a box predicted
/// off the edge cannot yield a centroid outside the image — an aim with no
/// angular interpretation (ADR-0019 §6, `REQ-AIM-001`).
[[nodiscard]] float clamp_unit(float value) { return std::clamp(value, 0.0F, 1.0F); }

/// One `Yolov3DetectionOutput` row, copied out of the model's buffer so the
/// mapping below reads named fields rather than offsets into a raw pointer.
struct DetectionRow {
  float label{};
  float probability{};
  float x1{};
  float y1{};
  float x2{};
  float y2{};
};

/// Map one normalised box onto this frame's own pixel dimensions. The resize
/// stretches, so the inverse mapping is a pure scale.
[[nodiscard]] core::Detection to_detection(const DetectionRow& row, const core::ImageSize& size) {
  const auto width_px = static_cast<double>(size.width_px);
  const auto height_px = static_cast<double>(size.height_px);

  const double left = static_cast<double>(clamp_unit(row.x1)) * width_px;
  const double top = static_cast<double>(clamp_unit(row.y1)) * height_px;
  const double right = static_cast<double>(clamp_unit(row.x2)) * width_px;
  const double bottom = static_cast<double>(clamp_unit(row.y2)) * height_px;

  const core::BoundingBox box{
      .top_left_px = core::PixelPoint{.x_px = left, .y_px = top},
      .width_px = right - left,
      .height_px = bottom - top,
  };

  return core::Detection{
      .centroid_px =
          core::PixelPoint{
              .x_px = box.top_left_px.x_px + (box.width_px / 2.0),
              .y_px = box.top_left_px.y_px + (box.height_px / 2.0),
          },
      .bounding_box_px = box,
  };
}

}  // namespace

/// The ncnn runtime, hidden from every other translation unit by the pimpl the
/// header declares.
struct NcnnDetector::Impl {
  ncnn::Net net;
  /// Applied on top of the model's own baked-in 0.55 floor, which it may only
  /// raise (ADR-0019 §5). Defaults above 1.0, so an unconfigured rig is inert.
  float confidence_threshold{2.0F};
};

NcnnDetector::NcnnDetector(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

NcnnDetector::~NcnnDetector() = default;

NcnnDetector::LoadResult NcnnDetector::load(const NcnnDetectorSettings& settings) {
  LoadResult result;

  if (settings.param_path.empty() || settings.weights_path.empty()) {
    result.status = ModelLoadStatus::UNCONFIGURED;
    return result;
  }

  // Loading is the only fallible operation on this seam and it reports failure
  // as a value: no exception crosses this module boundary (ADR-0019 §3).
  try {
    // ncnn answers -1 for both an absent and an unparseable file, so the two
    // are distinguished here. A deployment fault must be diagnosable from the
    // status alone.
    std::error_code ignored;
    if (!std::filesystem::is_regular_file(settings.param_path, ignored)) {
      result.status = ModelLoadStatus::PARAM_NOT_FOUND;
      return result;
    }
    if (!std::filesystem::is_regular_file(settings.weights_path, ignored)) {
      result.status = ModelLoadStatus::WEIGHTS_NOT_FOUND;
      return result;
    }

    auto impl = std::make_unique<Impl>();

    // Fixed by configuration, never derived from the machine: the thread count
    // changes numerics, and numerics that depend on the host are not
    // reproducible (`REQ-DEV-002`). A non-positive count is clamped to one
    // rather than rejected (maintainer ruling Q25), because the alternative is
    // ncnn's own machine-derived default.
    impl->net.opt.num_threads = std::max(settings.num_threads, 1);
    impl->net.opt.use_bf16_storage = settings.use_bf16_storage;
    impl->net.opt.use_vulkan_compute = false;
    impl->confidence_threshold = settings.confidence_threshold;

    if (impl->net.load_param(settings.param_path.c_str()) != 0) {
      result.status = ModelLoadStatus::MALFORMED;
      return result;
    }
    if (impl->net.load_model(settings.weights_path.c_str()) != 0) {
      result.status = ModelLoadStatus::MALFORMED;
      return result;
    }

    // `new` rather than `std::make_unique`: the constructor is private, which
    // is what keeps a detector unobtainable except through this factory, and
    // `make_unique` cannot see it. The result is owned from the next
    // expression onwards.
    result.detector = std::unique_ptr<NcnnDetector>(new NcnnDetector(std::move(impl)));
    result.status = ModelLoadStatus::OK;
    return result;
  } catch (...) {
    result.status = ModelLoadStatus::MALFORMED;
    result.detector.reset();
    return result;
  }
}

core::DetectionOutcome NcnnDetector::detect(const core::FrameView& frame) const {
  if (!frame_is_usable(frame)) {
    return core::DetectionOutcome::none();
  }

  // Total and never throwing, because the caller in `core/` has no handler and
  // no safe answer (`REQ-DET-001`).
  try {
    // One conversion and resize, honouring the row stride so padded rows are
    // skipped rather than fed to the model (`REQ-DET-004`). The resize
    // stretches; it does not letterbox (ADR-0019 §6).
    ncnn::Mat input = ncnn::Mat::from_pixels_resize(
        reinterpret_cast<const unsigned char*>(frame.pixels.data()),  // NOLINT
        ncnn::Mat::PIXEL_RGB, static_cast<int>(frame.size.width_px),
        static_cast<int>(frame.size.height_px), static_cast<int>(frame.stride_bytes),
        model_input_width, model_input_height);
    if (input.empty()) {
      return core::DetectionOutcome::none();
    }

    const std::array<float, core::bytes_per_pixel> norm{
        channel_normalisation,
        channel_normalisation,
        channel_normalisation,
    };
    input.substract_mean_normalize(nullptr, norm.data());

    ncnn::Extractor extractor = impl_->net.create_extractor();
    if (extractor.input(input_blob, input) != 0) {
      return core::DetectionOutcome::none();
    }

    ncnn::Mat output;
    if (extractor.extract(output_blob, output) != 0) {
      return core::DetectionOutcome::none();
    }
    if (std::cmp_less(output.w, detection_row_width)) {
      return core::DetectionOutcome::none();
    }

    std::vector<core::Detection> detections;
    for (int row_index = 0; row_index < output.h; ++row_index) {
      // Copied out of ncnn's buffer and destructured, so the six numbers are
      // read by name rather than by offset.
      std::array<float, detection_row_width> values{};
      std::ranges::copy(std::span<const float>{output.row(row_index), values.size()},
                        values.begin());
      const auto [label, probability, x1, y1, x2, y2] = values;

      if (static_cast<int>(label) != coco_bird_label) {
        continue;
      }
      if (probability < impl_->confidence_threshold) {
        continue;
      }

      // Confidence is applied here and dropped here: `core::Detection` carries
      // none, and ADR-0003 rejected it as a selection key.
      detections.push_back(to_detection(
          DetectionRow{
              .label = label,
              .probability = probability,
              .x1 = x1,
              .y1 = y1,
              .x2 = x2,
              .y2 = y2,
          },
          frame.size));
    }

    return core::DetectionOutcome::found(std::move(detections));
  } catch (...) {
    return core::DetectionOutcome::none();
  }
}

}  // namespace pigeon::raspberry

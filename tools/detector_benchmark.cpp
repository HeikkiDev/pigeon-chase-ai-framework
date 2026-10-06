/// Throughput benchmark for the real detector (`REQ-DET-003`, ADR-0019 §9).
///
/// **Not a test, and never registered with CTest.** It is built only when ncnn
/// is present and is run by hand, on a Raspberry Pi 3B running 64-bit
/// Raspberry Pi OS (maintainer ruling Q22). A figure measured on any other
/// machine says nothing about `REQ-DET-003` and must not be recorded as
/// evidence for it.
///
/// What it measures is **model inference only** — maintainer ruling Q21 — so
/// the timed region is exactly `NcnnDetector::detect`: the stride-honouring
/// conversion and resize of a 640 x 480 RGB888 frame (ruling Q24), the forward
/// pass, and the mapping of the boxes back into frame pixels. Capture, colour
/// conversion from the camera's own format, tracking and the device protocol
/// are outside it.
///
/// Usage:
///
///     pigeon_detector_benchmark --param <file> --weights <file> \
///         [--threshold <float>] [--threads <n>] [--bf16] [--repeats <n>] \
///         <frame.ppm> [<frame.ppm> ...]
///
/// The frames are binary PPM ("P6"), whose payload is already the RGB888 byte
/// layout of `REQ-DET-004`, and every one of them must be 640 x 480. They are
/// replayed in the order given, `--repeats` times, so the run is reproducible:
/// nothing here reads a directory listing or a random number, and the only
/// clock it reads is the stopwatch it exists to report.

#include <sys/resource.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <ios>
#include <iostream>
#include <iterator>
#include <ratio>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "pigeon/core/detection.hpp"
#include "pigeon/core/frame.hpp"
#include "pigeon/raspberry/ncnn_detector.hpp"

namespace {

using pigeon::core::DetectionResult;
using pigeon::core::FrameView;
using pigeon::core::ImageSize;
using pigeon::raspberry::ModelLoadStatus;
using pigeon::raspberry::NcnnDetector;
using pigeon::raspberry::NcnnDetectorSettings;

/// The capture resolution the camera delivers (maintainer ruling Q24). A frame
/// of any other size would measure a resize nobody deploys.
constexpr std::uint32_t capture_width_px = 640;
constexpr std::uint32_t capture_height_px = 480;

struct Frame {
  std::vector<std::byte> bytes;
  ImageSize size{};
  std::size_t stride_bytes{};

  [[nodiscard]] FrameView view(std::uint64_t sequence_number) const {
    return FrameView{
        .size = size,
        .stride_bytes = stride_bytes,
        .pixels = std::span<const std::byte>{bytes},
        .sequence_number = sequence_number,
    };
  }
};

struct Options {
  NcnnDetectorSettings settings;
  std::vector<std::string> frame_paths;
  int repeats{20};
  bool valid{false};
  bool help{false};
};

struct Measurement {
  std::uint64_t processed_frames{};
  std::uint64_t found_frames{};
  std::chrono::nanoseconds total{};
  std::chrono::nanoseconds median{};
  std::chrono::nanoseconds slowest{};
};

/// Read one PPM header token, skipping whitespace and `#` comments.
bool read_token(std::istream& in, std::string& token) {
  token.clear();
  int next = in.get();
  while (in) {
    if (std::isspace(next) != 0) {
      next = in.get();
      continue;
    }
    if (next == '#') {
      while (in && next != '\n') {
        next = in.get();
      }
      continue;
    }
    break;
  }
  while (in && std::isspace(next) == 0) {
    token.push_back(static_cast<char>(next));
    next = in.get();
  }
  return !token.empty();
}

/// Load a binary PPM as a packed RGB888 frame. Returns an empty frame and
/// explains itself on failure; the caller stops rather than quietly measuring
/// a shorter sequence than it was asked for.
Frame load_ppm(const std::string& path) {
  Frame frame;

  std::ifstream file(path, std::ios::binary);
  if (!file) {
    std::cerr << "error: cannot open frame: " << path << "\n";
    return frame;
  }

  std::string magic;
  std::string width_token;
  std::string height_token;
  std::string max_value_token;
  if (!read_token(file, magic) || magic != "P6" || !read_token(file, width_token) ||
      !read_token(file, height_token) || !read_token(file, max_value_token) ||
      max_value_token != "255") {
    std::cerr << "error: not an 8-bit binary PPM: " << path << "\n";
    return frame;
  }

  const auto width = static_cast<std::uint32_t>(std::stoul(width_token));
  const auto height = static_cast<std::uint32_t>(std::stoul(height_token));
  if (width != capture_width_px || height != capture_height_px) {
    std::cerr << "error: " << path << " is " << width << "x" << height
              << ", but the benchmark replays the " << capture_width_px << "x" << capture_height_px
              << " capture resolution of ruling Q24\n";
    return frame;
  }

  const std::size_t stride = static_cast<std::size_t>(width) * pigeon::core::bytes_per_pixel;
  std::vector<std::byte> buffer(stride * static_cast<std::size_t>(height), std::byte{0});
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): a file is bytes.
  auto* destination = reinterpret_cast<char*>(buffer.data());
  if (!file.read(destination, static_cast<std::streamsize>(buffer.size()))) {
    std::cerr << "error: truncated frame: " << path << "\n";
    return frame;
  }

  frame.bytes = std::move(buffer);
  frame.size = ImageSize{.width_px = width, .height_px = height};
  frame.stride_bytes = stride;
  return frame;
}

/// Peak resident set size in bytes. `ru_maxrss` is bytes on macOS and
/// kilobytes on Linux, and the deployment target is Linux.
std::uint64_t peak_resident_bytes() {
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) {
    return 0;
  }
  const auto maximum = static_cast<std::uint64_t>(usage.ru_maxrss);
#ifdef __APPLE__
  return maximum;
#else
  return maximum * 1024ULL;
#endif
}

void print_usage() {
  std::cout << "usage: pigeon_detector_benchmark --param <file> --weights <file>\n"
            << "         [--threshold <float>] [--threads <n>] [--bf16] [--repeats <n>]\n"
            << "         <frame.ppm> [<frame.ppm> ...]\n";
}

Options parse_options(std::span<char*> arguments) {
  Options options;
  // The model's own baked-in floor: raising the threshold would measure the
  // same work and report fewer detections, so the default is the lowest value
  // that has any effect at all (ADR-0019 §5).
  options.settings.confidence_threshold = 0.55F;
  options.settings.num_threads = 4;

  auto argument = arguments.begin();
  if (argument != arguments.end()) {
    ++argument;  // the program name
  }

  for (; argument != arguments.end(); ++argument) {
    const std::string name{*argument};
    const bool has_value = std::next(argument) != arguments.end();
    const auto take_value = [&argument] { return std::string{*(++argument)}; };

    if (name == "--help" || name == "-h") {
      options.help = true;
      return options;
    }
    if (name == "--bf16") {
      options.settings.use_bf16_storage = true;
    } else if (name == "--param" && has_value) {
      options.settings.param_path = take_value();
    } else if (name == "--weights" && has_value) {
      options.settings.weights_path = take_value();
    } else if (name == "--threshold" && has_value) {
      options.settings.confidence_threshold = std::stof(take_value());
    } else if (name == "--threads" && has_value) {
      options.settings.num_threads = std::stoi(take_value());
    } else if (name == "--repeats" && has_value) {
      options.repeats = std::stoi(take_value());
    } else if (name.starts_with('-')) {
      std::cerr << "error: unknown or incomplete option: " << name << "\n";
      return options;
    } else {
      options.frame_paths.push_back(name);
    }
  }

  options.valid = !options.frame_paths.empty() && options.repeats >= 1 &&
                  !options.settings.param_path.empty() && !options.settings.weights_path.empty();
  return options;
}

Measurement measure(const NcnnDetector& detector, const std::vector<Frame>& frames, int repeats) {
  Measurement measurement;

  // One untimed pass, so lazily allocated workspaces are paid for before the
  // stopwatch starts rather than charged to the first frame.
  for (const Frame& frame : frames) {
    (void)detector.detect(frame.view(0));
  }

  std::vector<std::chrono::nanoseconds> per_frame;
  per_frame.reserve(frames.size() * static_cast<std::size_t>(repeats));

  const auto started = std::chrono::steady_clock::now();
  for (int repeat = 0; repeat < repeats; ++repeat) {
    for (const Frame& frame : frames) {
      const auto frame_started = std::chrono::steady_clock::now();
      const auto outcome = detector.detect(frame.view(measurement.processed_frames));
      per_frame.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - frame_started));

      if (outcome.result() == DetectionResult::FOUND) {
        ++measurement.found_frames;
      }
      ++measurement.processed_frames;
    }
  }
  measurement.total = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - started);

  std::ranges::sort(per_frame);
  measurement.median =
      *std::next(per_frame.begin(), static_cast<std::ptrdiff_t>(per_frame.size() / 2));
  measurement.slowest = per_frame.back();
  return measurement;
}

double to_milliseconds(std::chrono::nanoseconds duration) {
  return std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(duration).count();
}

int run(std::span<char*> arguments) {
  const Options options = parse_options(arguments);
  if (options.help) {
    print_usage();
    return 0;
  }
  if (!options.valid) {
    print_usage();
    return 2;
  }

  std::vector<Frame> frames;
  frames.reserve(options.frame_paths.size());
  for (const std::string& path : options.frame_paths) {
    Frame frame = load_ppm(path);
    if (frame.bytes.empty()) {
      return 1;
    }
    frames.push_back(std::move(frame));
  }

  const NcnnDetector::LoadResult loaded = NcnnDetector::load(options.settings);
  if (loaded.status != ModelLoadStatus::OK || loaded.detector == nullptr) {
    std::cerr << "error: the model did not load (status "
              << static_cast<int>(static_cast<std::uint8_t>(loaded.status)) << ")\n";
    return 1;
  }

  const Measurement measurement = measure(*loaded.detector, frames, options.repeats);
  const double total_ms = to_milliseconds(measurement.total);
  const double fps = total_ms > 0.0
                         ? (static_cast<double>(measurement.processed_frames) * 1000.0) / total_ms
                         : 0.0;

  std::cout << "frames            " << measurement.processed_frames << " (" << frames.size()
            << " x " << options.repeats << ", " << capture_width_px << "x" << capture_height_px
            << " RGB888)\n"
            << "threads           " << std::max(options.settings.num_threads, 1) << "\n"
            << "bf16 storage      " << (options.settings.use_bf16_storage ? "on" : "off") << "\n"
            << "threshold         " << options.settings.confidence_threshold << "\n"
            << "frames FOUND      " << measurement.found_frames << "\n"
            << "median inference  " << to_milliseconds(measurement.median) << " ms\n"
            << "slowest inference " << to_milliseconds(measurement.slowest) << " ms\n"
            << "sustained         " << fps << " FPS\n"
            << "peak RSS          " << peak_resident_bytes() / 1024 / 1024 << " MiB\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  // A benchmark may fail; it may not abort through an unhandled exception and
  // leave the operator without a diagnosis.
  try {
    return run(std::span<char*>{argv, static_cast<std::size_t>(argc)});
  } catch (const std::exception& failure) {
    std::cerr << "error: " << failure.what() << "\n";
    return 1;
  } catch (...) {
    std::cerr << "error: unknown failure\n";
    return 1;
  }
}

#pragma once

// Verifies: REQ-DET-004 (support code — turns a version-controlled PPM into
// the packed RGB888 FrameView the detector contract is written in),
// REQ-DEV-001 (no camera is involved), REQ-DEV-002 (the bytes come from git,
// not from a network or a clock).
//
// Binary PPM ("P6") is used because its payload *is* RGB888: a short ASCII
// header followed by the exact byte layout REQ-DET-004 declares. Reading it
// needs no image library, so the fixtures cost no new dependency (AGENTS.md
// rule 12, ADR required otherwise).

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <span>
#include <string>
#include <vector>

#include "pigeon/core/frame.hpp"
#include "pigeon/core/geometry.hpp"

namespace pigeon::test_support {

/// Padding written between rows when a test asks for a stride wider than the
/// pixels. Distinct from any plausible image content, so a reader that ignores
/// `stride_bytes` is caught rather than tolerated (`REQ-DET-004`).
inline constexpr std::uint8_t ppm_row_padding_level = 0x00;

/// A fixture image: the pixel buffer plus the borrowed view `core/` is given
/// of it. The ownership split is the one deployment uses, with a file in git
/// in place of a camera (`REQ-DET-002`).
struct PpmImage {
  std::vector<std::byte> bytes;
  core::ImageSize size{};
  std::size_t stride_bytes{};

  [[nodiscard]] bool loaded() const { return !bytes.empty(); }

  [[nodiscard]] core::FrameView view(std::uint64_t sequence_number = 0) const {
    return core::FrameView{
        .size = size,
        .stride_bytes = stride_bytes,
        .pixels = std::span<const std::byte>{bytes},
        .sequence_number = sequence_number,
    };
  }
};

namespace detail {

/// Read one PPM header token, skipping whitespace and `#` comments.
inline bool read_ppm_token(std::istream& in, std::string& token) {
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

}  // namespace detail

/// Load a binary PPM fixture as a packed RGB888 frame.
///
/// `row_padding_bytes` pads every row so a fixture with
/// `stride > width * 3` can be exercised (`REQ-DET-004`). Returns an image
/// with an empty buffer when the file is missing or is not a `P6` PPM; the
/// caller asserts on `loaded()` rather than being thrown at, so a missing
/// fixture reports itself as a failing test instead of a crash.
[[nodiscard]] inline PpmImage load_ppm(const std::string& path, std::size_t row_padding_bytes = 0) {
  PpmImage image;

  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return image;
  }

  std::string magic;
  std::string width_token;
  std::string height_token;
  std::string max_value_token;
  if (!detail::read_ppm_token(file, magic) || magic != "P6" ||
      !detail::read_ppm_token(file, width_token) || !detail::read_ppm_token(file, height_token) ||
      !detail::read_ppm_token(file, max_value_token) || max_value_token != "255") {
    return image;
  }

  const auto width = static_cast<std::uint32_t>(std::stoul(width_token));
  const auto height = static_cast<std::uint32_t>(std::stoul(height_token));

  const std::size_t row_pixels_bytes = static_cast<std::size_t>(width) * core::bytes_per_pixel;
  const std::size_t stride = row_pixels_bytes + row_padding_bytes;

  std::vector<std::byte> buffer(stride * static_cast<std::size_t>(height),
                                std::byte{ppm_row_padding_level});
  for (std::uint32_t row = 0; row < height; ++row) {
    char* destination =
        reinterpret_cast<char*>(buffer.data() + (static_cast<std::size_t>(row) * stride));
    if (!file.read(destination, static_cast<std::streamsize>(row_pixels_bytes))) {
      return image;
    }
  }

  image.bytes = std::move(buffer);
  image.size = core::ImageSize{.width_px = width, .height_px = height};
  image.stride_bytes = stride;
  return image;
}

}  // namespace pigeon::test_support

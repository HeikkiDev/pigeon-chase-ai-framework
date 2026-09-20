#pragma once

#include "pigeon/core/detection.hpp"
#include "pigeon/core/frame.hpp"

namespace pigeon::core {

/// The seam between image acquisition and the domain (`REQ-DET-002`).
///
/// Everything that knows how to obtain or decode an image lives on the far
/// side of this interface: a model runtime in `raspberry/`, a scripted stub in
/// `tests/`. `core/` only ever holds a `Detector&` and calls `detect`.
///
/// Implementations shall:
/// * be **total** — return a `DetectionOutcome` for every well-formed frame and
///   never throw across this boundary (`REQ-DET-001`, `cpp.instructions.md`);
/// * be **deterministic** — the same frame yields the same outcome, so
///   recorded fixtures produce reproducible runs (`REQ-DEV-002`);
/// * treat an empty or malformed frame as `NONE` rather than as an error,
///   because inaction is the safe response;
/// * not retain the frame's pixel buffer beyond the call.
class Detector {
 public:
  Detector() = default;
  Detector(const Detector&) = delete;
  Detector& operator=(const Detector&) = delete;
  Detector(Detector&&) = delete;
  Detector& operator=(Detector&&) = delete;
  virtual ~Detector() = default;

  /// Classify one frame and report every pigeon found in it.
  ///
  /// `const` because detection is a function of the frame alone: a detector
  /// that accumulates state across frames would make `REQ-DEV-002`
  /// unverifiable. Cross-frame reasoning belongs to `track.hpp`.
  [[nodiscard]] virtual DetectionOutcome detect(const FrameView& frame) const = 0;
};

}  // namespace pigeon::core

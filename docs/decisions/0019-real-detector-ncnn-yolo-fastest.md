# ADR-0019 — A real detector: ncnn and Yolo-Fastest behind `core::Detector`

**Status:** Accepted
**Date:** 2026-10-04
**Affects:** `REQ-DET-001`, `REQ-DET-002`, `REQ-DET-003`, `REQ-DET-004`,
`REQ-DEV-001`, `REQ-DEV-002`, `REQ-DEV-003`, `REQ-AIM-003`; new module
`raspberry/`; `core/include/pigeon/core/detector.hpp` (unchanged)

## Context

Every `Detector` in the repository today is simulated
(`tests/core/support/simulated_detectors.hpp`, driven by
`tests/core/fixtures/rgb888_fixtures.hpp`). Nothing has ever classified a real
image. `REQ-DET-003` — at least 5 FPS on a Raspberry Pi 3B with 1 GB of RAM —
is one of only three `UNVERIFIED` requirements, and it is the one with the
largest chance of being unsatisfiable: if no model runtime reaches 5 FPS on
that hardware, the frame-counted design of ADR-0005 and the four-frame
engagement latency it buys are both invalidated. The maintainer therefore
chose to attack detection before the device protocol (`REQ-COM-001`).

Constraints that bear on the choice:

* **1 GB of RAM, four Cortex-A53 cores, no GPU and no NPU.** The detector
  shares that budget with the capture stack, the application and the OS.
* **`core/` depends on the C++ standard library only** (`REQ-DET-002`,
  `scripts/arch-check.sh`). Whatever runtime is chosen may not be visible from
  `core/` or from the default `core/` test suite.
* **`make check` must stay green on a clean macOS checkout**
  (`REQ-DEV-001`), and CI runs the same script.
* **Tests must be deterministic** (`REQ-DEV-002`): the same frame must produce
  the same outcome, with no network access at test time.
* **Licensing.** Code and model weights enter a repository that is public;
  AGPL-encumbered weights and unlicensed scraped images do not.

Two maintainer rulings frame this decision and are quoted as its source:

1. *"you must investigate what object detection library fits our hardware
   requirements, I dont mind if it is opencv or other."* The choice of runtime
   is delegated to engineering, on evidence.
2. *"Accuracy will depend on how the object detection library scores, not
   important."* **There is no accuracy, precision or recall requirement, and
   none is to be invented.** This is recorded against `REQ-DET-001` in
   `docs/requirements/requirements.md` so that a later agent cannot mistake
   its absence for an oversight.

Four further rulings, given on 2026-10-04 after this ADR was drafted, closed
the questions it had raised and are the maintainer approval under which it was
accepted:

3. *"Model inference only, not the full capture → conversion → inference →
   command pipeline."* (Q21) What `REQ-DET-003` measures; the requirement was
   reworded to say it.
4. *"64-bit Raspberry Pi OS (arm64)."* (Q22) The deployment OS, so the
   upstream arm64 figure transfers.
5. *"Any bird is an acceptable target for now."* (Q23) The COCO `bird` proxy
   is accepted; pigeon-only fine-tuning is not required work.
6. *"640×480."* (Q24) The capture resolution delivered to the detector.

### Evidence

Measured and verified before this ADR was written:

| Runtime + model | Evidence | Verdict |
| --------------- | -------- | ------- |
| **ncnn + Yolo-Fastest-1.1 (COCO)** | Yolo-Fastest README reports `Raspberrypi 3B \| 4xCortex-A53 \| Linux(arm64) \| ncnn \| 62.31ms` — about 16 FPS of raw inference at 320×320, four threads, bf16 storage, **64-bit** Raspberry Pi OS. Model is 17.9 KB of `.param` plus 667 KB of `.bin`. ncnn is BSD-3-Clause (licence file verified); the weights are under the permissive "YOLO LICENSE Version 2". `brew install ncnn` exists (stable 20260526). | **Chosen** |
| TensorFlow Lite / LiteRT + SSD-MobileNet v2 int8 | Apache-2.0, plausible, but no published Pi 3B figure, no Homebrew formula, and an API rename in progress. | Fallback |
| OpenCV DNN + MobileNet-SSD | ≈0.9 FPS on Pi-3-class hardware. Five times short of `REQ-DET-003`. | Rejected |
| Ultralytics YOLOv5n / v8n | 1.6–3.1 FPS on a Pi **4** at 640; AGPL-3.0 weights. | Rejected |
| ONNX Runtime | No official armhf build, no Pi 3B data, open determinism defects. | Rejected |
| NanoDet-Plus | ≈5 FPS on a Pi 4, so below 5 on a Pi 3B. | Rejected |
| MNN, ArmNN | No usable data for this hardware. | Rejected |

A throwaway spike (macOS, Apple Silicon, Python `ncnn` 1.0.20260526, not
committed) ran the chosen model over 105 pigeon photographs from images.cv. It
measured two things that matter here and one that does not:

* **Determinism (matters):** identical output across five runs per image, with
  both one and four threads.
* **Detection rate (matters only as a sanity check, per ruling 2):** the COCO
  `bird` class was reported for 50/105 images at the model's baked-in 0.55
  threshold, 60/105 at 0.40 and 64/105 at 0.25. Misses were typically
  mislabelled `cat`, `person`, `bear` or `sheep`; many of the images are
  close-ups, drawings or cut-outs rather than balcony scenes.
* **Latency on a Mac (does not matter):** 1.7 ms median. Not representative of
  a Cortex-A53.

The spike images carry **no published licence** (images.cv exposes none), so
they stay out of the repository.

## Decision

A real detector is introduced as **`ncnn` running Yolo-Fastest-1.1, in a new
`raspberry/` module, behind the existing `core::Detector` interface, with no
change to any `core/` header.** Nine points settle what that means.

### 1. The dependency: ncnn, statically linked, with the Yolo-Fastest-1.1 COCO model

ncnn (Tencent, BSD-3-Clause) is the only candidate with a *published figure for
this exact board* that clears `REQ-DET-003` with a margin — 62.31 ms of
inference against a 200 ms budget. It has no third-party runtime dependencies,
no protobuf, no BLAS and no Python, which is what keeps it affordable here.

Against the 1 GB budget: the model is **685 KB on disk**; its weights are
loaded once and shared; the working set is dominated by the 320×320×3 input
blob and the layer activations, on the order of **a few tens of megabytes**,
plus one `ncnn::Mat` of about 1.2 MB for the resized input. No per-frame
allocation of the camera buffer occurs at the `core/` boundary, because
`FrameView` is still a borrowed `std::span` (ADR-0007). The alternative
runtimes are all larger and none is faster on this board.

The *measured* footprint on the target is unknown until the hardware benchmark
of point 9 runs. That is stated as a risk, not as a figure.

### 2. Where it lives: a new `raspberry/` module, one-way into `core/`

```text
raspberry/
  include/pigeon/raspberry/ncnn_detector.hpp   public, standard-library only
  src/ncnn_detector.cpp                        the only file that includes <ncnn/...>
  models/yolo-fastest-1.1/                     vendored weights, licence, checksums
```

`AGENTS.md` already reserves `raspberry/` for "camera, model runtime, serial
host side", and `docs/architecture/architecture.md` already grants it a
dependency on `core/`. This decision uses that seam rather than inventing one.

The public header is **standard-library only**: the `ncnn::Net` lives behind a
pimpl, so no translation unit outside `raspberry/src/` ever sees an ncnn
header. `core/` gains nothing, includes nothing and changes not at all;
`tests/` sees `pigeon::raspberry::NcnnDetector`, never `ncnn::`.

`REQ-DET-002` is thereby satisfied structurally rather than by inspection: the
runtime cannot leak into `core/`, because `core/` does not link it.

### 3. The interface: a factory that returns failure as a value

Declarations only; bodies belong to the implementation-engineer.

```cpp
namespace pigeon::raspberry {

/// Everything the detector needs that is not a frame. Calibration, not code
/// (`REQ-AIM-003` by analogy): parsed from the key/value file in `raspberry/`
/// and passed in as a plain value type.
struct NcnnDetectorSettings {
  std::string param_path;          // empty by default: unconfigured
  std::string weights_path;        // empty by default: unconfigured
  float confidence_threshold{2.0F};// above 1.0: detects nothing until configured
  int num_threads{1};              // fixed, never derived from the machine
  bool use_bf16_storage{false};    // off by default; a deployment setting
};

enum class ModelLoadStatus : std::uint8_t {
  UNCONFIGURED, OK, PARAM_NOT_FOUND, WEIGHTS_NOT_FOUND, MALFORMED,
};

class NcnnDetector final : public core::Detector {
 public:
  struct LoadResult {                 // default-constructs to failure
    ModelLoadStatus status{ModelLoadStatus::UNCONFIGURED};
    std::unique_ptr<NcnnDetector> detector;  // null unless status == OK
  };

  [[nodiscard]] static LoadResult load(const NcnnDetectorSettings& settings);

  [[nodiscard]] core::DetectionOutcome detect(
      const core::FrameView& frame) const override;
};

}  // namespace pigeon::raspberry
```

Three properties are deliberate. Loading is the only fallible operation and it
reports failure as a named value, never an exception across a module boundary
(ADR-0007). A `LoadResult` that nobody filled in is a failure, not a working
detector (ADR-0008). And `detect` honours the contract already written on
`core::Detector`: total, `const`, stateless across frames, never throwing, and
treating an empty or malformed frame as `NONE`.

### 4. `bird` is the proxy class, and confidence stops at the seam

The COCO model has no `pigeon` class. The detector reports the COCO `bird`
class — label index 15 in the ncnn `Yolov3DetectionOutput` convention, where
label 0 is background — and discards every other class.

`DetectionOutcome::found(...)` is constructed from the surviving boxes; if
none survive, `DetectionOutcome::none()`. The invariant "`FOUND` if and only if
at least one detection is present" is maintained by the existing type, not by
this module (`REQ-DET-001`).

**Confidence is not propagated.** `core::Detection` has no confidence field and
is not gaining one: ADR-0003 already rejected confidence as a selection key,
and `REQ-TRK-008` orders by bounding-box area. The threshold is applied inside
`raspberry/` and the score is dropped there.

Retraining to a single `pigeon` class later changes the weights file and
possibly the label constant. It does not change one line of this interface.
It is not required work: maintainer ruling, 2026-10-04 (Q23): *"any bird is an
acceptable target for now"*, so the `bird` proxy is accepted as the shipping
behaviour, not as a temporary stand-in waiting on a decision.

### 5. The threshold is calibration data, inert by default

`confidence_threshold` defaults to `2.0F`, which no softmax output can reach,
so a default-constructed `NcnnDetectorSettings` yields a detector that reports
`NONE` for every frame. This is the ADR-0008 pattern — an unconfigured system
is inert *structurally*, not because somebody remembered to validate it —
applied to detection: an uncalibrated rig does not confirm, does not lock and
cannot fire.

The deployment value is uncalibrated and lives in the same version-controlled
key/value file as the rest of the calibration (`REQ-AIM-003`), parsed in
`raspberry/`. The spike's 0.55 / 0.40 / 0.25 figures are inputs to that
calibration, not a decision made here. The model's own baked-in 0.55 threshold
in the `.param` file is a floor; the configured value may only raise it.

### 6. Geometry: stretch to 320×320, map boxes back, clamp to the frame

The frame arrives as packed RGB888 with a row stride (`REQ-DET-004`).
`raspberry/` converts and resizes it to the model's 320×320 input in one step,
honouring `stride_bytes`, normalising by `1/255` with no mean subtraction. The
resize **stretches**; it does not letterbox.

Letterboxing would preserve aspect ratio at the cost of a padding colour, a
second coordinate transform and an off-by-one in the inverse mapping — on the
one path where a coordinate error aims water somewhere nobody authorised.
Stretching makes the inverse mapping a pure scale:

```text
box.top_left_px = { x1 * width_px, y1 * height_px }
box.width_px    = (x2 - x1) * width_px
box.height_px   = (y2 - y1) * height_px
centroid_px     = centre of the box
```

where `x1, y1, x2, y2` are the normalised outputs of
`Yolov3DetectionOutput`, clamped to `[0, 1]` before scaling so that a box
predicted off the edge cannot produce a centroid outside the image and hence an
aim outside the frame's angular extent. Any detection quality lost to the
distorted aspect ratio is accepted under ruling 2.

`core/` performs no colour conversion and no resizing; `REQ-DET-004` already
says the producer converts on its own side.

### 7. Build health: optional locally, required in CI, by the existing precedent

ncnn is **not** vendored and **not** fetched by CMake. The repository already
has a pattern for a tool that is optional locally and mandatory in CI —
`clang-format` and `clang-tidy` in `scripts/check.sh`, gated by
`PIGEON_STRICT_TOOLS`. The same pattern is used:

* CMake probes for ncnn (`find_package(ncnn CONFIG)`).
* **Found:** `raspberry/` and `tests/raspberry/` are configured, built and run
  as part of `make check`.
* **Not found, `PIGEON_STRICT_TOOLS` unset:** both are skipped with a visible
  warning. `make check` stays green on a clean macOS checkout with no ncnn
  installed (`REQ-DEV-001`), and every existing test still runs.
* **Not found, `PIGEON_STRICT_TOOLS=1`:** configuration fails. CI sets that
  variable already, so CI must install ncnn (`brew install ncnn`, alongside
  `ninja` and `llvm`), and CI therefore always builds and runs the real
  detector.

No new preset is added. `macos-debug`, `macos-release` and `macos-asan` all
pick ncnn up automatically when it is installed.

This is the one trade-off in the decision worth naming: a developer without
ncnn runs a strictly smaller suite than CI does. The alternative — making ncnn
a hard build dependency — would break `REQ-DEV-001`'s "clean macOS checkout"
promise for a component that is not on the critical path of most changes.

### 8. The model is vendored, with its licence and a checksum

The 685 KB of `.param` and `.bin` are committed under
`raspberry/models/yolo-fastest-1.1/`, together with:

* `LICENSE` — the verbatim "YOLO LICENSE Version 2" text that covers them;
* `SOURCE.md` — upstream repository, commit or release, and retrieval date;
* `checksums.sha256` — verified by CMake at configure time, so a corrupted or
  swapped model fails the build rather than silently changing behaviour.

Fetching at build time with a pinned hash was rejected: it puts a network
dependency into a build that `REQ-DEV-002` requires to be reproducible, breaks
offline and air-gapped builds, and makes the repository's behaviour depend on a
third-party host staying up. 685 KB is a defensible one-off cost in git for a
file that changes only when the model is retrained.

### 9. Verification is split: behaviour in CI, throughput on hardware

**In the default suite (macOS, no hardware, deterministic):** the real detector
is exercised by `tests/raspberry/`, registered with CTest under the label
`model` so it can be selected or excluded, but **included in the default run**.
It is not hardware-in-the-loop: it opens no device and reads no clock. It
covers the `REQ-DET-001` contract (total, two-valued), the `REQ-DEV-002`
determinism property (the same frame, detected twice, yields an equal
`DetectionOutcome`), the unconfigured-is-inert property of point 5, the
load-failure statuses of point 3, and the geometry mapping of point 6.

**Image fixtures are CC0 or public-domain only.** The spike's images.cv set is
unlicensed and is not committed. Real photographs come from Wikimedia Commons
under CC0 / public domain, converted to RGB888 and committed next to a
`FIXTURES.md` recording source URL, author and licence for each one. CC-BY is
*not* used: an attribution obligation that travels with a test fixture is a
licence obligation on every downstream user of this repository, for no
engineering benefit. Synthetic frames from `rgb888_fixtures.hpp` continue to
cover the structural cases, where no real image is needed.

**On hardware (`REQ-DET-003`, `REQ-DEV-003`):** throughput is measured by a
standalone benchmark executable in `tools/`, built only when ncnn is present,
**never registered with CTest**, so it cannot run in CI by construction. It
loads a recorded sequence of RGB888 frames from disk, runs them through the
same `NcnnDetector` the application uses, and reports the sustained frames per
second together with peak resident set size. The result is recorded in
`docs/architecture/` with the board, OS image, thread count and bf16 setting it
was measured under. `REQ-DET-003` stays `UNVERIFIED` until that run happens on
a real Pi 3B.

The benchmark measures **model inference only** — maintainer ruling,
2026-10-04 (Q21): *"model inference only, not the full capture → conversion →
inference → command pipeline."* `REQ-DET-003` was reworded to say so. The
frames it replays are **640 × 480** RGB888, the capture resolution the camera
delivers (maintainer ruling, 2026-10-04, Q24), so the resize to the model's
320×320 input is measured at the size it will really run at.

The source benchmark that motivated this choice was measured on **64-bit**
Raspberry Pi OS with bf16 storage enabled, and that is now the deployment OS:
maintainer ruling, 2026-10-04 (Q22): *"64-bit Raspberry Pi OS (arm64)."* The
upstream figure therefore transfers to the target platform, and the benchmark
runs on arm64. `docs/architecture/architecture.md` records it.

## Alternatives considered

| Option | Why it was rejected |
| ------ | ------------------- |
| OpenCV DNN + MobileNet-SSD | ≈0.9 FPS on Pi-3-class hardware: five times short of `REQ-DET-003`. OpenCV also drags in a large dependency tree against a 1 GB budget, and `scripts/arch-check.sh` already bans `opencv2/` from `core/` and the default suite. |
| Ultralytics YOLOv5n / YOLOv8n | 1.6–3.1 FPS on a Pi **4** at 640×640, so worse on a 3B; AGPL-3.0 weights are unacceptable for this repository. |
| ONNX Runtime + a nano model | No official armhf build, no published Pi 3B figure, and open determinism defects — directly against `REQ-DEV-002`. |
| NanoDet-Plus | ≈5 FPS on a Pi 4 puts it below 5 FPS on a 3B. No margin at all. |
| TensorFlow Lite / LiteRT + SSD-MobileNet v2 int8 | Credible and permissively licensed, but no Pi 3B figure, no Homebrew formula (so `make check` on macOS gets harder, not easier), and an in-flight API rename. **Retained as the documented fallback** if ncnn fails the hardware benchmark. |
| MNN, ArmNN | No usable data for this board. Choosing on vendor claims is how we ended up needing a spike. |
| Classical CV (background subtraction, blob detection) | No published figures to compare, no class concept at all, and it would make "is that a pigeon" a problem we own rather than one the model owns. Ruling 2 explicitly declines to own detection quality. |
| Put the real detector in `core/` behind `#ifdef` | Violates `REQ-DET-002` and ADR-0001, and reintroduces the untested-code-path problem ADR-0001 already rejected. |
| Vendor ncnn sources into the repository | Tens of megabytes of third-party C++ in git, built from scratch on every clean configure, and a fork we would own. `brew install ncnn` on macOS and a source build on the Pi is cheaper. |
| Fetch the model at build time with a pinned hash | Network dependency in a build that must be reproducible and offline-capable; relies on a third-party host. |
| Commit the spike's images.cv pigeon photographs | No published licence. Unlicensed images do not enter a public repository, whatever their convenience. |
| Add a confidence field to `core::Detection` | ADR-0003 rejected confidence as a selection key. Adding the field would put a model-specific quantity into the hardware-independent domain and invite its use downstream. |
| Make ncnn a hard build dependency | Breaks `REQ-DEV-001`'s promise that a clean macOS checkout builds and tests, for a component most changes never touch. |
| Letterbox the frame to preserve aspect ratio | A second coordinate transform on the aiming path, in exchange for detection quality that ruling 2 says we are not optimising. |

## Consequences

### Positive

* `REQ-DET-003` becomes *measurable*. The highest technical risk in the project
  moves from "unknown" to "one benchmark run away from known".
* `core/` is untouched. The seam designed in ADR-0007 absorbed a real model
  runtime with no interface change, which is the first evidence that the seam
  was cut in the right place.
* The licence story is clean end to end: BSD-3-Clause runtime, permissive
  weights, CC0 fixtures, all recorded in-tree with checksums.
* Detection becomes testable against real images in CI, deterministically,
  with no hardware.
* The fallback (TFLite) is documented, so failing the hardware benchmark is a
  known branch rather than a crisis.

### Negative / accepted trade-offs

* **A developer without ncnn runs a smaller suite than CI.** Mitigated by a
  visible warning and by CI's `PIGEON_STRICT_TOOLS=1`, but it is a real gap
  between "green locally" and "green in CI", and `AGENTS.md` currently promises
  those are the same thing.
* **The Pi 3B figure is not ours.** 62.31 ms is an upstream README claim for
  raw inference at four threads on 64-bit Raspberry Pi OS — which is the
  deployment OS (Q22), so the platform matches. It excludes capture, colour
  conversion, resize, tracking and the rest of the pipeline; under Q21 those
  are outside what `REQ-DET-003` measures, but they still consume real time on
  the rig and remain unmeasured, as does single-thread performance.
* **`bird` is not `pigeon`.** The system will engage seagulls, crows and
  magpies. Accepted by the maintainer (Q23) for now.
* **Determinism is per-binary, not universal.** Identical inputs give identical
  outputs on one machine with fixed settings (which is what `REQ-DEV-002`
  requires), but `use_bf16_storage` and the thread count change numerics, so a
  threshold near a decision boundary could flip between a Mac and a Pi. Tests
  must therefore assert the *contract*, never a specific box coordinate.
* **A model file is now a build input.** Someone will eventually regenerate it
  and forget the checksum; the configure-time check is what turns that into a
  build failure rather than a behaviour change.
* 685 KB of binary data in git, forever.

### Follow-up work

* **Requested of the maintainer** (`scripts/` is maintainer-owned, as with
  defect D3): extend `scripts/arch-check.sh` so that an `ncnn/` include under
  `core/` or `tests/` is a violation, added test-first in
  `tests/scripts/arch_check_test.sh`. Without it, this ADR's central
  containment claim is prose.
* Add `brew install ncnn` to the CI toolchain step, so `PIGEON_STRICT_TOOLS=1`
  configures the module rather than failing.
* Implementation-engineer: `raspberry/ncnn_detector.{hpp,cpp}`, the CMake
  probe, the vendored model bundle and the `tools/` benchmark.
* Test-engineer: `tests/raspberry/`, the CC0 fixture set and `FIXTURES.md`.
* Run the benchmark on a Pi 3B and record the result; if it misses 5 FPS, the
  fallback is TFLite + SSD-MobileNet v2 int8, and that is a new ADR.
* Fine-tuning to a single `pigeon` class is an **optional improvement**, not
  required work (Q23), and costs no interface change if it is ever done.

## Verification

* **Containment (`REQ-DET-002`):** `scripts/arch-check.sh` already fails on a
  hardware or platform header under `core/` or `tests/`; the requested `ncnn/`
  rule above extends it to this runtime. `scripts/header-check.sh` keeps every
  `core/` public header self-contained, and `core/` does not link ncnn, so an
  accidental dependency fails to link rather than fails review.
* **Hardware-free build (`REQ-DEV-001`):** `scripts/check.sh` passes on a clean
  macOS checkout with ncnn absent, because the module is skipped; and with ncnn
  present, because the module builds and its tests pass. Both states are part
  of the contract.
* **CI really builds it:** CI exports `PIGEON_STRICT_TOOLS=1`, under which a
  missing ncnn is a configure failure. The absence of the real detector from a
  CI run is therefore not possible silently.
* **Determinism (`REQ-DEV-002`):** a test in `tests/raspberry/` detects the
  same fixture frame twice and asserts the two `DetectionOutcome`s are equal;
  `scripts/check.sh`'s determinism step reruns the suite shuffled.
* **Inert by default (ADR-0008):** a test constructs `NcnnDetectorSettings{}`,
  loads the real model with it, and asserts `NONE` for a frame that the
  calibrated detector reports `FOUND` for.
* **Model integrity:** the configure-time `checksums.sha256` comparison fails
  the build if either model file changes without the checksum changing with it.
* **Throughput (`REQ-DET-003`):** the `tools/` benchmark on a Pi 3B running
  64-bit Raspberry Pi OS (Q22), replaying 640×480 frames (Q24) and timing
  model inference only (Q21), outside
  CTest and outside CI (`REQ-DEV-003`). Until it runs, `scripts/trace.sh`
  continues to report `REQ-DET-003` as `UNVERIFIED`, which is the honest
  status.

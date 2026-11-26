# ADR-0020 — CI also runs the gate on 64-bit Linux (arm64)

**Status:** Proposed
**Date:** 2026-11-26
**Affects:** `.github/workflows/ci.yml`; `REQ-DEV-001`, `REQ-DET-003`
(deployment platform); ADR-0002, ADR-0017, ADR-0019

## Context

The deployment target is a Raspberry Pi 3B running **64-bit Raspberry Pi OS
(arm64)** (`REQ-DET-003`, ADR-0019): Linux, glibc, and in practice GCC.
CI runs only on `macos-latest`: Apple clang, libc++, the BSD userland and
bash 3.2.

That gap has already cost real defects, each found only by a manual Linux run
long after it was written:

* Three GCC/glibc-only build failures on `real-detector`:
  `-Werror=comment` and `-Werror=range-loop-construct` in the detector tests
  and benchmark, and a clang-tidy `cppcoreguidelines-pro-type-union-access`
  finding on glibc's `rusage` (fixed in `2d65861`).
* A gate-script defect: off macOS, `scripts/check.sh` passed clang-tidy an
  empty argument, so the gate failed on Linux whatever the code (fixed in
  PR #6, guarded by `gate.check_tidy_test` since PR #7).

None of these can be seen on macOS. The next one will not be either, and it
will be found on the Pi, which is the most expensive place to find it.

Constraints that bear on the choice:

* **`scripts/check.sh` stays the single entry point** (ADR-0002). A Linux job
  must run the same script, not a Linux-specific variant.
* **ncnn is mandatory under `PIGEON_STRICT_TOOLS=1`** (ADR-0019). Ubuntu 24.04
  has no ncnn package, so a Linux job must build it from source. A job that
  silently skipped `raspberry/` would prove nothing about the detector.
* **Toolchain versions are reported, not pinned** (ADR-0017).
* **No new third-party dependency** enters the product: everything below is CI
  tooling, and ncnn is already accepted by ADR-0019.

## Decision

Add one job, `check (Linux arm64)`, to `.github/workflows/ci.yml`. It runs on
GitHub's hosted `ubuntu-24.04-arm` runner and executes exactly what the macOS
`check` job executes: `PIGEON_STRICT_TOOLS=1 scripts/check.sh`, with the same
`PIGEON_COMMIT_RANGE`.

* **Compiler:** the runner's default GCC (13 on Ubuntu 24.04), since that is
  the compiler family of the deployment OS.
* **ncnn:** built from source at the tag ADR-0019 uses (`20260526`), Vulkan,
  tools, examples, benchmarks and Python bindings off, installed to a prefix
  that is cached by tag. A cache hit skips the build.
* **clang-format / clang-tidy:** installed from PyPI (`clang-format`,
  `clang-tidy` wheels) into a venv put first on `PATH`. They are not pinned,
  matching the unpinned Homebrew `llvm` on the macOS job (ADR-0017); the gate
  prints the version it used.
* **Presets:** the existing `macos-debug` preset is used unchanged. It sets no
  platform-specific flags and configures on Linux as is; renaming the presets
  is a separate, optional change.

The macOS jobs are unchanged.

## Alternatives considered

| Option | Why it was rejected |
| ------ | ------------------- |
| Keep macOS-only CI and rely on manual Linux runs | That is the current practice, and it is how the defects above reached a reviewed branch. |
| `ubuntu-24.04` (x86_64) runner | Catches GCC and glibc issues but not arm64 ones: ncnn's NEON code paths, `char` signedness, alignment. The arm64 runner is equally free for a public repository and is the deployment architecture. |
| Cross-compile for arm64 and run tests under QEMU | Slower, more moving parts, and no closer to the target than a native arm64 runner. |
| Run in a Raspberry Pi OS container image | Closer to the target userland, but adds a container image as a CI dependency for little gain over Ubuntu arm64 + GCC; can be revisited if a Pi-OS-specific defect appears. |
| Install ncnn from a prebuilt release archive | Avoids the build, but the archives' CMake package and OpenMP linkage differ from a source build, and an unverified binary enters CI. Building from source is ~5 min once, then cached. |
| Install LLVM from apt.llvm.org | Adds a third-party apt repository and key to CI; PyPI wheels give the same tools with no repository changes. |
| Also run ASan/UBSan on Linux | Useful, but doubles Linux CI time. Deferred until the first job has proven stable (follow-up). |

## Consequences

### Positive

* GCC-, glibc- and arm64-only defects fail the pull request that introduces
  them, instead of being found on the device.
* The detector (`raspberry/`) and its tests are exercised on the deployment
  architecture on every pull request.
* The gate scripts are exercised under GNU coreutils and bash 5 as well as BSD
  and bash 3.2.

### Negative / accepted trade-offs

* CI takes longer in wall-clock terms only if the Linux job is slower than the
  macOS one; the jobs run in parallel. A cold ncnn cache adds a few minutes.
* Two unpinned LLVM sources (Homebrew and PyPI) can briefly report different
  patch versions; ADR-0017 already accepts that and the gate prints both.
* The job depends on GitHub's arm64 hosted runners remaining free for public
  repositories.

### Follow-up work

* Decide whether `check (Linux arm64)` becomes a required status check on
  `main` (repository setting, maintainer's call).
* Optionally add a Linux ASan/UBSan job once this one is stable.
* Optionally rename the `macos-*` presets to platform-neutral names.

## Verification

The `check (Linux arm64)` job in `.github/workflows/ci.yml`. Its log must show:

* `ncnn found (...)` at configure time, and compilation of
  `raspberry/src/ncnn_detector.cpp`, `tools/detector_benchmark.cpp` and the
  `tests/raspberry/ncnn_detector_*` tests;
* the same test count as the macOS job, including the `NcnnDetector*` and
  `gate.*` tests;
* `All checks passed.`

Because `PIGEON_STRICT_TOOLS=1` makes ncnn, clang-format and clang-tidy
mandatory, the job cannot pass by silently skipping any of them.

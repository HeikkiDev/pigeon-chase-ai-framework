# ADR-0022 — clang-format and clang-tidy are pinned in one file the gate checks

**Status:** Accepted
**Date:** 2026-11-26
**Affects:** `scripts/check.sh`, `scripts/lint-tools.txt`, `.github/workflows/ci.yml`,
`AGENTS.md`. Supersedes the **Versions** decision of ADR-0017 and the "not
pinned" clause of ADR-0020. Infrastructure only: no requirement is added or
changed.

## Context

ADR-0017 chose to report the `clang-format` and `clang-tidy` versions rather
than pin them, and recorded when that would change: "Pinning remains available if
a real cross-machine disagreement is ever observed; this ADR records that none
has been." One has now been observed, three times, in a single pull request
(PR #11):

* CI installed the latest `clang-format` (23.1.5). A gate that passed locally on
  18.1.3 failed on it: the two disagree about a block of one statement
  (`{ const SerialActuatorLink link{device, clock}; }`), and no layout of it
  satisfies both.
* CI installed the latest `clang-tidy` (23.1.4). The local 18.1.3 had not flagged
  a positional aggregate (`modernize-use-designated-initializers`) or two reads
  of an `optional` with no visible check (`bugprone-unchecked-optional-access`).
* The two CI jobs did not agree with each other either: Homebrew's `llvm` on
  macOS, PyPI wheels on Linux.

Each failure was invisible to a developer's `make check` and fatal in CI, which is
the opposite of what the gate exists for (ADR-0002: green locally means green in
CI). Printing the version, as ADR-0017 did, did not help: the version was
printed, and nobody was comparing it with anything.

ADR-0017's reason for deferring still deserves an answer. Pinning "fails
outright on any machine that cannot install that exact build, which on Homebrew
means every machine a few months after the pin is written." That is true of
Homebrew, which keeps only the current formula. It is not true of PyPI, which
keeps every release of the `clang-format` and `clang-tidy` wheels, so an exact
version can be installed on any machine with Python.

## Decision

* **`scripts/lint-tools.txt` is the single source of truth.** One exact version
  of each tool, in pip requirements syntax: `clang-format==23.1.5` and
  `clang-tidy==23.1.4`, the versions CI used when PR #11's failures were
  reproduced and cleared.
* **Both CI jobs install from it**, with `pip install -r scripts/lint-tools.txt`
  into a venv put first on `PATH`. The macOS job no longer installs Homebrew
  `llvm` (Apple clang still compiles; only the linters change), so the two jobs
  cannot drift apart.
* **`scripts/check.sh` compares the tools it finds with the file.** A match is
  silent. A difference is a **warning** with the install command, and a
  **failure under `PIGEON_STRICT_TOOLS=1`**, which CI sets. A developer on another
  version can keep working and is told exactly what differs; CI can never run a
  tool it was not meant to. The comparison is covered by
  `tests/scripts/check_tool_versions_test.sh`.
* **Changing a version is a normal change:** edit the file, run
  `scripts/check.sh --fix`, and commit the reformatting and any lint fixes with
  it.

If `scripts/lint-tools.txt` is missing, `check.sh` warns and checks nothing; it
does not fail, so a sandbox that copies `check.sh` alone, as the gate's own tests
do, is not broken by it.

## Alternatives considered

| Option | Why it was rejected |
| ------ | ------------------- |
| Keep reporting the version only (ADR-0017) | Three failures in one PR, each found only in CI. |
| Pin in the workflow only, document the versions | A local run stays silent about a mismatch, which is how the failures got through. |
| Pin Homebrew `llvm` on macOS | Homebrew keeps one version per formula; a pin there is stale within months, the failure ADR-0017 predicted. |
| Pin the Ubuntu apt packages (18.x) | Moves the repository back from the checks CI already enforces, and the two OSes would still differ. |
| Fail on any mismatch, even off CI | Stops a contributor on another version from doing anything, including the `make fast` loop; a warning that names the fix costs less. |
| A container image for every run | A much heavier tool for a two-line problem. |

## Consequences

### Positive

* A local gate and CI use the same `clang-format` and `clang-tidy`, or say so
  plainly when they do not.
* The `ok:` line's version string is now the same everywhere the pin is
  followed, so pull request descriptions quoting it agree.
* Upgrading is a deliberate, reviewable commit, not something that happens to a
  pull request on the day a wheel is released.

### Negative / accepted trade-offs

* A contributor must install two pinned wheels in a venv (the commands are in
  `AGENTS.md` and in the pin file) instead of using Homebrew `llvm`. Without them
  the gate warns locally and skips nothing.
* Moving to a newer version is now a chore someone must do. Lint findings that a
  new version adds land in that change, not in whichever pull request happened to
  be open.
* The macOS job's linters change source. The first CI run on macOS is the
  evidence that the PyPI wheels work there (SDK path, system headers); if they do
  not, the fix is forward, in this ADR's scope.
* The pin covers the tools' versions, not their configuration: `.clang-format` and
  `.clang-tidy` remain the shared rules.

## Verification

* `tests/scripts/check_tool_versions_test.sh`: a match is silent; a mismatch of
  either tool warns off strict tools and fails under them, names the pinned
  version and gives the install command; and the repository's own pin file names
  both tools at exact versions.
* The `check (macOS)` and `check (Linux arm64)` jobs print the versions they
  use (`Show toolchain`) and run with `PIGEON_STRICT_TOOLS=1`, so a drift between
  the workflow and the pin fails the job.

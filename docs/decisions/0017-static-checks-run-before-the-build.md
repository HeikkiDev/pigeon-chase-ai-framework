# ADR-0017 — Static checks run before the build, and toolchain versions are reported rather than pinned

**Status:** Accepted
**Date:** 2026-09-20
**Affects:** `scripts/check.sh`, ADR-0002

## Context

`scripts/check.sh` ran its steps in this order:

```text
configure, build, test, arch, determinism, traceability, workflow, format, tidy
```

Formatting was ninth of nine. It depends on nothing the build produces — it
reads the files git already tracks — so its position was arbitrary, and the
arbitrariness had a cost that went unnoticed until it was large.

While the `core/` acceptance suite was red, the build failed at link. The gate
exited there, on the second step. The formatting step was never reached on any
run, by anyone, for the entire time the suite was red. **97 formatting
violations accumulated behind the failing build**, and were discovered only
when reaching green finally let the gate walk that far — at which point they
appeared as a 14-file diff attributable to nobody in particular.

This is a specific instance of a general defect. The gate is described in
`AGENTS.md` as "the definition of healthy", but a check placed after the build
can only describe a repository that already builds. For a repository that does
not, it reports nothing at all, while still exiting non-zero — so the gate
looks like it is working.

The incident was initially diagnosed, in the pull request that surfaced it, as
a consequence of not pinning the `clang-format` version. That diagnosis is
wrong, and worth correcting explicitly: no version of `clang-format` was ever
executed on those files. A pinned version would have changed nothing.

## Decision

**Ordering.** Checks that do not depend on build output run before the build.
`check.sh` now runs:

```text
format, configure, build, test, arch, determinism, traceability, workflow, tidy
```

`clang-tidy` stays last: it genuinely requires `compile_commands.json`, so it
cannot move.

The ordering rule, stated so the next step added to the gate has an answer:
**a step runs as early as its inputs allow.** A step that needs only the source
tree runs before configure; a step that needs the build runs after it. Position
in the gate is determined by dependency, not by taste.

**Versions.** `clang-format` and `clang-tidy` are **not pinned**, and the
version actually used is **printed** in the step's output instead:

```text
ok: 39 file(s) correctly formatted (Homebrew clang-format version 23.1.1)
```

Pinning was considered and deferred. It trades one failure mode for another:
an unpinned toolchain can disagree between two machines, but a pinned one
fails outright on any machine that cannot install that exact build, which on
Homebrew means every machine a few months after the pin is written. Reporting
the version makes a disagreement diagnosable in one line of existing output,
which is the part that was actually missing — the 97 violations were invisible,
not misattributed. Pinning remains available if a real cross-machine
disagreement is ever observed; this ADR records that none has been.

## Alternatives considered

| Option | Why it was rejected |
| ------ | ------------------- |
| Leave the order, pin the toolchain version | Addresses a cause that was not operating. The violations were never examined by any version of the tool, so the pin would not have surfaced one of them. Fixes the wrong thing and leaves the hiding place open. |
| Leave the order, run the gate with `--skip-tests` periodically | Relies on someone remembering to run a second, different command during exactly the period when the first one is failing. A check that depends on discipline during a crisis is not a check. |
| Run every step unconditionally and report all failures at the end | Attractive, and genuinely better for some gates, but the later steps are not meaningful against a tree that does not build: clang-tidy has no compilation database, and the determinism check has no tests to shuffle. It would trade silent omission for a wall of spurious failures. |
| Run formatting after configure but before build | No benefit over running it first — configure is not one of its inputs — and it leaves the step downstream of something that can fail. |
| Pin the version *and* print it | The printing is what recovers the lost information; the pin adds an install-time failure mode for a disagreement nobody has observed. Revisit when one is. |

## Consequences

### Positive

* A formatting violation is now reported against a repository that does not
  build, which is exactly the repository most likely to be accumulating them.
* Feedback on the cheapest check arrives first, in about a second, rather than
  after a full configure, build and test cycle.
* The gate's claim to be "the definition of healthy" is no longer conditional
  on the subject already being healthy.
* Toolchain disagreements are visible in the output of every run, including
  CI's, without anyone having to reproduce them.

### Negative / accepted trade-offs

* A developer with a formatting slip now sees that before their test failures,
  which is a worse experience mid-refactor. `make fast` and `--skip-format`
  both exist for the inner loop, and `make fix` resolves it in one command.
* The version string in the `ok:` line makes that output machine-dependent, so
  the gate's output can no longer be compared verbatim between machines. Pull
  request descriptions quoting it will differ in that one line.
* Versions remain unpinned, so a genuine cross-machine disagreement is still
  possible. It is now diagnosable rather than prevented, which is a deliberate
  and reversible choice.

## Verification

* `tests/scripts/` covers the gate scripts that can be exercised against a
  throwaway tree. `check.sh` orchestrates real builds, so its ordering was
  verified directly: a deliberate compile error was planted in
  `core/src/geometry.cpp` and the gate run. The formatting step reported before
  the build failed —

  ```text
  ==> Checking formatting
      ok: 39 file(s) correctly formatted (Homebrew clang-format version 23.1.1)

  ==> Configuring (macos-debug)
      ok: configured in build/macos-debug

  ==> Building (macos-debug)
  FAILED: core/CMakeFiles/pigeon_core.dir/src/geometry.cpp.o
  ```

  — which under the previous ordering produced no formatting output at all.
  The planted error was then reverted.
* The step order is documented in the `check.sh` header, which is also its
  `--help` output, so the two cannot drift apart.

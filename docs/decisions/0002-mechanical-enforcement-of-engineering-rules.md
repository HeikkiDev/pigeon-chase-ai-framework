# ADR-0002 — Mechanical enforcement of the engineering rules

**Status:** Accepted
**Date:** 2026-09-15
**Affects:** `REQ-DET-002`, `REQ-DEV-001`, `REQ-DEV-002`, `REQ-DEV-003`, the
agent workflow, CI

## Context

The repository stated strong rules and enforced almost none of them.

* `core/` must contain no hardware, platform or I/O dependency. Nothing checked
  this. The rule survived only because `core/` contained one file.
* The test suite must be deterministic (`REQ-DEV-002`), whose acceptance
  criterion is "repeated runs, including shuffled runs, produce identical
  results". Nothing ever ran the suite shuffled or repeated.
* `scripts/trace.sh` failed only on requirements marked `Implemented`. Every
  requirement is `Draft`, so the traceability gate could not fail for any
  reason. It also never checked the reverse direction, so a typo in a
  `// Verifies:` comment produced a test that traced to nothing while looking
  like it traced to something.
* The Definition of Done required `scripts/trace.sh`, but `make check` — "the
  one command", the thing CI runs — did not invoke it.
* `.github/agents/README.md` now mandates acceptance-test-first development and
  claims red-before-green is "mechanically checkable" from the commit history.
  Nothing checked it.

The common failure mode is the same in every case: a rule that is documented
but unenforced degrades into a rule that is cited but not followed. For human
teams this is a slow decay. For agents it is immediate, because an agent's
compliance is a function of what the feedback loop rejects, not of what the
documentation says. An agent can assert "I kept `core/` hardware-free" at zero
cost, and the assertion is indistinguishable from the truth until something
breaks.

The prime directive of this repository is that evidence, not code, is the
deliverable. A rule nothing can check produces no evidence.

## Decision

Every rule that can be checked mechanically is checked mechanically, by a
script, inside `make check`, and each of those scripts is itself covered by
tests that prove it rejects the violation it claims to reject.

Three new or rewritten gates:

| Script                  | Enforces                                                                                   |
| ----------------------- | ------------------------------------------------------------------------------------------ |
| `scripts/arch-check.sh` | No hardware, platform or I/O headers and no non-deterministic calls in `core/`; no hardware header or device path in the default suite (`REQ-DET-002`, `REQ-DEV-001`, `REQ-DEV-002`, `REQ-DEV-003`) |
| `scripts/trace.sh`      | Bidirectional traceability: `Approved` and `Implemented` requirements need a verifying test, and tests may not cite requirements that do not exist |
| `scripts/tdd-check.sh`  | Conventional Commits, requirement references, and red-before-green commit shape             |

`scripts/check.sh` now runs, in order: configure, build, test, architectural
rules, determinism, traceability, workflow evidence, formatting, clang-tidy.
CI runs that same script, so the Definition of Done and "the one command" can
no longer disagree.

Two supporting decisions:

* **The gates are tested.** `tests/scripts/` plants a real violation into a
  throwaway tree for every rule and asserts the script fails on it. An
  enforcement script that has never been observed to reject anything is exactly
  the kind of unproven claim this repository exists to prevent.
* **Enforcement has an adoption baseline.** `.github/workflow-baseline` names
  the commit at which the workflow rules begin to apply; earlier history is out
  of scope. Without it the gate would be unadoptable in a repository that
  already has commits.

### Deliberate non-rules

* `<chrono>` and `<random>` remain legal in `core/`. The ban is on reading the
  wall clock and on unseeded randomness, not on the vocabulary types for
  durations and generators. A bounded fire duration (`REQ-SAF-001`) needs
  `std::chrono::milliseconds`, and the testing conventions explicitly endorse
  explicitly seeded engines.
* `refactor`, `docs`, `build`, `ci` and `chore` commits do not require a
  preceding failing test. A refactor changes no behaviour and is covered by the
  tests that already exist; demanding a new red test would be theatre.
* A `Red: <sha>` trailer lets a commit cite a specification committed outside
  the range under inspection. The cited commit must itself have changed
  `tests/`.

## Alternatives considered

| Option                                                   | Why it was rejected                                                                                 |
| -------------------------------------------------------- | ---------------------------------------------------------------------------------------------------- |
| Keep the rules as documentation and rely on code review   | This is the status quo that produced the gap. It also makes the reviewer the enforcement mechanism, which does not scale and is exactly the judgement an agent reviewer is worst at. |
| Enforce only in CI, not in `make check`                   | Agents would discover violations after handing off rather than before, and local green would stop meaning CI green. |
| Use clang-tidy alone for the `core/` purity rules         | clang-tidy has no concept of "this directory may not depend on that one", and it is optional locally. |
| Trust the agent's report that it wrote the test first     | Unverifiable narration. The entire point of the change is that self-reporting is not evidence.        |
| Write the gate scripts without tests                      | Would reproduce the original defect one level up: an unproven checker verifying an unproven codebase. |

## Consequences

### Positive

* The rules now fail builds instead of accumulating as review comments.
* `make check` is genuinely the single definition of healthy.
* The determinism requirement is actually exercised rather than asserted.
* Traceability catches typos and unverified commitments, in both directions.
* Red-before-green is a property of the history, checkable by anyone, including
  a reviewer who does not trust the author.

### Negative / accepted trade-offs

* The gate is slower: the suite runs three times (once plain, twice under
  shuffling and repetition).
* The commit-shape rules constrain how work is committed, and will occasionally
  be wrong — hence the `Red:` trailer and the baseline file.
* `arch-check.sh` is grep-based. It sees text, not semantics: it cannot detect a
  hardware dependency introduced through a macro or a transitively included
  header. It is a tripwire, not a proof.
* Shell is a poor language for this. It was chosen because it adds no
  dependency to a 1 GB-RAM deployment target and runs identically in CI and on
  a developer's machine.

### Follow-up work

* Mutation testing, to prove tests constrain behaviour independently of the
  order in which they were written.
* Coverage measurement per acceptance criterion, once product code exists.
* Acceptance criteria may need individual IDs (`REQ-TRK-002.a`); today a
  requirement with four criteria is "verified" by one assertion.

## Verification

`tests/scripts/` covers all three gates and is part of the default suite:

```text
    Start 1: BuildInfo.ReportsANonEmptyVersion
1/4 Test #1: BuildInfo.ReportsANonEmptyVersion ...   Passed
    Start 2: gate.arch_check_test
2/4 Test #2: gate.arch_check_test ................   Passed
    Start 3: gate.trace_test
3/4 Test #3: gate.trace_test .....................   Passed
    Start 4: gate.tdd_check_test
4/4 Test #4: gate.tdd_check_test .................   Passed

100% tests passed out of 4
```

Each of those suites asserts both directions: that a clean tree passes, and
that a tree containing a planted violation fails.

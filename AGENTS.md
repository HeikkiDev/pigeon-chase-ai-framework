# AGENTS.md — Operating Manual

**Read this before doing anything in this repository.**

This file describes **how to work here**. It does not describe the product.
For what the system does, read:

| Question                        | Document                             |
| ------------------------------- | ------------------------------------ |
| What must the system do?        | `docs/requirements/requirements.md`  |
| How is the system structured?   | `docs/architecture/architecture.md`  |
| What does this term mean?       | `docs/glossary.md`                   |
| Why was this decided?           | `docs/decisions/`                    |
| Who does what?                  | `.github/agents/README.md`           |

## Prime directive

> Producing code is not the goal. Producing **evidence that the code is
> correct** is the goal.

An agent's output is not a diff. It is a diff **plus** the command output that
proves it works, **plus** the requirement IDs it satisfies.

## The one command

```bash
make check          # or: scripts/check.sh
```

This configures, builds, runs the tests, and then enforces the architectural
rules, test determinism, requirement traceability and the test-first commit
shape, before checking formatting and running clang-tidy. **It is the
definition of "healthy".** CI runs this exact script, so green locally means
green in CI.

Every one of those gates is itself covered by tests in `tests/scripts/`, which
plant a real violation and assert the gate rejects it. See ADR-0002.

| Command                     | Use                                                |
| --------------------------- | -------------------------------------------------- |
| `make check`                | Full gate. Required before claiming any task done.  |
| `make fast`                 | Build + tests only. Inner development loop.         |
| `make asan`                 | Full gate under AddressSanitizer and UBSan.         |
| `make arch`                 | `core/` purity and hardware-free test suite.        |
| `make trace`                | Requirement → test traceability matrix.             |
| `make workflow`             | Red-before-green evidence in the commit history.    |
| `make fix`                  | Reformat sources with clang-format.                 |
| `make clean`                | Delete `build/`.                                    |
| `ctest --preset macos-debug -R <regex>` | Run a subset of tests.                  |

First-time setup: `brew install cmake ninja llvm`. GoogleTest is fetched
automatically by CMake; do not vendor or install it manually.

If `clang-format` or `clang-tidy` are missing, `check.sh` warns and skips
those steps locally, but CI runs with `PIGEON_STRICT_TOOLS=1` and will fail.
Install them.

## Repository layout

```text
core/                 Hardware-independent domain logic. No hardware headers. Ever.
  include/pigeon/core/  Public headers
  src/                  Implementation
raspberry/            Raspberry Pi integration (future). Camera, serial host side.
arduino/              Arduino firmware (future). Servos, water actuator.
tests/                GoogleTest suites, mirroring the source tree.
  scripts/              Tests for the gate scripts themselves.
cmake/                Build helper modules.
scripts/              check.sh (the gate), arch-check.sh, trace.sh, tdd-check.sh.
docs/
  requirements/       Authoritative specification, REQ-* IDs.
  architecture/       System structure and boundaries.
  decisions/          ADRs.
  glossary.md         Shared vocabulary.
.github/
  agents/             Agent role definitions.
  instructions/       Path-scoped coding conventions.
  workflows/          CI.
```

The dependency direction is strict and one-way:

```text
raspberry/ ──▶ core/ ◀── tests/
arduino/   (separate firmware target)
```

`core/` depends on nothing but the C++ standard library.

## Definition of Done

A task is done only when **every** box is ticked:

* [ ] The change is traceable to one or more `REQ-*` IDs, or is explicitly
      labelled as infrastructure.
* [ ] New or changed behaviour is covered by a test that names its `REQ-*` ID
      in a comment.
* [ ] `make check` passes, and its output is included in the response or PR.
* [ ] `scripts/trace.sh` passes.
* [ ] No new compiler warnings (the build is `-Werror`).
* [ ] Hardware independence is preserved: `core/` still builds and tests with
      no hardware present.
* [ ] Any architectural decision is recorded as an ADR in `docs/decisions/`.
* [ ] Assumptions made are stated explicitly in the response.

**Never claim a task is complete without having actually run `make check`.**
Reporting an unverified result is the single worst failure mode in this
repository.

## Rules for agents

### Evidence

1. Run commands; do not predict their output.
2. Quote real output. Never fabricate, abbreviate misleadingly, or reconstruct
   from memory.
3. If a command fails, report the failure. Do not silently work around it.
4. State confidence honestly. "I believe" and "I verified" are different
   claims.

### Requirements

5. Reference `REQ-*` IDs in commits, PRs, and test comments.
6. **Do not invent requirements.** If behaviour is unspecified, add an entry to
   the Open Questions table in `docs/requirements/requirements.md` and stop.
7. Changing an approved requirement requires an ADR and maintainer approval.

### Code

8. Do not invent APIs. Read the header before calling into it.
9. Do not reference files, functions or targets without confirming they exist.
10. Keep `core/` free of hardware, camera, GPIO, serial and platform headers.
11. Prefer interfaces and dependency injection at every hardware boundary.
12. No new third-party dependency without an ADR. Justify it against the
    Raspberry Pi 3B's 1 GB RAM budget.
13. Make surgical changes. Do not reformat, rename or "tidy" unrelated code.

### Tests

14. Never weaken, skip, `DISABLED_`, or delete a test to make a build pass.
15. Never modify production code solely to make a test pass without
    understanding the failure.
16. Tests must be deterministic: no wall clock, no unseeded randomness, no
    network, no dependence on test ordering (`REQ-DEV-002`).
17. Hardware-in-the-loop tests stay out of the default suite (`REQ-DEV-003`).

### Scope

18. Do the task asked. Raise adjacent problems; do not fix them unprompted.
19. When stuck after two genuine attempts, stop and ask rather than guessing.
20. Do not create planning or summary markdown files unless asked.

## Escalate instead of guessing

Stop and ask the maintainer when:

* A requirement is ambiguous or contradicts another requirement.
* The task needs an answer from the Open Questions table.
* A change would break hardware independence.
* A change would alter a documented interface or the device protocol.
* A safety requirement (`REQ-SAF-*`) is affected.
* A new dependency seems necessary.

## Commits and pull requests

Conventional Commits, with requirement references in the body:

```text
feat(core): confirm targets after three consecutive detections

Implements the confirmation counter and the SEARCHING -> TARGET_LOCKED
transition.

Refs: REQ-TRK-002, REQ-TRK-003
```

Types: `feat`, `fix`, `test`, `docs`, `refactor`, `build`, `ci`, `chore`.
Scopes: `core`, `raspberry`, `arduino`, `tests`, `docs`, `ci`, `agents`.

Every PR description must contain:

1. What changed and why.
2. The `REQ-*` IDs addressed.
3. The `make check` output.
4. Assumptions made and open questions raised.

## Current phase

Hardware integration is a **future** phase. Everything must build, run and be
tested on macOS with no Raspberry Pi, Arduino, camera, servo, water actuator or
GPIO present (`REQ-DEV-001`). Hardware-dependent behaviour is exercised through
simulated implementations behind interfaces.

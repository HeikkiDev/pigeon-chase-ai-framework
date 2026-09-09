---
applyTo: "**/tests/**/*.cpp,**/tests/**/*.hpp"
---

# Testing conventions

Tests are the evidence this project runs on. Treat them as the most important
code in the repository.

## Framework and structure

- GoogleTest and GoogleMock, fetched by CMake. Do not install or vendor them.
- Register every test target with `pigeon_add_test()` in the relevant
  `tests/*/CMakeLists.txt`.
- Mirror the source layout: `core/src/target_state.cpp` →
  `tests/core/target_state_test.cpp`.
- Wrap test bodies in an anonymous namespace.
- Name tests `TEST(SubjectUnderTest, BehaviourBeingVerified)` — describe the
  behaviour, not the function name.

## Requirement traceability — mandatory

Every test file (or individual test, where they differ) starts with a comment
naming the requirements it verifies:

```cpp
// Verifies: REQ-TRK-002, REQ-TRK-003
```

`scripts/trace.sh` parses these. A test with no requirement reference must say
why, e.g. `// Verifies: build harness only (no product requirement).`

## Determinism — mandatory (`REQ-DEV-002`)

Forbidden in the default suite:

- Wall-clock reads, `sleep`, timeouts, or timing-sensitive assertions.
- Unseeded randomness. Seed explicitly and record the seed.
- Network or filesystem access outside version-controlled fixtures.
- Dependence on test execution order, or shared mutable state between tests.
- Any real hardware: camera, serial port, GPIO (`REQ-DEV-003`).

Inject a fake clock and a simulated hardware implementation instead.

## Writing good tests here

- Test observable behaviour, not implementation details. Do not assert on
  private state.
- One behaviour per test. Prefer several small tests to one long one.
- Cover, for each requirement's Acceptance criteria: the nominal case, the
  boundaries, the reset/interleaving cases, and invalid input.
- For `REQ-SAF-*`, explicitly test the cases where the system must **refuse**
  to act. "No fire command was emitted" is as important an assertion as any.
- Prefer table-driven tests (`TEST_P` / value-parameterised) for frame
  sequences through the state machine.
- Use fakes and simulated implementations for behaviour; use mocks only when
  the interaction itself is the requirement.
- Fixtures go in a `fixtures/` directory next to the tests, are version
  controlled, and are small.

## Rules

- **Never** weaken, narrow, `DISABLED_`, skip or delete a test to make a build
  pass. If a test is wrong, fix it deliberately and say so.
- **Never** modify production code purely to satisfy a test without
  understanding the failure.
- Confirm a new test can actually fail before trusting it: break the behaviour,
  see it go red, restore it. Report that you did this.
- Assertion messages should explain what was expected and why it matters.

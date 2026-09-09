---
applyTo: "**/*.cpp,**/*.h,**/*.hpp,**/*.cc"
---

# C++ conventions

## Language and build

- C++20. No compiler extensions (`CMAKE_CXX_EXTENSIONS OFF`).
- The build is `-Werror` with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion`.
  Fix warnings; never silence one with a pragma without a comment explaining
  why.
- Add new targets through CMake. Never hand-edit anything under `build/`.

## Files and naming

- Headers use `.hpp`, sources use `.cpp`. Arduino sketches use `.ino`.
- Public headers live in `<module>/include/pigeon/<module>/`, implementation in
  `<module>/src/`.
- Use `#pragma once`, not include guards.
- Include your own header first, then the standard library, then third party,
  then project headers.

| Entity             | Style        | Example              |
| ------------------ | ------------ | -------------------- |
| namespace          | `lower_case` | `pigeon::core`       |
| type               | `CamelCase`  | `TargetStateMachine` |
| function, variable | `lower_case` | `next_state`         |
| private member     | trailing `_` | `frame_count_`       |
| enum type          | `CamelCase`  | `DetectionResult`    |
| enum constant      | `UPPER_CASE` | `TARGET_LOCKED`      |
| file               | `snake_case` | `target_state.cpp`   |

All project code lives under the `pigeon` namespace with a module
sub-namespace. Put implementation-only helpers in an anonymous namespace.

## Types and memory

- RAII everywhere. No `new`/`delete` in application code.
- No owning raw pointers. Prefer values, then `std::unique_ptr`, then
  `std::shared_ptr`. Raw pointers and references are non-owning observers only.
- Pass non-owning views as `std::span` or `std::string_view`. Do not copy frame
  buffers at boundaries — the deployment target has 1 GB of RAM.
- Prefer `enum class` over plain `enum`.
- Mark constructors `explicit` unless implicit conversion is genuinely wanted.
- Use `[[nodiscard]]` on functions whose result must not be ignored — notably
  anything returning a detection result, a state, or an error.
- Prefer `constexpr` and `const` by default. Use `noexcept` where it holds.
- No `using namespace` at namespace scope in a header.

## Error handling

- Domain logic should be total: prefer returning a result type or
  `std::optional` over throwing.
- Do not let exceptions cross a module boundary. Validate at the boundary and
  convert to an explicit error value.
- Never use an assertion as the only guard on a safety-relevant condition
  (`REQ-SAF-*`); assertions vanish in release builds.
- Handle every error path. Silently ignoring a failure at a hardware boundary
  is a defect.

## Design

- Hardware sits behind an interface. Inject dependencies through constructors.
- Keep domain logic free of I/O, the wall clock and randomness. Inject a clock
  or a source rather than reading the environment directly.
- Prefer free functions over classes when there is no state.
- Prefer composition over inheritance. A polymorphic base needs a virtual
  destructor.

## Comments

- Comment **why**, not what. Do not narrate the code.
- Document every public interface: purpose, preconditions, error behaviour,
  units, and coordinate frame where relevant.
- Reference the `REQ-*` ID behind a non-obvious rule, e.g.
  `// Three consecutive detections required (REQ-TRK-002).`

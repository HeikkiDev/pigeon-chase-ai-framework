# Pigeon Chase — AI Engineering Framework

A project exploring how an **AI Engineering Framework** can be designed, built, and used to develop a small C++ robotics system.

The project uses a deliberately small scope — Raspberry Pi, Arduino, computer vision, tracking, and simulation — as a practical environment for experimenting with AI-driven software engineering workflows.

## Goals

* Define requirements and architecture in a way that AI agents can understand and operate on.
* Enable AI agents to plan, implement, test, review, and validate software.
* Provide automated tools and simulations that give agents reliable feedback.
* Explore agent roles, instructions, validation loops, and engineering workflows.
* Keep the scope small while making the engineering framework reusable and extensible.

## Philosophy

The goal is not to have AI simply generate code.

The goal is to create an environment where AI agents can **perform engineering work within well-defined constraints and provide evidence that their work is correct**.
## Quick start

```bash
brew install cmake ninja llvm     # one-time setup
make check                        # configure, build, test, format, lint
```

`make check` (equivalently `scripts/check.sh`) is **the gate**: the single
pass/fail answer to "is this repository healthy?". CI runs the same script, so
green locally means green in CI. No hardware is required at any point.

| Command            | Purpose                                          |
| ------------------ | ------------------------------------------------ |
| `make check`       | Full gate. Required before any task is complete. |
| `make fast`        | Build and test only — the inner loop.            |
| `make fix`         | Reformat sources with clang-format.              |
| `scripts/trace.sh` | Requirement → test traceability matrix.          |

## How the framework works

The framework is built on three mechanisms that turn "the AI says it works"
into verifiable evidence:

1. **One command.** `make check` is the only definition of healthy. Agents run
   it and quote its real output; they never predict it.
2. **Traceable requirements.** Every requirement in
   `docs/requirements/requirements.md` has a stable `REQ-*` ID. Tests name the
   IDs they verify, and `scripts/trace.sh` fails when an implemented
   requirement has no verifying test.
3. **Separated roles.** The agent that writes code is not the agent that
   decides whether it works. See `.github/agents/README.md`.

Unspecified behaviour is an **Open Question**, not an assumption. Agents are
instructed to stop and ask rather than invent requirements.

## Documentation map

| Question                      | Document                            |
| ----------------------------- | ----------------------------------- |
| How do I work in this repo?   | `AGENTS.md`                         |
| What must the system do?      | `docs/requirements/requirements.md` |
| How is the system structured? | `docs/architecture/architecture.md` |
| What does this term mean?     | `docs/glossary.md`                  |
| Why was this decided?         | `docs/decisions/`                   |
| Who does what?                | `.github/agents/README.md`          |
| Coding conventions            | `.github/instructions/`             |

## Status

The engineering framework — build, test, CI, traceability, agent roles — is in
place and verified. The anti-pigeon system itself is specified but not yet
implemented; all requirements are currently `Draft`.

# Anti-Pigeon Deterrence System

## Overview

This project is a computer-vision-based system for detecting pigeons, tracking them, calculating where the system should point, and issuing deterrence commands.

The eventual system will use a Raspberry Pi for computer vision and an Arduino for low-level servo and actuator control.

**Hardware integration is a future phase. The current project must be completely runnable and testable on a MacBook without any physical hardware.**

## Current Development Environment

Development and testing must work entirely on macOS.

The codebase should not require:

* Raspberry Pi
* Arduino
* Physical camera
* Servo motors
* Water actuator
* GPIO
* Other physical hardware

Hardware-specific functionality must be isolated behind interfaces so that it can be replaced by software implementations, mocks, or simulators.

## Architecture

The intended system consists of two logical components:

### Vision / Control

Responsible for:

* Image acquisition
* Pigeon detection
* Target tracking
* Target position calculation
* Generating commands for the actuator system

During development, image acquisition and actuator control should use simulated or mocked implementations.

### Actuator Control

Eventually responsible for:

* Receiving commands
* Servo positioning
* Actuator control
* Hardware safety limits

For now, this component should be implemented and tested as a software simulation. No Arduino hardware should be required.

## Development Principles

* Keep hardware-independent domain logic separate from hardware integration.
* Prefer interfaces and dependency injection at hardware boundaries.
* Every important behavior should be testable without physical hardware.
* Provide simulations for hardware-dependent components where practical.
* Do not introduce Raspberry Pi or Arduino dependencies into core application logic.
* Keep the system executable on macOS throughout development.
* Changes should not require physical hardware to validate correctness.
* Preserve clear boundaries between detection, tracking, targeting, communication, and actuation.

## Testing

Tests must be executable on macOS without hardware.

Prefer:

* Unit tests for individual algorithms and components
* Integration tests using simulated components
* Deterministic test images and recorded scenarios
* Simulated actuator responses
* End-to-end tests that exercise the complete software pipeline

Hardware-in-the-loop testing will be introduced later and must remain separate from the normal development and CI test suite.

## Documentation

The system architecture and requirements are in `docs/architecture` and `docs/requirements`.

Document important architectural decisions as ADRs in `docs/decisions/`.

Document interfaces between logical components even when their eventual implementation will run on different physical devices.

# System Architecture

The system is designed to detect pigeons using computer vision, confirm and track detections, calculate the required pointing direction, and control a water-based deterrent.

The software is being developed and tested independently of the physical hardware. The eventual deployment target consists of a Raspberry Pi and an Arduino.

## Target Hardware Architecture

### Raspberry Pi

The Raspberry Pi 3 Model B, with 1 GB of RAM, will be responsible for high-level processing.

A camera connected to the Raspberry Pi will provide image frames to an object detection model running locally on the device.

The Raspberry Pi will be responsible for:

1. Capturing camera frames.
2. Running object detection.
3. Confirming pigeon detections across multiple frames.
4. Tracking confirmed pigeons.
5. Calculating the target position relative to the deterrent.
6. Converting the target position into X and Y servo angles.
7. Sending the required angles to the Arduino.

The Raspberry Pi's 1 GB of RAM is a significant resource constraint. Memory usage should therefore be considered when selecting object detection models, image-processing pipelines, and runtime architectures.

### Arduino

The Arduino will be connected to the Raspberry Pi via USB.

It will be responsible for low-level physical control:

* Receiving commands from the Raspberry Pi.
* Moving the X and Y servo motors.
* Positioning the deterrent according to the requested angles.
* Controlling the water actuator.

The communication protocol between the Raspberry Pi and Arduino is a defined system boundary and should be documented separately.

## Detection and Confirmation

An object detection in a single camera frame is not sufficient to consider a pigeon confirmed.

A pigeon detection must be confirmed across **three consecutive positive camera frames** before it becomes a confirmed target.

Conceptually:

```text
Frame 1: pigeon detected
        ↓
Frame 2: pigeon detected
        ↓
Frame 3: pigeon detected
        ↓
Confirmed target
```

Tracking is responsible for maintaining the identity and position of confirmed targets across subsequent frames.

The exact detection, tracking, and temporal association algorithms are implementation details and should remain independent from the camera and hardware interfaces.

## Targeting

Once a pigeon has been confirmed, the system calculates its position relative to the deterrent.

The targeting subsystem converts the detected target position into the X and Y angles required to point the deterrent toward the target.

This calculation depends on the geometry and calibration of the camera and actuator system. The coordinate systems, calibration parameters, and transformations must be explicitly defined and documented.

## Hardware Independence

**The current development phase does not depend on physical hardware.**

The complete system must be buildable, runnable, simulated, and tested on macOS without:

* Raspberry Pi
* Arduino
* Physical camera
* Servo motors
* Water actuator
* GPIO hardware

Hardware-dependent components must be isolated behind software interfaces so they can be replaced by simulated implementations during development.

The intended development pipeline is:

```text
Simulated camera / test images
          ↓
   Object detection
          ↓
      Tracking
          ↓
 Detection confirmation
          ↓
      Targeting
          ↓
  Simulated communication
          ↓
   Simulated servos
          ↓
 Simulated water actuator
```

The physical Raspberry Pi and Arduino implementations will be introduced later without requiring changes to the core application logic.

## Architectural Principle

The system is organized around three main responsibilities:

* **Vision** — captures and processes images, detects pigeons, and confirms and tracks detections across frames.
* **Targeting** — determines the relative position of a confirmed pigeon and calculates the X and Y angles required to point the deterrent.
* **Control** — sends targeting commands to the actuator system and, eventually, controls the physical servos and water actuator through the Arduino.

These responsibilities should remain loosely coupled through well-defined interfaces.

The core application must remain hardware-independent so that the complete pipeline can be simulated and tested on macOS.

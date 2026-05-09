# BowlMania 🎳

A Wii Sports-inspired 10-frame bowling game running on bare-metal RISC-V on the DE1-SoC. Built for ECE243 at the University of Toronto.

> Motion controls. Live VGA graphics. Hardware audio.

---

## Demo

[![BowlMania Gameplay Demo](https://github.com/user-attachments/assets/e20862d8-81fc-40ba-b4cd-bff7cac9f79c)](https://youtu.be/Y0TXjXi514Q)

---

## Repository Structure
* `/src` - Core C source files and custom I2C drivers.
* `/docs` - Contains the full ECE243 Technical Final Report detailing the system architecture and circuit designs.

---

## Overview

BowlMania maps physical motion to in-game bowling mechanics, the player swings a custom handheld remote to set throw power and lane drift, mirroring real bowling. The game renders at 60 FPS on a 640×480 VGA display with full scoring, animations, and audio feedback.

A 4-state FSM drives the gameplay loop:

| State | Description |
|---|---|
| `AIMING` | Player moves left/right along the lane using pushbuttons |
| `ARMED` | Release button held; accelerometer continuously sampled for peak swing force and tilt angle |
| `THROWING` | Ball physics tick-by-tick — forward speed from swing power, lateral drift from tilt |
| `RESULT` | Strike/spare feedback displayed; frame bookkeeping runs (including 10th-frame bonus rules) |

---

## Hardware

<img width="527" height="411" alt="image" src="https://github.com/user-attachments/assets/fdd69e4e-c8ba-4a0e-9394-c4d5b1c84b84" />

*Figure 1: Custom physical remote enclosure and the underlying GY-521 MPU-6050 accelerometer circuit.*

- **DE1-SoC** (RISC-V / Nios V) — bare-metal C, no OS
- **GY-521 MPU-6050** — Accelerometer & gyroscope connected via bit-banged I2C over JP1 GPIO
- **Custom remote** — 3D-printed enclosure with 3 pushbuttons wired to JP1
- **VGA monitor** — 640×480, hardware double buffering with active VSYNC polling
- **DE1-SoC audio peripheral** — sound samples on game events + background music
---

## Technical Highlights

**I2C driver:** custom bit-banged I2C protocol over JP1 GPIO to read raw accelerometer data at runtime.

**Signal filtering:** Exponential Moving Average (EMA) filter on raw accelerometer output to reduce noise and smooth swing detection. Swing power derived from 3D magnitude with g-value conversion while tilt angle from x-axis data.

**Graphics engine:** perspective-correct animated lane with gutter geometry, Mii-style character, disco ceiling lights, crowd fans with randomized speech bubbles and camera flash effects, live power HUD, and a full 10-frame scorecard. Double-buffered with VSYNC polling to eliminate tearing at 60 FPS.

<img width="461" height="322" alt="image" src="https://github.com/user-attachments/assets/97b08b1e-5f4d-489d-9535-59b56ec0bde4" />

*Figure 2: VGA display demonstrating the 60 FPS graphics engine, custom scoreboard, and animated crowd.*

**Audio:** 128-sample hardware audio FIFO streaming sound effects and continuous background music (Wii Music).

---

## Controls

| Input | Action |
|---|---|
| Hold Left button (JP1 D3) | Move player left |
| Hold Right button (JP1 D4) | Move player right |
| Hold Middle button (JP1 D2) | Enter ARMED state, sample accelerometer |
| Release Middle button | Throw — locks in power and angle |
| Physical swing of remote | Swing magnitude → ball speed; tilt → direction |


---

## Authors

Ansh Shah · Bilaal Khan

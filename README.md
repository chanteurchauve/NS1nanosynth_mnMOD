# NS1nanosynth mnMOD v1.1.0

A feature-expanded firmware for the NS1 Nanosynth platform, focused on MIDI-to-CV conversion, synchronized modulation, external hardware control, hardware portamento, and low-latency digital synthesis.

---

## Introduction

Once I got hold of the soundmachines NS1nanosynth I was thrilled to explore the possibilities given by the fact of having an Arduino Leonardo + MCP4922 running under the hood. Many years passed since the development of the original firmware, making it impossible to compile and load it on the NS1 nowadays. That implied a complete rewriting of the firmware from the ground up. Once the basic features were reimplemented, I started adding new ones, which are listed below. Enjoy!

## Core Synthesis & Audio Engine

### Mozzi 2.0 Integration

Built on the Mozzi 2.0 synthesis engine, optimized for:

* Audio Rate: **16384 Hz**
* Control Rate: **256 Hz**

The high control rate guarantees perfectly smooth interpolations for performance features like fast portamento sweeps, preventing audible stepping artifacts.

### Dual Digital Oscillators

The audio engine includes:

* Main digital sawtooth oscillator
* Dedicated sub-oscillator

The sub-oscillator is permanently tuned:

* **-24 semitones**
* **2 octaves below** the main oscillator

### 12-bit SPI DAC Support

Native support for the **MCP4922** external DAC.

| DAC Output | Function |
| --- | --- |
| DAC A | 1V/Oct Pitch CV |
| DAC B | Note Velocity CV |

Provides precise CV generation for external analog circuitry.

---

## MIDI, Polyphony & Performance

### Class-Compliant USB MIDI

Native USB MIDI implementation.

Features:

* Plug-and-play operation
* No external MIDI interface required
* Compatible with macOS, Windows, Linux, and iOS hosts supporting USB MIDI

### Optimized 8-Note Monophonic LIFO Buffer

Advanced note-priority system with intelligent note recall and bidirectional glide.

Features:

* 8-note buffer
* Last-In / First-Out (LIFO) priority
* Seamless note restoration after key release
* Full support for legato trills (glides dynamically trigger both on note attack and note release when overlapping)

Example:

1. Hold C
2. Hold G (glides up to G)
3. Hold D (glides up to D)
4. Release D → glides back down to G
5. Release G → glides back down to C

### Portamento (Glide) Matrix

Hardware-configurable portamento speeds selected by grounding analog pins A0, A1, and/or A2. 

| Pins Connected to GND | Glide Time |
| --- | --- |
| None | Off |
| Pin A0 | 50 ms |
| Pin A1 | 100 ms |
| Pin A2 | 250 ms |
| Any 2 Pins | 500 ms |
| All 3 Pins | 1000 ms |

### Standard Performance Controls

* 14-bit Pitch Bend (Internally constrained to maintain safe DAC scaling)
* Sustain Pedal (CC64)

---

## Clock, Sync & Rhythmic Modulation

### MIDI Clock-Synchronized LFO

A proportional phase-accumulator LFO generated entirely with integer arithmetic.

Features:

* PWM output
* MIDI Clock synchronization (24 PPQN)
* Zero accumulated phase drift
* Sample-accurate tempo tracking

### Hardware LFO Rate Matrix

LFO division can be selected dynamically by grounding Pins 6, 7, and/or 8.

| Pins Connected to GND | Division |
| --- | --- |
| None | 1/4 Note |
| Pin 8 | 1/8 Note |
| Pin 7 | 2/4 (Half Note) |
| Pin 6 | 4/4 (Whole Note) |
| Pin 7 + Pin 8 | 1/8 Note Triplets |
| Pin 6 + Pin 8 | Dotted 1/8 Note |
| Pin 6 + Pin 7 | Dotted 1/4 Note |
| Pin 6 + Pin 7 + Pin 8 | 3-over-4 Triplets |

### Dedicated Trigger Output

Generates a hardware trigger synchronized to MIDI Clock.

Specifications:

* Pulse Width: **15 ms**
* Voltage: **+5V**
* Resolution: **1/16 Note**

Suitable for: modular sequencers, envelope generators, clock inputs, and trigger-based hardware.

---

## Control Voltage Inputs & DSP

### 5x Analog CV to MIDI CC Conversion

Five analog inputs continuously transmit MIDI CC messages over USB.

| Input | MIDI CC |
| --- | --- |
| A1 | CC102 |
| A2 | CC103 |
| A3 | CC104 |
| A4 | CC105 |
| A5 | CC106 |

### Advanced DSP Jitter Filtration

Input processing includes:

* Exponential Moving Average (EMA) filtering
* Bit-shift optimized implementation
* Deadband hysteresis thresholding

Benefits: Stable controller values, noise suppression, reduced MIDI bandwidth usage, minimal CPU cost.

### Floating-Pin Protection

Unused analog inputs are internally biased using pull-up resistors. Eliminates antenna-effect noise and prevents random MIDI CC transmission.

---

## External Hardware Control

### I²C Digital Potentiometer Integration

Supports remote control of a 4-channel digital potentiometer via MIDI.

| MIDI CC | Function |
| --- | --- |
| CC30 | Potentiometer 1 |
| CC31 | Potentiometer 2 |
| CC32 | Potentiometer 3 |
| CC33 | Potentiometer 4 |

Communication is handled over the I²C bus for direct external parameter control.

---

## Technical Summary

| Feature | Specification |
| --- | --- |
| Synthesis Engine | Mozzi 2.0 |
| Audio Rate | 16384 Hz |
| Control Rate | 256 Hz |
| Oscillators | Saw + Sub (-24 Semitones) |
| DAC | MCP4922 (12-bit SPI) |
| MIDI | Native USB MIDI |
| Polyphony Buffer | 8-Note Monophonic LIFO |
| Portamento | Matrix (50ms -> 1000ms) |
| Pitch CV | DAC A |
| Velocity CV | DAC B |
| MIDI Clock Sync | 24 PPQN |
| LFO Output | PWM |
| Trigger Output | +5V / 15 ms / 16th Note |
| CV Inputs | 5 |
| MIDI CC Outputs | CC102–106 |
| Digital Pot Control | I²C / CC30–33 |

---

## License

This project is licensed under the GNU General Public License v3.0 (GPL-3.0).

You are free to:

* Use
* Study
* Modify
* Distribute

this software under the terms of the GPL-3.0 license.

Any derivative work distributed to others must also be released under the GPL-3.0 license and must make its source code available.

For the full license text, see the LICENSE file included with this repository or visit:

[https://www.gnu.org/licenses/gpl-3.0.en.html](https://www.gnu.org/licenses/gpl-3.0.en.html)
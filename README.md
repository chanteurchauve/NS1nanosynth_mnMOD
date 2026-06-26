# NS1nanosynth mnMOD v1.2.1

A high-performance, feature-expanded firmware for the soundmachines NS1 Nanosynth platform, focused on high-resolution MIDI-to-CV conversion, sample-accurate synchronized modulation, hardware control matrices, and low-latency digital synthesis.

---

## Introduction

Once I got hold of the soundmachines NS1nanosynth I was thrilled to explore the possibilities given by the fact of having an Arduino Leonardo + MCP4922 running under the hood. Many years passed since the development of the original firmware, making it impossible to compile and load it on the NS1 nowadays. That implied a complete rewriting of the firmware from the ground up. 

Version 1.2.0 introduces an advanced architecture featuring a high-resolution 256-step synchronized LFO engine, real-time wave calculation syntax, dedicated hardware wave-switching matrices, and refined analog CV tracing.

---

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

The sub-oscillator is permanently tuned **-24 semitones** (**2 octaves below** the main oscillator) for thick analog-style layering.

### 12-bit SPI DAC Support
Native support for the **MCP4922** external DAC.

| DAC Output | Function |
| --- | --- |
| DAC A | 1V/Oct Pitch CV |
| DAC B | Note Velocity CV |

Provides precise, linear CV generation for external analog circuitry.

---

## MIDI, Polyphony & Performance

### Class-Compliant USB MIDI
Native USB MIDI implementation providing plug-and-play operation across macOS, Windows, Linux, and iOS hosts without external midi interfaces.

### Optimized 8-Note Monophonic LIFO Buffer
Advanced note-priority system with intelligent note recall and bidirectional glide. Full support for legato trills—glides dynamically trigger both on note attack and note release when overlapping.

**Example Sequence:**
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

---

## Clock, Sync & High-Resolution Modulation

### High-Resolution Synchronized LFO (Pin 11)
Version 1.2.0 upgrades modulation capabilities to a fluid **256-sample resolution** engine. The phase-accumulator tracks incoming MIDI Clock pulses (24 PPQN) natively, ensuring perfect phase alignment with no drift over time. LFO output is hardcoded to output continuously through **Pin 11**.

### LFO Waveform Selection Matrix
By grounding combination patterns across **Pin 0** and **Pin 1**, the engine dynamically alters the waveform output. To conserve system memory, only the Sine wave relies on a table lookup, while the remaining waveforms are generated in real-time via optimized mathematical calculations.

| Pin 1 | Pin 0 | Active Waveform (Pin 11) | Engine Generation Mode |
| :---: | :---: | :--- | :--- |
| **GND** | **GND** | **Inverse Sawtooth** | Real-time Mathematical Formula |
| **GND** | Open | **Sawtooth** | Real-time Mathematical Formula |
| Open | **GND** | **Square / Pulse (50% Duty)** | Real-time Mathematical Formula |
| Open | Open | **Sine Wave** | High-Res 256-Sample PROGMEM Table |

### LFO Hardware Rate Matrix
LFO division relative to the master clock can be selected dynamically by grounding Pins 6, 7, and/or 8.

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

### Dedicated Trigger Output (Pin 12)
Generates a hardware clock trigger synchronized to incoming MIDI clock.
* **Pulse Width:** 15 ms
* **Voltage:** +5V
* **Resolution:** 1/16 Note
* **Usage:** Driving modular sequencers, clock dividers, or gate envelopes.

---

## Control Voltage Inputs & DSP

### 3x Analog CV to MIDI CC Conversion
Three analog inputs continuously scan external hardware control signals and transmit them as MIDI CC over USB. 

| Input Pin | Destination MIDI CC |
| :---: | :---: |
| A3 | CC102 |
| A4 | CC103 |
| A5 | CC104 |

*Note: In v1.2.0, pins A1 and A2 are exclusively assigned to the Glide Matrix to avoid signal overlapping and preserve performance filtering stability.*

### Advanced DSP Jitter Filtration
All active CV inputs are processed using an Exponential Moving Average (EMA) filter combined with bit-shift optimization and a strict deadband hysteresis threshold (`CV_NOISE_THRESHOLD = 12`). This ensures steady controller tracking and eliminates parasitic MIDI stream clutter. Unused inputs are biased via internal pull-ups.

---

## External Hardware Control

### I²C Digital Potentiometer Integration
Supports real-time parameter tracking of a 4-channel digital potentiometer (Address 0x2C) via dedicated MIDI CC parameters across the I²C bus.

| MIDI CC | Targeted Output Node |
| :---: | :--- |
| **CC30** | Potentiometer Channel 1 |
| **CC31** | Potentiometer Channel 2 |
| **CC32** | Potentiometer Channel 3 |
| **CC33** | Potentiometer Channel 4 |

---

## Technical Summary

| Feature | Specification |
| --- | --- |
| Synthesis Engine | Mozzi 2.0 Core Core Configuration |
| Audio Rate / Control Rate | 16384 Hz / 256 Hz |
| Native Signal Generation | Sawtooth + Sub (-24 Semitones) |
| Audio DAC Architecture | MCP4922 (12-bit SPI SPI Connection) |
| Pitch CV / Velocity CV | DAC A / DAC B Output Paths |
| MIDI System Format | Native Class-Compliant USB MIDI |
| Polyphonic Memory Mode | 8-Note Monophonic LIFO Buffer |
| Portamento Structure | 3-Pin Grounding Matrix (50ms to 1000ms) |
| LFO Sample Resolution | **256-Step Resolution Accumulator** |
| LFO Modulation Shapes | **Sine, Square/Pulse, Saw, Inverse Saw** |
| Physical Modulation Node | **Pin 11 (PWM Only)** |
| Master Sync Trigger Out | Pin 12 (+5V / 15 ms width / 16th-note ticks) |
| Active CV Input Channels | 3 Channels (A3, A4, A5) |
| Transmitted CC Messages | CC102, CC103, CC104 |
| Peripheral Hardware Link | I²C Bus Architecture (CC30–CC33 Control) |

---

## License

This project is licensed under the GNU General Public License v3.0 (GPL-3.0).

You are free to use, study, modify, and distribute this software under the terms of the GPL-3.0 license. Any derivative work distributed to others must also be released under the GPL-3.0 license and must make its source code available.

For the full license text, see the LICENSE file included with this repository or visit: [https://www.gnu.org/licenses/gpl-3.0.en.html](https://www.gnu.org/licenses/gpl-3.0.en.html)
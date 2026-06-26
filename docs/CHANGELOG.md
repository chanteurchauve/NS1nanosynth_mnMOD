# Changelog

All notable changes to the NS1nanosynth mnMOD firmware project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.2.1] - 2026-06-26

### Fixed
- **V/Oct Pitch Tracking Calibration:** Partially resolved a linear scale drift causing a ~36-cent tuning drop when moving from the lowest C to the highest C across the 5-octave (60 semitones) hardware range.
- **DAC Scaling Optimization:** Recalibrated the `DacVal` lookup table in `PROGMEM` with an increased step resolution (~68.66 per semitone) to correct the analog oscillator tracking. High-register capping is documented at 4095 due to the physical 12-bit ceiling of the MCP4922 DAC.

## [1.2.0] - 2026-06-20

### Added
- **256-Sample High-Resolution LFO Engine:** Upgraded the LFO accumulator calculation from a coarse 24-step configuration to a highly fluid 256-step resolution, removing visual and auditory staircase voltage artifacts on Pin 11.
- **Hardware Waveform Selection Matrix:** Integrated physical pin tracking via Pin 0 and Pin 1 utilizing internal pull-ups. Users can ground these pins to dynamically swap LFO wave outputs on the fly.
- **On-The-Fly Mathematical Waveform Generation:** Implemented raw mathematical calculation formulas for Sawtooth, Inverse Sawtooth, and 50% Duty Cycle Square/Pulse waves, allowing zero-latency signal transitions based directly on accumulator position.

### Changed
- **LFO Output Consolidation:** Hardcoded LFO generation to route strictly and exclusively out of **Pin 11** across all waveform states, stabilizing board hardware infrastructure.
- **CV Input Architecture Optimization:** Refactored CV-to-MIDI inputs down to 3 dedicated channels (**A3, A4, A5** mapping to **CC102, CC103, CC104**). This shift guarantees no hardware overlap or line-noise interference with the Portamento configuration matrix running on A0–A2.
- **PROGMEM Layout Architecture:** Rebuilt Flash arrays to isolate the Sine wave table as the exclusive resident waveform inside PROGMEM, freeing up critical microcontroller storage resources.

### Removed
- Legacy 24-sample low-resolution arrays for structural waveforms.
- Floating-pin scanning on pins A1 and A2 to safeguard performance glide integrity.

## [1.1.0] - 2026-06-20

### Added
- **Hardware Portamento (Glide) Matrix**: Introduced a dynamic, hardware-controlled glide timing system utilizing analog pins A0, A1, and A2.
  - Pin A0 connected: 50 ms
  - Pin A1 connected: 100 ms
  - Pin A2 connected: 250 ms
  - Any 2 pins connected: 500 ms
  - All 3 pins connected: 1000 ms

### Changed
- **Audio Engine Control Rate**: Increased `MOZZI_CONTROL_RATE` from 64 Hz to 256 Hz. This quadruples the control resolution, drastically improving the interpolation of portamento sweeps and ensuring that quick transitions (like the 50 ms glide) remain completely smooth and free of "zipper" or stepping artifacts.

### Fixed
- **Legato Release (Trill) Behavior**: Rewrote the LIFO buffer's `removeNote()` logic to support bidirectional glide. Releasing a top key while sustaining a lower key now correctly calculates and triggers a downward glide to the exposed note, rather than jumping abruptly.

## [1.0.0] - 2026-06-19

- Initial NS1 mnMOD firmware release.
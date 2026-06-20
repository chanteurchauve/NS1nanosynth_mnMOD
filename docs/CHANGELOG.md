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
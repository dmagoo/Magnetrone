# Requirements

## Hardware

- Teensy 3.5 or 4.1 (drop-in socket, no hardware changes between boards)
- Teensy Audio Shield (SGTL5000) - output only, no mic/line in
- A4988 stepper driver + NEMA17 motor, 20:220 tooth GT2 belt drive (11:1), 24V
- 8x analog Hall effect sensors
- MIDI IN/OUT via DIN connectors; MIDI IN uses optoisolator
- Audio output: headphone/line out jack to external device
- Power: 24V DC barrel jack -> MP1584EN buck converter -> 5V -> Teensy
- 16x2 I2C LCD (PCF8574T backpack, address 0x27)
- Main encoder with push button (menu navigation)
- Volume encoder with push button (mute/unmute)
- Speed encoder with push button (stop/resume)
- 1x aux encoder (reserved, unused)
- 3x aux buttons (reserved, unused)

---

## UI / Interaction Model

Navigation philosophy: fast, preset-driven, Prusa MK4S style.

### Encoders

- **Main encoder**: menu navigation and value selection
- **Speed encoder**: turn to start motion and adjust speed; press to stop/resume
- **Volume encoder**: turn to adjust volume; press to mute/unmute
- Live speed and volume controls work at all times, including while inside menus

### Boot behavior

- Device boots in stopped state - platter stationary, no auto-start
- Persisted across power cycles:
  - Root note
  - Scale / mode
  - Octave
  - Last speed value
  - Last volume value
  - Mute state
  - Sensor phase shift

---

## Display

Default view is the **Status Screen**:

- Line 1: BPM and RPM
- Line 2: root note + scale name (abbreviated) + volume bar or [MUTE]

After inactivity on the main encoder, menus auto-return to the Status Screen.
Live encoder changes update the display immediately without leaving the current screen.

Scale display abbreviations:
- Pentatonic Major -> PMajor
- Pentatonic Minor -> PMinor
- Mixolydian -> Mixolyd
- Chromatic -> Chromat

---

## Menu Structure

All submenus have a Back option.

### Root Note
C, C#, D, D#, E, F, F#, G, G#, A, A#, B

### Scale / Mode
Major, Minor, PMajor, PMinor, Blues, Chromat, Dorian, Mixolyd

### Octave
0-7 (default 4, middle C)

### Calibration

1. Prompt: "Place a magnet on the table" - user confirms OK or Back
2. Table rotates; system searches for a single magnet
3. Prefers outer track for timing reliability
4. Captures magnet timing to determine RPM relationship

Error: if multiple magnets detected, show error and offer retry.

---

## Tempo

- 1 revolution = 1 bar (4/4 time, 4 beats per revolution)
- BPM = RPM * 4
- Configurable beats-per-revolution under Advanced menu (future)

---

## Advanced (Future)

- Beats per revolution
- Sensor phase shift (which sensor index plays root note)
- Aux encoder mappable to any menu parameter (key, phase, octave, etc.)
- LCD backlight on/off and timeout
- MIDI clock in/out
- MIDI Program Change and CC mapping

# Magnetrone

A motorized MIDI sequencer built around a rotating platter driven by a stepper motor.
Magnets placed on the platter pass over Hall effect sensors to trigger MIDI notes,
turning physical arrangement into musical pattern.

---

## Features

- Rotating platter driven by NEMA17 stepper via GT2 belt drive (11:1)
- 8 analog Hall effect sensors, one per note in the current scale
- MIDI note output on DIN connector
- Teensy Audio Shield output (headphone/line out) with a built-in synth and drum kit
- Voices: Piano, Strings, Leads, Bass and Drums, each with its own envelope and note length
- Two magnet layers: a magnet's normal pole plays Layer A, its reversed pole Layer B,
  each with its own voice, MIDI channel, octave offset, level, Track Shift and
  Low Note
- Per-layer Track Shift raises the run by scale degrees, with Wrap (the run
  rotates around the arm) or No Wrap (the whole run transposes)
- Per-layer Low Note: Inner or Outer sets which end of the sensor arm plays
  the low end of the run
- Drums play a fixed GM kit (one drum per track, busiest on the outer tracks) on
  MIDI channel 10; Track Shift rotates the kit around the tracks and Low Note
  Outer flips it end to end
- 1 revolution = 1 bar; BPM tracks RPM automatically
- Root note, scale, and octave selectable from LCD menu
- Speed and volume adjustable live at any time via dedicated encoders; touching
  either one returns to the live display from any menu (prompts excepted)
- Menu knob opens the main menu from the live display by turning or pressing;
  the menu button leaves the Aux screens straight back to the live display
- Welcome tune plays on boot -- previews what Layer A's tracks play, hall 1 to 8
  and back, including its octave offset, Track Shift, Wrap and Low Note; a menu
  button press skips it
- LCD backlight timeout: configurable from always off to always on
- Stepper motor sleeps when idle to reduce heat and power draw
- Calibration routine measures magnet signal and actual platter RPM, and shows
  the measured belt reduction (e.g. "Belt 10.9:1") against the assumed 11:1
- Reset Calibration and Reset to Defaults available from the menu
- All settings persisted to EEPROM across power cycles
- Teensy 4.1

---

## Code Summary

The firmware is structured as a set of independent modules: stepper motor control
uses an IntervalTimer ISR for non-blocking step generation; Hall sensor polling
detects rising-edge triggers with hysteresis and debounce; the sequencer maps sensor
index to scale degree and fires timed MIDI note on/off pairs; the audio module drives
an 8-voice synth bank per magnet layer plus a shared drum kit through the SGTL5000
codec in parallel with MIDI out; the menu
is a simple state machine driven by three encoders with live speed and volume handling
at all times. A calibration routine samples the sensor baseline, times one full platter
revolution, and derives a threshold and RPM correction factor stored in EEPROM.

---

## Hardware Summary

The controller is built on a Teensy 4.1 with a Teensy Audio Shield (SGTL5000) for
audio output. A NEMA17 stepper motor drives the platter through a GT2 belt at 11:1
reduction, controlled by an A4988 driver at 1/16 microstepping from 24V DC. Eight
ratiometric Hall effect sensors (A1301) are powered at 3.3V and read directly by the
Teensy ADC. A 16x2 I2C LCD with PCF8574T backpack provides the display. MIDI DIN
in/out is present with optoisolation on the input. Power is supplied via 24V barrel
jack through a panel-mount power switch and an MP1584EN buck converter to 5V.

---

## Installation

### Requirements

- PlatformIO (CLI or VS Code extension)
- Teensy 4.1

### Build and Upload

1. Open the `firmware/` directory in VS Code with the PlatformIO extension,
   or use the PlatformIO CLI.

2. Build:

       cd firmware
       pio run -e teensy41

3. Upload:

       pio run -e teensy41 --target upload

### First Run

On first boot, no calibration data is present. Place a single magnet on the
outer track of the platter and run Calibration from the menu (Main > Calibration).

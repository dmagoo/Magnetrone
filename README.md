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
- Voices: Piano, Strings, Leads, Bass and Drums, each with its own envelope and note
  length, plus None to mute a layer
- Voice Edit (Aux): live tweaks to a layer's wave, envelope and note length,
  its first 16 harmonics, and its own filter with a filter envelope; Save As keeps them in Custom 1-8 (shared by all
  scenes) or as the current scene's own Scene Voice
- Edit Scale: a Custom scale with one chosen note per track, from Sound
  Defaults or live on the Aux; Save As keeps it in Custom 1-8 (shared by all
  scenes)
- Two magnet layers: a magnet's normal pole plays Layer A, its reversed pole Layer B
  (or both, with Layer B's Mode on Stack, or taking turns by revolution, with
  Layer Turns on Alternate),
  each with its own voice, MIDI channel, root note, scale, octave, level, Track
  Shift, Wrap and Low Note
- Per-layer effects, in a fixed chain: Tone (low-pass Cutoff and Resonance),
  Chorus (Rate, Depth, Mix), Delay (Sync to the beat or Free in ms, up to
  2.4 s, Feedback up to 90%, Mix) and Reverb (Room Size, Damping, Mix). Saved
  in scenes, all off by default; Layer B can follow A's per effect. Tone
  Cutoff, Delay Mix, Delay Feedback and Reverb Mix are also Aux Fns
- Per-layer Track Shift raises the run by scale degrees, with Wrap (the run
  rotates around the arm) or No Wrap (the whole run transposes)
- Per-layer Low Note: Inner or Outer sets which end of the sensor arm plays
  the low end of the run
- Drums play a fixed GM kit (one drum per track, busiest on the outer tracks) on
  MIDI channel 10; Track Shift rotates the kit around the tracks and Low Note
  Outer flips it end to end
- 1 revolution = 1 bar; BPM tracks RPM automatically. Beats/Rev from 1 to 32,
  platter speed from 1 to 120 RPM
- The platter is marked in 16 steps
- Bar start (StartPos): a start mark on the platter, located by calibration or
  re-found later (Auto from one magnet, or Manual by jogging the platter), and
  remembered across power cycles when the platter was at rest
- Front: where the player sits, set first in calibration and Calib. StartPos
  (the magnet is turned to the player, by hand or with the Speed knob, before
  the spin). Tools > Go to Front turns the mark there the shorter way round
- Placement Mode (Tools): Speed jogs the platter (press: Go to Front), Aux snaps
  to the next step or beat, Volume mutes and unmutes single tracks
- Scenes: the whole sound of both layers, 8 saved slots plus a Defaults scene
  edited from the menu; loaded live on the next bar start (or at once, Play
  Setup > Scene Load). Aux changes are live
  only, on top of the current scene. Read-only demo scenes are built into the
  firmware from JSON files (docs/scene-format.md), and a `scenes` serial
  command prints the saved scenes in that format
- Scene Codes (Aux > Scene Codes): a code of up to 10 characters for the
  current sound's main settings (each layer's root, octave, scale, voice,
  shift, Low Note, Wrap and mode, plus Layer Turns), to enter on another
  table or after a firmware update. Fields at their defaults cost nothing, so
  a code changing only the roots is 3 characters
- [Record Designer](https://dmagoo.github.io/Magnetrone/tools/record-designer/) (`tools/record-designer/`): a
  browser tool to design a "record", a magnet pattern on a step grid plus its
  Scene Code, play it back, save songs and share them as links, and export a
  full-size SVG/PDF template to print or laser cut into a platter cover. Runs
  from the link or straight from disk
- MIDI beat clock driven by platter position, with Song Position Pointer so
  external bars line up with the start mark
- MIDI in (control only): a per-layer input channel; keys drive a MIDI Fn
  (Pitch, Shift, Scale Learn, Set Scale, Fingered or One Finger), the bend wheel bends the table, CC 7 volume,
  CC 20 octave; with Play Setup > MIDI CC on, CCs 74, 71, 93, 94, 12 and 91
  set the effects
- LCD menu: Sound Defaults (both layers), Play Setup, System and Tools
- Speed and volume adjustable live at any time via dedicated encoders; touching
  either one returns to the home screen from any menu (prompts excepted).
  From a stop, turning Speed starts from zero in the turned direction; a
  press resumes the last speed
- Aux knob changes one bound function live (Pitch, A/B Balance, Layer Turns, Load Scene,
  or a layer's Voice, Root Note, Scale, Octave, Shift, Low Note, Wrap, Mode,
  Tone Cutoff, Delay Mix, Delay Feedback or Reverb Mix); its list also holds
  Save Scene, Scene Codes and Reset All
- Menu knob opens the main menu from the home screen by turning or pressing;
  the Menu button leaves the Aux screens straight back to the home screen, and
  the Aux button leaves the main menu (prompts excepted)
- Welcome tune plays on boot -- previews what Layer A's tracks play, hall 1 to 8
  and back, including its key, octave, Track Shift, Wrap and Low Note; a Menu
  button press skips it
- LCD backlight timeout: configurable from always off to always on
- Stepper motor sleeps when idle to reduce heat and power draw
- Calibration (at 24 RPM, clear platter then one magnet on the start mark)
  measures sensor baselines and noise, threshold, magnet pole, the belt reduction
  (e.g. "Belt 10.9:1", counted in motor steps) and the bar start
- Tools menu: Go to StartPos, Go to Front, Placement Mode, calibration,
  StartPos, Machine Info (RPM, belt ratio, StartPos, threshold, motor driver), live
  Sensor Levels, Sensor Timing and Track Notes screens, Reset Settings (keeps calibration, Front and scenes
  1-8) and Factory Reset
- All settings persisted to EEPROM across power cycles
- Teensy 4.1

---

## Code Summary

The firmware is structured as a set of independent modules: stepper motor control
uses an IntervalTimer ISR for non-blocking step generation; Hall sensor polling
fires each note when the magnet is centered on its sensor, predicted from a
threshold-to-peak angle each sensor learns as it plays (so timing holds on every
track and at any speed), with re-arm and debounce; the sequencer maps sensor
index to scale degree and fires timed MIDI note on/off pairs; the audio module drives
an 8-voice synth bank per magnet layer plus a shared drum kit through the SGTL5000
codec in parallel with MIDI out; the menu
is a simple state machine driven by three encoders with live speed and volume handling
at all times. The platter keeps a signed step count, so the bar phase is exact; MIDI
clock and scene loads are driven from it. A calibration routine samples the sensor
baselines and noise, counts motor steps over one platter revolution, and derives a threshold,
RPM correction factor and bar start stored in EEPROM.

---

## Hardware Summary

The controller is built on a Teensy 4.1 with a Teensy Audio Shield (SGTL5000) for
audio output. A NEMA17 stepper motor drives the platter through a GT2 belt at 11:1
reduction, controlled by a TMC2209 driver (configured over UART) from 24V DC. Eight
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

On first boot, no calibration data is present and the table offers to calibrate
(later: Menu > Tools > Full Calibrate). Clear the platter when asked, then place a
single magnet on the start mark on the outer track. See the
[user guide](docs/user-guide.md) for details.

### Diagnostics

For hardware faults, the firmware has a diagnostics mode used over the USB
serial monitor (`pio device monitor`). Hold the Menu button at power-up, or
type `diag` in the monitor while the table runs. It reports the display, audio
board, motor driver and every sensor every 5 seconds, and takes commands for
running and tuning the motor. Type `help` in the monitor for the commands; see
Troubleshooting > Diagnostics mode in the [user guide](docs/user-guide.md).

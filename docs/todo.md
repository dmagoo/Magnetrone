# Todo / Backlog

## Firmware

- [x] Implement stepper sleep: drive PIN_SLEEP low when motor is idle to reduce heat and power draw; drive high before stepping

- [ ] On first boot (uncalibrated), show welcome screen prompting calibration with Yes/Skip options
- [x] MIDI Clock output (0xF8, 24 ppqn) - send MIDI beat clock so external devices sync to platter BPM
- [x] MIDI Start/Stop/Continue output (0xFA, 0xFC, 0xFB) - send when platter starts or stops
- [ ] MIDI Clock input - receive beat clock from external device and drive platter speed to match (if platter control enabled)
- [ ] MIDI Start/Stop/Continue input - external device starts or stops the platter motor (if midi platter control enabled)
- [ ] Magnet strength -> note duration: use peak ADC deviation from Hall sensor to scale how long a note plays
- [ ] Per-sensor dwell time: during calibration, record how long the magnet was over each sensor; use to scale note duration per track
- [x] Widen encoder speed range so that going below "0" reverses the platter
- [x] Pitch Step under the main menu. Sets how far one click of the Pitch Fn
  moves. Defaults to a half step (one semitone); smaller values detune between
  the normal notes for deliberately alien, microtonal sounds.
  MIDI note numbers are integers, so sub-semitone steps need pitch bend on the
  way out or they quantise to the nearest semitone on any external synth. The
  internal synth needs none of this -- its oscillator takes a float frequency.
- [x] Microtonal MIDI OUT via automatic pitch bend. When the Pitch Fn lands
  between semitones, send the nearest note number plus a pitch bend for the
  remainder, so external synths track the same alien tuning the internal synth
  produces.
  This works here where it usually does not: pitch bend is per-CHANNEL, not
  per-note, so microtonal MIDI normally breaks as soon as two notes need
  different offsets. The Pitch Fn shifts the whole instrument, so every sounding
  note shares one offset and a single channel-wide bend is exactly right.
  Two requirements:
  - Send RPN 0 (CC 101=0, CC 100=0, CC 6=semitones) at init to set the
    receiver's bend sensitivity. The default is +/-2 semitones but receivers
    vary, and the maths needs to know the range.
  - Re-base the note number whenever the accumulated offset would exceed that
    range, instead of letting the bend clip at the rail.
  Side effect, and a desirable one: notes already sounding bend with the knob,
  which makes it real modulation rather than a stepped change.
  Interacts with the "MIDI output channel" item -- if per-sensor channels are
  ever added, each channel needs its own bend.
- [ ] Momentary modulation on a long press of the aux button (UNDECIDED -- recorded
  for consideration, not agreed). Hold the button, turn the knob, and the value
  snaps back to where it was when you release. A performance move that is not
  otherwise reachable: every other path leaves the value where you left it.
  Long press is the only unused gesture on that knob. It was also floated as a
  shortcut for Reset All, which was rejected -- that is already an entry on the
  Fn list you would be holding the button on, so the gesture would buy nothing.
  Runner-up idea if this one does not pan out: A/B between the last two Fns for
  quick switching mid-song.
- [ ] Calibration should report the measured stepper-revs to platter-revs ratio, so the real belt reduction can be compared against GEAR_RATIO (11:1) instead of being assumed

## Audio (speculative)

- [ ] Voice presets: define a library of named presets (e.g. "Piano", "Strings", "Leads", "Bass", "Drums") each with a waveform type and ADSR envelope. One global setting selects the active preset for all 8 sensors.
- [ ] Drums preset: sensors trigger fixed GM drum note numbers (kick, snare, hi-hat, toms etc.) regardless of scale/root/octave settings. Each sensor uses a noise-based waveform with a per-sensor envelope tuned to its drum type.
- [ ] MIDI output channel: configurable, default 1. "Auto" mode assigns channel by preset -- Piano=1, Bass=2, Strings=3, Leads=4, Drums=10. If negative matnets are enabled, allow alternate channel for each side (for instance, drums/10 for negative polarity)

- [ ] MIDI input sets the key: play a note on an attached keyboard and it becomes the root note, so the table is retuned by playing rather than by scrolling a menu. Hold a chord and pick the scale that matches it.
- [ ] MIDI CC input mapped to octave and volume, so external knobs drive them live.

## Bugs
- [x] Notes seem to be triggered when magnet is sensed and released! The evidence is that there are double notes, with quicker firing as the outer ring (faster magnet movement) is reached. This should be debounced, perhaps, or maybe it's a simpler matter of tracking?
- [x] Debounce encoder presses in software

## Audio

- [x] Welcome tune: on boot (when cfg.playWelcomeTune is true), play each hall sensor's note in scale order at the current BPM tempo -- one note per beat, using the saved root/scale/octave settings

## Menu / UI

- [ ] Investigate reset button hang -- Teensy resets but setup() appears to hang (likely SGTL5000 I2C init); may need I2C bus reset before audioInit()
- [x] Turning on Welcome Tune from the menu plays it immediately, as a preview
- [x] Menu option: Reset Calibration -- sets hallBaseline, hallThreshold, rpmCorrection, and calibrated flag back to defaults without touching other settings
- [x] Menu option: Reset to Defaults -- wipes all saved config back to factory defaults (prompts for confirmation first)

- [x] LCD backlight timeout: configurable inactivity timeout, resets on any control interaction. Options: Always Off, 1s, 5s (default), 10s, 30s, 60s, Always On. Backlight toggled via lcd.backlight() / lcd.noBacklight() over I2C (PCF8574 backpack).
- [x] BEATS_PER_REV configurable under Advanced menu (default 4 - one revolution = one 4/4 bar)
- [x] sensorShift configurable under Advanced menu (default 0 - shifts which sensor index plays the root note)
- [x] Aux encoder as a live modulation control ("Aux Fn"). Lets a performer change
  one parameter while the table is playing, without leaving the live display or
  navigating the main menu.

  Scope: any parameter that affects live performance in an interesting way is a
  candidate. This is not a second copy of the main menu -- settings that are set
  once and forgotten stay out. Speed and volume are exempt by nature: they have
  dedicated knobs and are not in the menu at all.
  Current targets: Octave, Root Note, Scale, Track Shift, Pitch.

  Wrap vs clamp, per target: wrap anything cyclic, clamp anything that is a
  magnitude. Root Note wraps (pitch class is a circle), Scale wraps (unordered
  list), Track Shift wraps (it is a rotation around the platter). Octave clamps
  at 0 and 7 -- it is a range, not a circle, and wrapping 7 to 0 is a
  seven-octave jump.

  "Pitch" is an Aux Fn ONLY, deliberately not in the main menu. It moves root and
  octave together as a single continuous value, so turning up always raises pitch
  instead of dropping a seventh at the root wrap point. Root Note and Octave stay
  as separate Fns for when you want to move just one.

  The knob is ALWAYS bound to a target. On the live display, turning it modulates
  the currently selected Fn straight away, no press required. The button flow
  below is only a fast way to rebind it without stopping play.

  The selected Fn persists to EEPROM, and it is also settable from the main menu,
  so the binding can be chosen before a session and then simply used.

  Terms: the "Fn" is what the knob currently controls (Octave, Root Note, Scale,
  Track Shift, Pitch). The "parameter" is the value that Fn is set to.

  Three screens: the live display, the Fn select list, and the parameter screen.
  On the parameter screen, turning the knob moves to the next value AND APPLIES
  IT IMMEDIATELY, with no confirming press. That is what makes this modulation
  rather than menu navigation. There is no press-to-confirm anywhere in the flow.

  The live display shows no Fn indicator -- there is no room on 16x2, and none is
  needed, since one button press shows you the binding.

  Two ways into the parameter screen:

  - Direct: from the live display, turn the knob. That first step only opens the
    parameter screen for the current Fn, showing the current value unchanged;
    modulating starts from the next step onward.
  - Via Fn select: from the live display, press the button to open the Fn list
    (with the current Fn shown as selected), turn to choose a different Fn, then
    press to enter its parameter screen.

  THE BUTTON IS ALWAYS "BACK", so the parameter screen has to remember how it was
  reached:

  - Entered by turning the knob from the live display -> button returns to the
    live display.
  - Entered from the Fn select list -> button returns to the Fn select list.

  The Fn list also carries an "Exit" entry back to the live display.

  Watch the EEPROM writes. Saving the selected Fn is fine, since the binding
  changes rarely. Saving the modulated VALUE on every detent is not: the
  2026-06-10 audit already flags per-detent blocking writes as dropping hall
  triggers and wearing the flash-emulated EEPROM. Commit values on a dirty flag
  once the knob goes idle.

  Note the pins: aux encoder 1 is GPIO 30/31/32 per pins.md. The older "pins 34-36"
  note was stale -- 34/35 were repurposed for the TMC2209 UART (Serial8).

## Sensors

- [x] Hall sensor ON/OFF currently uses absolute deviation -- magnet polarity is ignored. Future revision: use signed deviation to differentiate north vs south pole, allowing magnet orientation to carry musical meaning.
- [ ] Sub-threshold deviation (below trigger point) could map to proximity/velocity -- e.g. attack time or note velocity scales with how close the magnet is before full trigger.

## Motion

- [x] Rework stepper firmware around a motor-state object: holds current and target speed and tweens between them via a changeTo(targetSpeed) call (AccelStepper-style). Centralizes all ramp logic instead of scattering rampTo() calls.
- [x] Stepper speed changes must ramp -- jumping to a new step rate will stall the motor. Implement a rampTo() in stepper.cpp that steps delay 1us at a time.
- [x] Allow speed control to reverse direction
- [ ] If RPM is controlled by MIDI clock input, apply it to the current direction

## Features
- [ ] Loop-lock. Passively track the last iteration (loop). Allow it to be "locked". Platter stops physically, but all other mechanisms can still work (speed, scale, octave, etc) with last detected loop. Platter often doesn't change! Input mode = live platter, buffer (stored plater), possibly 4th (aux encoder?)

## Testing

- [ ] Full system test: a single sketch (and menu option) that walks through each subsystem in sequence -- MIDI, encoders, hall sensors, stepper, audio. Runnable as a standalone flash or triggered from the menu.

## Hardware / PCB

- [x] Widen motor phase traces (J1 to A4988) from 0.25mm to 1mm+ - they carry up to 1.7A
- [x] Enlarge C1 footprint (100uF bulk on 24V) from D5mm to D8mm+ so a 35V/50V-rated cap fits
- [x] MIDI OUT: feed DIN pin 4 from +3.3V instead of +5V; change resistors to 33R (pin 4) and 10R (TX to pin 5)
- [x] Add 24V input protection after the barrel jack: fuse/polyfuse + reverse-polarity diode or P-FET

- [x] Verify pin ordering on LCD backpack and confirm it matches the UI PCB footprint

- [x] WONTFIX - Encoder debounce: add 100nF cap from each A and B pin to GND, and 10k pull-up from each button pin to 3.3V. 2 caps + 1 resistor per encoder, 6 caps + 3 resistors total for the three active encoders (menu, speed, volume). (Happy with current behavior; can fix in software if needed.)

- [x] Review all trace widths - power traces (24V, 5V) should be wider than default 0.2mm; signal traces fine at default
- [x] Plan power plane strategy for main board - 4-layer: GND on one inner layer, split power plane on other inner layer (separate zones for 24V, 5V, 3.3V with gaps between)

- [x] Confirm MS1/MS2/MS3 pins on A4988 are hardwired for 1/16 microstepping (all three pulled high)

## Repo

- [x] Init git repo in music-table/ root
- [ ] Add OnShape model share link to README

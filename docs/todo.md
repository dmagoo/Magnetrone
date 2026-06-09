# Todo / Backlog

## Firmware

- [x] Implement stepper sleep: drive PIN_SLEEP low when motor is idle to reduce heat and power draw; drive high before stepping

- [ ] On first boot (uncalibrated), show welcome screen prompting calibration with Yes/Skip options
- [ ] MIDI Clock output (0xF8, 24 ppqn) - send MIDI beat clock so external devices sync to platter BPM
- [ ] MIDI Start/Stop/Continue output (0xFA, 0xFC, 0xFB) - send when platter starts or stops
- [ ] MIDI Clock input - receive beat clock from external device and drive platter speed to match
- [ ] MIDI Start/Stop/Continue input - external device starts or stops the platter motor
- [ ] Magnet strength -> note duration: use peak ADC deviation from Hall sensor to scale how long a note plays
- [ ] Per-sensor dwell time: during calibration, record how long the magnet was over each sensor; use to scale note duration per track

## Audio (speculative)

- [ ] Voice presets: define a library of named presets (e.g. "Piano", "Strings", "Leads", "Bass", "Drums") each with a waveform type and ADSR envelope. One global setting selects the active preset for all 8 sensors.
- [ ] Drums preset: sensors trigger fixed GM drum note numbers (kick, snare, hi-hat, toms etc.) regardless of scale/root/octave settings. Each sensor uses a noise-based waveform with a per-sensor envelope tuned to its drum type.
- [ ] MIDI output channel: configurable, default 1. "Auto" mode assigns channel by preset -- Piano=1, Bass=2, Strings=3, Leads=4, Drums=10.

## Audio

- [x] Welcome tune: on boot (when cfg.playWelcomeTune is true), play each hall sensor's note in scale order at the current BPM tempo -- one note per beat, using the saved root/scale/octave settings

## Menu / UI

- [ ] Investigate reset button hang -- Teensy resets but setup() appears to hang (likely SGTL5000 I2C init); may need I2C bus reset before audioInit()
- [x] Turning on Welcome Tune from the menu plays it immediately, as a preview
- [x] Menu option: Reset Calibration -- sets hallBaseline, hallThreshold, rpmCorrection, and calibrated flag back to defaults without touching other settings
- [x] Menu option: Reset to Defaults -- wipes all saved config back to factory defaults (prompts for confirmation first)

- [x] LCD backlight timeout: configurable inactivity timeout, resets on any control interaction. Options: Always Off, 1s, 5s (default), 10s, 30s, 60s, Always On. Backlight toggled via lcd.backlight() / lcd.noBacklight() over I2C (PCF8574 backpack).
- [ ] BEATS_PER_REV configurable under Advanced menu (default 4 - one revolution = one 4/4 bar)
- [ ] sensorShift configurable under Advanced menu (default 0 - shifts which sensor index plays the root note)
- [ ] Aux encoder (pins 34-36) mappable to any menu parameter for live tweaking (key, octave, phase, etc.)

## Sensors

- [ ] Hall sensor ON/OFF currently uses absolute deviation -- magnet polarity is ignored. Future revision: use signed deviation to differentiate north vs south pole, allowing magnet orientation to carry musical meaning.
- [ ] Sub-threshold deviation (below trigger point) could map to proximity/velocity -- e.g. attack time or note velocity scales with how close the magnet is before full trigger.

## Motion

- [ ] Stepper speed changes must ramp -- jumping to a new step rate will stall the motor. Implement a rampTo() in stepper.cpp that steps delay 1us at a time.
- [ ] Allow speed control to reverse direction
- [ ] If RPM is controlled by MIDI clock input, apply it to the current direction

## Testing

- [ ] Full system test: a single sketch (and menu option) that walks through each subsystem in sequence -- MIDI, encoders, hall sensors, stepper, audio. Runnable as a standalone flash or triggered from the menu.

## Hardware / PCB

- [ ] Verify pin ordering on LCD backpack and confirm it matches the UI PCB footprint

- [ ] Encoder debounce: add 100nF cap from each A and B pin to GND, and 10k pull-up from each button pin to 3.3V. 2 caps + 1 resistor per encoder, 6 caps + 3 resistors total for the three active encoders (menu, speed, volume).

- [ ] Review all trace widths - power traces (24V, 5V) should be wider than default 0.2mm; signal traces fine at default
- [ ] Plan power plane strategy for main board - 4-layer: GND on one inner layer, split power plane on other inner layer (separate zones for 24V, 5V, 3.3V with gaps between)

- [ ] Research KiKit integration for multi-board workflow (kikit separate + kikit panelize commands)

- [ ] Confirm MS1/MS2/MS3 pins on A4988 are hardwired for 1/16 microstepping (all three pulled high)
- [ ] Remove voltage dividers from sensor array schematic - A1301 is powered at 3.3V so output is already 0-3.3V, dividers are unnecessary and reduce signal range
- [ ] Update firmware HALL_BASELINE_DEFAULT constant - current value (2069) was calculated for a 5V sensor through a 2/3 voltage divider; with A1301 at 3.3V the resting output is ~1.65V = ~2048 counts (close but should be verified against actual hardware)
- [ ] Confirm sensor supply voltage in schematic is correct (currently +3.3V for A1301 - this is intentional)

## Repo

- [ ] Init git repo in music-table/ root
- [ ] Add OnShape model share link to README

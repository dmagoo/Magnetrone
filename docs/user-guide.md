# Magnetrone User Guide

## What It Is

The Magnetrone is a spinning sequencer. You place magnets on a rotating platter,
and each magnet plays a note as it passes over a sensor arm. One revolution is
one bar, so the pattern on the platter is the pattern you hear.

The platter has eight concentric tracks, one sensor each. By default the innermost track
plays the lowest note of the current scale and the outermost plays the highest.

Sound comes from the built-in synth (headphone or line out) and from MIDI OUT,
so it can also drive external synths and send them tempo. See [MIDI](#midi).

## Controls

| Knob   | Turn                                  | Press                    |
|--------|---------------------------------------|--------------------------|
| Speed  | Platter speed. Below zero it reverses | Start or stop the platter |
| Volume | Volume                                | Mute                     |
| Menu   | Opens the main menu, then scrolls     | Opens the main menu, then selects |
| Aux    | Changes the selected function live    | Choose the function      |

Speed and Volume work from any screen, and touching either one returns to the
live display. Menus also return to the live display after 5 seconds of
inactivity.

The live display shows:

```
BPM:120 RPM:30
C  Major  [███ ]
```

The top line is tempo and platter speed (negative RPM means reverse). The bottom
line is the root note, the scale and the volume, or `[MUTE]`.

## First Run

On first power-up the screen shows **Not calibrated**. Choose **Setup** to
calibrate now, or **Skip** to do it later from **Main > Calibration**. Until it
is calibrated, some tracks may play once and then go quiet.

To calibrate:

1. Remove all magnets and choose **OK**. The platter starts spinning.
2. While the screen shows **Sampling... Keep magnets off**, keep the platter
   clear.
3. When it shows **Searching...**, place a single magnet on the outer track.
   Whichever way up it sits becomes the normal side (Layer A).
4. **Calibrated!** means it worked. **Too many magnets** means remove the extras
   and try again.

## Playing

Place magnets on the tracks and start the platter. Inner tracks are small and
fit 4 or 5 magnets per revolution; the outer tracks fit around 20, so put fast
parts on the outside.

Each magnet plays a different layer depending on which way up it sits:

- **Layer A**: the magnet the same way up as during calibration.
- **Layer B**: the magnet flipped over.

Each layer can have its own voice, octave, level, shift and direction, so one
set of magnets can play piano one way up and bass the other.

For single-voice play, leave Layer B on **Same as A** (the default). Both
sides then play the same, and it never matters which way up a magnet sits.

## Main Menu

The main menu holds every setting, including the ones the Aux knob can change
live. Each one is described under [Performance Functions](#performance-functions)
or [Setup Functions](#setup-functions).

Choosing a value in the menu saves it.

## Aux Knob

The Aux knob changes one function while the table plays, without leaving the
live display.

- **Turn** from the live display: the first click shows the current value, and
  each click after that changes it straight away.
- **Press** from the live display: opens the function list with the current one
  selected. Turn to pick another, press to start changing it.
- Press again to go back. The menu button returns straight to the live display.

**Aux changes are not saved.** They are for playing. Power off, or choose
**Reset All** at the bottom of the Aux function list, and everything returns to
the saved settings. To keep a value, set it in the main menu.

The function the knob controls is saved, so it is still selected next time.
You can also set it from **Main > Aux Fn**.

## Performance Functions

Functions meant to be changed while playing. "Layer" means the setting is in the
**Layer A** and **Layer B** submenus.

| Function    | Where  | Aux | What it does | With two layers |
|-------------|--------|-----|--------------|-----------------|
| Root Note   | Main   | Yes | Key of the scale. | Shared. Drums ignore it. |
| Scale       | Main   | Yes | Major, Minor, Pentatonic Major and Minor, Blues, Chromatic, Dorian, Mixolydian. | Shared. Drums ignore it. |
| Octave      | Main   | Yes | Base octave, 0 to 7. | Shared. Each layer can offset it. Drums ignore it. |
| Pitch       | Aux only | Yes | Moves root and octave together, so turning up always raises the pitch. Step size is set by Pitch Step. | Shared. Drums ignore it. |
| Voice       | Layer  | Yes | Piano, Strings, Leads, Bass or Drums. | Per layer. |
| Shift       | Layer  | Yes | Moves the run up by scale degrees, 0 to 7. | Per layer. Layer B follows A's shift by default; set a number to unbind it. |
| Wrap        | Layer  | No  | With Wrap, shifted notes past the top drop back to the bottom, so the run rotates across the arm. With No Wrap, the whole run moves up. | Follows Shift: if B follows A's shift, it uses A's Wrap. Drums always wrap. |
| Low Note    | Layer  | Yes | Which end of the arm plays the lowest note: Inner or Outer. | Per layer. Layer B follows A by default. On Drums it flips the kit end to end. |
| Octave (layer) | Layer | No | Offset from the main Octave, -3 to +3. | Per layer. Drums ignore it. |
| Level       | Layer  | No  | Layer volume, 0 to 100%. Also sets MIDI velocity. | Per layer. |
| A/B Balance | Aux only | Yes | Crossfades between the layers. Centre is both at full level. | Needs both layers on. |
| Mode        | Layer  | No  | On or Off. Layer B also has Same as A. | Off silences every magnet that way up. |

When Layer B is on **Same as A**, it plays exactly like Layer A, and the Aux
knob shows **Layer B is Same as A** instead of changing B's settings.

## Setup Functions

Settings you choose once and leave alone.

| Function     | Where | Aux | What it does | With two layers |
|--------------|-------|-----|--------------|-----------------|
| Channel      | Layer | No  | MIDI channel. Auto follows the voice (see [Voices and Drums](#voices-and-drums)), or pick 1 to 16. | Per layer. |
| Beats/Rev    | Main  | No  | Beats per revolution (1, 2, 3, 4, 6 or 8). Sets the BPM shown and the MIDI clock. Default 4. | Shared. |
| Welcome Tune | Main  | No  | Plays each track's Layer A note at power-up. Press the menu button to skip it. Turning it on plays it once as a preview. | Layer A only. |
| LCD Timeout  | Main  | No  | How long the backlight stays on after you touch a knob, from Always Off to Always On. | - |
| Aux Fn       | Main  | No  | Which function the Aux knob controls. | - |
| Pitch Step   | Main  | No  | How far one Aux click moves Pitch, from 1 semitone down to 1/8. Small steps give detuned, alien tunings. | - |
| Magnet Pole  | Main  | No  | Swaps which way up is Layer A. Calibration sets it. | Swaps the layers. |
| Calibration  | Main  | No  | See [First Run](#first-run). | - |
| Reset Cal    | Main  | No  | Clears calibration only. | - |
| Reset All    | Main  | No  | Returns every setting to factory defaults. Asks first. | - |

## Voices and Drums

| Voice   | Sound                     | MIDI channel (Auto) |
|---------|---------------------------|---------------------|
| Piano   | Plucky, short             | 1                   |
| Bass    | Punchy                    | 2                   |
| Strings | Slow swell, long notes    | 3                   |
| Leads   | Bright, sustained         | 4                   |
| Drums   | Drum kit, one drum per track | 10               |

The drum kit, innermost track first:

| Track | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|-------|---|---|---|---|---|---|---|---|
| Drum  | Crash | Low tom | High tom | Clap | Open hi-hat | Snare | Kick | Closed hi-hat |

The busiest drums sit on the outer tracks, which fit the most magnets. Shift
rotates the kit around the tracks, so any drum can be moved outward. A closed
hi-hat cuts off an open one, as on a real kit.

## MIDI

The Magnetrone sends on MIDI OUT:

- **Notes** on each layer's channel.
- **Clock** at the platter's tempo, so drum machines and DAWs follow it.
- **Start** and **Stop** when the platter starts and stops.
- **Pitch bend** for the Pitch function, so external synths follow detuned
  settings too.

### MIDI In (work in progress)

Planned:

- **Clock in**: the platter follows an external tempo.
- **Start and Stop in**: an external device starts and stops the platter.
- **CC in**: external knobs control octave and volume.
- **MIDI Fn**: keys on an attached keyboard drive a function, like the Aux
  knob, bound separately so both can be used at once. The pitch bend wheel
  always drives Pitch.

| MIDI Fn     | What the keys do |
|-------------|------------------|
| Pitch       | A key sets root and octave together. |
| Shift       | A key sets which note the low track plays. |
| Scale Learn | Play seven different notes and they become the scale, lowest note as root. Saved as the **Learned** scale. |

## Troubleshooting

**Speaker noise.** This is a quirk of the audio board. Turn the Magnetrone's
volume up and your speakers down.

**Loud noise from the speakers when the Magnetrone is off.** Unplug the audio
cable whenever the Magnetrone is powered off.

**Motor doesn't turn, or moves erratically.** The motor needs the 24 V supply
plugged in and the power switch on. USB power alone is not enough.

**Power switch does nothing.** The switch is ignored when the unit is powered
from USB.

**USB.** Use USB only for firmware updates and debugging, never to power the
unit. Do not connect USB while the 24 V supply is plugged in.

**No sound, or the wrong layer plays.** Check **Main > Magnet Pole**, and check
that the layer's Mode is On. If some tracks play once and then stop,
recalibrate.

# Magnetrone User Guide

## What It Is

The Magnetrone is a spinning sequencer. You place magnets on a rotating platter,
and each magnet plays a note as it passes over a sensor arm. One revolution is
one bar, so the pattern on the platter is the pattern you hear.

The platter has eight concentric tracks, one sensor each. By default the innermost track
plays the lowest note of the current scale and the outermost plays the highest.

Sound comes from the built-in synth (headphone or line out) and from MIDI OUT,
so it can also drive external synths and send them tempo. See [MIDI](#midi).

The **home screen** is where you play. It is a live display of the tempo, the
current scene, the key and the volume, and every menu leads back to it.

## Controls

| Knob   | Turn                                  | Press                    |
|--------|---------------------------------------|--------------------------|
| Speed  | Platter speed. Below zero it reverses | Start or stop the platter |
| Volume | Volume                                | Mute                     |
| Menu   | Opens the main menu, then scrolls     | Opens the main menu, then selects. Anywhere outside the main menu and its questions, it is the Home button |
| Aux    | Changes the selected function live    | Choose the function      |

Speed and Volume work from any screen, and touching either one returns to the
home screen. Menus also return to the home screen after a while untouched
(Menu Timeout, 30 seconds by default).

The home screen shows:

```
BPM:120 Scene 2*
C  Major  [███ ]
```

The top line is the tempo (negative means the platter runs in reverse) and the
current [scene](#scenes), with `*` once you have changed something since loading
it. It is blank until a scene has been loaded or saved. The bottom line is the
root note, the scale and the volume, or `[MUTE]`.

## First Run

On first power-up the screen shows **Not calibrated**. Choose **Setup** to
calibrate now, or **Skip** to do it later from **Main > Tools > Full Calibrate**. Until it
is calibrated, some tracks may play once and then go quiet.

To calibrate:

1. At **Clear platter**, remove all magnets and choose **OK**. The platter
   spins while the screen shows **Sampling...**, then stops.
2. At **Magnet on mark**, place a single magnet on the start mark, on the outer
   track, and choose **OK**. Whichever way up it sits becomes the normal side
   (Layer A).
3. **Calibrated!** means it worked. The start mark is now where each bar
   begins. On an error, fix the magnet and choose **OK** again:
   - **No magnet found**: the magnet is not on the mark.
   - **Wrong track**: move it to the outer track.
   - **Too many magnets**: remove the extras.

### The start mark

The table keeps track of where the start mark is (StartPos), so external MIDI
gear starts its bars there. It remembers this when the table is turned off with
the platter stopped. If the power goes off while the platter is spinning, the
table shows **StartPos unknown** at power-up. Choose **Find** to set it again,
or **Skip**. Turning the platter by hand while the table is off moves the mark
without the table knowing.

**Tools > Calib. StartPos** sets it again without a full calibration:

- **Auto**: place one magnet on the start mark, with nothing else on the outer
  track. Magnets on the other tracks can stay. Choose **OK** and the platter
  spins until it has found the mark.
- **Manual**: turn the menu knob to move the platter until the mark is under
  the arm, press, then choose **Yes**. **More** goes back to moving it.

To check it, choose **Tools > Go to StartPos**. The platter turns until the
mark should be under the arm.

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

Calibration and maintenance live in **Main > Tools**.

## Aux Knob

The Aux knob changes one function while the table plays, without leaving the
home screen.

- **Turn** from the home screen: the first click shows the current value, and
  each click after that changes it straight away.
- **Press** from the home screen: opens the function list with the current one
  selected. Turn to pick another, press to start changing it.
- Press again to go back. The menu button returns straight to the home screen.

The function list:

| Entry | |
|-------|-|
| Octave, Root Note, Scale, Pitch | Shared by both layers. |
| Layer A >, Layer B > | Open that layer's own functions: Voice, Octave, Shift, Low Note. |
| A/B Balance, Load Scene | See the tables below and [Scenes](#scenes). |
| Save Scene | Saves the current setup as a [scene](#scenes). |
| Reset All | Undoes every Aux change. |
| Exit | Back to the home screen. |

**Aux changes are not saved.** They are for playing. Power off, or choose
**Reset All** at the bottom of the Aux function list, and everything returns to
the saved settings. To keep a value, set it in the main menu, or save the whole
setup as a scene.

The function the knob controls is saved, so it is still selected next time.
You can also set it from **Main > Aux Fn**.

## Scenes

A scene saves everything the Aux knob can change, so a setup you like can be
brought back mid-song. There are 8, plus a fixed **Defaults** scene that holds
the factory settings.

- **Save Scene** (in the Aux function list): turn the Aux knob to pick a slot
  and press. Saving over a used slot asks first.
- **Load Scene** (an Aux function): turn the Aux knob to step through the saved
  scenes. The one you land on loads at the start of the next bar, when the
  start mark passes the arm, so you can pick it any time and it lands on the
  downbeat. With the platter stopped it loads at once.

Each slot shows its root and scale, such as `2: D Minor`, or `3: (empty)`.
**0: Defaults** comes first in the Load Scene list. It can be loaded but not
saved over, and shows as `Scene 0` on the home screen.

Loading or saving a scene also saves its settings, as if you had picked them in
the main menu. At power-up the table plays the last scene, plus any main menu
changes made since. **Reset All** in the Aux list goes back to it.

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
| Octave (layer) | Layer | Yes | Offset from the main Octave, -3 to +3. | Per layer. Drums ignore it. |
| Level       | Layer  | No  | Layer volume, 0 to 100%. Also sets MIDI velocity. | Per layer. |
| A/B Balance | Aux only | Yes | Crossfades between the layers. Centre is both at full level. | Needs both layers on. |
| Load Scene  | Aux only | Yes | Loads a saved scene at the next bar. See [Scenes](#scenes). | Covers both layers. |
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
| Menu Timeout | Main  | No  | How long a menu waits untouched before returning to the home screen: 5 s, 10 s, 30 s, 1 min or Never. Default 30 s. | - |
| Aux Fn       | Main  | No  | Which function the Aux knob controls. | - |
| Pitch Step   | Main  | No  | How far one Aux click moves Pitch, from 1 semitone down to 1/8. Small steps give detuned, alien tunings. | - |
| Go to StartPos  | Tools | No | Turns the platter until the start mark is under the arm. See [The start mark](#the-start-mark). | - |
| Full Calibrate  | Tools | No | See [First Run](#first-run). | - |
| Reset Calib.    | Tools | No | Clears calibration only, including the start mark. | - |
| Calib. StartPos | Tools | No | Sets the start mark again, Auto or Manual. See [The start mark](#the-start-mark). | - |
| Magnet Pole     | Tools | No | Swaps which way up is Layer A. Calibration sets it. | Swaps the layers. |
| StartPos Check  | Tools | No | On: at power-up, offers to find the start mark if it was lost. Off: never asks. Default On. | - |
| Info            | Tools | No | Read-only pages, turned through with the menu knob: belt ratio, StartPos (and where the platter is in the bar now), threshold, and whether the motor driver is answering. | - |
| Sensor Levels   | Tools | No | Live reading of all 8 sensors, 1 to 4 on top and 5 to 8 below: which way each is pushed (+ or -) and by how much. Pass a magnet over a track to see its sensor respond. | - |
| Reset Settings  | Tools | No | Returns every setting to factory defaults, keeping calibration, the start mark and your scenes. Asks first. | - |
| Factory Reset   | Tools | No | Erases everything, scenes and calibration included. Asks first. | - |
| MIDI Fn         | Main  | No | What keys on an attached MIDI keyboard do. See [MIDI In](#midi-in). Default Off. | - |
| MIDI In         | Layer | No | The MIDI channel this layer listens on, or Off. Default: Layer A 1, Layer B 2. | Per layer. |

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
- **Clock** that follows the platter exactly, speed changes included, so drum
  machines and DAWs follow it.
- **Start**, **Stop** and **Continue** when the platter starts and stops. Once
  the start mark is set, starting also sends the position in the bar, so the
  external bars line up with the mark even when the platter starts mid-bar.
  Some gear ignores the position.
- **Pitch bend** for the Pitch function, so external synths follow detuned
  settings too.

### MIDI In

MIDI IN only controls the table; incoming notes never play its sound.

Each layer listens on its own channel (**Layer > MIDI In**). A message on a
layer's channel applies to that layer, and with both layers on one channel it
applies to both. Shared settings (root, scale, octave, pitch, volume) respond
on either layer's channel.

- **Keys** drive the **MIDI Fn** (**Main > MIDI Fn**), much as the Aux knob
  drives its function.
- **Pitch bend wheel** bends the whole table up to 2 semitones either way and
  springs back.
- **CC 7** sets the volume. **CC 20** sets the octave. (A CC is the message a
  knob or slider on a controller sends; most controllers let you choose the
  number.)

| MIDI Fn     | What the keys do |
|-------------|------------------|
| Off         | Nothing. |
| Pitch       | A key sets root and octave together: G3 makes the root G, octave 3. |
| Shift       | A key sets which note the layer's low track plays. A key outside the scale picks the nearest scale note. Drum layers ignore it. |
| Scale Learn | Play seven different notes and they become the scale, shown as **Learned**, with the lowest note as root. Keep going and each new note replaces the oldest. |
| Chord       | Single-finger chords, as on arranger keyboards. The highest key sets root and octave; extra keys to its left pick the scale: none = Major, a black key = Minor, a white key = Mixolydian (7th), both = Dorian (minor 7th). Keys pressed together count as one chord. |

Like Aux changes, all of this is live and not saved, except the volume. Save a
scene to keep a learned scale.

Not yet: following an external clock.

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

**MIDI keys do odd things, or Scale Learn learns the wrong notes.** The other
device or DAW is probably echoing the table's MIDI OUT back to its input. Turn
off MIDI thru (echo) on that device.

**No sound, or the wrong layer plays.** Check **Main > Tools > Magnet Pole**, and check
that the layer's Mode is On. If some tracks play once and then stop,
recalibrate.

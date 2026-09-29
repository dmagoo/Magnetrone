# Magnetrone User Guide

## What It Is

The Magnetrone is a spinning sequencer. You place magnets on a rotating platter,
and each magnet plays a note as it passes over a sensor arm. One revolution is
one bar, so the pattern on the platter is the pattern you hear.

The platter has eight concentric tracks, one sensor each. By default the
innermost track plays the lowest note of the current scale and the outermost
plays the highest.

The platter is marked in 16 equal divisions. Each one is a **step**, so one
revolution is 16 steps.

Sound comes from the built-in synth (headphone or line out) and from MIDI OUT,
so it can also drive external synths and send them tempo. See [MIDI](#midi).

The **home screen** is where you play. It is a live display of the tempo, the
current scene, the key and the volume, and every menu leads back to it.

## Controls

| Knob   | Turn                                  | Press                    |
|--------|---------------------------------------|--------------------------|
| Menu   | Opens the main menu, then scrolls     | Opens the main menu, then selects. On the Aux screens, returns to the home screen |
| Speed  | Platter speed. Below zero it reverses | Start or stop the platter |
| Volume | Volume                                | Mute                     |
| Aux    | Changes the selected function live    | Opens the Aux function list. In the main menu, returns to the home screen |

Speed and Volume work from any screen, and touching either one returns to the
home screen. Menus also return to the home screen after a while untouched
(Menu Timeout, 30 seconds by default). Calibration and reset questions are the
exception: they stay on screen until answered.

In this guide, **Menu >** paths start at the main menu and **Aux >** paths
start at the Aux function list.

The home screen shows:

```
BPM:120 Scene 2*
C  Major  [███ ]
```

The top line is the tempo (negative means the platter runs in reverse) and the
current [scene](#scenes), with `*` once you have changed something with the Aux
knob since loading it. The bottom line is Layer A's root note and scale, and the
volume, or `[MUTE]`.

## First Run

On first power-up the screen shows **Not calibrated**. Choose **Setup** to
calibrate now, or **Skip** to do it later from **Menu > Tools > Full
Calibrate**. Until it is calibrated, some tracks may play once and then go
quiet.

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

**Menu > Tools > Calib. StartPos** sets it again without a full calibration:

- **Auto**: place one magnet on the start mark, with nothing else on the outer
  track. Magnets on the other tracks can stay. Choose **OK** and the platter
  spins until it has found the mark.
- **Manual**: turn the Menu knob to move the platter until the mark is under
  the arm, press, then choose **Yes**. **More** goes back to moving it.

To check it, choose **Menu > Tools > Go to StartPos**. The platter turns until the
mark should be under the arm.

## Playing

Place magnets on the tracks and start the platter. Inner tracks are small and
fit 4 or 5 magnets per revolution; the outer tracks fit around 20, so put fast
parts on the outside.

Each magnet plays a different layer depending on which way up it sits:

- **Layer A**: the magnet the same way up as during calibration. Piano by
  default.
- **Layer B**: the magnet flipped over. Drums by default.

Each layer has its own voice, key, octave, level, shift and direction, so one
set of magnets can play piano one way up and bass the other.

For single-voice play, set Layer B's Mode to **Same as A**. Both sides then play
the same, and it never matters which way up a magnet sits.

## Main Menu

```
Sound Defaults   Layer A, Layer B
Play Setup       Beats/Rev, Pitch Step, Aux Fn, MIDI Fn
System           LCD Timeout, Menu Timeout, Welcome Tune, StartPos Check, Magnet Pole
Tools            calibration and maintenance
Exit
```

Each setting is described under [Performance Functions](#performance-functions)
or [Setup Functions](#setup-functions). Choosing a value saves it.

**Sound Defaults** edits the Defaults scene (see [Scenes](#scenes)), and you
hear each change as you make it. If another scene is playing, it first asks
**Load Defaults?**. In these menus, a layer's name shows `(=A)` when Layer B is
on Same as A, and `(off)` when the layer is off, since its settings then have
no effect.

## Aux Knob

The Aux knob changes one function while the table plays, without leaving the
home screen.

- **Turn** from the home screen: the first click shows the current value, and
  each click after that changes it straight away.
- **Press** from the home screen: opens the function list at the top. Turn to
  pick a function, press to start changing it.
- While changing a function, press to go back: to the home screen if you got
  there by turning, otherwise to the list. In the lists, **Back** goes up a
  level. The Menu button returns straight to the home screen.

The function list:

| Entry | What it does |
|-------|--------------|
| Pitch | Moves both layers together. |
| Layer A, Layer B | Open that layer's own functions: Voice, Voice Edit, Root Note, Scale, Octave, Shift, Low Note, Wrap. |
| A/B Balance | Crossfades between the layers. See [Performance Functions](#performance-functions). |
| Load Scene | Loads a scene at the next bar. See [Scenes](#scenes). |
| Save Scene | Saves the current sound as a [scene](#scenes). |
| Reset All | Undoes every Aux change. |
| Exit | Back to the home screen. |

**Aux changes are not saved.** They are for playing, on top of the current
scene. Power off, or choose **Reset All**, and the scene comes back as it was
saved. To keep the changes, save them as a scene.

The function the knob controls is saved, so it is still selected next time.
You can also set it from **Menu > Play Setup > Aux Fn**.

### Voice Edit

**Aux > Layer A > Voice Edit** (or Layer B) tweaks the voice that layer is
playing. Turn to pick a setting, press to change it, and each click is heard
from the next note:

| Setting   | What it sets |
|-----------|--------------|
| Wave      | Sine, Triangle, Saw or Square |
| Harmonics | The wave's harmonics, H1 to H16, each 0 to 100%. See below |
| Attack    | How long the note takes to reach full volume, 0 to 2000 ms |
| Decay     | How long it then takes to fall to the Sustain level, 0 to 2000 ms |
| Sustain   | The level it holds while the note lasts, 0 to 100%. At 100% Decay does nothing |
| Release   | How long it fades after the note ends, 0 to 3000 ms |
| Length    | How long each note is held, 10 to 2000 ms |

A tweaked voice shows `*` in the Voice list, such as `Piano*`. Tweaks affect
only that layer. Like other Aux changes they are not saved, and choosing
another voice or loading a scene also drops them. **Save Scene** does not keep
them; use **Save As...** below. Drums and None cannot be edited.

#### Harmonics

**Harmonics** reshapes the wave by its harmonics, **H1** to **H16**, each with
a level from 0 to 100%. H1 is the note itself, H2 an octave above, and each
one after is a little higher again. The low ones change the character of the
sound the most; the high ones add brightness or buzz.

- The list starts with the levels of the current wave. The first change
  marks the edit: the list shows `Harmonics*` and Wave shows, for example,
  `Triangle*`. Saw and Square come out slightly duller once edited.
- Changing Wave drops the harmonic edits and plays the new wave as it is.
  Save the voice first to keep them.
- **Reset**, below H16, drops the edits and goes back to the stock wave,
  including the fuller Saw and Square.
- The overall volume stays the same however the levels are set. Only how
  loud each harmonic is compared to the others matters: all at 50% sounds the
  same as all at 100%.
- The steps widen as the level rises (0, 1, 2, 3, 5, 7, 10, 15, 20, 30, 40,
  50, 70, 100%), so each click is about the same change to the ear.

To keep a tweaked voice, choose **Save As...** at the bottom of the Voice Edit
list:

- **Custom 1** to **Custom 8**: shared by every scene. The layer then plays
  it, and it appears in every Voice list. Saving over a used one asks first,
  and changes every scene that uses it.
- **Scene 3 Voice A** (the current scene, and the layer you are editing): kept
  with this scene only, as its **Scene Voice**. The scene is set to play it
  straight away. Nothing else in the scene changes. Not offered in the
  Defaults scene.

Saving a scene to another slot copies its Scene Voice along.

## Scenes

A scene is the whole sound of the table: both layers (mode, voice, key, octave,
level, shift, wrap, low note, channel), Pitch and A/B Balance. The table always
plays one scene, shown on the home screen, with any Aux changes on top.

- **0: Defaults** is what **Sound Defaults** in the main menu edits. It is
  always there. Until you change it, it holds the factory sound.
- **1 to 8** change only when you save to them.

Using them:

- **Save Scene** (in the Aux function list): turn the Aux knob to pick a slot
  and press. The current sound, Aux changes included, goes into that slot.
  Saving over a used slot asks first.
- **Load Scene** (an Aux function): turn the Aux knob to step through the
  scenes. The one you land on loads at the start of the next bar, when the
  start mark passes the arm, so you can pick it any time and it lands on the
  downbeat. With the platter stopped it loads at once.

Each slot shows Layer A's root and scale, such as `2: D Minor`, or `3: (empty)`.

At power-up the table plays the last scene loaded or saved, without Aux changes.

## Performance Functions

Functions meant to be changed while playing. "Layer" means the setting is in
**Menu > Sound Defaults > Layer A** and **Layer B**, and each layer has its own.

| Function    | Where  | Aux | What it does | Default |
|-------------|--------|-----|--------------|---------|
| Mode        | Layer  | No  | On or Off. Layer B also has Same as A: it plays exactly like Layer A. Off silences every magnet that way up. | On |
| Voice       | Layer  | Yes | Piano, Strings, Leads, Bass, Drums or None, plus any saved Custom voices and, on the Aux, the scene's own Scene Voice. None silences the layer, handy for muting it live from the Aux. | A: Piano, B: Drums |
| Root Note   | Layer  | Yes | Key of the scale. Drums ignore it. | C |
| Scale       | Layer  | Yes | Major, Minor, Pentatonic Major and Minor, Blues, Chromatic, Dorian, Mixolydian, or a scale learned from MIDI (see [MIDI In](#midi-in)). Drums ignore it. | Major |
| Octave      | Layer  | Yes | 0 to 7. Drums ignore it. | A: 4, B: 3 |
| Shift       | Layer  | Yes | Moves the run up by scale degrees, 0 to 7. On Layer B, Same as A follows A's shift. On Drums it moves each drum to another track. | 0 |
| Wrap        | Layer  | Yes | With Wrap, shifted notes past the top drop back to the bottom, so the run rotates across the arm. With No Wrap, the whole run moves up. If B follows A's shift, it uses A's Wrap. Drums always wrap. | No Wrap |
| Low Note    | Layer  | Yes | Which end of the arm plays the lowest note: Inner or Outer. On Layer B, Same as A follows A. On Drums it flips the kit end to end. | Inner |
| Level       | Layer  | No  | Layer volume, 0 to 100%. Also sets MIDI velocity. | 100% |
| Pitch       | Aux only | Yes | Moves both layers together, root and octave as one, so turning up always raises the pitch. Step size is set by Pitch Step. Drums ignore it. | 0 |
| A/B Balance | Aux only | Yes | Crossfades between the layers. Centre is both at full level. | Centre |
| Load Scene  | Aux only | Yes | Loads a scene at the next bar. See [Scenes](#scenes). | - |

When Layer B is on **Same as A**, the Aux knob shows **Layer B is Same as A**
instead of changing B's settings.

## Setup Functions

Settings you choose once and leave alone, plus the Tools. "Layer" means the
same as above; the rest are under **Menu > Play Setup**, **Menu > System** and
**Menu > Tools**.

| Function     | Where | What it does | Default |
|--------------|-------|--------------|---------|
| Channel      | Layer | MIDI channel. Auto follows the voice (see [Voices and Drums](#voices-and-drums)), or pick 1 to 16. Saved in scenes. | Auto |
| MIDI In      | Layer | The MIDI channel this layer listens on, or Off. Not part of scenes. | A: 1, B: 2 |
| Beats/Rev    | Play Setup | Beats per revolution (1, 2, 3, 4, 6, 8, 12, 16, 24 or 32). Sets the BPM shown and the MIDI clock. | 4 |
| Pitch Step   | Play Setup | How far one Aux click moves Pitch, from 1 semitone down to 1/8. Small steps give detuned, alien tunings. | 1 semitone |
| Aux Fn       | Play Setup | Which function the Aux knob controls. | Pitch |
| MIDI Fn      | Play Setup | What keys on an attached MIDI keyboard do. See [MIDI In](#midi-in). | Off |
| LCD Timeout  | System | How long the backlight stays on after you touch a knob, from Always Off to Always On. | 5 s |
| Menu Timeout | System | How long a menu waits untouched before returning to the home screen: 5 s, 10 s, 30 s, 1 min or Never. | 30 s |
| Welcome Tune | System | Plays each track's Layer A note at power-up. Press the Menu button to skip it. Turning it on plays it once as a preview. | On |
| StartPos Check | System | On: at power-up, offers to find the start mark if it was lost. Off: never asks. | On |
| Magnet Pole  | System | Swaps which way up is Layer A. Calibration sets it. | Set by calibration |
| Go to StartPos  | Tools | Turns the platter until the start mark is under the arm. See [The start mark](#the-start-mark). | - |
| Full Calibrate  | Tools | See [First Run](#first-run). | - |
| Reset Calib.    | Tools | Clears calibration only, including the start mark. | - |
| Calib. StartPos | Tools | Sets the start mark again, Auto or Manual. See [The start mark](#the-start-mark). | - |
| Info            | Tools | Read-only pages, turned through with the Menu knob: RPM, belt ratio, StartPos (and where the platter is in the bar now), threshold, and whether the motor driver is answering. | - |
| Sensor Levels   | Tools | Live reading of all 8 sensors, 1 to 4 on top and 5 to 8 below: which way each is pushed (+ or -) and by how much. Pass a magnet over a track to see its sensor respond. | - |
| Reset Settings  | Tools | Returns every setting to factory defaults, the Defaults scene included, keeping calibration, the start mark, scenes 1 to 8 and the saved voices. Asks first. | - |
| Factory Reset   | Tools | Erases everything, scenes, saved voices and calibration included. Asks first. | - |

## Voices and Drums

| Voice   | Sound                     | MIDI channel (Auto) |
|---------|---------------------------|---------------------|
| Piano   | Plucky, short             | 1                   |
| Bass    | Punchy                    | 2                   |
| Strings | Slow swell, long notes    | 3                   |
| Leads   | Bright, sustained         | 4                   |
| Drums   | Drum kit, one drum per track | 10               |
| None    | Silent                    | -                   |

Custom voices and Scene Voices (see [Voice Edit](#voice-edit)) use the MIDI
channel of the voice they were made from.

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

Each layer listens on its own channel (**Menu > Sound Defaults > Layer A >
MIDI In**, and the same for Layer B). A message on a
layer's channel applies to that layer, and with both layers on one channel it
applies to both. Pitch bend and volume respond on either layer's channel. Layer
B on Same as A does not listen, since it plays Layer A's settings.

- **Keys** drive the **MIDI Fn** (**Menu > Play Setup > MIDI Fn**), much as the
  Aux knob drives its function.
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
scene to keep it, a learned scale included.

Not yet: following an external clock.

## Things to Try

### With two layers

Set Layer B up the same as Layer A: Mode On (not Same as A), and the same
voice, root, scale and octave. Then change one thing on Layer B. A magnet plays
the layer of whichever side faces down, so a flipped magnet plays Layer B's
version of its note.

- **Thirds.** Set B's Shift to 2. A flipped magnet plays a third above its
  Layer A note (Shift 4 gives a fifth, 5 a sixth). The intervals stay in the
  scale, so some thirds are major and some minor.
- **Another key.** Set B's Root a fifth above A's (C to G) or a third above (C
  to E). A flipped magnet plays exactly that interval higher. B is now in
  another key, so some notes clash with A.
- **Double-length runs.** Set B's Octave one above A's. The eight tracks now
  reach about two octaves: run a line up the tracks, then carry on up with
  flipped magnets.
- **Mirror.** Set B's Low Note to Outer. A flipped magnet plays the note from
  the other end of the arm.

### With one layer

- **Backwards.** Turn Speed down past zero. The platter reverses and the
  pattern plays backwards.
- **Rotating melody.** Turn Wrap on, choose Shift on the Aux knob, and turn it
  while playing. The melody rotates across the arm.
- **Slow and dense.** Set a very low speed and Beats/Rev 16 or 32
  (**Menu > Play Setup > Beats/Rev**).

## Troubleshooting

**Noise or hiss from the speakers.** The audio board adds some noise of its
own. Turn the Magnetrone's volume up and your speakers down.

**Loud noise from the speakers when the Magnetrone is off.** Unplug the audio
cable whenever the Magnetrone is powered off.

**Motor doesn't turn, or moves erratically.** The motor needs the 24 V supply
plugged in and the power switch on. USB power alone is not enough.

**Power switch does nothing.** The unit is running from USB, which the switch
does not control. Use USB only for firmware updates and debugging, never to
power the unit, and do not connect it while the 24 V supply is plugged in.

**MIDI keys do odd things, or Scale Learn learns the wrong notes.** The other
device or DAW is probably echoing the table's MIDI OUT back to its input. Turn
off MIDI thru (echo) on that device.

**No sound, or the wrong layer plays.** Check **Menu > System > Magnet Pole**,
and check that the layer's Mode is On and its Voice is not None. If some tracks
play once and then stop, recalibrate.

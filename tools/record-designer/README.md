# Record Designer

Design a record for Magnetrone: a magnet pattern on a step grid plus the
Scene Code that sets the table up to play it. Exports a full-size template
to laser cut into a platter cover, or to print and place magnets by.

Use it online at https://dmagoo.github.io/Magnetrone/tools/record-designer/, or open `index.html`
in a browser straight from disk (no install, no internet). Work is kept in
the browser's local storage.

Two views, picked at the top right: **Sequence** (grid, playback, scene)
and **Template** (preview and export). The button left of them picks the
theme: Auto (follows your system), Light or Dark.

## Grid

- One row per track, track 1 (inner) at the bottom, track 8 at the top.
  One column per step, step 1 at the left, at the start mark.
- Steps per revolution = Beats/Rev x Subdivisions (default 4 x 4 = 16).
  Changing them keeps every magnet that lands on a step of the new grid.
  Steps are named by beat: 1, 1.2, 1.3, 1.4, 2, ...
- Click an empty slot to place a magnet: A (normal pole, plays Layer A) or
  B (reversed pole, plays Layer B), whichever layer you used last (A at
  first). Click again to flip it to the other layer, again to remove it.
  Clicking a magnet already there flips it, then removes it. Right click
  removes a magnet at once. With the
  colored magnets, A is blue side
  up and B is black side up (the table's calibration sets this; Menu >
  System > Magnet Pole swaps it).
- Block collisions: striped slots would overlap a placed magnet and cannot
  be used; hover one to see which. With it off, overlapping magnets are
  outlined in red. The check uses the larger of the magnet diameter and the
  magnet hole.
- The toolbar counts the magnets placed, in all and per layer.
- Undo and Redo (Ctrl+Z, Ctrl+Y) cover grid edits: slots, Clear and step
  count changes. The history lasts until the page is closed.
- Each magnet shows what it plays with the current scene: a note ("C3"),
  or for drums the letter Track Notes uses (X crash, t low tom, T high tom,
  C clap, O open hat, S snare, K kick, H closed hat). With Stack it shows
  both layers' notes. Each row label shows what an A and a B magnet on
  that track play ("C4/X"), once if they match ("C4"); hovering an empty
  slot shows the same.

## Scene

The Scene Code fields, grouped as Sound (voice, root, octave, scale, with
Layer B's Root and Scale Same as A, on by default),
Arrangement (shift, Low Note, Wrap, Layer B's Same as A settings) and
Layers (modes, Layer Turns, Edit Turns). Settings that have no effect right now (a Drums
layer's root, a layer that is Off, Layer B on Same as A) are greyed out but
stay editable, since the code still holds them; hover one to see why.
Hover a setting's name (dotted underline) for what it does. Each Shift
option shows what the low track then plays ("1  D4", or "5  Snare" on
Drums); Low Note's options say which track is low (Inner, track 1; Outer,
track 8). Octave is a number box, 0 to 7.

**Hear: A / B** (Scene header) plays that layer's tracks 1 to 8 up and
back down in sixteenths, as the table plays its welcome tune at power-up
(the table's tune is Layer A). With **Auto** on, changing a scene setting
plays the changed layer's run, unless the grid is playing.

Beside it, the swap button (two arrows) swaps Layers A and B, and **A→B**
copies Layer A to Layer B, leaving A as it is. Both cover the settings the
two layers share: Voice, Root, Octave, Scale, Shift, Low Note, Wrap and the
turn patterns. Mode, Layer B's Same as A boxes and Layer Turns stay as they
are.

**Edit Turns** shows each layer's turns as the table does: a turn's number
if the layer's magnets are heard on that revolution, - if not, | where the
cycle ends (8 turns without one). Click a turn to change it: on, off, the
end of the cycle (not turn 1), on again. Clicking the | or past it makes the
cycle 8 turns again. Changing a turn sets Layer Turns to Custom, starting
from what it was playing; Together and Alternate keep the Custom patterns.

**Reset Scene**, under the settings, sets every scene setting to the factory
values, as with nothing saved (asks first). The grid, BPM and template
settings stay.

The code shows beside Play, with a copy button, and updates as you change
the settings. It is up to 10 characters, or 14 with Layer Turns on Custom or
a voice from E. Piano on. A code can be loaded back. Enter the
code on the table under Aux > Scene Codes > Enter. Beats/Rev is not in the
code; set it on the table under Menu > Play Setup > Beats/Rev.

## Play

Plays the grid at the BPM given, with the table's note mapping (scale
degree, Shift, Wrap, Low Note, Same as A, Stack, Off, Layer Turns, the drum
kit). The RPM that BPM means on the table is shown beside it; BPM is kept
within the table's 1 to 120 RPM at the current Beats/Rev. The sounds are
approximations of the table's voices. Turn 1 of Layer Turns is the first
revolution, and beside BPM the layers heard on the current revolution are
shown ("Turn: A + B") unless Layer Turns is Together.

- Play / Pause (Space). Pause keeps the position; Play goes on from there.
- To start (Home) moves the position back to step 1.
- The arrows button (right of Play) plays backwards, as the table does with
  Speed turned below zero. Playing, it turns round at the current step.
- Click a step number to move the position there. Paused, that column
  plays once; playing, it jumps there.
- Placing a magnet plays its note.
- The shaded band shows the position.

## Songs and sharing

The **Songs** strip under the transport keeps saved songs as chips, in
this browser's local storage.

- **Save** keeps your changes in the chip you are on (it is greyed when
  there is nothing to save). A song not saved yet (new, or started from
  Empty) is named first and becomes a chip. Until you save, the chip shows
  "(edited)".
- **Save as new** makes a new chip and leaves the one you are on as it was.
  The name is pre-filled with a copy number, "My tune (2)"; only a
  trailing "(n)" counts up, so "Waltz no. 3" becomes "Waltz no. 3 (2)".
- **Revert** discards your edits and goes back to the saved chip (asks
  first; greyed when there is nothing to revert). Clicking the edited chip
  does the same.
- Click a chip to switch to it. Playing carries on into the new song. If
  the current song has unsaved changes, it asks first.
- Double-click a chip to rename it; x removes it. **Empty** is always
  there: the factory scene on an empty grid.
- The link button beside the Scene Code copies a link holding the whole
  song and its name: grid, scene, BPM and template settings. It asks for a
  name if the song has none, and saves the song as Save does.
- Opening a link adds its song as a chip (or picks the chip it already is),
  so you can always get back to it. It points at this page's address, so
  it opens the song for anyone who has the page there, such as the hosted
  copy; a link to a file on your drive only works on your machine.

## Template

SVG or PDF, full size, on Letter or A4. If printing, print at 100% (actual
size, no fit to page) and check the 100 mm scale bar.

| Color | Lines |
|---|---|
| Red `#FF0000` | Through cut: outline (200 mm) and center hole (14 mm) |
| Green `#00FF00` | Layer A magnet holes (default 11 mm, for 10 mm magnets): cut through or kiss cut |
| Magenta `#FF00FF` | Layer B magnet holes, same size |
| Blue `#0000FF` | Score: track rings, start mark, the A/B mark beside each hole, Scene Code and Beats/Rev, song name |
| Black | Labels and scale bar, below the record. Not part of the record. |

All lines are hairlines (0.001 in). The start mark is at the top.

- Track rings are broken around every hole, with a gap 1 mm wider than the
  hole, so they never cross one. A ring with no holes is a whole circle.
- The A/B mark sits just after its hole, in step order; just before it if a
  magnet is packed right after; left off if magnets are packed on both
  sides.
- The Scene Code and Beats/Rev ("J  4 BEATS/REV") are 5 mm tall along a
  track ring, centered in the longest stretch with no magnets, outer tracks
  first; the ring is broken under them. Without room at 5 mm they are 3 mm;
  without room at 3 mm they fall back to 1.5 mm along the outer edge, and
  the Template view says so. Leaving a few steps in a row free on one track
  (outer tracks need the fewest) keeps them readable.
- **Platter marks** (Template settings: Off, 10, 12 or 16, default 16): 5 mm ticks
  across every ring, evenly spaced from the start mark, like the marks on a
  platter cover, so the template can be used as a guide on a marked cover
  without cutting it. They are the cover's, not the song's: they ignore
  Beats/Rev and the steps, are kept per browser, and are not in the song
  or its link. A tick near a hole (or its A/B mark) or across text on its
  ring is left out.
- A saved song's name is scored the same way, after the code, in the
  longest stretch left clear of magnets and the code. Without room on a
  ring it takes the outer edge, unless the code is already there; then it
  is left off the record. The Template view says so either way. It is also
  the first label below the record. Letters the template font lacks are
  dropped.

Lay the template on the platter with the center hole on the spindle and
the start mark on the platter's start mark.

## Platter direction

- **Platter forward, seen from above** (Template settings): Clockwise,
  confirmed on the table 2026-10-04. Steps run around the template in the
  order they pass the sensor arm, so this must match the table or the
  record plays backwards.

## Sources

- Track radii and magnet diameter: `firmware/include/config.h`
  (`TRACK_RADIUS_MM`, `MAGNET_DIAMETER_MM`), which match the platter CAD.
- Outline and center hole: `hardware/cad/platter/dxf/Platter - Disc.dxf`.
- Scene Code: a port of `firmware/src/sequencer/scene_code.cpp`.
- Note mapping: `firmware/src/sequencer/layers.cpp`, `sequencer.cpp`,
  `scale.cpp`, `audio/kit.h`, `audio/voice.cpp`.

If any of these change in the firmware, change them here too.

# Record Designer

Design a record for Magnetrone: a magnet pattern on a step grid plus the
Scene Code that sets the table up to play it. Exports a full-size template
to print and place magnets by, or to laser cut into a platter cover.

Open `index.html` in a browser, straight from disk. No install, no
internet. Work is kept in the browser's local storage.

## Grid

- One row per track, track 1 (inner) at the bottom, track 8 at the top.
  One column per step, step 1 at the left, at the start mark.
- Steps per revolution = Beats/Rev x Subdivisions (default 4 x 4 = 16).
  Changing them keeps every magnet that lands on a step of the new grid.
- Click a cell to cycle it: empty, A (normal pole, plays Layer A), B
  (reversed pole, plays Layer B).
- Block collisions: striped cells would overlap a placed magnet and cannot
  be used. With it off, overlapping magnets are outlined in red. The check
  uses the larger of the magnet diameter and the magnet hole.
- Each row shows what an A magnet on that track plays with the current
  scene. Show Layer B adds what a B magnet plays.

## Scene

The Scene Code fields: each layer's mode, voice, root, octave, scale,
shift, Low Note and Wrap, Layer B's Same as A settings and Layer Turns. The
code updates as you change them, and a code can be loaded back. Enter the
code on the table under Aux > Scene Codes > Enter. Beats/Rev is not in the
code; set it on the table under Menu > Play Setup > Beats/Rev.

## Play

Plays the grid at the BPM given, with the table's note mapping (scale
degree, Shift, Wrap, Low Note, Same as A, Stack, Off, Layer Turns, the drum
kit). The RPM that BPM means on the table is shown beside it; BPM is kept
within the table's 1 to 120 RPM at the current Beats/Rev. The sounds are
approximations of the table's voices. With Layer Turns on Alternate, Layer
A plays the first revolution.

## Template

SVG or PDF, full size, on Letter or A4. Print at 100% (actual size, no fit
to page) and check the 100 mm scale bar.

| Color | Lines |
|---|---|
| Red `#FF0000` | Through cut: outline (200 mm) and center hole (14 mm) |
| Green `#00FF00` | Layer A magnet holes (default 11 mm, for 10 mm magnets): cut through or kiss cut |
| Magenta `#FF00FF` | Layer B magnet holes, same size |
| Blue `#0000FF` | Score: track rings, start mark, the A/B mark beside each hole, Scene Code and Beats/Rev |
| Black | Labels and scale bar, below the record. Not part of the record. |

All lines are hairlines (0.001 in). The A/B mark sits just after its hole,
in step order. The Scene Code and Beats/Rev run along the outer edge at the
bottom; the start mark is at the top.

Lay the template on the platter with the center hole on the spindle and
the start mark on the platter's start mark.

## Values to confirm on the table

- **Platter forward, seen from above** (Template settings, default
  Clockwise). The firmware does not record which way the platter turns
  when it runs forward. Steps run around the template in the order they
  pass the sensor arm, so this must match the table or the record plays
  backwards. To check: run the platter forward and watch it from above.

## Sources

- Track radii and magnet diameter: `firmware/include/config.h`
  (`TRACK_RADIUS_MM`, `MAGNET_DIAMETER_MM`), which match the platter CAD.
- Outline and center hole: `hardware/cad/platter/dxf/Platter - Disc.dxf`.
- Scene Code: a port of `firmware/src/sequencer/scene_code.cpp`.
- Note mapping: `firmware/src/sequencer/layers.cpp`, `sequencer.cpp`,
  `scale.cpp`, `audio/kit.h`, `audio/voice.cpp`.

If any of these change in the firmware, change them here too.

# Scene Format

A scene written as JSON. It is the source format for the demo scenes built into
the firmware (the demos at the end of the **Load Scene** list), and what the table prints when it dumps
its saved scenes over USB serial. It is meant for whoever makes demos, and is
the implementation guide for the feature, not for playing the table: see the
[User Guide](user-guide.md) for that.

## Rules

- **Leave out what you did not change.** Any field left out takes its factory
  value, the same one Factory Reset gives. So a demo lists only what makes it
  different, and if the factory values change, demos that leave a field out
  follow. This applies everywhere, harmonics included. The scene that is loaded
  is always complete.
- **Names, not numbers.** Settings such as voice, scale or root are written by
  name. Names are not case sensitive; this document writes them in lowerCamel
  (`drums`, `off`, `pentatonicMinor`). An unknown name fails the build.
- **Keys are lowerCamel** (`lowNote`, `shiftSameAsA`).
- **The dump leaves out factory values** for you, so a dumped scene already
  holds only its changes. Prune further by hand.
- **Demos must not use Custom 1 to 8.** Those are edited by the player, so a
  demo would change under them. Use a scene voice (`sceneVoice`) instead.

## Scene

| Key | Value |
|-----|-------|
| `name` | The scene's name. |
| `pitch` | Pitch offset in semitones; fractions allowed. Factory 0. |
| `balance` | A/B Balance, -10 (all A) to 10 (all B). Factory 0. |
| `layerA`, `layerB` | The two layers, below. |
| `customVoices` | Dump only, printed on its own after the scenes: Custom 1 to 8 (`custom1` ... `custom8`), each a voice as below. They are shared by every scene, not part of one, so a demo cannot have this. |

## Layer

| Key | Value |
|-----|-------|
| `mode` | `on`, `off`. Layer B also: `sameAsA`, `stack`. |
| `voice` | `piano`, `strings`, `leads`, `bass`, `drums`, `none`, `custom1` to `custom8`, or `sceneVoice`. |
| `sceneVoice` | The layer's own voice, a voice as below. Used when `voice` is `sceneVoice`. |
| `channel` | `auto` (follows the voice), or a MIDI channel 1 to 16. |
| `root` | `c`, `c#`, `d`, `d#`, `e`, `f`, `f#`, `g`, `g#`, `a`, `a#`, `b`. |
| `scale` | `major`, `minor`, `pentatonicMajor`, `pentatonicMinor`, `blues`, `chromatic`, `dorian`, `mixolydian`, `learned`. |
| `learned` | The Learned scale: semitones above the root, 0 to 11. Empty if none (then `learned` plays as major). |
| `octave` | 0 to 7. |
| `level` | 0 to 100 (percent). |
| `shift` | Track Shift, 0 to 7. |
| `wrap` | `true` or `false`. |
| `shiftSameAsA` | Layer B only: play A's Shift and Wrap. |
| `lowNote` | `inner` or `outer`. |
| `lowNoteSameAsA` | Layer B only: play A's Low Note. |
| `turns` | Layer B only: Layer Turns, `together` or `alternate`. |
| `tone`, `chorus`, `delay`, `reverb` | The layer's effects, below. |

## Effects

Percentages run 0 to 100. On Layer B, each effect also takes `sameAsA`
(`true` plays A's settings for that effect).

| Effect | Keys |
|--------|------|
| `tone` | `cutoff` (`off` = 100), `resonance`. |
| `chorus` | `rate`, `depth`, `mix` (`off` = 0). |
| `delay` | `mode` (`sync` or `free`), `sync` (`1`, `1/2`, `3/8`, `1/3`, `1/4`, `1/5`, `1/6`, `3/16`, `1/8`, `1/10`, `1/12`, `1/16`, `1/32` of a beat), `time` (free time, 10 to 2400 ms), `feedback` (0 to 90), `mix` (`off` = 0). |
| `reverb` | `roomSize`, `damping`, `mix` (`off` = 0). |

## Voice

Used by `sceneVoice` and `customVoices`.

| Key | Value |
|-----|-------|
| `base` | The built-in voice it was made from (`piano` ... `drums`); sets the Auto MIDI channel. |
| `wave` | `sine`, `triangle`, `saw`, `square`. |
| `harmonics` | Up to 16 levels, 0 to 100, from the fundamental up. Present means the voice plays these instead of the stock wave; left out, it plays the stock wave. A shorter list takes the rest from the stock wave (of `wave`, or of `base`). |
| `attack`, `decay`, `release` | Milliseconds. |
| `sustain` | 0 to 100 (percent). |
| `length` | Note length, milliseconds. |
| `filter` | The voice filter: `cutoff` (`off` = 100), `resonance`, `amount`, `attack`, `decay`, `release` (milliseconds), `sustain` (percent). |

## Dumping scenes from the table

1. Save the scene you dialed in to a slot (**Aux > Save Scene**).
2. Switch the table off and unplug the 24V supply. Never connect USB while
   the 24V supply is plugged in.
3. Connect USB and open a serial monitor.
4. Type `scenes`. The table prints Defaults and every used scene 1 to 8, each
   under a `--- Scene 3 ---` line, then the used Custom voices, as JSON. It
   leaves out fields that match the factory values, and in a voice, those that
   match its `base`.
5. Copy the scene you want, from `{` to `}`, into a demo file (below). Set its
   `name` and prune it by hand. A scene playing a Custom voice has to be
   changed to a `sceneVoice` first (copy the voice from the Custom voices).

## Adding a demo

1. Add a file to `firmware/demos/`, one demo per file. The file name sets the
   of the demos in **Load Scene**, so start it with a number:
   `01-drift.json`, `02-undertow.json`.
2. Set `name`: that is what the Load Scene list shows.
3. Build. A script runs before every build, checks each demo and turns them
   into firmware data. A bad file (unknown key or name, value out of range,
   a Custom voice) fails the build with the file and the field. To check the
   files without building, run `python tools/build_demos.py` in `firmware/`.
4. Flash, and load it from **Aux > Load Scene**: the demos follow scene 8, as
   `Demo1: Drift` (the name cut to fit the display).

## Implementation

- **Dump:** the `scenes` serial command in normal mode
  (`src/sequencer/scene_dump.cpp`, called from the serial poll that also
  listens for `diag`). It compares against `storageFactoryScene()` and, for a
  voice, `storageStockVoice(base)`.
- **Build:** `tools/build_demos.py`, run by PlatformIO before each build
  (`extra_scripts = pre:...`). It reads `demos/*.json` in file name order,
  validates them, and writes `src/sequencer/demos_data.h` (generated, not in
  git). Names are matched case insensitively.
- **Factory fill:** the generated data holds only the fields each demo sets.
  The firmware builds a demo by starting from `storageFactoryScene()` and
  applying them (`src/sequencer/demos.cpp`), so a change to the factory values
  reaches every demo that leaves that field out.
- **Storage:** demos live in flash, read-only, and take none of the 8 slots.
  Their scene ids follow the slots (Demo 1 is id 9). The current scene id is
  saved as for a slot, so a demo loads again at power-up; if a later build has
  fewer demos, Defaults loads instead. Adding or reordering demo files shifts
  which demo an id names.
- **Menu:** the demos follow scene 8 in the **Load Scene** list and load like
  any scene, at the next bar. The home screen shows `Demo 1`. The `*` mark and
  Aux **Reset All** compare against the demo.
- **Scene voices:** every scene load, saved or demo, copies the scene's own
  voices into the live sound, and a layer set to Scene Voice plays that copy.
- **Saving:** **Save Scene** to a slot saves a loaded demo like any scene, its
  voices included. **Voice Edit > Save As** offers no Scene Voice in a demo,
  as in Defaults: save the scene to a slot first.

## Example

The factory Defaults scene with every field written out, except that Layer A
plays Piano as a scene voice, plus a Custom 1 as a dump would show it. A demo
would leave out everything here that matches the factory values.

```json
{
  "name": "defaults",
  "pitch": 0.0,
  "balance": 0,
  "layerA": {
    "mode": "on",
    "voice": "sceneVoice",
    "sceneVoice": {
      "base": "piano",
      "wave": "triangle",
      "attack": 5,
      "decay": 400,
      "sustain": 20,
      "release": 400,
      "length": 250,
      "filter": {
        "cutoff": "off",
        "resonance": 0,
        "amount": 0,
        "attack": 5,
        "decay": 300,
        "sustain": 50,
        "release": 300
      }
    },
    "channel": "auto",
    "root": "c",
    "scale": "major",
    "learned": [],
    "octave": 4,
    "level": 100,
    "shift": 0,
    "wrap": false,
    "lowNote": "inner",
    "tone":   { "cutoff": "off", "resonance": 0 },
    "chorus": { "rate": 30, "depth": 50, "mix": "off" },
    "delay":  { "mode": "sync", "sync": "1/2", "time": 300, "feedback": 30, "mix": "off" },
    "reverb": { "roomSize": 50, "damping": 50, "mix": "off" }
  },
  "layerB": {
    "mode": "on",
    "voice": "drums",
    "channel": "auto",
    "root": "c",
    "scale": "major",
    "learned": [],
    "octave": 3,
    "level": 100,
    "shift": 0,
    "wrap": false,
    "shiftSameAsA": false,
    "lowNote": "inner",
    "lowNoteSameAsA": false,
    "turns": "together",
    "tone":   { "sameAsA": false, "cutoff": "off", "resonance": 0 },
    "chorus": { "sameAsA": false, "rate": 30, "depth": 50, "mix": "off" },
    "delay":  { "sameAsA": false, "mode": "sync", "sync": "1/2", "time": 300, "feedback": 30, "mix": "off" },
    "reverb": { "sameAsA": false, "roomSize": 50, "damping": 50, "mix": "off" }
  },
  "customVoices": {
    "custom1": {
      "base": "strings",
      "wave": "saw",
      "harmonics": [100, 50, 33, 25, 20, 17, 14, 12, 11, 10, 9, 8, 8, 7, 7, 6],
      "attack": 150,
      "decay": 100,
      "sustain": 80,
      "release": 600,
      "length": 800,
      "filter": {
        "cutoff": 60,
        "resonance": 20,
        "amount": 40,
        "attack": 10,
        "decay": 300,
        "sustain": 50,
        "release": 300
      }
    }
  }
}
```

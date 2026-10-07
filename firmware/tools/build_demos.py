# Turns the demo scenes in firmware/demos/*.json into src/sequencer/demos_data.h.
#
# PlatformIO runs it before every build (extra_scripts = pre:...). It can also
# be run by hand: python tools/build_demos.py
#
# The format is docs/scene-format.md. The generated data holds only the fields
# each demo sets; the firmware applies them over storageFactoryScene(), so this
# script holds no copy of the factory values. A bad file fails the build with
# the file and the field.

import glob
import json
import os
import sys

MAX_DEMOS = 246   # scene ids are a byte, after the 9 slots

VOICES = ["piano", "strings", "synth", "bass", "drums", "none",
          "epiano", "organ", "brass", "mallets", "reed", "guitar"]
VOICE_ENUM = ["Piano", "Strings", "Synth", "Bass", "Drums", "None",
              "EPiano", "Organ", "Brass", "Mallets", "Reed", "Guitar"]
BASE_VOICES = [v for v in VOICES if v != "none"]
ROOTS = ["c", "c#", "d", "d#", "e", "f", "f#", "g", "g#", "a", "a#", "b"]
SCALES = ["major", "minor", "pentatonicmajor", "pentatonicminor", "blues",
          "chromatic", "dorian", "mixolydian", "learned", "custom"]
SCALE_ENUM = ["Major", "Minor", "PentatonicMajor", "PentatonicMinor", "Blues",
              "Chromatic", "Dorian", "Mixolydian", "Learned", "Custom"]
CUSTOM_SLOTS = 8           # CUSTOM_SCALE_SLOTS in scale.h
CUSTOM_MIN, CUSTOM_MAX = -12, 35
MODES = {"on": "On", "off": "Off", "sameasa": "SameAsA", "stack": "Stack"}
WAVES = ["sine", "triangle", "saw", "square"]
WAVE_ENUM = ["Sine", "Triangle", "Saw", "Square"]
LOW_NOTES = {"inner": "Inner", "outer": "Outer"}
TURNS = {"together": "Together", "alternate": "Alternate", "custom": "Custom"}
TURN_MAX = 8
DELAY_MODES = {"sync": "Sync", "free": "Free"}
# Order is the stored position, as SYNC_TIMES in layers.cpp.
SYNC_TIMES = ["1", "1/2", "3/8", "1/3", "1/4", "1/5", "1/6", "3/16", "1/8",
              "1/10", "1/12", "1/16", "1/32"]
FX_BIT = {"tone": 0, "chorus": 1, "delay": 2, "reverb": 3}   # FxId


class DemoError(Exception):
    pass


# `path` is the field, from the top of the file: ".layerA.tone.cutoff".
def fail(path, msg):
    raise DemoError("%s: %s" % (path.lstrip(".") or "file", msg))


def check_keys(path, obj, allowed):
    if not isinstance(obj, dict):
        fail(path, "must be an object")
    for k in obj:
        if k not in allowed:
            fail(path + "." + k, "unknown key")


def integer(path, v, lo, hi):
    if isinstance(v, bool) or not isinstance(v, int):
        fail(path, "must be a whole number")
    if v < lo or v > hi:
        fail(path, "%d is out of range (%d to %d)" % (v, lo, hi))
    return v


def boolean(path, v):
    if not isinstance(v, bool):
        fail(path, "must be true or false")
    return "true" if v else "false"


def name(path, v, names):
    if not isinstance(v, str) or v.lower() not in names:
        fail(path, "unknown value %s" % json.dumps(v))
    return v.lower()


# A percentage that may also read "off" (`off_at`).
def pct(path, v, off_at=None, hi=100):
    if isinstance(v, str) and off_at is not None and v.lower() == "off":
        return off_at
    return integer(path, v, 0, hi)


def c_string(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def voice_code(path, v, out, tgt):
    check_keys(path, v, {"base", "wave", "harmonics", "attack", "decay",
                         "release", "sustain", "length", "filter"})
    if "base" not in v:
        fail(path + ".base", "missing")
    base = name(path + ".base", v["base"], BASE_VOICES)
    out.append("%s = storageStockVoice((uint8_t)VoiceId::%s);" % (tgt, VOICE_ENUM[VOICES.index(base)]))
    if "wave" in v:
        w = name(path + ".wave", v["wave"], WAVES)
        out.append("%s.wave = (uint8_t)Wave::%s;" % (tgt, WAVE_ENUM[WAVES.index(w)]))
    if "harmonics" in v:
        h = v["harmonics"]
        if not isinstance(h, list) or len(h) > 16:
            fail(path + ".harmonics", "must be a list of up to 16 levels")
        # Levels left out take the stock wave's.
        out.append("voiceHarmonicsFrom((Wave)(%s.wave & ~SLOT_HARMONICS_EDITED), %s.harmonics);" % (tgt, tgt))
        for i, lv in enumerate(h):
            out.append("%s.harmonics[%d] = %d;" % (tgt, i, integer("%s.harmonics[%d]" % (path, i), lv, 0, 100)))
        out.append("%s.wave |= SLOT_HARMONICS_EDITED;" % tgt)
    for key, field, lo, hi in (("attack", "attackMs", 0, 2000), ("decay", "decayMs", 0, 2000),
                               ("release", "releaseMs", 0, 3000), ("sustain", "sustainPct", 0, 100),
                               ("length", "noteMs", 10, 2000)):
        if key in v:
            out.append("%s.%s = %d;" % (tgt, field, integer(path + "." + key, v[key], lo, hi)))
    if "filter" in v:
        f = v["filter"]
        fp = path + ".filter"
        check_keys(fp, f, {"cutoff", "resonance", "amount", "attack", "decay", "release", "sustain"})
        ft = tgt + ".filter"
        if "cutoff" in f:
            out.append("%s.cutoff = %d;" % (ft, pct(fp + ".cutoff", f["cutoff"], off_at=100)))
        for key, field, lo, hi in (("resonance", "resonance", 0, 100), ("amount", "amount", 0, 100),
                                   ("sustain", "sustainPct", 0, 100), ("attack", "attackMs", 0, 2000),
                                   ("decay", "decayMs", 0, 2000), ("release", "releaseMs", 0, 3000)):
            if key in f:
                out.append("%s.%s = %d;" % (ft, field, integer(fp + "." + key, f[key], lo, hi)))


def fx_code(path, key, fx, is_b, out, tgt):
    keys = {"tone": {"cutoff", "resonance"},
            "chorus": {"rate", "depth", "mix"},
            "delay": {"mode", "sync", "time", "feedback", "mix"},
            "reverb": {"roomSize", "damping", "mix"}}[key]
    if is_b:
        keys = keys | {"sameAsA"}
    check_keys(path, fx, keys)
    t = tgt + ".fx"
    if "sameAsA" in fx:
        bit = 1 << FX_BIT[key]
        if boolean(path + ".sameAsA", fx["sameAsA"]) == "true":
            out.append("%s.sameAsA |= %d;" % (t, bit))
        else:
            out.append("%s.sameAsA &= (uint8_t)~%d;" % (t, bit))
    if key == "tone":
        if "cutoff" in fx:
            out.append("%s.cutoff = %d;" % (t, pct(path + ".cutoff", fx["cutoff"], off_at=100)))
        if "resonance" in fx:
            out.append("%s.resonance = %d;" % (t, pct(path + ".resonance", fx["resonance"])))
    elif key == "chorus":
        for k, field in (("rate", "chorusRate"), ("depth", "chorusDepth")):
            if k in fx:
                out.append("%s.%s = %d;" % (t, field, pct(path + "." + k, fx[k])))
        if "mix" in fx:
            out.append("%s.chorusMix = %d;" % (t, pct(path + ".mix", fx["mix"], off_at=0)))
    elif key == "delay":
        if "mode" in fx:
            m = name(path + ".mode", fx["mode"], DELAY_MODES)
            out.append("%s.delayMode = (uint8_t)DelayMode::%s;" % (t, DELAY_MODES[m]))
        if "sync" in fx:
            s = fx["sync"]
            if isinstance(s, int) and not isinstance(s, bool):
                s = str(s)
            if not isinstance(s, str) or s not in SYNC_TIMES:
                fail(path + ".sync", "unknown value %s" % json.dumps(fx["sync"]))
            out.append("%s.delaySync = %d;" % (t, SYNC_TIMES.index(s)))
        if "time" in fx:
            out.append("%s.delayMs = %d;" % (t, integer(path + ".time", fx["time"], 10, 2400)))
        if "feedback" in fx:
            out.append("%s.delayFeedback = %d;" % (t, pct(path + ".feedback", fx["feedback"], hi=90)))
        if "mix" in fx:
            out.append("%s.delayMix = %d;" % (t, pct(path + ".mix", fx["mix"], off_at=0)))
    elif key == "reverb":
        for k, field in (("roomSize", "roomSize"), ("damping", "damping")):
            if k in fx:
                out.append("%s.%s = %d;" % (t, field, pct(path + "." + k, fx[k])))
        if "mix" in fx:
            out.append("%s.reverbMix = %d;" % (t, pct(path + ".mix", fx["mix"], off_at=0)))


def turn_pattern(path, p):
    """A turn pattern as Edit Turns shows it ("1-3-5-7-", "1--|"): turn n's
    number if the layer plays it, - if not, then | where the cycle ends
    (left out for 8 turns). Returns (length, mask)."""
    if not isinstance(p, str):
        fail(path, "must be a string such as \"1-3|\"")
    body = p[:-1] if p.endswith("|") else p
    if not 1 <= len(body) <= TURN_MAX or (len(body) == TURN_MAX and p.endswith("|")):
        fail(path, "must be 1 to %d turns, with | after fewer than %d" % (TURN_MAX, TURN_MAX))
    if not p.endswith("|") and len(body) != TURN_MAX:
        fail(path, "fewer than %d turns must end in |" % TURN_MAX)
    mask = 0
    for i, ch in enumerate(body):
        if ch == str(i + 1):
            mask |= 1 << i
        elif ch != "-":
            fail(path, "turn %d must be %d or -" % (i + 1, i + 1))
    return len(body), mask


def layer_code(path, lay, l, out):
    is_b = l == 1
    keys = {"mode", "voice", "sceneVoice", "channel", "root", "scale", "learned", "custom",
            "octave", "level", "shift", "wrap", "lowNote", "turnPattern", "tone", "chorus",
            "delay", "reverb"}
    if is_b:
        keys |= {"shiftSameAsA", "lowNoteSameAsA", "rootSameAsA", "scaleSameAsA",
                 "voiceSameAsA", "octaveSameAsA", "turns"}
    check_keys(path, lay, keys)
    t = "s.layer[%d]" % l
    if "mode" in lay:
        modes = MODES if is_b else {k: v for k, v in MODES.items() if k in ("on", "off")}
        m = name(path + ".mode", lay["mode"], modes)
        out.append("%s.mode = LayerMode::%s;" % (t, MODES[m]))
    voice = None
    if "voice" in lay:
        v = lay["voice"]
        if isinstance(v, str) and v.lower().startswith("custom"):
            fail(path + ".voice", "demos must not use Custom 1 to 8; use sceneVoice")
        if isinstance(v, str) and v.lower() == "scenevoice":
            voice = "sceneVoice"
            out.append("%s.voice = VOICE_SCENE;" % t)
        else:
            voice = name(path + ".voice", v, VOICES)
            out.append("%s.voice = (uint8_t)VoiceId::%s;" % (t, VOICE_ENUM[VOICES.index(voice)]))
    if voice == "sceneVoice" and "sceneVoice" not in lay:
        fail(path + ".sceneVoice", "missing (voice is sceneVoice)")
    if "sceneVoice" in lay:
        if voice != "sceneVoice":
            fail(path + ".sceneVoice", "given, but voice is not sceneVoice")
        voice_code(path + ".sceneVoice", lay["sceneVoice"], out, "v[%d]" % l)
    if "channel" in lay:
        c = lay["channel"]
        if isinstance(c, str) and c.lower() == "auto":
            out.append("%s.channel = LAYER_CHANNEL_AUTO;" % t)
        else:
            out.append("%s.channel = %d;" % (t, integer(path + ".channel", c, 1, 16)))
    if "root" in lay:
        r = name(path + ".root", lay["root"], ROOTS)
        out.append("%s.root = (RootNote)%d;" % (t, ROOTS.index(r)))
    if "scale" in lay:
        sc = name(path + ".scale", lay["scale"], SCALES)
        out.append("%s.scale = Scale::%s;" % (t, SCALE_ENUM[SCALES.index(sc)]))
    if "learned" in lay:
        ln = lay["learned"]
        if not isinstance(ln, list):
            fail(path + ".learned", "must be a list of semitones")
        mask = 0
        for i, n in enumerate(ln):
            mask |= 1 << integer("%s.learned[%d]" % (path, i), n, 0, 11)
        if mask:
            mask |= 1   # the root is always in the scale
        out.append("%s.learned = 0x%03X;" % (t, mask))
    if "custom" in lay:
        cs = lay["custom"]
        if not isinstance(cs, list) or len(cs) != CUSTOM_SLOTS:
            fail(path + ".custom", "must be a list of %d semitone steps" % CUSTOM_SLOTS)
        for i, n in enumerate(cs):
            out.append("%s.custom[%d] = %d;" % (t, i, integer("%s.custom[%d]" % (path, i), n,
                                                                CUSTOM_MIN, CUSTOM_MAX)))
    for key, lo, hi in (("octave", 0, 7), ("level", 0, 100), ("shift", 0, 7)):
        if key in lay:
            out.append("%s.%s = %d;" % (t, key, integer(path + "." + key, lay[key], lo, hi)))
    for key in ("wrap", "shiftSameAsA", "lowNoteSameAsA", "rootSameAsA", "scaleSameAsA",
                "voiceSameAsA", "octaveSameAsA"):
        if key in lay:
            out.append("%s.%s = %s;" % (t, key, boolean(path + "." + key, lay[key])))
    if "lowNote" in lay:
        n = name(path + ".lowNote", lay["lowNote"], LOW_NOTES)
        out.append("%s.lowNote = (uint8_t)LowNote::%s;" % (t, LOW_NOTES[n]))
    if "turns" in lay:
        n = name(path + ".turns", lay["turns"], TURNS)
        out.append("%s.turns = LayerTurns::%s;" % (t, TURNS[n]))
    if "turnPattern" in lay:
        length, mask = turn_pattern(path + ".turnPattern", lay["turnPattern"])
        out.append("%s.turnLen = %d;" % (t, length))
        out.append("%s.turnMask = 0x%02X;" % (t, mask))
    for key in ("tone", "chorus", "delay", "reverb"):
        if key in lay:
            fx_code(path + "." + key, key, lay[key], is_b, out, t)


def demo_code(path, d):
    check_keys(path, d, {"name", "pitch", "balance", "layerA", "layerB", "customVoices",
                         "customScales"})
    if "customVoices" in d:
        fail(path + ".customVoices", "demos must not use Custom 1 to 8 (dump only)")
    if "customScales" in d:
        fail(path + ".customScales", "demos must not use Custom 1 to 8 (dump only)")
    n = d.get("name")
    if not isinstance(n, str) or not n.strip():
        fail(path + ".name", "missing")
    if any(ord(ch) < 32 or ord(ch) > 126 for ch in n):
        fail(path + ".name", "plain ASCII only (the LCD has no other characters)")
    out = []
    if "pitch" in d:
        p = d["pitch"]
        if isinstance(p, bool) or not isinstance(p, (int, float)):
            fail(path + ".pitch", "must be a number")
        out.append("s.pitch = %rf;" % float(p))
    if "balance" in d:
        out.append("s.balance = %d;" % integer(path + ".balance", d["balance"], -10, 10))
    for key, l in (("layerA", 0), ("layerB", 1)):
        if key in d:
            layer_code(path + "." + key, d[key], l, out)
    return n.strip(), out


def generate(project_dir):
    files = sorted(glob.glob(os.path.join(project_dir, "demos", "*.json")))
    if len(files) > MAX_DEMOS:
        raise DemoError("demos: %d files, at most %d" % (len(files), MAX_DEMOS))
    names, bodies = [], []
    for f in files:
        rel = "demos/" + os.path.basename(f)
        try:
            with open(f, encoding="utf-8") as fh:
                d = json.load(fh)
        except ValueError as e:
            raise DemoError("%s: not valid JSON: %s" % (rel, e))
        try:
            n, body = demo_code("", d)
        except DemoError as e:
            raise DemoError("%s: %s" % (rel, e))
        names.append(n)
        bodies.append((rel, body))

    lines = [
        "// Generated by tools/build_demos.py from firmware/demos/*.json before",
        "// each build. Do not edit: change the JSON files instead.",
        "// demos.h includes it for DEMO_COUNT; demos.cpp again, with DEMOS_DATA",
        "// defined, for the data.",
        "#ifndef DEMOS_COUNT_H",
        "#define DEMOS_COUNT_H",
        "#include <stdint.h>",
        "constexpr uint8_t DEMO_COUNT = %d;" % len(names),
        "#endif",
        "",
        "#if defined(DEMOS_DATA) && !defined(DEMOS_DATA_H)",
        "#define DEMOS_DATA_H",
        "static const char* const DEMO_NAMES[] = {",
    ]
    lines += ["    %s," % c_string(n) for n in names] or ["    \"\","]
    lines += [
        "};",
        "",
        "// Applies demo `i` over the factory scene in `s`; v[] are the layers'",
        "// scene voices, unused unless a layer sets one.",
        "static void demoApply(uint8_t i, Scene& s, VoiceSlot v[NUM_LAYERS]) {",
        "    (void)s; (void)v;",
        "    switch (i) {",
    ]
    for i, (rel, body) in enumerate(bodies):
        lines.append("    case %d:   // %s" % (i, rel))
        lines += ["        " + b for b in body]
        lines.append("        break;")
    lines += ["    default: break;", "    }", "}", "#endif", ""]
    text = "\n".join(lines)

    out = os.path.join(project_dir, "src", "sequencer", "demos_data.h")
    old = None
    if os.path.exists(out):
        with open(out, encoding="utf-8") as fh:
            old = fh.read()
    if old != text:   # unchanged: leave it, so nothing rebuilds
        with open(out, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(text)
    return len(names)


def run(project_dir):
    try:
        n = generate(project_dir)
    except DemoError as e:
        print("Demo scenes: " + str(e))
        return False
    print("Demo scenes: %d" % n)
    return True


try:
    Import("env")   # noqa: F821 -- defined when PlatformIO runs this
    if not run(env.subst("$PROJECT_DIR")):   # noqa: F821
        env.Exit(1)   # noqa: F821
except NameError:
    if __name__ == "__main__":
        sys.exit(0 if run(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))) else 1)

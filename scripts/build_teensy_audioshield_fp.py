#!/usr/bin/env python3
"""Build a merged Teensy 4.1 + Audio Shield Rev D footprint.

Reads two existing footprints (left untouched):
  - teensy.pretty/Teensy41.kicad_mod          (v5 module, electrical base)
  - teensy_audio_shield.pretty/4.0audioshield.kicad_mod (v8, outline source)

Emits a new v8 footprint that is the Teensy footprint verbatim plus the
shield's silk outline shifted onto Dwgs.User and a courtyard at the shield's
true size. Shield pin 1 -> Teensy pin 1 (GND by USB): offset (-29.21, +7.62).
"""
import re

TEENSY = r"Z:/electronics/kicad-libs/Teensy/teensy.pretty/Teensy41.kicad_mod"
SHIELD = r"Z:/electronics/kicad-libs/Teensy/teensy_audio_shield.pretty/4.0audioshield.kicad_mod"
OUT = r"Z:/electronics/music-table/hardware/kicad/shared/magnetrone.pretty/Teensy41_AudioShield_RevD.kicad_mod"

DX, DY = -29.21, 7.62          # shield frame -> teensy frame
DOC_LAYER = "Dwgs.User"        # where shield outline lands (no DRC keepout)

def num(s):
    return float(s)

# ---- parse Teensy v5 ----
tee = open(TEENSY).read()

out = []
out.append('(footprint "Teensy41_AudioShield_RevD"')
out.append('\t(version 20240108)')
out.append('\t(generator "music-table-merge")')
out.append('\t(layer "F.Cu")')
out.append('\t(descr "Teensy 4.1 with PJRC Audio Adaptor Rev D overlaid (outline on Dwgs.User, courtyard at shield size). Electrical pads are the Teensy only.")')
out.append('\t(attr through_hole)')
# reference / value as properties
out.append('\t(property "Reference" "REF**"\n\t\t(at 0 -10.16)\n\t\t(layer "F.SilkS")\n\t\t(effects (font (size 1 1) (thickness 0.15)))\n\t)')
out.append('\t(property "Value" "Teensy41_AudioShield_RevD"\n\t\t(at 0 10.16)\n\t\t(layer "F.Fab")\n\t\t(effects (font (size 1 1) (thickness 0.15)))\n\t)')

# --- Teensy graphics ---
for m in re.finditer(r'\(fp_line \(start (-?\d+\.?\d*) (-?\d+\.?\d*)\) \(end (-?\d+\.?\d*) (-?\d+\.?\d*)\) \(layer (\S+)\) \(width (-?\d+\.?\d*)\)\)', tee):
    sx,sy,ex,ey,lay,w = m.groups()
    out.append(f'\t(fp_line (start {sx} {sy}) (end {ex} {ey}) (stroke (width {w}) (type solid)) (layer "{lay}"))')

for m in re.finditer(r'\(fp_circle \(center (-?\d+\.?\d*) (-?\d+\.?\d*)\) \(end (-?\d+\.?\d*) (-?\d+\.?\d*)\) \(layer (\S+)\) \(width (-?\d+\.?\d*)\)\)', tee):
    cx,cy,ex,ey,lay,w = m.groups()
    out.append(f'\t(fp_circle (center {cx} {cy}) (end {ex} {ey}) (stroke (width {w}) (type solid)) (fill none) (layer "{lay}"))')

for m in re.finditer(r'\(fp_poly \(pts ((?:\(xy -?\d+\.?\d* -?\d+\.?\d*\) ?)+)\) \(layer (\S+)\) \(width (-?\d+\.?\d*)\)\)', tee):
    pts,lay,w = m.groups()
    out.append(f'\t(fp_poly (pts {pts.strip()}) (stroke (width {w}) (type solid)) (fill solid) (layer "{lay}"))')

for m in re.finditer(r'\(fp_text user (?:"([^"]*)"|(\S+)) \(at (-?\d+\.?\d*) (-?\d+\.?\d*)(?: (-?\d+\.?\d*))?\) \(layer (\S+)\)\s*\(effects \(font \(size (\S+) (\S+)\) \(thickness (\S+)\)\)\)', tee):
    q,uq,x,y,rot,lay,sw,sh,th = m.groups()
    txt = q if q is not None else uq
    at = f'{x} {y}' + (f' {rot}' if rot else '')
    out.append(f'\t(fp_text user "{txt}" (at {at}) (layer "{lay}") (effects (font (size {sw} {sh}) (thickness {th}))))')

# --- Teensy pads ---
for m in re.finditer(r'\(pad (\S+) (\S+) (\S+) \(at (-?\d+\.?\d*) (-?\d+\.?\d*)(?: (-?\d+\.?\d*))?\) \(size (\S+) (\S+)\) \(drill (\S+)\) \(layers ([^)]*)\)\)', tee):
    pn,typ,shp,x,y,rot,sx,sy,dr,lays = m.groups()
    at = f'{x} {y}' + (f' {rot}' if rot else '')
    layl = ' '.join(f'"{l}"' for l in lays.split())
    out.append(f'\t(pad "{pn}" {typ} {shp} (at {at}) (size {sx} {sy}) (drill {dr}) (layers {layl}))')

# --- shield silk outline -> Dwgs.User, translated ---
shd = open(SHIELD).read()
sxs=[]; sys=[]
for m in re.finditer(r'\(fp_line\s+\(start (-?\d+\.?\d*) (-?\d+\.?\d*)\)\s+\(end (-?\d+\.?\d*) (-?\d+\.?\d*)\).*?\(layer "([^"]+)"\)', shd, re.S):
    sx,sy,ex,ey,lay = m.groups()
    if lay != "F.SilkS":
        continue
    nsx,nsy,nex,ney = num(sx)+DX, num(sy)+DY, num(ex)+DX, num(ey)+DY
    sxs += [nsx,nex]; sys += [nsy,ney]
    out.append(f'\t(fp_line (start {nsx:.4f} {nsy:.4f}) (end {nex:.4f} {ney:.4f}) (stroke (width 0.12) (type solid)) (layer "{DOC_LAYER}"))')

# shield silk arcs/circles (jack rings etc.) -> Dwgs.User
for m in re.finditer(r'\(fp_circle\s+\(center (-?\d+\.?\d*) (-?\d+\.?\d*)\)\s+\(end (-?\d+\.?\d*) (-?\d+\.?\d*)\).*?\(layer "([^"]+)"\)', shd, re.S):
    cx,cy,ex,ey,lay = m.groups()
    if lay != "F.SilkS":
        continue
    out.append(f'\t(fp_circle (center {num(cx)+DX:.4f} {num(cy)+DY:.4f}) (end {num(ex)+DX:.4f} {num(ey)+DY:.4f}) (stroke (width 0.12) (type solid)) (fill none) (layer "{DOC_LAYER}"))')

# --- courtyard at shield outline bbox (translated) ---
x0,x1 = min(sxs), max(sxs)
y0,y1 = min(sys), max(sys)
for (ax,ay,bx,by) in [(x0,y0,x1,y0),(x1,y0,x1,y1),(x1,y1,x0,y1),(x0,y1,x0,y0)]:
    out.append(f'\t(fp_line (start {ax:.4f} {ay:.4f}) (end {bx:.4f} {by:.4f}) (stroke (width 0.05) (type solid)) (layer "F.CrtYd"))')

# label on doc layer
cx = (x0+x1)/2; cy = (y0+y1)/2
out.append(f'\t(fp_text user "AUDIO SHIELD REV D (overhead)" (at {cx:.3f} {cy:.3f}) (layer "{DOC_LAYER}") (effects (font (size 1 1) (thickness 0.15))))')

# --- Teensy 3D model ---
mm = re.search(r'\(model (\S+)\s*\(offset \(xyz (\S+) (\S+) (\S+)\)\)\s*\(scale \(xyz (\S+) (\S+) (\S+)\)\)\s*\(rotate \(xyz (\S+) (\S+) (\S+)\)\)', tee)
if mm:
    p,ox,oy,oz,scx,scy,scz,rx,ry,rz = mm.groups()
    out.append(f'\t(model "{p}"\n\t\t(offset (xyz {ox} {oy} {oz}))\n\t\t(scale (xyz {scx} {scy} {scz}))\n\t\t(rotate (xyz {rx} {ry} {rz}))\n\t)')

out.append(')')

open(OUT, "w", encoding="utf-8").write("\n".join(out) + "\n")
print("wrote", OUT)
print(f"shield courtyard bbox  x {x0:.3f}..{x1:.3f}  y {y0:.3f}..{y1:.3f}")

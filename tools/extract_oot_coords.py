#!/usr/bin/env python3
"""
Extract map coordinates from your own Ocarina of Time ROM for Zombies Mode.

Reads scene/room data straight out of the ROM (NTSC/PAL, .z64/.v64/.n64) and writes, per scene:
  * spawn points, actor placements per room, transition actors (doors), collision bounds
  * a coarse ASCII "walkable grid" from the floor collision (so layouts can be placed on real floor)
  * every positions both in absolute world units and RELATIVE TO A SPAWN POINT, which is exactly
    how Zombies Mode stores layouts (relative to where Link starts)
  * which entrance indices lead into the scene (the warp entrance for the Start button)
  * optionally a top-down PNG (needs Pillow) or SVG

Nothing from the ROM is copied into the output except numbers (positions, ids). Keep the ROM itself
OUT of git. Only needs Python 3.8+, no packages (Pillow optional for --render png).

Examples
  python3 extract_oot_coords.py oot.z64 --list
  python3 extract_oot_coords.py oot.z64 --scene 0x63 --render
  python3 extract_oot_coords.py oot.z64 --scene 0x63,0x49 --setup 3 --spawn 0 --render
  python3 extract_oot_coords.py oot.z64 --find-entrance 0x157

Scene names are from memory and only a hint; --list prints room/actor/size stats so you can
confirm which scene is which.
"""
import argparse
import hashlib
import json
import os
import struct
import sys

SCENE_COUNT_EXPECTED = 110
ENTRANCE_COUNT_EXPECTED = 1556

# From memory of the decomp scene table. Treat as hints and verify with --list / --render.
SCENE_NAME_HINTS = {
    0x00: "Deku Tree", 0x01: "Dodongo's Cavern", 0x02: "Jabu-Jabu", 0x03: "Forest Temple",
    0x04: "Fire Temple", 0x05: "Water Temple", 0x06: "Spirit Temple", 0x07: "Shadow Temple",
    0x08: "Bottom of the Well", 0x09: "Ice Cavern",
    0x42: "Market entrance (day)", 0x43: "Market entrance (night)", 0x44: "Market entrance (ruins)",
    0x45: "Back alley (day)", 0x46: "Back alley (night)", 0x47: "Market (day)",
    0x48: "Market (night)", 0x49: "Market (ruins)", 0x4A: "Temple of Time ext (day)",
    0x4B: "Temple of Time ext (night)", 0x4C: "Temple of Time ext (ruins)",
    0x51: "Hyrule Field", 0x52: "Kakariko Village", 0x53: "Graveyard", 0x54: "Zora's River",
    0x55: "Kokiri Forest", 0x56: "Sacred Forest Meadow", 0x57: "Lake Hylia", 0x58: "Zora's Domain",
    0x59: "Zora's Fountain", 0x5A: "Gerudo Valley", 0x5B: "Lost Woods", 0x5C: "Desert Colossus",
    0x5D: "Gerudo's Fortress", 0x5E: "Haunted Wasteland", 0x5F: "Hyrule Castle",
    0x60: "Death Mountain Trail", 0x61: "Death Mountain Crater", 0x62: "Goron City",
    0x63: "Lon Lon Ranch", 0x64: "Outside Ganon's Castle",
}


def die(msg):
    print("error: " + msg, file=sys.stderr)
    sys.exit(1)


def u32(b, o):
    return struct.unpack_from(">I", b, o)[0]


def u16(b, o):
    return struct.unpack_from(">H", b, o)[0]


def s16(b, o):
    return struct.unpack_from(">h", b, o)[0]


# ---------------------------------------------------------------- ROM / DMA
def normalize_rom(d):
    d = bytearray(d[: len(d) // 4 * 4])
    head = bytes(d[:4])
    if head == b"\x80\x37\x12\x40":
        return d, "z64 (big endian)"
    if head == b"\x37\x80\x40\x12":
        d[0::2], d[1::2] = bytes(d[1::2]), bytes(d[0::2])
        return d, "v64 (byte swapped)"
    if head == b"\x40\x12\x37\x80":
        c = bytes(d)
        d[0::4], d[1::4], d[2::4], d[3::4] = c[3::4], c[2::4], c[1::4], c[0::4]
        return d, "n64 (little endian)"
    die("not an N64 ROM (unknown header %s)" % head.hex())


def yaz0_decompress(src):
    if src[:4] != b"Yaz0":
        die("expected Yaz0 data")
    size = u32(src, 4)
    out = bytearray(size)
    s, d = 16, 0
    n = len(src)
    while d < size and s < n:
        code = src[s]
        s += 1
        for _ in range(8):
            if d >= size:
                break
            if code & 0x80:
                out[d] = src[s]
                d += 1
                s += 1
            else:
                b1, b2 = src[s], src[s + 1]
                s += 2
                dist = ((b1 & 0xF) << 8) | b2
                cnt = b1 >> 4
                if cnt == 0:
                    cnt = src[s] + 0x12
                    s += 1
                else:
                    cnt += 2
                ref = d - (dist + 1)
                for _ in range(cnt):
                    if d >= size:
                        break
                    out[d] = out[ref]
                    d += 1
                    ref += 1
            code = (code << 1) & 0xFF
    return bytes(out)


class Rom:
    def __init__(self, raw):
        self.d, self.fmt = normalize_rom(raw)
        self.title = bytes(self.d[0x20:0x34]).decode("ascii", "replace").strip()
        self.code = bytes(self.d[0x3B:0x3F]).decode("ascii", "replace")
        self.version = self.d[0x3F]
        self.crc = (u32(self.d, 0x10), u32(self.d, 0x14))
        self.entries = self._read_dmadata()
        self.by_virt = {e[0]: e for e in self.entries}
        self._cache = {}

    def _read_dmadata(self):
        d = self.d
        limit = min(len(d), 0x200000) - 32
        for off in range(0, limit, 16):
            vs, ve, ps, pe = struct.unpack_from(">4I", d, off)
            if vs == 0 and ps == 0 and pe == 0 and 0x400 < ve < 0x10000 and u32(d, off + 16) == ve:
                self.dmadata_offset = off
                break
        else:
            die("could not find the file table (dmadata). Is this an Ocarina of Time ROM?")
        entries = []
        i = 0
        while off + i * 16 + 16 <= len(d) and i < 4000:
            vs, ve, ps, pe = struct.unpack_from(">4I", d, off + i * 16)
            if vs == 0 and ve == 0 and i > 0:
                break
            if ve < vs:
                break
            entries.append((vs, ve, ps, pe))
            i += 1
        return entries

    def file(self, vs):
        if vs in self._cache:
            return self._cache[vs]
        e = self.by_virt.get(vs)
        if e is None or e[2] == 0xFFFFFFFF:
            return None
        _, ve, ps, pe = e
        if pe == 0:
            data = bytes(self.d[ps : ps + (ve - vs)])
        else:
            comp = bytes(self.d[ps:pe])
            data = yaz0_decompress(comp) if comp[:4] == b"Yaz0" else comp
        self._cache[vs] = data
        return data

    def file_range(self, vs, ve):
        e = self.by_virt.get(vs)
        return e is not None and e[1] == ve


# ---------------------------------------------------------------- locating tables
def find_scene_table(rom):
    """Scene table = run of 20-byte records whose first two words are a file's virtual start/end."""
    best = (0, None, 0)  # run, data, byte offset
    for vs, ve, ps, pe in rom.entries:
        size = ve - vs
        if size < 0x20000 or size > 0x600000:
            continue
        data = rom.file(vs)
        if not data:
            continue
        n = len(data) // 4
        words = struct.unpack(">%dI" % n, data[: n * 4])
        ends = {e[0]: e[1] for e in rom.entries}
        ok = [words[j] in ends and ends[words[j]] == words[j + 1] and words[j] != 0 for j in range(n - 1)]
        run = [0] * (n + 5)
        for j in range(n - 2, -1, -1):
            if ok[j]:
                run[j] = 1 + (run[j + 5] if j + 5 < n else 0)
        j = max(range(n), key=lambda k: run[k]) if n else 0
        if run[j] > best[0]:
            best = (run[j], data, j * 4)
    if best[0] < 60:
        die("could not find the scene table in the ROM")
    return best


def find_entrance_table(code, override=None):
    if override is not None:
        return override, None
    n = len(code) // 4
    run = [0] * (n + 1)
    for i in range(n - 1, -1, -1):
        o = i * 4
        if code[o] < 0x70 and code[o + 1] < 0x40:
            run[i] = 1 + run[i + 1]
    best, best_i = 0, None
    for i in range(n):
        if run[i] > best and run[i] > 200:
            if len({code[(i + k) * 4] for k in range(min(run[i], 400))}) >= 30:
                best, best_i = run[i], i
    if best_i is None:
        return None, 0
    return best_i * 4, best


# ---------------------------------------------------------------- scene / room parsing
def seg_off(ptr):
    return ptr & 0x00FFFFFF


def walk_header(data, off):
    cmds = {}
    p = off
    while p + 8 <= len(data):
        cmd, d1 = data[p], data[p + 1]
        d2 = u32(data, p + 4)
        if cmd == 0x14:
            break
        cmds.setdefault(cmd, (d1, d2))
        p += 8
    return cmds


def pick_header(data, setup):
    main = walk_header(data, 0)
    if setup > 0 and 0x18 in main:
        lst = seg_off(main[0x18][1])
        if lst + 4 * setup <= len(data):
            ptr = u32(data, lst + 4 * (setup - 1))
            if ptr and (ptr >> 24) in (2, 3) and seg_off(ptr) < len(data):
                alt = walk_header(data, seg_off(ptr))
                for k, v in main.items():  # inherit anything the alt header doesn't define
                    alt.setdefault(k, v)
                return alt
    return main


def read_actor_entries(data, ptr, count):
    out = []
    o = seg_off(ptr)
    for i in range(count):
        if o + 16 > len(data):
            break
        aid, x, y, z, rx, ry, rz, params = struct.unpack_from(">H3h3hh", data, o)
        out.append({"id": aid, "pos": [x, y, z], "rot": [rx, ry, rz], "params": params & 0xFFFF})
        o += 16
    return out


def parse_scene(data, setup):
    h = pick_header(data, setup)
    info = {"spawns": [], "entrance_list": [], "rooms": [], "transitions": [], "collision": None}
    if 0x00 in h:
        info["spawns"] = read_actor_entries(data, h[0x00][1], h[0x00][0])
        n = h[0x00][0]
        if 0x06 in h:
            o = seg_off(h[0x06][1])
            for i in range(n):
                if o + 2 * i + 2 <= len(data):
                    info["entrance_list"].append({"spawn": data[o + 2 * i], "room": data[o + 2 * i + 1]})
    if 0x04 in h:
        o = seg_off(h[0x04][1])
        for i in range(h[0x04][0]):
            if o + 8 * i + 8 <= len(data):
                info["rooms"].append((u32(data, o + 8 * i), u32(data, o + 8 * i + 4)))
    if 0x0E in h:
        o = seg_off(h[0x0E][1])
        for i in range(h[0x0E][0]):
            q = o + 16 * i
            if q + 16 > len(data):
                break
            s = struct.unpack_from(">4b", data, q)
            aid, x, y, z, ry, params = struct.unpack_from(">h3hhh", data, q + 4)
            info["transitions"].append({"front_room": s[0], "back_room": s[2], "id": aid & 0xFFFF,
                                        "pos": [x, y, z], "rot_y": ry, "params": params & 0xFFFF})
    if 0x03 in h:
        info["collision"] = parse_collision(data, seg_off(h[0x03][1]))
    return info


def parse_collision(data, o):
    if o + 0x2C > len(data):
        return None
    mn = struct.unpack_from(">3h", data, o)
    mx = struct.unpack_from(">3h", data, o + 6)
    nv = u16(data, o + 0x0C)
    vptr = seg_off(u32(data, o + 0x10))
    npoly = u16(data, o + 0x14)
    pptr = seg_off(u32(data, o + 0x18))
    verts = [struct.unpack_from(">3h", data, vptr + 6 * i) for i in range(nv) if vptr + 6 * i + 6 <= len(data)]
    floors = []
    walls = 0
    for i in range(npoly):
        q = pptr + 16 * i
        if q + 16 > len(data):
            break
        a, b, c = (u16(data, q + 2) & 0x1FFF, u16(data, q + 4) & 0x1FFF, u16(data, q + 6))
        c &= 0x1FFF
        ny = s16(data, q + 10)
        if a >= len(verts) or b >= len(verts) or c >= len(verts):
            continue
        if ny > 0.5 * 32767:
            floors.append((verts[a], verts[b], verts[c]))
        elif abs(ny) < 0.3 * 32767:
            walls += 1
    return {"min": list(mn), "max": list(mx), "vertices": nv, "polys": npoly,
            "floor_tris": floors, "wall_polys": walls}


def parse_room(data, setup):
    h = pick_header(data, setup)
    if 0x01 in h:
        return read_actor_entries(data, h[0x01][1], h[0x01][0])
    return []


# ---------------------------------------------------------------- output helpers
def rel(p, origin):
    return [p[0] - origin[0], p[1] - origin[1], p[2] - origin[2]]


def point_in_tri(px, pz, a, b, c):
    d = (b[2] - c[2]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[2] - c[2])
    if d == 0:
        return False
    l1 = ((b[2] - c[2]) * (px - c[0]) + (c[0] - b[0]) * (pz - c[2])) / d
    l2 = ((c[2] - a[2]) * (px - c[0]) + (a[0] - c[0]) * (pz - c[2])) / d
    return l1 >= 0 and l2 >= 0 and l1 + l2 <= 1


def walkable_grid(floors, origin, cell=100):
    """ASCII rows (z down, x right), coordinates relative to origin. '#' = floor, '.' = none."""
    if not floors:
        return None
    cells = set()
    for t in floors:
        xs = [v[0] for v in t]
        zs = [v[2] for v in t]
        for gx in range(int((min(xs) - origin[0]) // cell), int((max(xs) - origin[0]) // cell) + 1):
            for gz in range(int((min(zs) - origin[2]) // cell), int((max(zs) - origin[2]) // cell) + 1):
                cx = origin[0] + gx * cell + cell / 2
                cz = origin[2] + gz * cell + cell / 2
                if point_in_tri(cx, cz, t[0], t[1], t[2]):
                    cells.add((gx, gz))
    if not cells:
        return None
    x0 = min(c[0] for c in cells)
    x1 = max(c[0] for c in cells)
    z0 = min(c[1] for c in cells)
    z1 = max(c[1] for c in cells)
    rows = ["".join("#" if (x, z) in cells else "." for x in range(x0, x1 + 1)) for z in range(z0, z1 + 1)]
    return {"cell": cell, "origin_cell_x": x0, "origin_cell_z": z0,
            "note": "column i is x = (origin_cell_x + i) * cell, row j is z = (origin_cell_z + j) * cell, "
                    "relative to the chosen spawn; the spawn is at cell (0,0)",
            "rows": rows}


def render_png(path, floors, origin, actors, spawns, trans, title):
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        return False
    if not floors:
        return False
    xs = [v[0] for t in floors for v in t]
    zs = [v[2] for t in floors for v in t]
    minx, maxx, minz, maxz = min(xs), max(xs), min(zs), max(zs)
    scale = min(2000.0 / max(1, maxx - minx), 2000.0 / max(1, maxz - minz), 1.0)
    W = int((maxx - minx) * scale) + 40
    H = int((maxz - minz) * scale) + 60

    def P(x, z):
        return (20 + (x - minx) * scale, 40 + (z - minz) * scale)

    img = Image.new("RGB", (W, H), (20, 20, 24))
    dr = ImageDraw.Draw(img)
    for t in floors:
        dr.polygon([P(v[0], v[2]) for v in t], fill=(70, 90, 70))
    gx = int((minx - origin[0]) // 500) * 500
    while gx + origin[0] <= maxx:
        x, _ = P(gx + origin[0], 0)
        dr.line([(x, 30), (x, H)], fill=(60, 60, 90))
        dr.text((x + 2, 30), str(gx), fill=(130, 130, 200))
        gx += 500
    gz = int((minz - origin[2]) // 500) * 500
    while gz + origin[2] <= maxz:
        _, y = P(0, gz + origin[2])
        dr.line([(0, y), (W, y)], fill=(60, 60, 90))
        dr.text((2, y + 1), str(gz), fill=(130, 130, 200))
        gz += 500
    for a in actors:
        x, y = P(a["pos"][0], a["pos"][2])
        dr.ellipse([x - 3, y - 3, x + 3, y + 3], fill=(240, 200, 60))
    for t in trans:
        x, y = P(t["pos"][0], t["pos"][2])
        dr.rectangle([x - 5, y - 5, x + 5, y + 5], outline=(80, 160, 255), width=2)
    for i, s in enumerate(spawns):
        x, y = P(s["pos"][0], s["pos"][2])
        dr.ellipse([x - 7, y - 7, x + 7, y + 7], outline=(255, 70, 70), width=3)
        dr.text((x + 9, y - 6), "spawn %d" % i, fill=(255, 120, 120))
    dr.text((10, 8), title + "   (green=floor, yellow=actors, blue squares=doors/transitions, "
            "red=spawns; grid labels are RELATIVE to the chosen spawn)", fill=(255, 255, 255))
    img.save(path)
    return True


def render_svg(path, floors, origin, actors, spawns, trans, title):
    if not floors:
        return False
    xs = [v[0] for t in floors for v in t]
    zs = [v[2] for t in floors for v in t]
    minx, minz = min(xs), min(zs)
    scale = min(2000.0 / max(1, max(xs) - minx), 2000.0 / max(1, max(zs) - minz), 1.0)
    W = (max(xs) - minx) * scale + 40
    H = (max(zs) - minz) * scale + 60

    def P(x, z):
        return "%.1f,%.1f" % (20 + (x - minx) * scale, 40 + (z - minz) * scale)

    out = ['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" style="background:#141418">' % (W, H),
           '<text x="10" y="20" fill="white" font-size="14">%s</text>' % title]
    for t in floors:
        out.append('<polygon points="%s" fill="#465a46"/>' % " ".join(P(v[0], v[2]) for v in t))
    for a in actors:
        x, y = P(a["pos"][0], a["pos"][2]).split(",")
        out.append('<circle cx="%s" cy="%s" r="3" fill="#f0c83c"/>' % (x, y))
    for s in spawns:
        x, y = P(s["pos"][0], s["pos"][2]).split(",")
        out.append('<circle cx="%s" cy="%s" r="7" fill="none" stroke="#ff4646" stroke-width="3"/>' % (x, y))
    out.append("</svg>")
    open(path, "w").write("\n".join(out))
    return True


# ---------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rom")
    ap.add_argument("--list", action="store_true", help="print a stats table of every scene")
    ap.add_argument("--scene", help="scene number(s), e.g. 0x63 or 0x63,0x49")
    ap.add_argument("--find-entrance", help="entrance index to look up (scene + spawn)")
    ap.add_argument("--setup", type=int, default=0,
                    help="scene setup: 0 child day, 1 child night, 2 adult day, 3 adult night (default 0)")
    ap.add_argument("--spawn", type=int, default=0, help="spawn index used as the relative origin (default 0)")
    ap.add_argument("--cell", type=int, default=100, help="walkable-grid cell size in units (default 100)")
    ap.add_argument("--render", action="store_true", help="write a top-down PNG (Pillow) or SVG")
    ap.add_argument("--entrance-offset", type=lambda x: int(x, 0), help="override entrance table offset in the code file")
    ap.add_argument("--out", default="oot_coords_out")
    a = ap.parse_args()

    raw = open(a.rom, "rb").read()
    rom = Rom(raw)
    print("ROM: %s [%s] code=%s ver=%d CRC=%08X %08X  sha1=%s" % (
        rom.title, rom.fmt, rom.code, rom.version, rom.crc[0], rom.crc[1], hashlib.sha1(rom.d).hexdigest()))
    print("file table at 0x%X, %d entries" % (rom.dmadata_offset, len(rom.entries)))

    run, code, off = find_scene_table(rom)
    print("scene table: %d scenes (expected %d)%s" % (run, SCENE_COUNT_EXPECTED,
          "" if run == SCENE_COUNT_EXPECTED else "  <-- unexpected, indices may be off"))
    scenes = []
    for i in range(run):
        o = off + 20 * i
        scenes.append((u32(code, o), u32(code, o + 4)))

    eoff, ecount = find_entrance_table(code, a.entrance_offset)
    entrances = None
    if eoff is None:
        print("warning: entrance table not found; entrance lookups unavailable (try --entrance-offset)")
    else:
        if ecount is not None:
            print("entrance table: %d entries%s" % (ecount, "" if ecount == ENTRANCE_COUNT_EXPECTED else
                  "  <-- expected %d; if this is wrong pass --entrance-offset" % ENTRANCE_COUNT_EXPECTED))
        entrances = [(code[eoff + 4 * i], code[eoff + 4 * i + 1]) for i in range(
            (ecount or ENTRANCE_COUNT_EXPECTED))]
        if len(code) < eoff + 4 * len(entrances):
            entrances = entrances[: (len(code) - eoff) // 4]

    def entrances_for(scene):
        if entrances is None:
            return []
        return [{"entrance": "0x%04X" % i, "spawn": sp} for i, (sc, sp) in enumerate(entrances) if sc == scene]

    if a.find_entrance:
        idx = int(a.find_entrance, 0)
        if entrances is None or idx >= len(entrances):
            die("entrance table unavailable or index out of range")
        sc, sp = entrances[idx]
        print("entrance 0x%04X -> scene 0x%02X (%s), spawn %d" % (idx, sc, SCENE_NAME_HINTS.get(sc, "?"), sp))

    if a.list:
        print("\n idx   name hint                      rooms spawns floors  bounds (min..max x, z)")
        for i, (vs, ve) in enumerate(scenes):
            data = rom.file(vs)
            if not data:
                print(" 0x%02X  (no file)" % i)
                continue
            inf = parse_scene(data, a.setup)
            c = inf["collision"]
            bounds = ("%d..%d, %d..%d" % (c["min"][0], c["max"][0], c["min"][2], c["max"][2])) if c else "-"
            print(" 0x%02X  %-30s %5d %6d %6d  %s" % (i, SCENE_NAME_HINTS.get(i, "?"), len(inf["rooms"]),
                  len(inf["spawns"]), len(c["floor_tris"]) if c else 0, bounds))

    if a.scene:
        os.makedirs(a.out, exist_ok=True)
        for tok in a.scene.split(","):
            sid = int(tok, 0)
            if sid >= len(scenes):
                die("scene 0x%X out of range" % sid)
            vs, ve = scenes[sid]
            data = rom.file(vs)
            if not data:
                die("scene 0x%X has no file" % sid)
            inf = parse_scene(data, a.setup)
            if not inf["spawns"]:
                print("scene 0x%02X: no spawn list at setup %d" % (sid, a.setup))
                continue
            sp = min(a.spawn, len(inf["spawns"]) - 1)
            origin = inf["spawns"][sp]["pos"]
            rooms_out, all_actors = [], []
            for ri, (rs, re_) in enumerate(inf["rooms"]):
                rd = rom.file(rs)
                acts = parse_room(rd, a.setup) if rd else []
                all_actors += acts
                rooms_out.append({"room": ri, "actors": [dict(x, rel=rel(x["pos"], origin)) for x in acts]})
            c = inf["collision"]
            floors = c["floor_tris"] if c else []
            name = SCENE_NAME_HINTS.get(sid, "scene_%02X" % sid)
            result = {
                "scene": sid, "name_hint": name, "setup": a.setup,
                "origin_spawn": sp, "origin_world": origin,
                "note": "all 'rel' values are relative to origin_world (the chosen spawn); x,z are the map plane, y is height",
                "entrances_into_scene": entrances_for(sid),
                "spawns": [dict(s, rel=rel(s["pos"], origin)) for s in inf["spawns"]],
                "entrance_list": inf["entrance_list"],
                "transitions": [dict(t, rel=rel(t["pos"], origin)) for t in inf["transitions"]],
                "rooms": rooms_out,
                "collision": None if not c else {"min": c["min"], "max": c["max"], "floor_tris": len(floors),
                                                  "wall_polys": c["wall_polys"], "polys": c["polys"]},
                "walkable_grid": walkable_grid(floors, origin, a.cell),
            }
            base = os.path.join(a.out, "scene_%02X_setup%d" % (sid, a.setup))
            with open(base + ".json", "w") as f:
                json.dump(result, f, indent=1)
            print("\nscene 0x%02X (%s) setup %d -> %s.json" % (sid, name, a.setup, base))
            print("  rooms %d, spawns %d, doors/transitions %d, floor triangles %d" % (
                len(inf["rooms"]), len(inf["spawns"]), len(inf["transitions"]), len(floors)))
            for i, s in enumerate(inf["spawns"]):
                print("  spawn %d at %s%s" % (i, s["pos"], "  <- origin" if i == sp else ""))
            ents = entrances_for(sid)
            print("  entrance indices into this scene: %s" % (
                ", ".join("%s(spawn %d)" % (e["entrance"], e["spawn"]) for e in ents) or "none found"))
            if a.render:
                title = "scene 0x%02X %s setup %d" % (sid, name, a.setup)
                if render_png(base + ".png", floors, origin, all_actors, inf["spawns"], inf["transitions"], title):
                    print("  wrote %s.png" % base)
                elif render_svg(base + ".svg", floors, origin, all_actors, inf["spawns"], title=title, trans=inf["transitions"]):
                    print("  Pillow not installed, wrote %s.svg instead (pip install pillow for PNG)" % base)
    if not (a.list or a.scene or a.find_entrance):
        print("\nnothing to do: pass --list, --scene, or --find-entrance")


if __name__ == "__main__":
    main()

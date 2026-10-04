#!/usr/bin/env python3
"""Self-test for extract_oot_coords.py using a synthetic ROM (no copyrighted data)."""
import json
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
P = lambda fmt, *a: struct.pack(">" + fmt, *a)


def yaz0_literals(data):  # valid Yaz0 stream, all literals
    out = bytearray(b"Yaz0" + P("I", len(data)) + bytes(8))
    for i in range(0, len(data), 8):
        chunk = data[i:i + 8]
        out.append(((0xFF << (8 - len(chunk))) & 0xFF) if len(chunk) < 8 else 0xFF)
        out += chunk
    return bytes(out)


def build_scene():
    d = bytearray(0x400)
    cmds = [(0x00, 2, 0x02000100), (0x06, 0, 0x02000140), (0x04, 1, 0x02000160),
            (0x0E, 1, 0x02000180), (0x03, 0, 0x020001C0), (0x14, 0, 0)]
    for i, (c, n, p) in enumerate(cmds):
        d[i * 8:i * 8 + 8] = P("BBHI", c, n, 0, p)
    # spawns: id 0 (player) at (1000, 20, -500) and (1100, 20, -500)
    d[0x100:0x110] = P("h3h3hh", 0, 1000, 20, -500, 0, 0, 0, 0x0FFF)
    d[0x110:0x120] = P("h3h3hh", 0, 1100, 20, -500, 0, 0, 0, 0x0FFF)
    d[0x140:0x144] = bytes([0, 0, 1, 0])           # entrance list: (spawn0, room0), (spawn1, room0)
    d[0x160:0x168] = P("II", 0x00200000, 0x00200100)  # room 0 file
    d[0x180:0x190] = P("4bh3hhh", 0, 0, 0, 0, 0x0009, 1000, 20, -900, 0, 0x1234)  # door
    # collision: floor square x 1000..1400, z -500..-100
    d[0x1C0:0x1C0 + 0x2C] = P("6hHHIHHIIIHHI", 1000, 20, -500, 1400, 20, -100, 4, 0, 0x02000200,
                              2, 0, 0x02000220, 0, 0, 0, 0, 0)
    for i, v in enumerate([(1000, 20, -500), (1400, 20, -500), (1400, 20, -100), (1000, 20, -100)]):
        d[0x200 + 6 * i:0x206 + 6 * i] = P("3h", *v)
    up = 32767
    d[0x220:0x230] = P("HHHHhhhh", 0, 0, 1, 2, 0, up, 0, -20)
    d[0x230:0x240] = P("HHHHhhhh", 0, 0, 2, 3, 0, up, 0, -20)
    return bytes(d)


def build_room():
    d = bytearray(0x100)
    d[0:8] = P("BBHI", 0x01, 2, 0, 0x03000040)
    d[8:16] = P("BBHI", 0x14, 0, 0, 0)
    d[0x40:0x50] = P("h3h3hh", 0x0015, 1200, 20, -300, 0, 0, 0, 7)
    d[0x50:0x60] = P("h3h3hh", 0x0090, 1050, 20, -150, 0, 90, 0, 0)
    return bytes(d)


def build_rom():
    rom = bytearray(0x400000)
    rom[0:4] = bytes([0x80, 0x37, 0x12, 0x40])
    rom[0x20:0x34] = b"THE LEGEND OF ZELDA "
    rom[0x3B:0x3F] = b"NZLE"
    scene, room = build_scene(), build_room()
    code = bytearray(b"\xff" * 0x30000)
    files = {}  # name -> (vs, ve, data, compressed?)
    files["makerom"] = (0x0, 0x1060, b"", False)
    files["code"] = (0x10000, 0x40000, bytes(code), False)
    files["sceneA"] = (0x60000, 0x60400, scene, True)
    files["sceneB"] = (0x70000, 0x70400, scene, False)
    files["room"] = (0x200000, 0x200100, room, False)
    # scene table (110 entries) at code+0x1000, everything points at sceneA except 0x63 -> sceneB
    st = bytearray()
    for i in range(110):
        vs, ve = (0x70000, 0x70400) if i == 0x63 else (0x60000, 0x60400)
        st += P("IIIIBBBB", vs, ve, 0, 0, 0, 0, 0, 0)
    code[0x1000:0x1000 + len(st)] = st
    # entrance table (1556 records) at code+0x8000; entrance 0x157 -> scene 0x63 spawn 1
    et = bytearray()
    for i in range(1556):
        sc, sp = (i * 7) % 100, i % 5
        if i == 0x157:
            sc, sp = 0x63, 1
        et += bytes([sc, sp, 0, 0])
    code[0x8000:0x8000 + len(et)] = et
    files["code"] = (0x10000, 0x40000, bytes(code), False)
    # place files in ROM (phys) and build dmadata at 0x7430
    phys = 0x100000
    dm = []
    for name, (vs, ve, data, comp) in files.items():
        if name == "makerom":
            dm.append((vs, ve, 0, 0))
        elif comp:
            blob = yaz0_literals(data)
            rom[phys:phys + len(blob)] = blob
            dm.append((vs, ve, phys, phys + len(blob)))
            phys += (len(blob) + 15) // 16 * 16
        else:
            rom[phys:phys + len(data)] = data
            dm.append((vs, ve, phys, 0))
            phys += (len(data) + 15) // 16 * 16
    # contiguous virt requirement for the 2nd entry (finder checks entry1.vs == makerom.ve)
    dm.insert(1, (0x1060, 0x1160, 0x1060, 0))
    dm.sort(key=lambda e: e[0])
    for i, e in enumerate(dm):
        rom[0x7430 + 16 * i:0x7430 + 16 * i + 16] = P("4I", *e)
    return bytes(rom)


def convert(rom, kind):
    b = bytearray(rom)
    if kind == "v64":
        b[0::2], b[1::2] = bytes(rom[1::2]), bytes(rom[0::2])
    elif kind == "n64":
        b[0::4], b[1::4], b[2::4], b[3::4] = rom[3::4], rom[2::4], rom[1::4], rom[0::4]
    return bytes(b)


def run(*args):
    r = subprocess.run([sys.executable, os.path.join(HERE, "extract_oot_coords.py")] + list(args),
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stderr + r.stdout
    return r.stdout


def main():
    rom = build_rom()
    with tempfile.TemporaryDirectory() as tmp:
        for kind in ("z64", "v64", "n64"):
            path = os.path.join(tmp, "t." + kind)
            open(path, "wb").write(convert(rom, kind))
            out = run(path, "--find-entrance", "0x157")
            assert "scene 0x63" in out and "spawn 1" in out, out
            assert "scene table: 110 scenes" in out, out
            assert "entrance table: 1556 entries" in out, out
        path = os.path.join(tmp, "t.z64")
        open(path, "wb").write(rom)
        out = run(path, "--list")
        assert "0x63" in out and "Lon Lon Ranch" in out
        for scene in ("0x63", "0x05"):  # 0x63 uncompressed, 0x05 compressed (Yaz0)
            out = run(path, "--scene", scene, "--render", "--out", tmp)
            j = json.load(open(os.path.join(tmp, "scene_%02X_setup0.json" % int(scene, 16))))
            assert j["origin_world"] == [1000, 20, -500]
            assert j["spawns"][1]["rel"] == [100, 0, 0]
            assert j["transitions"][0]["rel"] == [0, 0, -400] and j["transitions"][0]["id"] == 9
            acts = j["rooms"][0]["actors"]
            assert len(acts) == 2 and acts[0]["rel"] == [200, 0, 200] and acts[1]["id"] == 0x90
            assert j["collision"]["floor_tris"] == 2
            g = j["walkable_grid"]
            assert g["rows"] and all(set(r) <= set("#.") for r in g["rows"]) and len(g["rows"]) == 4, g
            assert j["entrances_into_scene"] or scene == "0x05"
        j = json.load(open(os.path.join(tmp, "scene_63_setup0.json")))
        assert {"entrance": "0x0157", "spawn": 1} in j["entrances_into_scene"]
        assert os.path.exists(os.path.join(tmp, "scene_63_setup0.png")) or os.path.exists(
            os.path.join(tmp, "scene_63_setup0.svg"))
    # Yaz0 back-reference path: "ab" then copy 6 bytes from distance 2 => "abababab"
    sys.path.insert(0, HERE)
    import extract_oot_coords as x
    # header(16) + code 0xC0 (two literals, then a back-reference) + 'a' 'b' + ref(count 4+2, dist 1+1)
    stream = b"Yaz0" + P("I", 8) + bytes(8) + bytes([0xC0, 97, 98, 0x40, 0x01])
    assert x.yaz0_decompress(stream) == b"abababab", x.yaz0_decompress(stream)
    print("all tests passed")


if __name__ == "__main__":
    main()

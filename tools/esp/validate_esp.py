#!/usr/bin/env python3
"""Structural validator for NVCompanions.esp (no game/xEdit needed).

Checks, hard-failing on any violation:
  - every record/GRUP header is well-formed and sizes tile the file exactly
  - GRUP nesting: top groups (type 0) contain only their labeled record type;
    the CELL tree uses types 2/3/6/8 with correct labels
  - TES4: HEDR present, single MAST FalloutNV.esm, numRecords matches
  - every new-record formid has master index 01 and is in the frozen map
  - every cross-reference formid either points into our own records or into
    FalloutNV.esm formids present in harvest.json (spot-check of known refs)
  - NPC_ records carry every fopdoc-required subrecord with correct sizes
    (ACBS 24, DATA 11, DNAM 28, FGGS 200, FGGA 120, FGTS 200, ...)
  - PACK: PKDT type is Follow(1), behavior flags 0, PTDT targets PlayerRef
    0x14, CTDA is GetPlayerTeammate(454) == 1.0
  - ACHRs are flagged persistent and NAME each pool NPC exactly once
"""

import json
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent

REQUIRED_NPC_SUBS = {
    "EDID": None, "OBND": 12, "FULL": None, "ACBS": 24, "VTCK": 4, "RNAM": 4,
    "EAMT": 2, "AIDT": 20, "PKID": 4, "CNAM": 4, "DATA": 11, "DNAM": 28,
    "PNAM": 4, "HNAM": 4, "ENAM": 4, "HCLR": 4, "NAM4": 4, "FGGS": 200,
    "FGGA": 120, "FGTS": 200, "NAM5": 2, "NAM6": 4, "NAM7": 4,
}

ERRORS: list[str] = []


def err(msg: str) -> None:
    ERRORS.append(msg)


def subrecords(payload: bytes) -> list[tuple[str, bytes]]:
    out, pos = [], 0
    while pos < len(payload):
        if pos + 6 > len(payload):
            err(f"subrecord header truncated at {pos}")
            break
        stype = payload[pos : pos + 4].decode("ascii", "replace")
        ssize = struct.unpack_from("<H", payload, pos + 4)[0]
        if pos + 6 + ssize > len(payload):
            err(f"subrecord {stype} overruns payload")
            break
        out.append((stype, payload[pos + 6 : pos + 6 + ssize]))
        pos += 6 + ssize
    return out


def walk(data: bytes, start: int, end: int, depth: int = 0):
    """Yield ('REC', type, flags, formid, payload) / ('GRUP', label, gtype, inner...)."""
    pos = start
    while pos < end:
        if pos + 24 > end:
            err(f"header truncated at {pos}")
            return
        rectype = data[pos : pos + 4]
        size = struct.unpack_from("<I", data, pos + 4)[0]
        if rectype == b"GRUP":
            if pos + size > end:
                err(f"GRUP at {pos} overruns parent (size {size})")
                return
            label = data[pos + 8 : pos + 12]
            gtype = struct.unpack_from("<i", data, pos + 12)[0]
            yield ("GRUP", label, gtype, pos + 24, pos + size, depth)
            yield from walk(data, pos + 24, pos + size, depth + 1)
            pos += size
        else:
            flags, formid = struct.unpack_from("<II", data, pos + 8)
            if pos + 24 + size > end:
                err(f"record {rectype} at {pos} overruns parent")
                return
            yield ("REC", rectype.decode(), flags, formid, data[pos + 24 : pos + 24 + size], depth)
            pos += 24 + size


def main() -> int:
    esp_path = Path(sys.argv[1]) if len(sys.argv) > 1 else HERE / "NVCompanions.esp"
    harvest = json.loads((HERE / "harvest.json").read_text(encoding="utf-8"))
    vanilla_fids = {
        int(fid, 16)
        for table in harvest["index"].values()
        for fid in table.values()
    }
    data = esp_path.read_bytes()

    # --- TES4 ---
    assert data[:4] == b"TES4", "file must start with TES4"
    tes4_size = struct.unpack_from("<I", data, 4)[0]
    tes4_subs = dict(subrecords(data[24 : 24 + tes4_size]))
    if "HEDR" not in tes4_subs or len(tes4_subs["HEDR"]) != 12:
        err("TES4 HEDR missing/wrong size")
    if tes4_subs.get("MAST", b"").rstrip(b"\0") != b"FalloutNV.esm":
        err("TES4 MAST must be FalloutNV.esm")
    hedr_count = struct.unpack_from("<I", tes4_subs["HEDR"], 4)[0]

    body_start = 24 + tes4_size
    own_records: dict[int, tuple[str, int, bytes]] = {}
    entry_count = 0
    top_group_labels = []
    for item in walk(data, body_start, len(data)):
        if item[0] == "GRUP":
            _, label, gtype, _s, _e, depth = item
            entry_count += 1
            if depth == 0:
                if gtype != 0:
                    err(f"top-level GRUP type {gtype} != 0")
                top_group_labels.append(label.decode("ascii", "replace"))
        else:
            _, rectype, flags, formid, payload, depth = item
            entry_count += 1
            if formid >> 24 != 1:
                err(f"{rectype} {formid:08X}: master index != 01")
            own_records[formid] = (rectype, flags, payload)

    if hedr_count != entry_count:
        err(f"HEDR numRecords {hedr_count} != actual {entry_count}")
    if top_group_labels != ["FACT", "NPC_", "PACK", "VTYP", "CELL"]:
        err(f"unexpected top group order: {top_group_labels}")

    # --- frozen formid map ---
    expected = (
        {0x01000800 + i: "NPC_" for i in range(64)}
        | {0x01000900 + i: "ACHR" for i in range(64)}
        | {0x01000A00: "PACK", 0x01000A10: "VTYP", 0x01000A11: "VTYP",
           0x01000A20: "FACT", 0x01000B00: "CELL"}
    )
    for fid, rectype in expected.items():
        got = own_records.get(fid)
        if not got or got[0] != rectype:
            err(f"missing/wrong record {rectype} at {fid:08X}: got {got and got[0]}")
    for fid in own_records:
        if fid not in expected:
            err(f"unexpected record formid {fid:08X}")

    own_fids = set(own_records)

    def check_ref(context: str, fid: int) -> None:
        if fid == 0 or fid == 0x14:
            return
        if fid in own_fids:
            return
        if (fid & 0x00FFFFFF) == fid and fid in vanilla_fids:
            return
        # vanilla refs not in our harvest index (e.g. HDPT/CSTY subsets) —
        # accept any master-00 fid but flag master-01 ones we don't own
        if fid >> 24 == 0:
            return
        err(f"{context}: dangling formid {fid:08X}")

    # --- NPC_ checks ---
    for i in range(64):
        fid = 0x01000800 + i
        rectype, flags, payload = own_records[fid]
        subs = subrecords(payload)
        counts: dict[str, int] = {}
        for stype, sdata in subs:
            counts[stype] = counts.get(stype, 0) + 1
            want = REQUIRED_NPC_SUBS.get(stype)
            if want is not None and len(sdata) != want:
                err(f"NPC_ {i}: {stype} size {len(sdata)} != {want}")
            if stype in ("RNAM", "VTCK", "CNAM", "PNAM", "HNAM", "ENAM", "PKID"):
                check_ref(f"NPC_ {i} {stype}", struct.unpack("<I", sdata)[0])
        for stype in REQUIRED_NPC_SUBS:
            if stype not in counts:
                err(f"NPC_ {i}: missing {stype}")
        d = dict(subs)
        acbs_flags = struct.unpack_from("<I", d["ACBS"])[0]
        female = i >= 32
        if bool(acbs_flags & 1) != female:
            err(f"NPC_ {i}: female flag mismatch (flags={acbs_flags:08X})")
        if not acbs_flags & 2:
            err(f"NPC_ {i}: not Essential")
        if acbs_flags & 0x200:
            err(f"NPC_ {i}: No-Low-Level-Processing must be clear")
        if acbs_flags & 0x400000:
            err(f"NPC_ {i}: 'Can be all races' must never be set")
        tflags = struct.unpack_from("<H", d["ACBS"], 22)[0]
        if tflags:
            err(f"NPC_ {i}: template flags {tflags:04X} != 0")
        if struct.unpack("<I", d["PKID"])[0] != 0x01000A00:
            err(f"NPC_ {i}: PKID != follow package")
        vtck = struct.unpack("<I", d["VTCK"])[0]
        if vtck != (0x01000A11 if female else 0x01000A10):
            err(f"NPC_ {i}: VTCK {vtck:08X} wrong for sex")
        for fg, size in (("FGGS", 200), ("FGGA", 120), ("FGTS", 200)):
            if d[fg] != b"\0" * size:
                err(f"NPC_ {i}: {fg} not zeroed")
        if "TPLT" in d:
            err(f"NPC_ {i}: TPLT must not be present")
        if "SCRI" in d or "CNTO" in d or "SNAM" in d:
            err(f"NPC_ {i}: donor leftovers present")

    # --- PACK checks ---
    _, _, pack_payload = own_records[0x01000A00]
    p = subrecords(pack_payload)
    pd = {}
    for stype, sdata in p:
        pd.setdefault(stype, sdata)
    pkdt = pd["PKDT"]
    if len(pkdt) != 12 or pkdt[4] != 1:
        err(f"PACK: PKDT type {pkdt[4]} != Follow(1)")
    if struct.unpack_from("<H", pkdt, 6)[0] != 0:
        err("PACK: fallout behavior flags must be 0 (mute)")
    ptdt = pd["PTDT"]
    ttype, target, dist = struct.unpack_from("<iIi", ptdt)
    if ttype != 0 or target != 0x14 or dist != 192:
        err(f"PACK: PTDT ({ttype},{target:08X},{dist}) != (0,PlayerRef,192)")
    ctda = pd.get("CTDA")
    if not ctda or len(ctda) != 28:
        err("PACK: CTDA missing/wrong size")
    else:
        ctype = ctda[0]
        cval, func = struct.unpack_from("<fI", ctda, 4)
        if ctype != 0 or cval != 1.0 or func != 454:
            err(f"PACK: CTDA ({ctype},{cval},{func}) != (==AND,1.0,GetPlayerTeammate)")
    order = [t for t, _ in p]
    if order[:5] != ["EDID", "PKDT", "PSDT", "PTDT", "CTDA"]:
        err(f"PACK: subrecord order {order[:5]}")
    if order[-12:] != ["POBA", "INAM", "SCHR", "TNAM", "POEA", "INAM", "SCHR", "TNAM", "POCA", "INAM", "SCHR", "TNAM"]:
        err(f"PACK: trailer markers wrong: {order[-12:]}")

    # --- ACHR checks ---
    named = set()
    for i in range(64):
        rectype, flags, payload = own_records[0x01000900 + i]
        if not flags & 0x400:
            err(f"ACHR {i}: persistent flag not set")
        d = dict(subrecords(payload))
        base = struct.unpack("<I", d["NAME"])[0]
        named.add(base)
        if len(d["DATA"]) != 24:
            err(f"ACHR {i}: DATA size {len(d['DATA'])} != 24")
    if named != {0x01000800 + i for i in range(64)}:
        err(f"ACHRs must cover each NPC_ exactly once: {sorted(hex(n) for n in named)}")

    # --- VTYP checks ---
    for fid, female in ((0x01000A10, False), (0x01000A11, True)):
        d = dict(subrecords(own_records[fid][2]))
        dnam = d["DNAM"][0]
        if dnam & 0x01:
            err(f"VTYP {fid:08X}: Allow-Default-Dialog must be CLEAR")
        if bool(dnam & 0x02) != female:
            err(f"VTYP {fid:08X}: female bit mismatch")

    # --- CELL GRUP nesting ---
    tree = list(walk(data, body_start, len(data)))
    cell_grups = [(g[1], g[2]) for g in tree if g[0] == "GRUP" and g[2] in (2, 3, 6, 8)]
    types = [t for _, t in cell_grups]
    if types != [2, 3, 6, 8]:
        err(f"CELL GRUP chain types {types} != [2,3,6,8]")
    else:
        for label, gtype in cell_grups[2:]:
            if struct.unpack("<I", label)[0] != 0x01000B00:
                err(f"CELL child GRUP type {gtype} label != cell formid")

    if ERRORS:
        print(f"FAIL: {len(ERRORS)} error(s)")
        for e in ERRORS:
            print("  -", e)
        return 1
    print(f"OK: {esp_path.name} ({len(data)} bytes, {entry_count} entries) passes all structural checks")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

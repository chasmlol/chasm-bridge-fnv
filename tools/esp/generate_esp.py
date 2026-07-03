#!/usr/bin/env python3
"""Generate NVCompanions.esp — the chasm companion template pool.

Authors a GECK-free FNV plugin from harvest.json (byte-exact vanilla donors
extracted from the user's FalloutNV.esm by harvest_fnv.py):

  - 8 template NPC_ records (4 male cloned from GSRingo, 4 female from
    GSSunnySmiles) with zeroed facegen, Essential, our silent voice types,
    and a single follow package. Faces/names/sex/race are overwritten at
    runtime by the NVSE plugin (TESNPC::CopyAppearance from the player).
  - 1 PACK ChasmCompanionFollow: byte-mirror of DefaultFollowPlayerFar plus
    a CTDA gate `GetPlayerTeammate == 1`, follow distance 192. Recruit =
    SetPlayerTeammate 1 + EvaluatePackage; dismiss = clear flag. The
    teammate flag persists in saves, so follow state self-restores on load.
  - 2 VTYP silent voice types (Allow-Default-Dialog CLEAR -> no vanilla
    barks/hellos; companions speak only through chasm TTS).
  - 1 holding CELL with 8 persistent ACHRs (one per template base). Spawn =
    MoveTo player; despawn = MoveTo holding cell. No PlaceAtMe: stable refs,
    no save bloat.

FormID map is FROZEN (the NVSE plugin hardcodes local formids; only append):
  NPC_ 0x000800+ (male block first, then female; grow by regenerating, max 256)
  ACHR 0x000900+ (ACHR N holds NPC_ N)
  PACK 0x000A00 (follow; 0x000A01 reserved for M3 wait package)
  VTYP 0x000A10 (male) / 0x000A11 (female)
  FACT 0x000A20 (wait-state gate, toggled by the NVSE plugin in M3)
  CELL 0x000B00

Usage: python generate_esp.py [-o NVCompanions.esp] [--harvest harvest.json]
"""

import json
import struct
import sys
from pathlib import Path

POOL_SIZE = 64
FEMALE_START = 32  # slots 32..63 are female

FID_NPC = 0x01000800
FID_ACHR = 0x01000900
FID_PACK = 0x01000A00
FID_VTYP_M = 0x01000A10
FID_VTYP_F = 0x01000A11
FID_FACT_WAIT = 0x01000A20
FID_CELL = 0x01000B00

PLAYER_REF = 0x00000014
FN_GET_PLAYER_TEAMMATE = 454  # console/script function index
FOLLOW_DISTANCE = 192

ACBS_FEMALE = 0x0001
ACBS_ESSENTIAL = 0x0002
ACBS_NO_LOW_LEVEL = 0x0200  # deliberately CLEAR: process while unloaded

MALE_DONOR = "GSRingo"
FEMALE_DONOR = "GSSunnySmiles"
PACK_DONOR = "DefaultFollowPlayerFar"

# Donor subrecords dropped from templates: factions, scripts, inventory,
# spell lists, vanilla packages, combat style (all irrelevant or replaced).
DROP_SUBS = {"SNAM", "SCRI", "SPLO", "CNTO", "COED", "PKID", "ZNAM"}


def sub(stype: str, data: bytes) -> bytes:
    assert len(data) <= 0xFFFF
    return stype.encode("ascii") + struct.pack("<H", len(data)) + data


def zstr(text: str) -> bytes:
    return text.encode("cp1252") + b"\0"


def record(rectype: str, formid: int, payload: bytes, flags: int = 0) -> bytes:
    return (
        rectype.encode("ascii")
        + struct.pack("<IIIIHH", len(payload), flags, formid, 0, 15, 0)
        + payload
    )


def grup(label: bytes, group_type: int, contents: bytes) -> bytes:
    header = (
        b"GRUP"
        + struct.pack("<I", 24 + len(contents))
        + label
        + struct.pack("<iHHI", group_type, 0, 0, 0)  # type, stamp, unknown x6
    )
    assert len(header) == 24
    return header + contents


def load_donor(harvest: dict, kind: str, edid: str) -> list[tuple[str, bytes]]:
    dumps = harvest[kind]
    if edid not in dumps:
        raise SystemExit(f"harvest.json is missing {kind}/{edid} — re-run harvest_fnv.py")
    return [(t, bytes.fromhex(d)) for t, d in dumps[edid]]


def build_npc(donor: list[tuple[str, bytes]], slot: int, female: bool) -> bytes:
    out = []
    seen_pkid = False
    for stype, data in donor:
        if stype in DROP_SUBS:
            if stype == "PKID" and not seen_pkid:
                # replace the donor's package list with our single follow package
                seen_pkid = True
                out.append(sub("PKID", struct.pack("<I", FID_PACK)))
            continue
        if stype == "EDID":
            data = zstr(f"ChasmCompanion{slot + 1:02d}")
        elif stype == "FULL":
            data = zstr(f"Companion {slot + 1}")
        elif stype == "ACBS":
            flags = ACBS_ESSENTIAL | (ACBS_FEMALE if female else 0)
            # keep donor fatigue/barter/level/speed/karma bytes, override flags
            # and zero the template flags (no TPLT in our records)
            data = struct.pack("<I", flags) + data[4:22] + b"\0\0"
        elif stype == "VTCK":
            data = struct.pack("<I", FID_VTYP_F if female else FID_VTYP_M)
        elif stype in ("FGGS", "FGTS"):
            data = b"\0" * 200
        elif stype == "FGGA":
            data = b"\0" * 120
        out.append(sub(stype, data))
    return b"".join(out)


def build_pack(donor: list[tuple[str, bytes]]) -> bytes:
    """Mirror DefaultFollowPlayerFar, add a GetPlayerTeammate==1 condition and
    tighten follow distance. Subrecord order preserved; CTDA inserted after
    PTDT per the canonical PACK layout."""
    out = []
    for stype, data in donor:
        if stype == "EDID":
            data = zstr("ChasmCompanionFollow")
        elif stype == "PTDT":
            # int32 type(0=specific ref), formid target, int32 distance, float unk
            data = struct.pack("<Ii", PLAYER_REF, FOLLOW_DISTANCE).join(
                [data[:4], data[12:16]]
            )
        out.append(sub(stype, data))
        if stype == "PTDT":
            ctda = struct.pack(
                "<B3xfIIIII",
                0x00,  # Equal-To, AND, float comparison
                1.0,
                FN_GET_PLAYER_TEAMMATE,
                0,
                0,
                0,  # run on subject
                0,
            )
            assert len(ctda) == 28
            out.append(sub("CTDA", ctda))
    return b"".join(out)


def build_vtyp(edid: str, female: bool) -> bytes:
    # DNAM: 0x01 allow default dialog (CLEAR -> silent), 0x02 female
    return sub("EDID", zstr(edid)) + sub("DNAM", bytes([0x02 if female else 0x00]))


def build_cell() -> bytes:
    return (
        sub("EDID", zstr("ChasmCompanionHold"))
        + sub("FULL", zstr("Chasm Companion Hold"))
        + sub("DATA", bytes([0x01]))  # interior
        + sub("LTMP", struct.pack("<I", 0))
    )


def build_achr(slot: int) -> bytes:
    x = 128.0 * (slot % 8) + 128.0 * 16 * (slot // 64)
    y = 128.0 * ((slot // 8) % 8)
    return (
        sub("EDID", zstr(f"ChasmCompanionRef{slot + 1:02d}"))
        + sub("NAME", struct.pack("<I", FID_NPC + slot))
        + sub("DATA", struct.pack("<6f", x, y, 0.0, 0.0, 0.0, 0.0))
    )


def count_entries(blob: bytes) -> int:
    """Count records + groups in a serialized blob (for HEDR numRecords)."""
    count, pos = 0, 0
    while pos + 24 <= len(blob):
        rectype = blob[pos : pos + 4]
        size = struct.unpack_from("<I", blob, pos + 4)[0]
        count += 1
        pos += 24 if rectype == b"GRUP" else 24 + size
    return count


def main() -> int:
    here = Path(__file__).resolve().parent
    harvest_path = here / "harvest.json"
    out_path = here / "NVCompanions.esp"
    argv = sys.argv[1:]
    if "--harvest" in argv:
        harvest_path = Path(argv[argv.index("--harvest") + 1])
    if "-o" in argv:
        out_path = Path(argv[argv.index("-o") + 1])

    harvest = json.loads(harvest_path.read_text(encoding="utf-8"))
    male_donor = load_donor(harvest, "npc_dumps", MALE_DONOR)
    female_donor = load_donor(harvest, "npc_dumps", FEMALE_DONOR)
    pack_donor = load_donor(harvest, "pack_dumps", PACK_DONOR)

    npcs = b"".join(
        record(
            "NPC_",
            FID_NPC + slot,
            build_npc(female_donor if slot >= FEMALE_START else male_donor, slot, slot >= FEMALE_START),
        )
        for slot in range(POOL_SIZE)
    )
    pack = record("PACK", FID_PACK, build_pack(pack_donor))
    vtyps = record("VTYP", FID_VTYP_M, build_vtyp("ChasmCompanionVoiceM", False)) + record(
        "VTYP", FID_VTYP_F, build_vtyp("ChasmCompanionVoiceF", True)
    )
    # DATA: flags1 (0x01 = hidden from PC), flags2, 2 unused
    fact = record(
        "FACT",
        FID_FACT_WAIT,
        sub("EDID", zstr("ChasmCompanionWaitFaction"))
        + sub("FULL", zstr("Chasm Companion Waiting"))
        + sub("DATA", bytes([0x01, 0x00, 0x00, 0x00])),
    )

    achrs = b"".join(
        record("ACHR", FID_ACHR + slot, build_achr(slot), flags=0x400)
        for slot in range(POOL_SIZE)
    )
    cell_children = grup(struct.pack("<I", FID_CELL), 8, achrs)  # persistent children
    cell_wrap = record("CELL", FID_CELL, build_cell()) + grup(
        struct.pack("<I", FID_CELL), 6, cell_children
    )
    # interior block/sub-block indices derive from the cell's object id
    block = FID_CELL % 10
    sub_block = (FID_CELL // 10) % 10
    cell_top = grup(
        struct.pack("<i", block),
        2,
        grup(struct.pack("<i", sub_block), 3, cell_wrap),
    )

    body = (
        grup(b"FACT", 0, fact)
        + grup(b"NPC_", 0, npcs)
        + grup(b"PACK", 0, pack)
        + grup(b"VTYP", 0, vtyps)
        + grup(b"CELL", 0, cell_top)
    )

    hedr = struct.pack("<fII", 1.34, count_entries(body), 0x0000C00)
    tes4 = record(
        "TES4",
        0,
        sub("HEDR", hedr)
        + sub("CNAM", zstr("chasm"))
        + sub("MAST", zstr("FalloutNV.esm"))
        + sub("DATA", struct.pack("<Q", 0)),
    )

    out_path.write_bytes(tes4 + body)
    print(f"wrote {out_path} ({out_path.stat().st_size} bytes, {count_entries(body)} entries)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

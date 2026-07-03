#!/usr/bin/env python3
"""Harvest vanilla formids + byte-exact subrecords from FalloutNV.esm.

Reads the user's FalloutNV.esm and emits harvest.json with everything
generate_esp.py needs to author NVCompanions.esp without GECK:
  - formids for races / classes / hairs / eyes (EDID -> formid)
  - the full subrecord dump (hex) of one male + one female generic NPC_
    (OBND, PNAM head-part sets, DNAM skills, HCLR, ... copied byte-exact)
  - the full subrecord dump of the DefaultFollowPlayerFar PACK so the
    generator can mirror the exact subrecord sequence + trailer markers.

Usage:
  python harvest_fnv.py ["C:\\...\\Fallout New Vegas\\Data\\FalloutNV.esm"] [-o harvest.json]

The output is committed so builds never need the game installed.
"""

import json
import struct
import sys
import zlib
from pathlib import Path

DEFAULT_ESM = r"C:\Program Files (x86)\Steam\steamapps\common\Fallout New Vegas\Data\FalloutNV.esm"

# Record types whose EDID->formid map we want.
INDEX_TYPES = {b"RACE", b"CLAS", b"HAIR", b"EYES", b"HDPT", b"VTYP", b"PACK", b"CSTY", b"FACT"}
# NPC_ records we dump in full (generic Goodsprings townsfolk, one per sex).
DUMP_NPCS = {"GSEasyPete", "GSSunnySmiles", "GSTrudy", "GSRingo"}
DUMP_PACKS = {"DefaultFollowPlayerFar", "DefaultFollowPlayerDefault"}

RECORD_FLAG_COMPRESSED = 0x00040000


def iter_records(data: bytes):
    """Yield (rectype, flags, formid, payload) for every record, recursing into GRUPs."""
    pos = 0
    end = len(data)
    while pos + 24 <= end:
        rectype = data[pos : pos + 4]
        size = struct.unpack_from("<I", data, pos + 4)[0]
        if rectype == b"GRUP":
            # group size includes the 24-byte header; recurse into contents
            inner_end = pos + size
            pos += 24
            # continue scanning linearly (records inside groups follow immediately)
            end_stack_note = inner_end  # noqa: F841  (groups are contiguous; linear scan works)
            continue
        flags, formid = struct.unpack_from("<II", data, pos + 8)
        payload = data[pos + 24 : pos + 24 + size]
        if flags & RECORD_FLAG_COMPRESSED and len(payload) >= 4:
            try:
                payload = zlib.decompress(payload[4:])
            except zlib.error:
                payload = b""
        yield rectype, flags, formid, payload
        pos += 24 + size


def iter_subrecords(payload: bytes):
    pos = 0
    while pos + 6 <= len(payload):
        stype = payload[pos : pos + 4]
        ssize = struct.unpack_from("<H", payload, pos + 4)[0]
        if stype == b"XXXX" and ssize == 4:
            real = struct.unpack_from("<I", payload, pos + 6)[0]
            pos += 6 + 4
            stype = payload[pos : pos + 4]
            pos += 6  # header of the actual subrecord (size field ignored)
            yield stype, payload[pos : pos + real]
            pos += real
            continue
        yield stype, payload[pos + 6 : pos + 6 + ssize]
        pos += 6 + ssize


def edid_of(payload: bytes) -> str:
    for stype, sdata in iter_subrecords(payload):
        if stype == b"EDID":
            return sdata.rstrip(b"\0").decode("cp1252", "replace")
    return ""


def main() -> int:
    args = [a for a in sys.argv[1:] if a != "-o"]
    out_path = Path("harvest.json")
    if "-o" in sys.argv:
        out_path = Path(sys.argv[sys.argv.index("-o") + 1])
        args = [a for a in args if str(out_path) != a]
    esm_path = Path(args[0]) if args else Path(DEFAULT_ESM)
    if not esm_path.is_file():
        print(f"FalloutNV.esm not found at {esm_path}", file=sys.stderr)
        return 1

    data = esm_path.read_bytes()
    index: dict[str, dict[str, str]] = {t.decode(): {} for t in INDEX_TYPES}
    npc_dumps: dict[str, list] = {}
    pack_dumps: dict[str, list] = {}

    for rectype, _flags, formid, payload in iter_records(data):
        if rectype in INDEX_TYPES:
            edid = edid_of(payload)
            if edid:
                index[rectype.decode()][edid] = f"{formid:08X}"
        if rectype == b"NPC_":
            edid = edid_of(payload)
            if edid in DUMP_NPCS:
                npc_dumps[edid] = [
                    [stype.decode("ascii", "replace"), sdata.hex()]
                    for stype, sdata in iter_subrecords(payload)
                ]
        elif rectype == b"PACK":
            edid = edid_of(payload)
            if edid in DUMP_PACKS:
                pack_dumps[edid] = [
                    [stype.decode("ascii", "replace"), sdata.hex()]
                    for stype, sdata in iter_subrecords(payload)
                ]

    result = {
        "source": str(esm_path),
        "index": index,
        "npc_dumps": npc_dumps,
        "pack_dumps": pack_dumps,
    }
    out_path.write_text(json.dumps(result, indent=1), encoding="utf-8")
    counts = {k: len(v) for k, v in index.items()}
    print(f"wrote {out_path}: {counts}, npcs={list(npc_dumps)}, packs={list(pack_dumps)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

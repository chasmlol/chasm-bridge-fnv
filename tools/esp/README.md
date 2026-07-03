# NVCompanions.esp tooling

- `harvest_fnv.py` — one-time extraction of vanilla formids + byte-exact donor
  records from the user's FalloutNV.esm into `harvest.json` (committed, so
  builds never need the game).
- `generate_esp.py` — authors `NVCompanions.esp` from `harvest.json` (GECK-free).
  The formid map is FROZEN (see docstring); only ever append.
- `validate_esp.py` — structural validator; run after any generator change.

Pipeline: `python harvest_fnv.py && python generate_esp.py && python validate_esp.py`,
then copy `NVCompanions.esp` to `mo2-mod/NVBridge/`. See
`docs/companions-architecture.md` for the full design.

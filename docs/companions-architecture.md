# Companions — architecture

How chasm-authored characters become spawned, named, voiced, face-designed followers in
Fallout: New Vegas, with **no third-party mod dependencies** (xNVSE + our own NVBridge
plugin + our own `NVCompanions.esp` only).

Research sources: fopdoc (TES5Edit's FNV plugin-format docs), GECK wiki, xNVSE and
JIP-LN-NVSE sources (JIP studied as GPL technique reference only — we depend on nothing
from it), plus the byte-level ground truth harvested from the user's own `FalloutNV.esm`
(`tools/esp/harvest_fnv.py` → `harvest.json`). Engine addresses below are for
FalloutNV.exe 1.4.0.525, the version this plugin already targets.

## 1. NPC creation & save persistence

**Decision: a frozen pool of 8 template NPC_ records in our own `NVCompanions.esp`,
each with a pre-placed persistent ACHR in a holding cell. No PlaceAtMe, no runtime
form creation.**

Validated facts driving this:

- Dynamically created base forms (0xFF `CreateFormInstance` forms) are **not** written
  to `.fos` saves; xNVSE's co-save "created objects" channel deliberately whitelists
  only `TESPackage`. Template records in an esp are the only robust base-form source.
- References created by `PlaceAtMe` of an esp base *do* persist (created-reference
  changed-forms), but accumulate as save bloat and have lifetime rules (purged 3 days
  after death). Pre-placed **persistent ACHRs** avoid all of it: "spawn" is
  `MoveTo player`, "dismiss/despawn" is `MoveTo` back to the holding cell. Ref-level
  state (position, cell, teammate flag, inventory, health) persists in the save for free.
- Runtime changes to a **base** NPC_ (facegen, name, package list, ACBS) are session-only
  — the save's changed-form system has no facegen/name flags for non-player NPCs
  (verified in NVSE source: `SetName` is a plain in-memory `String::Set`, no
  `MarkFormModified`). Therefore the plugin **re-applies name + appearance + wait-state
  on every `kMessage_PostLoadGame`** from its registry (§5).

Pool structure (formid map is FROZEN — the plugin hardcodes local ids; append-only):

| Record | Local formid | Notes |
|---|---|---|
| NPC_ ChasmCompanion01–04 | `0x000800–0x000803` | male, byte-cloned from GSRingo |
| NPC_ ChasmCompanion05–08 | `0x000804–0x000807` | female, byte-cloned from GSSunnySmiles |
| ACHR ChasmCompanionRef01–08 | `0x000900–0x000907` | persistent (flag 0x400), in holding cell |
| PACK ChasmCompanionFollow | `0x000A00` | see §3 |
| PACK ChasmCompanionWait | `0x000A01` | reserved for the wait state (M3) |
| VTYP ChasmCompanionVoiceM/F | `0x000A10/11` | silent voice types, §3 |
| FACT ChasmCompanionWaitFaction | `0x000A20` | wait-state gate (M3) |
| CELL ChasmCompanionHold | `0x000B00` | interior holding cell |

Template defaults: `ACBS = Essential (0x2) | Female (0x1 on 05–08)`, template flags 0,
No-Low-Level-Processing **clear** (they must process while unloaded), zeroed
FGGS/FGGA/FGTS (the FaceGen mean face — a valid face, overwritten at face-design time),
single PKID → ChasmCompanionFollow, VTCK → our silent VTYP. Donor subrecords
(OBND/MODL/RNAM/PNAM/HNAM/ENAM/HCLR/DNAM/CNAM/…) are copied byte-exact from vanilla
NPCs so every field the engine expects is present and internally consistent.

The esp is authored GECK-free by `tools/esp/generate_esp.py` (~250 lines of
`struct.pack`; the FNV format is plain little-endian TLV, fully documented in fopdoc,
and validated here against real FalloutNV.esm bytes). The generated `NVCompanions.esp`
is committed into `mo2-mod/NVBridge/` and ships with the mod. **The user must enable
it once in MO2's plugin list.**

**Chasm ↔ companion identity mapping:** a companion is `(slot 0–7) ↔ (chasm character
card)`. The plugin resolves the slot's base at runtime via the existing
mod+local-formid resolver (`NVCompanions.esp` + `0x000800+slot` — load-order safe,
cached). `ResolveMappedNpcImpl` gets a new first-chance check: if a ref's base form is
a claimed slot, its `npc_key` is the registry's stable key (slug of the companion name,
e.g. `ada_venture`) and its display name the companion name — so on the chasm side the
existing name-based character resolution binds it to the card with zero
`npcCharacterMap` config, and dialogue/TTS/actions all work like any mapped NPC.

Fallback (if the generated esp misbehaves in xEdit/in-game): author the same records
once in GECK and commit that esp; the runtime design is unchanged. (Not expected —
the generator mirrors vanilla bytes.)

## 2. In-game face design (chargen menu round-trip)

**Decision: full player round-trip — snapshot player appearance, open the vanilla
RaceSexMenu on the player, copy the result onto the companion base, restore the player.
This is feasible and is exactly the technique JIP's `CopyFaceGenFrom` + appearance-undo
machinery proves out.**

Validated engine facts:

- `ShowRaceMenu` (and the barber/surgeon variants) opens **RaceSexMenu, menu type 1036
  (0x40C)**, editing the **player's TESNPC base live** — sliders write the facegen
  coefficients directly; there is no shadow copy and no cancel/rollback. Accept just
  closes the menu (setting `g_bUpdatePlayerModel` at `0x011C5CB4` if a skeleton swap is
  needed). Hence snapshot-first, restore-after is the only correct shape.
- The appearance data to snapshot/copy (TESNPC, sizeof 0x20C):
  - `faceGenData[3]` at `+0x134` — three `FaceGenData` (0x20 each) whose `values`
    buffers are flat float arrays: **50 (geometry-symmetric) / 30 (geometry-asymmetric)
    / 50 (texture-symmetric)** — matching the FGGS/FGGA/FGTS subrecords. Copy the
    float *contents*, never the pointers. FNV has no discrete skin-tone field — skin
    tone lives in the texture coefficients.
  - `hair` (+0x1B8), `hairLength` (+0x1BC), `eyes` (+0x1C0), `hairColor` (+0x1D8,
    inline RGBA), `headPart` tList<BGSHeadPart> (+0x1DC, rebuild the list nodes — the
    nodes are owned, the BGSHeadPart* targets are shared), race (`race.race` +0x110),
    sex bit (`baseData.flags & 1`, NPC+0x34), `height/weight` (+0x1F4/1F8).
- Copy player → companion via the **engine's own `TESNPC::CopyAppearance(src)` at
  `0x603790`** preceded by `SetSex` (component flag-setter `0x47DD50`) and `SetRace`
  (plain `[npc+0x110]` write for non-player) — the exact JIP CopyFaceGenFrom sequence.
  Restore companion→player uses the same call in reverse (player SetRace goes through
  the full player race-change path `0x60B240`, which `SetRace` handles internally).
- **Face tint rendering without pregen assets:** head geometry is always FaceGen-morphed
  at runtime, but the tint texture defaults to *pregenerated* files
  (`bLoadFaceGenHeadEGTFiles=0`) — the classic gray-face bug for esp NPCs. The fix used
  by every runtime face tool: force the cached ini bool at **`0x11D5AE0` to true** (the
  plugin does this once at startup) so the engine computes tints from FGTS at 3D load,
  exactly as it always does for the player. Zero .egm/.egt/.dds files to ship; a pool
  of ≤8 runtime-tinted heads is negligible perf.
- 3D refresh after writing a loaded actor's appearance: `ThisCall(0x8D3FA0, characterRef)`
  (JIP's post-facegen rebuild; also its player Update3D path). For refs whose 3D isn't
  loaded, nothing to do — the head regenerates when 3D loads. Fallback refresh if
  0x8D3FA0 misbehaves on NPCs: `Set3D(NULL)` `0x5702E0` + `ModelLoader::QueueReference`
  `0x444850` (xNVSE's Update3D), or a MoveTo-to-self.

Flow (driven by the plugin's existing main-loop + focus tracking):

1. chasm sends `face_design slot=N` (command file, §4). Plugin waits until the game
   window has focus and no menu is open.
2. Snapshot player TESNPC appearance (all fields above) into memory + registry.
3. Run `ShowRaceMenu` through the plugin's existing compiled-script console runner.
4. Poll `IsMenuVisible(1036)` (existing `kMenuVisibilityArrayAddress` helper) for close.
5. Copy player→companion base (SetSex/SetRace/CopyAppearance), serialize the 130 floats
   + part formids into the registry as the slot's appearance blob.
6. Restore player from snapshot (same sequence toward the player base), set
   `0x11D5AE0=true`, rebuild player 3D via `0x8D3FA0`; refresh the companion ref if loaded.
7. Ack result to chasm (registry rev bump).

Known caveat (documented, accepted): changing the player's *race* inside the menu
strips temporary ability/perk effects (the "Safe ShowRaceMenu" problem); face/hair/sex
edits are safe, and effects re-establish on the next save/load cycle. First iteration
opens the full RaceSexMenu (sex+race+face+hair all editable); if this proves too
sharp-edged we fall back to `ShowPlasticSurgeonMenu`+`ShowBarberMenu` (face+hair only)
and take sex/race from the companion editor instead of the menu.

## 3. Follower behavior, natively

**Decision: teammate flag + esp-shipped follow package gated on `GetPlayerTeammate == 1`.
Zero runtime package creation, zero re-apply needed for follow state.**

- The plugin already implements teammate toggling (`SetActorPlayerTeammate` via its
  compiled-script helper — safer than raw writes because the vanilla command also
  handles teammate-count/sneak bookkeeping) and package add/remove helpers; the
  existing FOLLOW/STOP_FOLLOW game-master actions keep working for companions as-is.
- The companion templates' **base package list** contains ChasmCompanionFollow: a
  byte-mirror of vanilla `DefaultFollowPlayerFar` (type Follow, PTDT target =
  PlayerRef 0x14) with follow distance 192 and one CTDA: `GetPlayerTeammate == 1`
  (function 454, run on subject). Recruit = `SetPlayerTeammate 1` + `EvaluatePackage`;
  dismiss = clear + `EvaluatePackage` (+ optional MoveTo holding cell for full
  despawn). Because the **teammate flag persists in saves** (vanilla companions rely
  on this), follow behavior self-restores on load with no work.
  - Deliberately *not* AddScriptPackage for the core behavior: script packages are
    one-slot, dropped at the next package re-evaluation, and never saved.
- Teammate flag grants the vanilla follower envelope: player-faction combat treatment
  (combat assist), friendly-fire protection, sneak-state mimicry, teleport-to-player
  on cell change.
- **Vanilla dialogue suppression:** our own VTYP records with *Allow Default Dialog*
  clear exclude companions from every voice-type-conditioned generic line (hellos,
  barks), and the follow package's fallout-behavior flags are 0 (no hellos / random
  conversations / idle chatter) — structurally mute in vanilla content; they speak
  only through chasm (which already routes dialogue + TTS for mapped NPCs, holds
  conversations via the bridge conversation package, etc.). With no GREETING lines,
  activating a companion does nothing; chat happens through chasm's existing
  hotkey/HTTP flow.
- Wait state (M3): a second esp package (higher priority, type Sandbox/Guard at
  current location) gated on membership in `ChasmCompanionWaitFaction`; the plugin
  toggles faction membership + `EvaluatePackage`, and re-applies membership on load
  from the registry (faction changes on the base are assumed non-persistent).

## 4. Bridge protocol & chasm integration

Same file-based style as everything else on this bridge (all under
`%LOCALAPPDATA%\chasm\bridge\`):

- **chasm → plugin commands**: `control/companions/*.txt`, `CHASM_COMPANION_V1`
  header + `key=value` lines (mirroring `NVBRIDGE_ACTION_V2`), polled by the plugin
  main loop alongside `PollNativeActionCommands`. Ops: `create` (claim slot: sex,
  name, npc_key, character name, want_face_design), `face_design`, `rename`,
  `summon`, `dismiss` (stop following), `despawn` (return to holding cell),
  `release` (free the slot).
- **plugin → chasm state**: the plugin owns `companions/registry.json` (single
  writer) — per-slot: npc_key, name, sex, status (`claimed/spawned/face_designed`),
  appearance blob (hex floats + formids), wait state, `rev` counter, `last_error`.
  Written atomically (tmp+rename) on every change and re-read at plugin startup;
  this is also what PostLoadGame re-applies. chasm's web layer reads it for UI
  status (its bridge root comes from `chasm_core::default_bridge_root()`).
- **Name**: applied by writing the base's `TESFullName` string (the engine SetName
  path), re-applied on every load from the registry; the HUD shows it on next
  crosshair refresh.

chasm side (server + UI):

- `POST /api/ui/v1/companions` creates: (a) a real **character card PNG** in the
  active profile's `characters/` dir (same tEXt-chunk writer the Characters book
  uses, over a bundled placeholder image) with the standard fields (name,
  description, personality, first message, …) — so retrieval, chats, relationships
  all Just Work; (b) a **voice**: the uploaded clip lands at
  `voices/<Name>/reference.wav` and the existing clone pipeline
  (`clone-voices.ps1` → `tts_clone.py`, the same one behind `POST
  /api/voices/clone`) produces the per-engine cloned voice — TTS then picks it up
  by character name exactly like extracted vanilla voices; (c) a companion
  `create` command file for the plugin. NPC→card binding needs no
  `npcCharacterMap` entry: the plugin reports `npc_key = slug(name)` /
  `npc_name = name`, and the bridge's existing fallback resolution binds by name.
- `GET /api/ui/v1/companions` merges registry.json + card + voice-clone status;
  `POST /api/ui/v1/companions/:slot/:op` relays summon/dismiss/face_design/rename.
- UI: a **Companions** page in the left nav — list via the shared `Book` component
  (same editor fields as the Characters book) plus a create dialog with the extra
  inputs (sex, voice clip drag-and-drop, "design face in game" toggle).

Spawn timing: the `create`/`face_design` commands sit in the durable queue; the
plugin executes them when the game is running, in gamemode, and focused (it already
tracks focus) — i.e. "when the user tabs back into the game", per the flow.

## 4b. Game-agnostic boundary (profile-declared capabilities)

chasm is a universal NPC backend; nothing Fallout-specific lives in chasm code.
The split:

- **The game profile declares companion capabilities** in its `profile.json`
  (shipped inside the mod's `chasm-profile/` bundle and injected like the rest
  of the profile): a `companions` block with `enabled`, `bodies[]` (id, label,
  slot count, and the opaque `commandFields` chasm copies into `create`
  commands verbatim), `inGameFaceDesign`, and UI hint strings. No block = the
  Companions page shows "not supported by this game profile".
- **chasm-web renders capabilities** and speaks only the game-neutral bridge
  channel: `CHASM_COMPANION_V1` key=value command files, acks, and the
  plugin-owned registry (which reports each slot's `body` id back). It never
  interprets body variants, pool sizes, or face-design mechanics.
- **Everything FNV** — the template esp pool, chargen round-trip, follow AI,
  slot allocation, engine offsets — lives in this mod (the NVSE plugin + esp +
  profile bundle). A second game would ship its own mod + profile block and
  reuse chasm unchanged.

## 5. Persistence & multi-companion model

- **Save-proof by construction**: ref position/teammate/inventory persist in the
  save; follow behavior is condition-derived from the persisted teammate flag.
- **Re-applied on every PostLoadGame** (session-/load-order-local state): base-form
  name, appearance blob, wait-faction membership — idempotent, from registry.json.
  The existing save/load event dispatch (`DispatchSaveStateEvent`) already fires at
  the right moments; companion re-apply hooks the same message.
- **Cross-save semantics**: the registry is global (per bridge root), keyed by slot.
  A save from before a companion was claimed just shows the ref idle in the holding
  cell; re-summon from the UI. A save where a since-released companion still
  follows gets re-normalized on load (slot not in registry → dismissed to holding
  cell). Saves never contain dangling base forms because the bases ship in the esp.
- 8 concurrent companions max (pool size); slots are claimed/released, and the esp
  formid map is append-only if we ever grow the pool.

## 6. Risk register / fallbacks

| Risk | Mitigation / fallback |
|---|---|
| Generated esp rejected by engine | Validate in xEdit; fallback: author once in GECK, same design |
| `0x8D3FA0` refresh wrong for NPCs | xNVSE Update3D path (`Set3D(NULL)` + ModelLoader queue) or MoveTo-self |
| RaceSexMenu race change strips player ability effects | Documented; save/load restores; fallback flow = surgeon+barber menus (face+hair only) |
| CTDA-gated follow doesn't re-evaluate promptly | Explicit `EvaluatePackage` after every toggle (already the plan) |
| Face copied while companion 3D loaded doesn't show | Refresh ref (fallbacks above); worst case visible after cell transition |
| Name-by-base collides if two refs of one base | Impossible by design: one ACHR per base, PlaceAtMe never used |
| Old saves + changed registry | Idempotent re-apply + slot-not-claimed normalization on load |

Every new engine call is wrapped in defensive logging to
`%LOCALAPPDATA%\chasm\bridge\native_plugin.log` (existing `LogLine`), with a
`companions:` prefix, so in-game failures are diagnosable from log excerpts.

# Player persona capture

The native plugin watches the player's build and look, and — when they change —
sends chasm a **persona capture**: a stats snapshot (the same display strings
the [gamestate macros](gamestate-macros.md) send) plus, when possible, a stealth
screenshot of the player photographed from the front. chasm turns the capture
into a SillyTavern-style **user persona**: a vision-capable LLM (or a stats-only
text generation when no vision model is reachable) writes a compact third-person
description of the player, which is injected into every NPC prompt at
SillyTavern's story-string persona slot and shown on the chasm UI's **Persona**
page.

This file is the **frozen contract** between the plugin
(`native/nvse-plugin/main.cpp`, the "Player persona capture" section) and
chasm's receive endpoint (`chasm-source/crates/chasm-web/src/persona.rs`).
Change either side only in lockstep with the other — and with this file.

## Endpoint / transport

```
POST http://{http_host}:{http_port}{persona_http_path}
Content-Type: application/json
```

- Defaults: `http://127.0.0.1:7341/api/game/v1/persona` (`native_debug.cfg`
  keys `http_host`, `http_port`, `persona_http_path`).
- The upload always uses HTTP, **even when `transport=file`** — chasm's HTTP
  server runs regardless of which transport dialogue turns ride. There is no
  file-bridge fallback for persona (a missed capture costs nothing; the next
  stat change re-sends).
- Fire-and-forget from a detached sender thread with bounded timeouts
  (5 s resolve/connect, 15 s send, 20 s receive). A dead backend costs one log
  line. The sender never touches game state and can never block a dialogue
  turn (it is fully independent of the HTTP turn worker).
- One capture is in flight at a time, end to end. Success is any HTTP 2xx.
- Request bodies stay comfortably under chasm's 12 MB route limit: captures
  are downscaled to `persona_max_image_width` (default 1280) and JPEG-encoded
  (default quality 85) before base64, typically 150–500 KB on the wire.

## Payload vocabulary

A flat JSON object. **String display values** (macro-style, never form ids),
built by the same extractors the gamestate macros use. Any field the plugin
cannot read is **omitted** — the backend tolerates absence of everything.

| Key | Example | Notes |
|-----|---------|-------|
| `captured_at` | `2026-07-02T14:31:08Z` | UTC ISO-8601, set when the stats were snapped (trigger time) |
| `trigger` | `stats_changed` | `initial` \| `level_up` \| `stats_changed` \| `equipment_changed` (see below) |
| `player_name` | `Courier` | player base form's display name |
| `level` | `12` | actor level (string) |
| `special` | `STR 5, PER 6, END 5, CHA 4, INT 7, AGI 6, LCK 5` | same as `{{special}}` |
| `skills` | `Barter 15, …, Unarmed 14` | all 13 visible skills, same as `{{skills}}` |
| `perks` | `Rapid Reload, Toughness 2, Educated` | same as `{{perks}}` (rank > 1 appended, cap 24) |
| `equipped_weapon` | `9mm Pistol` | equipped weapon display name |
| `equipped_apparel` | `Leather Armor, Goggles` | worn armor/apparel, same as `{{equipped_apparel}}` (cap 8) |
| `location` | `Prospector Saloon, Goodsprings` | `minor, major` (either part omitted when unknown); informational only — chasm stores/shows it but does not feed it to the persona prompt |
| `image_format` | `jpeg` | only present when a screenshot is attached; the plugin always sends `jpeg` (the backend also accepts `png`) |
| `image_base64` | `…` | base64 of the encoded screenshot; **absent** when capture failed or `persona_screenshot=0` — the backend then generates a stats-only persona |

The backend's stats snapshot is exactly: `player_name`, `level`, `special`,
`skills`, `perks`, `equipped_weapon`, `equipped_apparel`, `location`. Keys
outside this vocabulary are stored verbatim in `capture.json` but ignored by
generation; the persona description prompt uses everything except `location`.

Response (informational; the plugin only checks the status code):

```json
{ "status": "stored", "generation": "queued" | "busy" | "unchanged" | "disabled", "image": "stored" | "none" | "rejected: …" }
```

`generation: "unchanged"` means the stats snapshot matches the stored persona's
and the description was kept (the fresh image is still stored).

## Trigger semantics

Every `persona_poll_interval_ms` (default 3 s) **while the player is idle**, the
plugin rebuilds a *persona fingerprint* — `player_name | level | special |
skills | perks | equipped_weapon | equipped_apparel` as one joined string — and
compares it with the last fingerprint chasm ACKed:

- **Changed → one capture sequence**, debounced to at most one per
  `persona_debounce_ms` (default 30 s). A burst of changes (a level-up's worth
  of skill points, an outfit swap piece by piece) coalesces into one capture.
- `trigger` classifies which segment changed: level → `level_up`; name /
  SPECIAL / skills / perks → `stats_changed`; weapon / apparel →
  `equipment_changed`; no previous fingerprint on record → `initial`.
- Closing the **level-up menu** forces an immediate fingerprint re-check
  (the debounce still applies).
- The ACKed fingerprint is persisted to
  `%LOCALAPPDATA%\chasm\bridge\persona_fingerprint.txt`, so game/plugin
  restarts do not re-fire unless the stats actually differ. A failed upload
  leaves the fingerprint stale → the change is re-detected and re-sent after
  the debounce window (self-healing).

**Idle gates** — a capture only ever starts (and only keeps going) when ALL of
these have held for ≥ 1.5 s continuously:

- game window focused; loaded into gameplay; player alive
- no combat (`PlayerCharacter` in-combat flag), not aiming down a scope
- no menu visible other than the HUD (Pip-Boy, dialogue, barter, level-up,
  loading, main menu … all block)
- no bridge activity: no turn in flight, no queued/playing reply, no text
  input, no voice capture, no conversation hold, no save-sync pending
- not walking/running/swimming

## Screenshot behavior — what "stealth" really means here

Technique (engine-native, no renderer hooks):

1. `tm` (hide all UI) and `tfc` (engine flycam) via the JIP `Console` helper
   the plugin already uses for script commands.
2. The flycam pose is written directly to `PlayerCharacter`'s flycam fields
   (offsets 0x7E0 rotZ / 0x7E4 rotX / 0x7E8 pos — cross-checked between the
   xNVSE SDK's annotations and JIP LN NVSE's headers, FNV 1.4.0.525):
   `persona_camera_distance_units` (140) in front of the player's facing at
   `persona_camera_height_units` (110) above their feet, yawed back at the
   player, level pitch. The pose is re-asserted every settle frame so mouse
   input cannot drift it.
3. After 3 settle frames (so the swung view has actually been rendered AND
   presented), the plugin copies the **front buffer** from the game's own
   `IDirect3DDevice9` (`NiDX9Renderer` singleton `0x11F4748`, device at
   `+0x288`), cropped to the game window's client rect — this works in
   exclusive fullscreen, borderless, and windowed, and it captures exactly
   what the player sees (including ENB-style post-processing).
4. Camera and HUD are restored **in the same frame** as the grab; JPEG
   encode + base64 + POST happen on the background sender thread.

**Honest artifact accounting** (there is no free lunch without re-entering the
renderer, which is not worth the crash risk):

- The player can see **~4 frames (~65 ms at 60 fps) of a HUD-less, front-view
  camera cut**, at most once per debounce window, only while idle by the gates
  above. It reads as a brief blink; an attentive player WILL occasionally
  notice it. It is not literally invisible.
- `GetFrontBufferData` stalls the GPU for a few ms on the grab frame — a
  one-frame hitch at the same cadence.
- During those frames, movement/mouse input briefly steers the (frozen)
  flycam instead of the player — practically irrelevant because captures only
  fire after 1.5 s of standing still.
- Aborts (combat starts, a menu opens, focus lost, watchdog at 2 s) restore
  camera/HUD immediately and **still upload the stats snapshot**, so the
  backend can regenerate a stats-only persona.
- If `tfc` cannot be issued (JIP missing — not a supported configuration) or
  the D3D9 read fails, the capture degrades to stats-only, silently for the
  player.

Residual risks, stated plainly:

- The restore is console-driven (`tfc`/`tm` re-toggles). If a console call
  fails, the plugin retries every frame (~3 s worth) and then logs loudly; a
  session reset (save load) also rebuilds camera/UI state engine-side. A game
  crash in the exact 4-frame window could theoretically leave `tm` latched
  until the next load — never observed, but the pairing is blind toggling.
- If the player stands facing a wall, the flycam can clip into geometry and
  the shot shows the wall/void. The vision model still gets *something*; the
  stats snapshot is unaffected. No raycast correction is attempted (yet).
- If the player is already in `tfc` (screenarcher workflows), a capture would
  exit and re-enter their flycam and move it. The movement gates make this
  unlikely (flycam users are actively flying) but it is possible.
- The camera frames a standing humanoid at default distances; extreme scales
  or seated/knocked-down poses may frame imperfectly (tunable via
  `persona_camera_*_units`).

## Config (`native_debug.cfg`)

| Key | Default | Meaning |
|-----|---------|---------|
| `persona` | `1` | master switch for the whole feature |
| `persona_screenshot` | `1` | `0` = never swing the camera; stats-only uploads |
| `persona_http_path` | `/api/game/v1/persona` | POST path on `http_host:http_port` |
| `persona_poll_interval_ms` | `3000` | fingerprint re-check cadence (500–60000) |
| `persona_debounce_ms` | `30000` | min gap between capture sequences (5000–600000) |
| `persona_camera_distance_units` | `140.0` | flycam distance in front of the player (40–600) |
| `persona_camera_height_units` | `110.0` | flycam height above the player's feet (0–300) |
| `persona_max_image_width` | `1280` | downscale bound before JPEG encode (320–3840) |
| `persona_jpeg_quality` | `85` | GDI+ JPEG quality (30–100) |

## What chasm does with it (for reference)

- Stores the capture under the active profile
  (`…/headless/persona/capture.json` + `capture.jpg`), then queues an async
  generation (never blocks turns): separate vision endpoint if configured →
  main LLM with the image → stats-only text prompt. Failures keep the previous
  good description.
- `GET /api/ui/v1/persona` (+ `/persona/image`, `POST /persona/regenerate`)
  drive the UI's **Persona** page (below Gamestate): screenshot, description,
  provenance, stats snapshot, manual Regenerate.
- Prompt assembly injects `Player persona:\n<description>` directly after the
  character card's scenario slot (SillyTavern's default persona position);
  nothing is injected before the first generation.

## Verifying in-game

1. Rebuild + install the plugin DLL, start chasm, load a save. Stand still
   (out of combat, no menus) and dump a skill point (or use the console:
   `player.incrementav Speech 5`, close the console) — within
   `persona_poll_interval_ms` + settle you should see a barely-perceptible
   camera blink. Within a few seconds the **Persona** page shows the fresh
   front screenshot, the new stats, and (once the LLM finishes) a new
   description that reflects the change (e.g. Speech extremes change how the
   description says they talk).
2. Equip visibly different armor → same flow with `trigger:
   "equipment_changed"`; the description should mention the new look (vision)
   or outfit line (stats-only).
3. `%LOCALAPPDATA%\chasm\bridge\native_plugin.log` carries `Persona: …` lines
   for every trigger, capture, abort, and upload result; the chasm-side store
   lives under the active profile's `headless/persona/` dir.

# Player persona capture

Each time the player **saves the game** (quicksave or manual save), the native
plugin sends chasm a **persona capture**: a stats snapshot (the same display
strings the [gamestate macros](gamestate-macros.md) send) plus, when possible,
a stealth screenshot of the player photographed from the front. chasm turns the
capture into a SillyTavern-style **user persona**: a vision-capable LLM (or a
stats-only text generation when no vision model is reachable) writes a compact
ONE-paragraph third-person description of the player, which is injected into
every NPC prompt at SillyTavern's story-string persona slot and shown on the
chasm UI's **Persona** page.

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
  save re-sends).
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
| `captured_at` | `2026-07-02T14:31:08Z` | UTC ISO-8601, set when the stats were snapped (sequence start) |
| `trigger` | `quicksave` | `save` \| `quicksave` \| `autosave` (see below; the pre-save-trigger vocabulary `initial` / `level_up` / `stats_changed` / `equipment_changed` is retired — the backend still accepts unknown triggers but only save triggers force regeneration) |
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

`generation: "unchanged"` can only occur for NON-save triggers (see below);
the fresh image is still stored when it does.

## Trigger semantics — capture fires on GAME SAVE

Saving the game is the capture trigger. The plugin hooks the NVSE SaveGame
message (the same detection point the save-sync machinery uses) and classifies
the save by file name:

- `quicksave*.fos` → `trigger: "quicksave"` — fires.
- `autosave*.fos` → `trigger: "autosave"` — ignored unless
  `persona_capture_on_autosave=1`.
- anything else (manual/full saves, console `save foo`) → `trigger: "save"` —
  fires.

Rules:

- **Debounce**: at most one capture sequence per `persona_debounce_ms`
  (default 30 s). Quicksave spam inside the window is dropped (logged); a save
  while a capture is already pending just coalesces into it. The debounce
  anchor survives save/load cycles, so quickload+quicksave loops do not
  double-capture.
- **Deferral**: if the save happens while the idle gates below are closed
  (mid-combat, in the save menu, during dialogue...), the capture is marked
  PENDING and taken the moment the gates have held for 1.5 s. A pending
  capture that stays blocked for 5 minutes degrades to a **stats-only upload**
  (the save still refreshes the persona, just without a new screenshot).
  Interrupts mid-sequence (window focus loss) re-arm the pending capture
  rather than dropping it.
- **Saves always regenerate.** The backend skips its unchanged-stats
  short-circuit for `save` / `quicksave` / `autosave` triggers: a fresh
  screenshot means a fresh description even when the stats are identical.
  (For any other/unknown trigger value the backend keeps the old behavior:
  identical stats + existing description → `generation: "unchanged"`.)
- There is no polling, no stat fingerprint, and no level-up-menu hook anymore;
  the old `initial` / `level_up` / `stats_changed` / `equipment_changed`
  triggers are retired. Nothing fires until the first save of a session.

**Idle gates** — a capture only ever starts (and only keeps going) when ALL of
these have held for ≥ 1.5 s continuously:

- game window focused; loaded into gameplay; player alive
- no combat (`PlayerCharacter` in-combat flag), not aiming down a scope
- no menu visible other than the HUD (Pip-Boy, dialogue, barter, level-up,
  loading, main menu … all block)
- no bridge activity: no turn in flight, no queued/playing reply, no text
  input, no voice capture, no conversation hold, no save-sync pending
- not walking/running/swimming

## Screenshot behavior — a true off-screen render, nothing visible

The portrait is **not** a screen capture. The plugin renders the player's
3rd-person model into a **private Direct3D render target** the screen never
sees — the game camera, the HUD, and the presented frame are untouched. There
is no camera cut, no blink, no hitch the player can perceive. The model is
**isolated on a flat studio background** (the world is never drawn).

Technique (one game-thread pass per capture):

1. Walk the player's scene graph (`player->GetNiNode()` — the 3rd-person
   skeleton, maintained by the engine even in first person; this is exactly
   how player shadows are rendered by graphics extenders). Collect every
   tri-shape/tri-strips under it. The walk is SEH-guarded: a bad engine
   pointer aborts the capture, never the game.
2. CPU-skin each skinned mesh from the **live skeleton pose** using the
   engine's own skinning data (`NiSkinData` bone weights + per-bone
   skin-to-bone transforms composed with the bones' world transforms). The
   skin layout is validated at runtime before it is trusted; a mesh that
   fails validation falls back to bind pose + node transform, logged.
3. Draw everything fixed-function textured (each mesh's own diffuse texture,
   alpha-tested for hair/lashes) into a `persona_portrait_size`-tall 3:4
   render target cleared to neutral gray. Default framing
   (`persona_portrait_framing=face`) tracks the recomputed **Bip01 Head**
   position: the camera sits `persona_face_distance_units` (45) in front of
   the head along the body's facing at `persona_face_fov_deg` (30) vertical
   FOV, so the face fills the frame natively (full render quality, no digital
   zoom) and body animation cannot move it out of frame. `body` framing
   instead shoots from `persona_camera_distance_units` (220) at eye height
   `persona_camera_height_units` (100), aimed at the model's bound center,
   `persona_portrait_fov_deg` (48) FOV.
4. Read the target back (private surface — no front-buffer stall), then JPEG
   encode + base64 + POST on the background sender thread. Every device
   state touched is saved and restored around the pass (state block + render
   target/depth/viewport).

Artifact accounting: **none visible.** The pass costs ~1–3 ms of game-thread
time (CPU skinning + a few hundred draw calls + readback) at most once per
debounce window. ENB post-processing is NOT captured (the portrait is a raw
model render, which is a feature: clean, evenly lit, no bloom/DoF).

Residual risks, stated plainly:

- Engine struct offsets (scene graph, skinning, texture plumbing) are
  reverse-engineered constants (cross-checked between TESReloaded's NEWVEGAS
  headers and JIP LN NVSE, FNV 1.4.0.525). A mismatch shows up as a wrong or
  empty portrait plus a log line — never a crash (SEH + runtime validation).
- If the portrait renders **mirrored** (left/right flipped), set
  `persona_portrait_mirror=1`. One calibration look at the first real
  screenshot settles it.
- Fixed-function fullbright rendering: no normal maps, no scene lighting —
  the portrait looks like a character-viewer preview, not an in-world photo.
  For the vision model this is strictly better (nothing to confuse it).
- Meshes whose diffuse texture cannot be resolved draw flat gray (logged as
  `textured` vs `geometries` counts in `bridge/native_plugin.log`).
- The camera frames a standing humanoid at default distances; extreme scales
  or seated/knocked-down poses may frame imperfectly (tunable via
  `persona_camera_*_units`, `persona_portrait_fov_deg`).

## Config (`native_debug.cfg`)

| Key | Default | Meaning |
|-----|---------|---------|
| `persona` | `1` | master switch for the whole feature |
| `persona_screenshot` | `1` | `0` = never swing the camera; stats-only uploads |
| `persona_capture_on_autosave` | `0` | `1` = autosaves (door transitions, waits...) trigger captures too |
| `persona_http_path` | `/api/game/v1/persona` | POST path on `http_host:http_port` |
| `persona_debounce_ms` | `30000` | min gap between capture sequences (5000–600000) |
| `persona_camera_distance_units` | `220.0` | portrait camera distance in front of the player (40–600) |
| `persona_camera_height_units` | `100.0` | portrait camera eye height above the player's feet (0–300) |
| `persona_portrait_framing` | `face` | `face` = head close-up tracked to Bip01 Head; `body` = full figure |
| `persona_face_distance_units` | `45.0` | face framing: camera distance in front of the head (15-300) |
| `persona_face_fov_deg` | `30.0` | face framing: vertical FOV in degrees (10-90) |
| `persona_portrait_size` | `1024` | offscreen portrait height; width is 3/4 of it (256–2048) |
| `persona_portrait_fov_deg` | `48.0` | portrait camera vertical FOV in degrees (15–100) |
| `persona_portrait_mirror` | `0` | set `1` if the portrait renders left/right mirrored |
| `persona_max_image_width` | `1280` | downscale bound before JPEG encode (320–3840) |
| `persona_jpeg_quality` | `85` | GDI+ JPEG quality (30–100) |

(`persona_poll_interval_ms` is retired along with the polling trigger; the key
is ignored if present.)

## What chasm does with it (for reference)

- Stores the capture under the active profile
  (`…/headless/persona/capture.json` + `capture.jpg`), then queues an async
  generation (never blocks turns): separate vision endpoint if configured →
  main LLM with the image → stats-only text prompt. Failures keep the previous
  good description. Save triggers always regenerate (no unchanged-stats
  short-circuit).
- The generation prompt demands exactly ONE compact paragraph (~100 words,
  `max_tokens`-clamped) and renders the mod's abbreviated stats as natural
  language — e.g. `STR 9` becomes `Strength 9 of 10 — exceptional (raw
  physical power and muscle)`, skills get their 0–100 scale plus callouts for
  extremes — so the model interprets values instead of guessing at
  abbreviations. The mod contract is unchanged: rendering happens
  backend-side from the same display strings.
- The exact prompt used is persisted with the persona record and shown on the
  Persona page ("Generation prompt", collapsible) for critique.
- `GET /api/ui/v1/persona` (+ `/persona/image`, `POST /persona/regenerate`)
  drive the UI's **Persona** page (below Gamestate): screenshot, description,
  provenance, stats snapshot, the generation prompt, manual Regenerate.
- Prompt assembly injects `Player persona:\n<description>` directly after the
  character card's scenario slot (SillyTavern's default persona position);
  nothing is injected before the first generation.

## Verifying in-game

1. Rebuild + install the plugin DLL, start chasm, load a save. Stand still
   (out of combat, no menus) and **quicksave** — within ~2 s you should see a
   with nothing visible on screen at all, and within ~30 s the **Persona** page
   shows the fresh front screenshot, the stats, the generation prompt
   (collapsible, below the stats), and a new ONE-paragraph description.
2. Quicksave again immediately → nothing (debounce, logged). Quicksave after
   30 s WITHOUT changing anything → a fresh capture AND a fresh description
   (saves always regenerate).
3. Save from the pause menu → the capture is deferred until the menu closes
   and you have stood still for 1.5 s, then fires (`trigger: "save"`).
4. Change armor or dump skill points, then save → the new stats appear in the
   snapshot + rendered naturally in the prompt (e.g. `Speech 4 of 100
   (dreadful)`), and the description reflects them.
5. `%LOCALAPPDATA%\chasm\bridge\native_plugin.log` carries `Persona: …` lines
   for every save trigger, pending/deferral, capture, abort, and upload
   result; the chasm-side store lives under the active profile's
   `headless/persona/` dir.

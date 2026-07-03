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
| `persona_capture_on_autosave` | `0` | `1` = autosaves (door transitions, waits...) trigger captures too |
| `persona_http_path` | `/api/game/v1/persona` | POST path on `http_host:http_port` |
| `persona_debounce_ms` | `30000` | min gap between capture sequences (5000–600000) |
| `persona_camera_distance_units` | `140.0` | flycam distance in front of the player (40–600) |
| `persona_camera_height_units` | `110.0` | flycam height above the player's feet (0–300) |
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
   barely-perceptible camera blink, and within ~30 s the **Persona** page
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

# Player persona capture

**Every time the player saves the game** — manual save, quicksave, or autosave,
without exception or throttle — the native plugin sends chasm a **persona
capture**: a pure game-data snapshot — the stats (the same display strings the
[gamestate macros](gamestate-macros.md) send) plus the player's appearance
records (sex, race, hair style/color/length, eye color, facial hair, worn
apparel). Nothing is rendered or screenshotted; the old offscreen portrait +
vision-LLM path is fully retired (it depended on the engine's lazily built
3rd-person head and produced garbage faces in first-person sessions). chasm
turns the capture into a **user persona**: the main text LLM
writes a two-paragraph third-person description — looks from the appearance
facts, manner from the attribute/skill magnitudes — which is injected into
every NPC prompt at the persona slot in the prompt story-string and shown on the
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
- Request bodies are a few KB of JSON (chasm's route limit is 256 KB — a
  guard, not a budget).

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
| `sex` | `male` | `male` \| `female` from the player base NPC record |
| `race` | `Caucasian Old` | race form display name — also encodes a coarse age bracket (GECK AgeRace variants: plain = adult, `Old`/`Old Aged` = elderly, `Middle Aged`, child races) |
| `age_years` | `34` | the chargen Age slider recovered from the FaceGen coefficients (dot products against the age control in the game's own `facegen\si.ctl`, shape + texture averaged, clamped 18–70). **Absent** when the coefficients can't be read or fail the gender self-check (the same coefficients must predict the character's actual sex, else the read was garbage). Backend precedence: race age marker > `age_years` > no age claim |
| `hair_style` | `Smooth Wave` | hair form display name |
| `hair_length` | `0.80` | GECK hair-length slider, `0.00`–`1.00` (scales the chosen style) |
| `hair_color` | `#54462E` | NPC hair color as `#RRGGBB`; chasm maps it to a plain color word |
| `eye_color` | `Blue` | eyes form display name |
| `facial_hair` | `Chin Curtain` | display names of the NPC's head-part forms (the list the barber menu edits), comma-joined; **absent** when none |

The backend's snapshot is exactly the keys above. Keys outside this
vocabulary are stored verbatim in `capture.json` but ignored by generation;
the persona description prompt uses everything except `location`,
`equipped_weapon`, and `perks`. (`image_format` / `image_base64` from an
outdated plugin are dropped on receipt, never stored.)

Response (informational; the plugin only checks the status code):

```json
{ "status": "stored", "generation": "queued" | "busy" | "unchanged" | "disabled" }
```

`generation: "unchanged"` can only occur for NON-save triggers (see below).
`generation: "busy"` also regenerates: the capture is stored and the
in-flight generation re-runs from it the moment the current pass finishes
(latest capture wins).

## Trigger semantics — EVERY game save captures, unconditionally

Saving the game is the capture trigger, and every save fires — there is no
debounce, no idle gating, and no autosave opt-out. The plugin hooks the NVSE
SaveGame message (the same detection point the save-sync machinery uses) and
classifies the save by file name:

- `quicksave*.fos` → `trigger: "quicksave"` — fires.
- `autosave*.fos` → `trigger: "autosave"` — fires.
- anything else (manual/full saves, console `save foo`) → `trigger: "save"` —
  fires.

Rules:

- **Immediate**: the capture is taken on the next main-loop frame after the
  save message, whatever the player is doing (menus, combat, moving — none of
  it defers the capture). The only wait is for an already-in-flight upload:
  one capture runs end-to-end at a time, and a burst of saves during it
  coalesces into ONE follow-up capture (latest trigger wins).
- **Saves always regenerate.** The backend skips its unchanged-stats
  short-circuit for `save` / `quicksave` / `autosave` triggers, and a capture
  that lands while a generation is running re-runs it when it finishes — a
  save never fails to refresh the description. (For any other/unknown trigger
  value the backend keeps the old behavior: identical stats + existing
  description → `generation: "unchanged"`.)
- There is no polling, no stat fingerprint, and no level-up-menu hook anymore;
  the old `initial` / `level_up` / `stats_changed` / `equipment_changed`
  triggers are retired. Nothing fires until the first save of a session.
  (The old 30 s debounce, 1.5 s idle-gate stability window, and 5-minute
  pending expiry are all retired too.)

## Config (`native_debug.cfg`)

| Key | Default | Meaning |
|-----|---------|---------|
| `persona` | `1` | master switch for the whole feature |
| `persona_http_path` | `/api/game/v1/persona` | POST path on `http_host:http_port` |

(All other former `persona_*` keys are retired with the screenshot feature —
`persona_screenshot`, `persona_debounce_ms`, `persona_capture_on_autosave`,
`persona_poll_interval_ms`, and every `persona_camera_*` / `persona_face_*` /
`persona_portrait_*` / `persona_max_image_width` / `persona_jpeg_quality`
key. All are ignored if present in an old cfg.)

## What chasm does with it (for reference)

- Stores the capture under the active profile
  (`…/headless/persona/capture.json`), then queues an async generation (never
  blocks turns): one text prompt against the main LLM endpoint. Failures keep
  the previous good description. Save triggers always regenerate (no
  unchanged-stats short-circuit), and a capture landing mid-generation re-runs
  the generation when the current pass finishes.
- The generation prompt demands TWO paragraphs — looks from the appearance
  facts at ~80 words, then manner from the stats at ~120 words,
  `max_tokens`-clamped — and renders all game data as natural language:
  appearance facts become plain words (`#54462E` → `dark brown`, `Caucasian
  Old` → `white` + `older, well past middle age`, head parts → `facial hair:
  chin curtain`), stats get full attribute names, explicit scales, and
  qualitative bands (`STR 9` → `Strength 9 of 10 — exceptional (raw physical
  power and muscle)`), skills get their 0–100 scale plus callouts for
  extremes — so the model interprets values honestly instead of guessing at
  abbreviations or inventing features. The mod contract is unchanged:
  rendering happens backend-side from the same display strings.
- The exact prompt used is persisted with the persona record and shown on the
  Persona page ("Generation prompt", collapsible) for critique.
- `GET /api/ui/v1/persona` (+ `POST /persona/regenerate`) drive the UI's
  **Persona** page (below Gamestate): description, provenance, the
  character-data snapshot, the generation prompt, manual Regenerate.
- Prompt assembly injects `Player persona:\n<description>` directly after the
  character card's scenario slot (the conventional persona position);
  nothing is injected before the first generation.

## Verifying in-game

1. Rebuild + install the plugin DLL, start chasm, load a save. **Quicksave** —
   the capture fires on the next frame, and within a few seconds the
   **Persona** page shows the character data, the generation prompt
   (collapsible, below the data), and a fresh description.
2. Quicksave again immediately → another capture, another regeneration (saves
   during an in-flight upload coalesce into one follow-up; saves during an
   in-flight generation re-run it — either way the description refreshes).
3. Save from the pause menu → the capture fires as soon as the save message
   lands (`trigger: "save"`); autosaves (door transitions, waiting) fire too.
4. Change armor, visit a barber, or dump skill points, then save → the new
   data appears in the snapshot + rendered naturally in the prompt (e.g.
   `Speech 4 of 100 (dreadful)`), and the description reflects it.
5. `%LOCALAPPDATA%\chasm\bridge\native_plugin.log` carries `Persona: …` lines
   for every save trigger, capture, and upload result; the chasm-side store
   lives under the active profile's `headless/persona/` dir.

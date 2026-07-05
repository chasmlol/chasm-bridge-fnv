# Gamestate macros

Every dialogue turn, the native plugin reads live player state and sends it to
chasm as `metadata.macros` — a flat **string → string** map of **full display
values** (`Goodsprings`, never a form id). chasm records the table on the
persisted turn (`message.extra.chasm.macros`), shows the latest snapshot on the
**Gamestate** page (left sidebar), and resolves `{{key}}` placeholders against
it — SillyTavern-style plain substitution.

The **mod is the source of truth**: whatever keys it sends that turn are
exactly the macros available that turn. This file is the vocabulary contract
between the plugin (`BuildPlayerMacros` in `native/nvse-plugin/main.cpp`) and
anything that writes `{{macro}}` templates (the Gamestate tester, and the
**global scenario** template on chasm's Globals page — the production
consumer; cards / lore / system prompts may follow later).

## Substitution rules

- `{{key}}` — double curly braces, plain string replacement only. No defaults,
  conditionals, randoms, or nesting (future iterations).
- Keys match **case-insensitively** and surrounding whitespace is trimmed:
  `{{ Player_Name }}` = `{{player_name}}`.
- Unknown or missing macro → **empty string** (the placeholder disappears).
- A stray `{{` with no closing `}}` is left untouched.
- Substituted values are never re-scanned (no recursion).

## Vocabulary

Any field the plugin cannot read that turn is **omitted** (resolves empty), so
templates degrade gracefully.

| Macro | Example value | Source |
|-------|---------------|--------|
| `{{player_name}}` | `Courier` | player base form's full name (the chosen name) |
| `{{level}}` | `12` | actor level |
| `{{major_location}}` | `Goodsprings` | nearest world-map marker (same value as the turn's top-level `location.major`) |
| `{{minor_location}}` | `Prospector Saloon` | nearest local landmark / cell (same as `location.minor`) |
| `{{time_of_day}}` | `2:32PM` | `GameHour` global, rendered as a 12-hour clock (`6:30PM`, `11:12AM`) |
| `{{health}}` | `185/220` | current HP / permanent max HP |
| `{{health_percent}}` | `84%` | derived from `{{health}}`, clamped 0–100 |
| `{{radiation}}` | `23 rads` | RadiationRads actor value |
| `{{condition}}` | `Left Arm crippled; rest OK` | limb-condition actor values; `All limbs OK` / `All limbs crippled` at the extremes |
| `{{effects}}` | `Well Rested, Med-X` | active (non-terminated) magic effects, deduped by source name, capped at 10 |
| `{{special}}` | `STR 5, PER 6, END 5, CHA 4, INT 7, AGI 6, LCK 5` | current S.P.E.C.I.A.L. values |
| `{{skills}}` | `Barter 15, …, Guns 45, …, Unarmed 14` | all 13 visible FNV skills (Big Guns omitted — hidden in FNV) |
| `{{perks}}` | `Rapid Reload, Toughness 2, Educated` | non-hidden perks incl. traits; rank > 1 appended; capped at 24 |
| `{{equipped_weapon}}` | `9mm Pistol` | equipped weapon's display name |
| `{{equipped_apparel}}` | `Leather Armor, Goggles` | worn armor/apparel names, capped at 8 |
| `{{inventory}}` | `Stimpak x3, 9mm Round x48, …, +7 more` | **curated**: aid (10) + ammo (6) + weapons (6) + apparel (6), each sorted by count; everything else folds into the trailing `+N more` |
| `{{quests}}` | `Ghost Town Gunfight - Defend Goodsprings; …` | named quests with a live (displayed, not completed) objective — `Quest - current objective`, tracked quest first, `; `-joined, capped at 8 |
| `{{misc_quests}}` | `Return Ringo's caravan deck` | live objectives of unnamed quests (the Pip-Boy "Misc" bucket), capped at 6 |

Notes:

- **Values are prompt-sized on purpose.** The capped lists end in `+N more`
  when truncated, so a template never balloons a prompt.
- Combined `{{special}}` / `{{skills}}` ship first; per-stat macros
  (`{{strength}}`, `{{skill_guns}}`, …) are a future addition if authors want
  them.
- `major_location` / `minor_location` are duplicated into the macro table (the
  turn also carries them at the top level for NPC resolution) so the table is
  self-contained.

## Verifying in-game

1. Rebuild + install the plugin DLL (see `native/README.md`), start chasm, and
   talk to any NPC.
2. Open the **Gamestate** page in the chasm UI: the table should show the
   turn's real values (e.g. `major_location` = `Goodsprings` while standing in
   Goodsprings; `time_of_day` tracking the game clock).
3. In the page's tester, run a template like
   `You are speaking to {{player_name}} in {{major_location}}. It is {{time_of_day}}.`
   — the **Resolved prompt** panel must show the current in-game values and the
   model reply should reflect them. Unknown macros (e.g. `{{nope}}`) resolve to
   nothing.

Raw payloads are also visible in `%LOCALAPPDATA%\chasm\bridge\traces\` (enable
`request_tracing` in `native_debug.cfg`) — each turn's `metadata` should carry
a well-formed `macros` object.

## Production injection (scenario-scoped)

The **global scenario** template (chasm's Globals → Scenario page) resolves
through this engine every turn and is injected into each NPC's prompt at the
scenario slot — that is the production surface for these macros today. chasm
additionally computes `{{participants}}` (the player + the other group NPCs,
excluding the prompted speaker) for that template; it is backend-derived, not
part of the plugin's table above. Cards / lore / other system prompts still
substitute nothing — widening the surface is a separate, later decision.

## npc_state (dynamic scenarios)

Alongside `metadata.macros`, each request carries `metadata.npc_state` — a flat
**bool** block describing the RESPONDING NPC's situation, built by
`BuildNpcStateMetadataFields` in `native/nvse-plugin/main.cpp`. chasm uses it
(never chat content) to pick the scenario wording variant on the Globals →
Scenario page. The block is omitted when the speaker can't be resolved; chasm
then treats every flag as false (default variant). Every flag is a real engine
read:

| Flag | Engine source |
|------|---------------|
| `teammate` | membership in `PlayerCharacter::teammates` (tList @ 0x5FC — the list the event-log teammate poll walks; persists in saves) |
| `following` | best source first: (1) explicit orders issued this session (gamemaster FOLLOW/STOP_FOLLOW, `movement.follow_target` / `movement.stop_follow_target` / `ai.wait_here` / `ai.resume_default` / `ai.sandbox_here` Action-Book actions, companion summon/dismiss); (2) spawned companion-pool slot (teammate flag + registry wait state — the companion esp follow package is conditioned on GetPlayerTeammate); (3) while a conversation hold masks the actor's package: the pre-hold snapshot captured at hold engage; (4) live `GetCurrentPackage == DefaultFollowPlayerFar` probe |
| `waiting` | same resolution chain, matching the `DefaultSandboxNoMoveCurrentLocation200` package / `ai.wait_here` order / companion registry `waiting` |
| `sneaking` / `player_sneaking` | compiled helper script calling the vanilla `IsSneaking` condition function on the actor / player |
| `weapon_drawn` / `player_weapon_drawn` | `Actor::IsWeaponOut()` (`baseProcess->IsWeaponOut`, SDK/lStewieAl) |
| `sitting` | `BaseProcess::GetSitSleepState()` in the sit range (LoadSitIdle / WantToSit / WaitingForSitAnim / Sitting), sleep states excluded |
| `player_swimming` | `PlayerCharacter::GetMovementFlags()` bit 11 via the actor mover (guarded null) |

The `traveling` scenario condition is CHASM-side (an en-route journey in the
movement store) and is deliberately not part of this block. Combat is likewise
separate (`in_combat` / `combat_with` above the scenario system).

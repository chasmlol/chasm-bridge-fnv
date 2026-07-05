# NPC Scheduler ("cronjob") architecture

NPCs can schedule an action to fire at a specific **in-game time** or when a
**condition** is met — without the model ever authoring a task spec. The model
picks ONE high-level action with 1–2 natural args; chasm does the rest (parse the
time, expand a composite action into a conditional chain, persist, fire).

This is deliberately split so the human integrator can back the condition engine
onto the in-flight `task/event-log` game-event stream later (see
[Overlap with task/event-log](#overlap-with-taskevent-log)).

## LLM-facing surface

The model uses the SAME `actions` array it always has (bare strings like `wave`),
and MAY tack on natural-language modifiers. The ONLY new action is `travel` (go to
a place); the scheduling/chaining is pure string parsing, not new schema:

| phrase | effect |
|--------|--------|
| `wave` | fires now (unchanged immediate path). |
| `wave at 1am` | `at <time>` -> schedule the `wave` action for 1am. |
| `loot the body then give it to you` | `then` -> a chain: fire the first, then (once done) the second. |
| `travel to the prospector saloon` | `travel` action + `to <place>` -> go to that map location. |
| `travel to prospector saloon at 1am` | destination + a scheduled time. |
| `travel to the saloon then wave` | chain: go there, then wave. |

Parsing (`scheduler::parse_phrase`, in `normalize`): split on the standalone words
` then ` (chain), ` at ` (schedule), and ` to ` (destination). An ` at ` only
schedules when its tail parses as a time ("look at the door" stays plain); a ` to `
is only a destination when the verb starts with a motion word (travel/go/head/
come/…), so "wave to the crowd" stays plain. The last valid-time ` at ` wins.

A plain action (no modifier) is left entirely to the existing immediate path
(`parse_phrase` reports `scheduled == false`). Anything with a time, a "then", or a
travel destination is peeled out of `structured.actions` into
`structured.scheduled`, so the immediate classifier never fires it now. The
`travel` action-book entry (alias `travel`, no execution script) exists only to
OFFER the verb; its firing is entirely chasm-driven (below).

### Firing a scheduled action for real

Each step's verb resolves to a canonical action id (`wave` -> `npc.gesture_wave`,
by whole-phrase then per-word alias match). At SCHEDULE time (in `run_turn`, where
the turn context / activated executions / args are available) the bridge builds
that action's native command file body (`actions::build_native_command_body`) and
stores it on the task step. At FIRE time the tick writes that body verbatim to
`control/actions/`; the plugin replays it, resolving the companion actor by
`npc_key` (`ResolveActionSpeaker` -> `npcSpeakersByKey`, populated when the
companion was remembered/summoned) and the script args live. So a captured command
replays correctly whenever it fires, regardless of conversation state -- NO plugin
change was needed for firing.

A step with a `to <place>` destination (or the `travel` action) fires a companion
`op=travel_to` carrying the place. The plugin (`FindMapMarkerByName`) resolves it
to a map-marker ref in the current worldspace — searching the worldspace's
permanent cell (all map markers, no distance limit) then a wide cell sweep,
matching by display name (exact, then substring) — and `MoveTo`s the companion
there. "me"/"you"/"here"/empty, or a name that matches no marker, falls back to the
player (rendezvous). Other movement/hand-over verbs with no destination (come, give,
return…) also rendezvous. Anything else is a recorded no-op step (visible in the
UI, advances the chain, no game effect — there is no first-class "loot" action).

## Data flow

```
 NVSE plugin (main.cpp)                         chasm (crates/chasm-web)
 ─────────────────────                          ────────────────────────
 runtime_heartbeat.json  ── every ~100ms ─────▶ scheduler::current_clock / world snapshot
   .game.days_passed / .hour                    (GameDaysPassed by name, GameHour 0x38)
   .player / .last_npc positions

 dialogue turn (metadata.macros                 game_bridge / run_turn:
   game_days_passed, game_hour) ─── HTTP/file ─▶  collect_scheduler_actions() →
                                                   ChasmClient::schedule_task(spec) →
                                                   scheduler::schedule_from_spec →
                                                   headless/scheduler.json  (persist)

 control/companions/*.txt  ◀── fire ───────────  scheduler::tick (every 3s):
   op=travel_to (Enable+MoveTo player)            clock/condition met → issue_command
                                                   (companion command file)
```

* **Clock**: GameHour is a direct read of global `0x38`; GameDaysPassed is read
  via a compiled GECK helper (`ReadGameDaysPassed`) that resolves the global BY
  NAME — its FalloutNV form id is NOT the commonly-cited `0x26`. Reported both in each
  turn's `metadata.macros` (`game_days_passed`, `game_hour`) and in
  `runtime_heartbeat.json` `.game` (every ~100ms). The heartbeat is the AUTHORITY
  for firing while the player is idle (not talking).
* **Store**: `crate::scheduler::SchedulerStore` at
  `<profile>/headless/scheduler.json` (write-safe under the content root, like
  persona/relationships), so it sits beside the save-sync snapshots.
* **Tick**: a tokio task spawned beside the in-process bridge evaluates pending
  tasks every 3 s and fires due ones. Fire-and-forget — a failed task is marked
  `failed` and logged, never stalling a turn.
* **Firing**: per step — a captured native Action-Book command replayed to
  `control/actions/` (the marquee path, e.g. `wave`); else a `CHASM_COMPANION_V1`
  `op=travel_to` (`npc_key`, `dest_name_base64`) to `control/companions/` for
  movement/hand-over verbs; else a recorded no-op. `travel_to` reuses the proven
  Enable + MoveTo-player summon primitive.

## Save-aware rollback

Scheduled tasks roll back with the save exactly like chat history. Hooked at the
save-sync event dispatch (`save_sync.rs`): on a **save** checkpoint the store is
copied to a sidecar keyed by the existing save-sync checkpoint id
(`SHA256(gameId|saveId)`), at `headless/save-sync/scheduler-checkpoints/<id>.json`;
on **load** it is restored from that sidecar. A load whose checkpoint has no
sidecar (predates the scheduler) CLEARS the store — so a task scheduled in a
discarded save branch vanishes. Unit-tested in
`scheduler::tests::task_in_discarded_branch_vanishes_on_load`.

## Condition engine

A task is a chain of one or more steps (a single scheduled action is a chain of
one). `advance_chain` fires the first not-done step once its trigger is met, one
step per tick, so a "then" naturally sequences (step N+1 fires after step N is
done). A step's trigger is `Time{day,hour}` (its parsed "at") or a `Condition`:

* `Immediate` — fire as soon as the previous step is done (the default for a
  "then" step with no time).
* `NpcArrived{x,y,z}` / `NpcNearPlayer` — distance ≤ 256 game units.
* `FlagSet{flag}` — a boolean raised on the task (e.g. `looted`).

The condition set is a minimal, self-contained predicate over a `WorldSnapshot`
(player/npc positions from the heartbeat + per-task event flags). It's kept for
richer chains (arrival/loot gating) and is separable from the event stream below.

## Overlap with `task/event-log`

The condition engine needs game-event signals ("body looted", "arrived at ref").
The in-flight **`task/event-log`** feature is building exactly such a stream, and
is the natural substrate for these predicates. To avoid a hard dependency:

* This feature ships its OWN minimal signals: positions from `runtime_heartbeat.json`
  and a `POST /api/ui/v1/scheduler/event` hook that raises a boolean flag on a task
  (stored under `args._flags`, so it rolls back with the save).
* The predicate evaluation is isolated in `Condition::is_met(&WorldSnapshot)` — a
  pure function over a plain struct, with NO coupling to any event source. The
  integrator can re-home `WorldSnapshot`/flag population onto the event stream
  without touching the scheduler, chain machine, or store.
* No plugin event hooks were added for looting/arrival beyond position reporting,
  precisely so they don't collide with `task/event-log`'s hooks.

## Files touched

* Mod (`native/nvse-plugin/main.cpp`): `ReadGlobalFloat`/`FormatClockFloat`,
  `game_days_passed`/`game_hour` macros, the heartbeat `.game` block, the
  `op=travel_to` companion command (`CompanionTravelTo`), and destination
  resolution (`FindMapMarkerByName`). NO plugin change was needed for firing
  native actions — captured commands replay as-is.
* chasm: `crates/chasm-web/src/scheduler.rs` (parser + store + tick),
  `generate.rs` `normalize` (peels "at"/"then"/"to" into `structured.scheduled`),
  `run_turn` (builds each step's native command, forwards to `schedule_task`),
  `actions::build_native_command_body` (capture-without-write), `ui/scheduler.rs`
  (API), the tick spawn in `router()`, the save-sync sidecar hooks, and
  `ProfilePaths::scheduler_store`.
* UI: `screens/Schedule.tsx`, `lib/api/scheduler.ts`, nav + route entries.
* Profile: ONE `travel` action-book entry (alias `travel`, no execution) — the
  only new action; scheduling itself needs no entries.

## In-game test steps

**M1 — scheduled action:** Have a companion. Tell them "wave at 1am" (or any
gesture the model knows + a time). Confirm a task appears on the **Schedule** page
(`/app/schedule`) with trigger "Day N, 1:00 AM". Sleep/wait until 1am in-game and
confirm they wave. If it doesn't fire, send back `native_plugin.log` lines matching
`scheduler:` / `Action Book`, and the `.game` block of `runtime_heartbeat.json`.

**M1b — travel:** Tell a companion "travel to the Prospector Saloon at 1am" (or
"go to Novac at dawn"). Confirm a task with trigger "Day N, …". At the time, they
`MoveTo` the named marker (log: `scheduler: resolved destination '…' -> marker …`).
An unmatched place logs `matched no map marker` and brings them to you instead.

**M2 — chain ("then"):** Tell a companion "travel to the saloon then wave" (or
"come to me then dance"). Confirm one task with two steps (progress `0/2` → `1/2` →
`2/2` as each fires). The travel step moves them; the gesture plays for real.

**M3 — rollback:** Schedule a task, save, schedule a SECOND task, then load the
first save. The second task should disappear from the Schedule page.

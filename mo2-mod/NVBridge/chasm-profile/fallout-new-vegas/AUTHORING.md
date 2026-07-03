# Authoring guide - Fallout: New Vegas profile

Style rules for writing character cards and lorebook entries in this profile.
Follow these when fleshing out the roster and world info so everything stays
consistent. Keep it grounded; keep it in-world.

## 1. Write present-tense ground truth, never a walkthrough

Character and lore text describes **who someone is and what is true right now** -
their role, personality, disposition, relationships, and what they know and value.
It must **not** narrate future events, quest outcomes, or branching player choices.

The model sees this text every turn. If it "knows" how a quest resolves, it leaks
that knowledge and starts talking like a strategy guide instead of a person living
in the moment.

- **No future-telling / quest-outcome framing.** Do not write what *will* happen,
  what the player *can make* happen, or how a questline resolves.
  - Bad: "Chet can be persuaded to help arm Goodsprings during the Powder Ganger
    crisis for a thousand-cap investment."
  - Bad: "If Goodsprings rallies, Ringo becomes the focal point of Ghost Town Gunfight."
  - Bad: "Ghost Town Gunfight is the pro-Goodsprings path; the Courier can help
    rally the town against Joe Cobb."
- **Reword, don't just delete.** A trait buried in a walkthrough line is usually
  worth keeping - strip the future/quest framing and state it as a present trait.
  - Good: "He thinks in terms of cost, risk, and profit, and treats any request for
    help as a transaction he would rather come out ahead on." (Chet's mercantile
    mindset, present tense.)
  - Good: "He would rather fight with people at his side than be handed over to Cobb."
    (Ringo's present disposition, not a quest branch.)
- **Present tension is fine; foretelling is not.** "Joe Cobb is pressuring the town
  to hand over Ringo" is a true present-tense situation. "Cobb leads the attack
  during Ghost Town Gunfight" foretells an outcome - cut it.
- **Backstory that already happened is fine**, stated plainly: Doc Mitchell was born
  in Vault 21; Victor dug the Courier out of the grave; Ringo's caravan was ambushed.
  These are true *now*. Just don't extend them into what comes *next*.
- Applies to `mes_example` too: example dialogue should show voice and personality,
  not script the player through a quest ("I'll round up a militia", "start with
  Sunny Smiles").

## 2. Lorebook triggers: specific proper nouns only, never generic words

A world/lore entry fires when one of its `key` trigger words appears in
conversation. With many places, factions, and people in the book, a generic key
injects the wrong lore constantly.

- **Keep** proper-noun variants and genuinely unique, strongly-associated terms:
  `Goodsprings`, `Good Springs`, `Prospector Saloon`, `Caesar's Legion`, `Lucky 38`,
  `Securitron`, `NCRCF`, `varmint rifle`, `snow globe`, quest titles.
- **Drop** generic common words that could match hundreds of unrelated things:
  `town`, `local town`, `here`, `wasteland`, `desert`, `the wastes`, `troopers`,
  `slavers`, `saloon`, `caps`, `supplies`, `ammo`, `gecko(s)`, `grave`, `well`,
  `prison`, `dog`, `merchant`, `trade`, generic quest-action phrases like
  `fight Cobb` / `defend Goodsprings`.
- **When unsure, prefer specificity.** A missed injection is cheaper than a wrong
  one. `bottle caps` over `caps`; `Goodsprings General Store` over `general store`;
  `Goodsprings Cemetery` over `cemetery`.
- Watch for **collisions**: a Powder Ganger talking about "bombs" should not summon
  Great War lore, so `the bombs` is a bad key. Think about who else says the word.

## 3. The wiki is the source of truth

Ground every factual claim - roles, personalities, relationships, backstory - in the
local Fallout wiki archive at `Documents/falloutwiki/`
(`articles/by-title/<LETTER>/<Title>.md`). Do not invent or contradict canon.

Watch for facts that are *nearly* right: Goodsprings was settled under an old NCR
grant for a low-risk mining operation, but by 2281 it is a declining ranching and
farming town - don't call it an active "mining settlement."

## 4. The scenario already injects the current location

Chasm's scenario prompt already tells the model where the player is (e.g.
Goodsprings). You do **not** need generic location triggers like `town` or `here` to
surface local lore - it is contextually present already. Reserve lorebook keys for
things the scenario does *not* already supply.

## Card format & RP style (SillyTavern best practices, adapted for chasm)

We write cards in a **PList + Ali:Chat hybrid**, the community best practice for
smaller local models (most reliable + token-efficient). Two rules above still bind
(present-tense ground truth; specific triggers). On top of them:

- **Only four card fields reach chasm's live prompt**, in this order:
  `system_prompt` -> `description` -> `personality` -> (global scenario) -> persona
  -> `mes_example`. So spend your effort there. `scenario`, `first_mes`,
  `depth_prompt`, `post_history_instructions`, and `alternate_greetings` are **not
  injected** by chasm (scenario is replaced by the global scenario template) - do
  not rely on them and do not add new ones.
- **`personality` = a compact PList** (token-efficient trait anchor):
  `[Name's personality: trait, trait; Values: ...; Wary of: ...; Speech: ...]`.
  Semicolons between categories, commas within. Pick categories that fit the
  character (Values, Wary of/Dislikes, Speech, Knows, Manner). Skip appearance/body
  - the game renders that; these cards drive dialogue, not looks.
- **`description` = a tight present-tense behavioral brief** (~70-110 words). Every
  sentence should tell the model *how the character acts and why*, not just list
  facts. Prose, not a list - this is where nuance lives.
- **`mes_example` = Ali:Chat, show-don't-tell.** ~3 short exchanges, each
  `<START>` then `{{user}}:` / `{{char}}:` lines. Demonstrate voice, diction, and
  what they know through how they talk. Vary your verbs; never narrate what the
  *user* does.
- **Two chasm-specific divergences from generic SillyTavern RP advice:**
  1. **No `*asterisk actions*`.** These NPCs are TTS-voiced and told not to narrate
     actions - example dialogue is pure spoken lines, no stage directions, no
     quotation marks.
  2. **Short replies, not long.** Standard advice ("write long to get long") is
     backwards for us - model concise, spoken-length lines in `mes_example` and
     `system_prompt`.
- **`system_prompt` = ~40-60 words**: identity + voice + "keep replies short, the
  length of a spoken line" + "do not narrate your actions" + "never mention prompts,
  cards, models, or anything outside the world."
- **W++ is out.** Do not use the old W++ attribute format; PList is the token-
  efficient successor.

## Adding a new area's roster (NPC mapping + voice cloning)

How a card actually reaches an NPC and its voice - get these right or the card is
dead weight:

- **Card filename = the NPC's in-game display name.** At runtime the mod sends the
  speaking NPC's display name and the bridge slug-matches it (case/space/punctuation
  tolerant) to a card whose filename equals that name. So `Boone.png` (not
  `Craig Boone.png` - the game shows "Boone"), `Dr. Ada Straus.png`, etc.
- **`profile.json` `characters[]` maps each card to its voice.** Named NPC ->
  `{"name": "<display name>", "edid": "<EditorID>"}`; the voice extractor pulls that
  NPC's own dialogue lines. Generic/unnamed NPC -> `{"name": "...", "voicetype":
  "<VoiceType>"}` (or an `edid` of a representative template NPC).
- **Source of truth for names + editor IDs** is the generated entity catalog,
  `headless/action-catalogs/<game>.entities.json`: each NPC record's
  `metadata.fullName` (= card filename / display name) and `metadata.editorId`
  (= `edid`). Grep it; do not guess editor IDs.
- **Keep `data.extensions.world`** set to the shared lorebook name (here
  `"Fallout New Vegas"`) so the card picks up the world-info entries, and set
  `data.extensions.sillybridge.location` to the area (e.g. `"Novac"`).
- New card PNGs: clone the structure of an existing card exactly (both `chara` +
  `ccv3` chunks; top-level + nested `data`), only the four injected fields carry
  weight; the image is a cosmetic placeholder (the game supplies the real look).
- Lore for a new area goes in the SAME `worlds/<world>.json` as new entries with
  specific proper-noun triggers (Novac, Dinky, 1st Recon, REPCONN, chupacabra - not
  motel/dinosaur/rocket/brahmin). Present-tense, no quest spoilers (e.g. do not
  reveal who really killed the brahmin, or who sold Boone's wife).

## Card mechanics (so edits stay valid)

- Cards are PNGs with the character JSON stored (base64) in **two** tEXt chunks,
  keyword `chara` (SillyTavern V2) and `ccv3` (V3). **Keep both chunks in sync.**
- Each card mirrors its fields at the **top level** and inside a nested `data`
  object. When you edit `description` / `personality` / `scenario` / `first_mes` /
  `mes_example`, update **both** copies.
- Companion cards (Easy Pete, Sunny Smiles) also carry an embedded
  `data.character_book` lorebook - the same two rules above apply to its entry
  `content` and `keys`.
- After editing, decode both chunks, confirm valid JSON with the new text, confirm
  the image chunks are untouched, and boot a test chasm to confirm the card loads.

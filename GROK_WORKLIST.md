# Grok worklist — OpenBFME backlog for external AI execution

**Status:** a curated backlog, nothing started. Hand items or whole sections to
Grok CLI as you see fit. Grok CLI has lookup + icon-generation skills; section A
is sized for that. Everything else is code work in this repo.

**House rules for anything Grok produces:**
- Retail payloads never leave `.private\` / never enter git. Generated art is
  placeholder art: it lives in the repo as `game/assets/placeholder/` (or a
  dedicated placeholder pack) and must be marked as generated, never presented
  as converted retail art.
- Fail closed: no silent generic substitutes for missing retail data. Recorded
  exclusions with reasons only.
- Gates must stay green: `run_retail_slice.bat --test` (men, currently 262+),
  `menu_skirmish_runner` (39), `retail_spellbook_runner` (72), map runners
  (55/33), consumer runners (43/74). Run Godot gates SEQUENTIALLY — two
  headless instances deadlock. On a hang: grep stdout for `SCRIPT ERROR` first.
- No git mutations by the agent unless explicitly told to commit.

---

## A. Placeholder art pack (Grok icon-generation skills)

Purpose: the game has genuine art gaps where retail art is absent or not yet
converted. Generated placeholders are explicitly marked `generated-placeholder`
in the placeholder pack manifest.

1. **Powers-orb frame art** — the powers screen currently uses styled chrome;
   retail has an ornate sphere frame, sockets, fork lines, glow states, and
   RESET/ACCEPT button frames (ref: retail screenshot set, `game.dat_Y1R9z9ni6M.jpg`
   in the owner's ShareX folder). Generate: sphere frame, empty socket,
   glow ring, cooldown sweep mask, 2 button frames, fork segment.
2. **Faction crests** (menu faction selection): Men, Elves, Dwarves, Isengard,
   Mordor, Goblins — one crest icon each (256×256, transparent PNG).
3. **Hero portrait placeholders** — for any hero whose retail portrait crop is
   missing in packs (most are converted; generate only for the gaps — check
   `.private/content-packs/*/data/playable-units/*.json` portraitImageIds vs
   resolved bindings). ~34 heroes across six factions; expect 0–6 gaps.
4. **Cursor set** — contextual mouse cursors (the game has none): default,
   move/target, attack/target-invalid, attack-move, select-add, place-building,
   place-building-invalid, garrison, sell, repair. ~10 cursors (32×32 + hot spot).
5. **Menu chrome** — BFME2-style green translucent button frame (normal/hover/
   pressed/disabled), panel frame, dropdown frame, skirmish screen heading
   ornament, loading-screen frame. (Currently styled UI; no retail shell
   textures exist in packs.)
6. **Map preview placeholders** — only for maps lacking converted preview art
   (Fords has one; check `maps/<slug>/map.json` `preview` in the five-maps
   pack; generate 512×384 for any missing of Rivendell/Mount Doom/Dagorlad/
   Mordor).
7. **Veterancy/rank pips** (for hero XP + horde veterancy UI): rank chevrons
   levels 1–10 (small, two states), XP bar frame for the hero portrait ring,
   "level up!" flash badge.
8. **Structure upgrade icons** — L2/L3 purchase buttons per building kind
   (fortress excluded), plus upgrade-gated unit lock overlay icon.
9. **Formation/stance icons** — stance socket already exists (top), stop
   (bottom); add: porcupine (pikes), wedge (cavalry), shield wall (Block),
   line formation — 64×64 each.
10. **Misc command icons** — sell, repair, set-rally, stop, attack-move,
    select-all-army, jump-to-fortress.

## B. Hero XP / veterancy system (user priority)

Retail behavior: heroes and hordes gain XP from kills; levels 1–10; stat
scaling per level; hero abilities unlock at authored levels (the ability docs
already carry level requirements); portrait shows rank ring + pips; hordes
show chevrons.

1. **XP accrual in sim** (`retail_slice_sim.gd`): per-battalion/hero XP pool,
   XP awarded on member kills from the victim's authored XPValue (converter:
   check `ExperienceValue`/leveling fields in unit INI — extend
   `playable_unit_compiler.py` if not yet emitted, recorded otherwise).
2. **Level state + stat scaling**: per-level damage/health/speed scalars from
   authored leveling data (retail `ExperienceLevels` / attribute modifiers);
   fail closed on missing per-level data — recorded provisional tables are NOT
   allowed to masquerade as retail.
3. **Ability unlock by level**: ability buttons already carry authored level
   gates (hero-abilities workstream) — wire level state into availability.
4. **HUD**: hero portrait rank ring + pips + XP bar (placeholder art from A7);
   horde chevrons on the selection portrait/health overlay; level-up event +
   flash + sound route (check pack for level-up stingers).
5. **Horde veterancy**: same XP pipeline for battalions; banner-carrier
   replenish loop becomes purchasable at level (pairs with equipment upgrades,
   C4).
6. **Revival tie-in**: fallen heroes retrain at fortress (already fixed in the
   runtime batch) — revived heroes restart at level 1 (retail behavior);
   verify.

## C. Combat/economy core gaps (ranked from the gameplay audit)

1. **Armor/damage-type counter matrix** — `armor.ini` has no compiler; only
   the fortress has armor. Add an armor-set compiler (importer) + per-unit
   armor application in `_apply_member_damage` + per-structure-kind armor.
   This is the single biggest "feels like BFME2" gap. (Interim recorded
   provisionals landed for fortress SIEGE/HERO — replace with compiled data.)
2. **Structure upgrade path (L2/L3)** — purchase buttons on producers,
   upgrade costs/times from INI, level-gated units unlock (Mirkwood Archers,
   Ranger). Unlocks two currently-locked unit types.
3. **Cavalry trample/knockback** — charge pass for cavalry through infantry
   (damage + scatter); currently knights behave as fast melee. (Trample vs
   Block already halved; direction-agnostic trample still flagged.)
4. **Real skirmish AI** — build-order AI (farm→barracks→expansions→second
   producer), wave attacks with rally/timing, upgrade purchases, 2–3
   difficulty tiers; replaces one-farm round-robin (`game/src/ai/` is empty
   and waiting; AI must issue the same public sim commands as the player).
5. **Structure firing** — battle towers/fortress arrows: no structure-attack
   mechanic exists; needs tower weapon INI extraction + per-tick structure
   firing pass.
6. **Equipment upgrades per horde** — Heavy Armor / Forged Blades / Fire
   Arrows / Banner Carrier: purchase commands, armor/damage modifiers, banner
   replenish (M3 extracted data exists).
7. **Building lifecycle polish** — auto-repair tick, sell command (refund
   fraction; SellableCommandSet already in HUD contract), settable rally
   point.
8. **Order queuing** — shift-queue waypoints + queued attack-moves; waypoint
   flag presentation exists (`retail_order_indicator.gd`).
9. **Victory widening** — eliminate all enemy structures/production, not just
   the fortress (verify against retail skirmish rule).
10. **Projectile-damage units** — MordorCatapult, DwarvenAxeThrowerHorde
    (ProjectileNugget), MordorMumakil (rangefinder), IsengardExplosiveMine
    (SpawnAndFade) — converter resolution, currently recorded exclusions.

## D. Converter/content unlocks for locked powers

Each unlocks a spellbook power with no sim changes (the sim's
`_spellbook_effect_support` is data-driven):

1. Arrow Volley + Earthquake — weapon/damage leaves on spell OCLs.
2. Five summons (Tom Bombadil, Hobbits, Rohan, Dunedain, Army of the Dead) —
   summon-egg hatch targets, stats, lifetimes.
3. Lone Tower — `GondorSentryTower_Independant` structure definition.
4. Elven Wood — `ElvenGrove` taint aura modifier + duration.
5. Cloud Break — `SPELL_CLOUDBREAK_DURATION` + enemy stun modifier leaf.
6. Remaining hero-ability kinds — mount/dismount toggles, disguise, other
   non-damage/heal/summon/modifier shapes (currently recorded unavailable).

## E. Map content for the four new maps (boot-verified, content-bare)

1. **Prop binding** — 2,196 unresolved placements (Rivendell); all four maps
   bind zero props. Convert the map prop model sets (same lifecycle pipeline
   as Fords props) and bind placements.
2. **Roads** — no cooked roads layer in the five-maps pack; road wire objects
   sit unconverted in `objects.json`.
3. **Per-map environment** — fog/sky/lights per map (global afternoon rig
   today); cook each map's authored environment.
4. **Blocking water rules** — Fords applies the reviewed crossing rule; other
   maps navigate source passability (water non-blocking by design,
   documented). Re-review per map where retail blocks (moats, rivers).
5. **Navmesh/routing graphs** — pack has none (`empty-no-authored-navmesh`);
   bounded A* over passability today. Cook retail waypoint graphs if authored.

## F. Presentation parity (from the visual/audio audit, ranked)

1. **Skybox** — flat color today; resolve the retail texture-set selection and
   render `new_skybox.w3d` (biggest frame-filling fix).
2. **Water** — planar reflection + cooked water textures with animated
   UV/normal distortion (flat tinted quad today).
3. **Building damage/construction particles** — fire/smoke/dust/collapse:
   10 dual-family systems parked pending the cross-family A/B; bind the 11
   single-family IDs first for most of the damage read.
4. **Cast shadows** — enable shadow mapping on the object-domain sun; blob
   decals stay for infantry if volumes can't match.
5. **Positional audio** — pooled AudioStreamPlayer3D for voices/combat SFX
   with source MinRange/MaxRange; ≥4 concurrent voices (mono 2D single-
   channel today).
6. **Radar minimap** — top-down terrain capture under the parchment mask
   (schematic outlines today).
7. **Camera feel** — interpolated zoom; source-derived scroll speed (stepped
   zoom + playability-guess scroll today).
8. **Corpse/decay polish** — corpse sink/decay, collapse dust/shake,
   BuildingSink audio timing.

## G. UX/UI polish

1. **Contextual mouse cursors** — art from A4; cursor rules engine.
2. **Tooltip hotkeys** — show shortcuts in command tooltips (input actions
   exist: attack_move=F, stop_units=H, stance_cycle=Z).
3. **Runtime rebind UI** — options screen for the input map.
4. **Loading screens** — faction-aware loading screen (colors currently
   hardcoded `FACTION_ALIGNMENT` provisional; music `Shell2MusicForLoadScreen`
   resolved — wire it).
5. **Options menu** — volume sliders (music/voice/sfx buses exist),
   resolution/window mode, scroll speed.
6. **Menu polish** — ornate button caps (A5 art), logo placement, faction
   crest integration (A2), map preview for all five maps.

## H. Cross-faction matches (multi-faction play)

Currently same-faction only (fail-closed both layers). Needs: per-team faction
manifests in the sim (structures/spawn roster/producers/AI plan per team),
enemy-faction resolution (`_resolve_enemy_faction` exists and blocks),
two-team content coexistence proof (men+elves packs cohabit today). Unlocks
Elves vs Men — the most requested play mode.

## I. Tech-debt queue (from the three adversarial reviews)

1. **HIGH** — multi-job Blender log guard: multi-job validates a synthesized
   marker, not real Blender output; a GLB with unresolved textures passes and
   poisons caches. Scan per-job real logs.
2. DDC salt additions: `playable_unit_import.py`, `spellbook_import.py`,
   `faction_census.py`, `sage_string.py` into `_COMPILER_SALT_MODULES`.
3. Manifest-less asset trees share one identity bucket under
   `OPENBFME_SHARED_CACHE` — fingerprint or refuse shared cache.
4. Media cache: add the W3D byte-identical tripwire on populate; catch the
   cross-process `os.replace` race.
5. W3D cache key: include `w3d_multi_to_glb.py` driver hash.
6. Warm-extract verification depth — owner decision: keep single-pass default
   vs verify manifest aggregate once per run.
7. m3 lane pins: ranger duplicate-binding guard + trebuchet census pin drift
   (both fail-closed guards tripping on in-flight work).
8. HUD oracle hash pins — final re-pin once the slice source settles.
9. Prerequisite OR-across-routes (currently min-cardinality set;
   `retail_slice_sim.gd` ~545).
10. `OPENBFME_STARTER_ARMY=1` env seam → rules flag.

## J. Bigger rocks (post-vertical-slice phases)

1. **Multiplayer** — deterministic lockstep per `docs/SIMULATION_PROTOCOL.md`
   (MatchConfig/GameCommand/StateDigest/checkpoints/replays); fixed-point C#
   sim port (write against pack JSON semantics, not a mechanical GDScript port).
2. **Instancing/LOD for 8-player scale** — MultiMesh for battalion members;
   per-member Node3D subtrees won't hold 60 FPS at 8 armies.
3. **deathModelSwaps runtime consumption** — recipe metadata complete (cave
   troll/mountain giant); Godot-side swap rendering unimplemented.
4. **Cinematics (.bik)** — no video family exists in the census at all.
5. **Create-a-Hero** — currently engine-managed exclusion; full lane later.
6. **More maps** — map conversion accepts ~18% of the runnable corpus;
   rejection taxonomy needs reconciliation before expanding the set.
7. **Campaigns / War of the Ring / naval / Ring mechanics** — roadmap phases
   per DIRECTION.md; not before the skirmish loop is frozen.

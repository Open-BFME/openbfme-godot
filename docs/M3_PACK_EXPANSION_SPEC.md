# M3 pack expansion — full Men of the West faction content

> **Historical — this spec has been executed and its scope rules no longer
> apply.** It reads as forward-looking work but the body is finished, and its
> instruction below to "not modify anything under `game/`" was a boundary for
> ONE importer-lane task in July 2026, not a standing rule. Taken as current it
> would stop legitimate runtime work. Retained for the conversion detail, which
> is still accurate; do not take scope or ownership direction from it.
>
> Current scope authority: `DIRECTION.md`. Current milestone:
> `docs/MILESTONE_CURRENT.md`.

Owner (at time of writing): Codex, importer lane only.

## Goal

Expand the private `bfme2-men-vslice` pack so it carries the complete Men of the
West faction: every building with its command-button icons ("internal icons"
players see when a building is selected), the remaining units, upgrade data and
icons, the real SpellBook tree, retail command-point caps, tooltip strings, and
house-color mask data for per-player faction tinting.

Everything converts from the user's own retail install via the existing
importer pipeline (`importer/openbfme_importer`, profile
`importer/profiles/men-fords-v0.json` — extend it or add a v1 profile that
supersedes it). Raw source lives in `.private/retail-work/cache/effective-assets/`.

## Required scope (each item is REQUIRED; report any item whose source assets
## genuinely do not exist in the retail cache instead of silently skipping)

1. **Buildings (models + all animation states authored in W3D: idle/build-up
   (construction)/damaged/destroyed):** fortress, farm, barracks, archery
   range, stables, siege works, battle tower, well, statue (heroic statue),
   blacksmith, marketplace, wall hub/plots if the Men fortress expansion
   sockets are data-driven. Include each building's INI object definition
   (hash-only data resource like the existing five).
2. **Building command icons:** for EVERY building above, the palantir button
   icon of the building itself AND the icons of everything it trains/researches
   (units, upgrades, wall options). Source: mappedimages INIs + the referenced
   TGA/DDS atlases; follow the existing texture-atlas-crop pattern
   (`importer/tests/test_texture_atlas_crops.py`). Produce an icon coverage
   census in the report: building → [expected commands] → icon present yes/no.
3. **Units:** trebuchet/catapult, battering ram, knights, tower guard, rangers
   — models, skeletons, animation clips (including `selectionTransition`
   attention clips for soldier + archer, which the current pack lacks), icons,
   INI definitions, audio (voice select/move/attack) following the existing
   audio closure pattern.
4. **Upgrades:** heavy armor, fire arrows, forged blades, banner carrier —
   INI upgrade definitions (hash-only) + icons.
5. **SpellBook:** extract the real Men/Good-faction SpellBook INI (power ids,
   point costs, tree prerequisites, recharge, icon mappings) into a pack data
   file (`data/spellbook.json` or similar typed extraction consistent with
   existing `sage_*` extractors). Include all power icons.
6. **GameData values:** retail `GoodCommandPointLimit` / command point caps and
   the values that scale them (game mode variants noted in the report).
7. **Tooltip strings:** closure over `lotr.str` for every command the expanded
   pack exposes (buildings, units, upgrades, powers) into the existing
   strings data path.
8. **House-color masks (faction colors):** extract the W3D house-color texture
   stage data — which materials/meshes carry the house-color mask and the mask
   textures themselves — for all units and buildings in the pack, emitted as
   per-model metadata (e.g. `houseColor` block in the typed visual graph /
   model metadata) plus the mask textures as pack assets. This powers
   player-blue vs enemy-red armor/building sections at runtime.
9. **BFME2 logo asset** as a pack leaf (for the game splash), if a clean
   logo image exists in the retail assets.

## Definition of done (ALL must hold; no partial credit)

- Profile builds **twice** with byte-identical bundle digests (A/B check).
- `python -m pytest importer/tests` fully green, including new tests covering:
  icon census, spellbook extraction shape, house-color metadata presence.
  Fix any importer test that pins stale identity as part of this work.
- `.private/content-packs/selection.json` points at the new bundle.
- Report written to `.private/retail-work/reports/m3-expansion-report.md`:
  content census (counts per category), icon coverage table, missing-source
  list, new profile + bundle hashes.
- No retail-derived bytes committed to the git repo — only profile JSON,
  importer source, tests, and docs. `.private` stays gitignored.
- **Do NOT modify anything under `game/`** — runtime wiring happens separately.
  (Historical constraint on this one task; see the banner at the top. It is not
  a current rule and must not be read as one.)
- Do not run any Godot gates; they pin the old bundle identity and are updated
  by the game-side owner after wiring.

## Anti-wander rules

- No speculative refactors of the importer; extend existing converter kinds.
- No smoke tests beyond the pytest suite and the A/B digest check.
- If a scope item is blocked >30 min, record it in the report and move on.

# Modding

You can ship **project-authored** packs (JSON + assets). You cannot ship EA's
retail files. For retail play you convert **your** install into a private pack
under `workspace/` (see [ONBOARDING.md](ONBOARDING.md)).

Two ideas, keep them separate:

1. **Loose packs (works today)** - drop a folder with `pack.json` and overrides.
   Good for local experiments. Not a multiplayer parity contract.
2. **Production sim vs presentation packs (target)** - strict manifests and
   digests so multiplayer stays fair. Policy lives in
   `contracts/openbfme-modding-contract.json`; full loader is **not finished**.

## Glossary

| Term | Meaning |
|---|---|
| **Pack** | A folder of content the game loads (units, maps, assets). |
| **Priority** | Higher number wins when two packs define the same `id` (loose lane). |
| **Simulation** | Rules that change who wins (HP, damage, pathing, AI). Must match in multiplayer. |
| **Presentation** | Looks/sounds (meshes, UI art, music). Can differ between players later. |
| **Fail closed** | Missing required data = error, not silent fake content. |

## Quick start: the example mod in this repo

There is a real example under `game/mods/example_hard_orcs/`.

**Layout:**

```text
game/mods/example_hard_orcs/
  pack.json
  units/orc.json
```

**`pack.json`** (from the repo):

```json
{
  "id": "example_hard_orcs",
  "name": "Example: Harder Orcs",
  "version": "1.0.0",
  "priority": 50,
  "author": "Open BFME"
}
```

**`units/orc.json`** (overrides the unit id `orc`):

```json
{
  "id": "orc",
  "hp": 620,
  "dmg": 12,
  "desc": "Modded tougher orcs (example override)."
}
```

How to try it (loose / dev lane, not a retail multiplayer proof):

1. Keep `game/mods/example_hard_orcs/` in the tree (already committed).
2. Set Godot and launch the client:
   ```bat
   set OPENBFME_GODOT=C:\Path\To\Godot_v4.7-stable_win64_console.exe
   run_game.bat
   ```
3. Loose packs under `game/mods/` load with base game data for local experiments.
4. Higher `priority` wins on the same object `id`. This example uses
   `"priority": 50`. Converted **retail** packs often use **100+**, so if both
   define `orc`, the retail pack may win. For a clear override against retail,
   raise the example priority above the retail pack or load base content only.

There is no single headless "orc HP = 620" gate for this sample yet. Treat it as
a **layout + JSON schema demo** you can edit and re-launch.

Optional: point at an external content root:

```bat
set OPENBFME_CONTENT=C:\Path\To\my_content_root
```

`my_content_root` should contain one or more pack folders, each with `pack.json`.

More notes: [game/mods/README.md](../game/mods/README.md).

## Make your own loose pack

```text
my_mod/
  pack.json
  units/*.json
  buildings/*.json
  factions/*.json
  abilities/*.json
  maps/*.json
  globals.json          optional
  assets/...            optional meshes/icons
```

Minimal `pack.json`:

```json
{
  "id": "my_mod",
  "priority": 50
}
```

Unit JSON follows the same shape as files under `game/data/base/` (ids must
match what you want to replace).

**Do not** put retail `.big` / `.w3d` / `.dds` extracts into a public mod.
Convert through the importer if you need retail-derived content, and keep that
under `workspace/`.

## Production direction (not fully implemented)

When the strict contract ships, packs declare category, version, dependencies,
and digests. Simulation packs must match on every peer. Presentation packs may
differ. See the machine-readable contract:

```text
contracts/openbfme-modding-contract.json
```

Snippet of the policy intent (from that file):

```json
"presentation_digest": {
  "required_match": "not-required",
  "purpose": "diagnostics, caching, provenance, and optional server policy",
  "constraint": "A presentation mismatch cannot change authoritative state, commands, event timing, collision, projectile origins, or command acceptance."
}
```

Until that loader is finished, treat multiplayer + custom packs as experimental
and do not claim retail multiplayer parity for loose overrides.

## Rules of the road

- **Never** commit retail archives or converted retail packs to Git.
- Loose overrides are for development; they are **not** proof of retail parity.
- Prefer small, testable packs with clear `id`s.
- When parity matters, use the private selected retail pack path, which fails
  closed if something is missing.

## Related

- [CONTENT_PIPELINE.md](CONTENT_PIPELINE.md)  
- [ONBOARDING.md](ONBOARDING.md)  
- [contracts/openbfme-modding-contract.json](../contracts/openbfme-modding-contract.json)  

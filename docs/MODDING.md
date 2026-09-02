# Modding

You can ship **project-authored** packs (JSON + assets). You cannot ship EA's
retail files. For retail play you convert **your** install into a private pack
under `workspace/` (see [ONBOARDING.md](ONBOARDING.md)).

Two ideas, keep them separate:

1. **Explicit development packs (works today)** - create a folder with
   `pack.json` and overrides, then name that pack or its selection explicitly.
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

## Example mod in this repo

The non-shipping example lives under `examples/mods/example_hard_orcs/`.
It is outside the exported Godot project on purpose: packs found under
`game/mods/` or `user://mods` are diagnosed as ambient content and are **not**
mounted. The runtime loads only an explicit pack root or an explicit
`selection.json` load set.

**Layout:**

```text
examples/mods/example_hard_orcs/
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

How to inspect it by itself (loose / dev lane, not a retail multiplayer proof):

1. Copy the example outside `game/` to a writable development folder:
   ```bat
   xcopy /E /I examples\mods\example_hard_orcs C:\OpenBFME-Mods\example_hard_orcs
   ```
2. Select that copied pack root explicitly, set Godot, and launch:
   ```bat
   set OPENBFME_CONTENT=C:\OpenBFME-Mods\example_hard_orcs
   set OPENBFME_GODOT=C:\Path\To\Godot_v4.7-stable_win64_console.exe
   run_game.bat
   ```
3. An explicit pack root is the complete load set for that run; the loader does
   not add base, retail, sibling, or ambient packs automatically. This small
   example is therefore a schema/layout inspection unless it is explicitly
   composed with a complete selected pack set.

To layer it onto a selected development load set, copy it under that content
root and add its cache-relative directory to the selection document's
`supplementalPacks` array. Every supplement is named explicitly; invalid or
missing entries fail closed, and sibling directories are never scanned. Do not
edit or add supplements to an immutable retail selection in place; publish a
new development selection through the content tooling instead.

Within an explicitly composed development selection, higher `priority` wins on
the same object `id`. This example uses `"priority": 50`; the selected pack may
therefore win if it declares the same `orc` row at a higher priority.

There is no single headless "orc HP = 620" gate for this sample yet. Treat it as
a **layout + JSON schema demo** you can edit and re-launch.

An external directory must be either one valid pack root, as above, or contain
a valid `selection.json`. A directory of sibling pack folders is not scanned:

```bat
set OPENBFME_CONTENT=C:\Path\To\my_content_root
```

If that path names neither a valid pack nor a valid selection, loading fails
closed instead of falling back to ambient or stale content.

## Make your own explicit development pack

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

## Validating a mod

Loose INI mods put overrides and additions beneath `MODDIR/data/ini/`. Validate
one against an extracted base INI tree with:

```bat
python -m openbfme_importer.cook.validate --ini-root BASE --mod MODDIR --json validation.json
```

Repeat `--mod MODDIR` to add layers; later mods win when virtual paths match.
The command exits `0` when there are no failures, `1` when any named reference
or parse failure remains, and `2` for invalid command usage. Gap rows are
reported in the JSON list but do not make validation fail.

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

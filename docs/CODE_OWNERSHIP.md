# Code and repository ownership map

OpenBFME has one shipping target: an exact clean-room Godot 4.7 port of
Rise of the Witch-king Patch 2.02 v9.7.7. The executable map in this document
describes where work belongs; it is not evidence that any gameplay,
presentation, or content domain has parity.

The machine-checked map is
[`config/repository-boundaries.json`](../config/repository-boundaries.json).
It is derived from `AGENTS.md`, the product/baseline contracts, `DIRECTION.md`,
`PLAN.md`, and the work-item ledger; it is not a competing canonical authority.
Run its focused check from the repository root:

```powershell
py -3 tools\check-repository-boundaries.py --check
```

A pass means that every tracked file matches exactly one non-overlapping path
rule, all declared runtime paths and Godot autoloads resolve, every file at or
above the conflict threshold has one planning owner and exact focused test
routes, every evidence ID/pointer resolves under its claim limit, and tracked
generated snapshots plus all of their consumers are mapped. It does not prove
SOURCE, CONVERT, LOAD, BEHAVIOR, VISUAL, or AUDIO parity.

## Shipping authority

The current shipping path is deliberately singular:

```text
run_game.bat
  -> game/project.godot
  -> scenes/startup_boot.tscn
  -> src/ui/startup_boot.gd
  -> scenes/boot.tscn
  -> src/ui/main_menu.gd
  -> scenes/retail_loading_boot.tscn
  -> src/ui/retail_loading_boot.gd
  -> scenes/retail_vertical_slice.tscn
  -> src/retail_slice/retail_vertical_slice.gd
  -> src/retail_slice/retail_slice_sim.gd
```

`RetailSliceSim` is the current gameplay authority. The checker also parses the
seven project autoloads (`DiagLog`, `Events`, `SimClock`, `ModLoader`,
`ContentDB`, `GameState`, and `GameAudio`) instead of treating the scene chain
as the entire runtime. The C# code under
`engine/OpenBfme.Sim` is a non-shipping comparison experiment. No worker may
route a feature through it, claim its tests as shipping proof, or cut it over
without an integration-owner architecture decision backed by exact
command/state/event trace equivalence.

## Repository classes

| Boundary | Class | Contribution rule |
|---|---|---|
| v9.7.7 contracts, `DIRECTION.md`, `PLAN.md` | canonical product contract | Integration owner only. BFME2 1.06 and RotWK 2.01 archive contracts are effective input layers, not alternate product targets. |
| `orchestration/work-items.json` | live work ledger | The only backlog. Briefs/reports are historical narrative; templates are workflow. |
| `game/src/`, `game/scenes/`, `game/project.godot` | shipping code | The Godot runtime. Tests and data are deliberately separate classes. |
| `game/tests/`, `importer/tests/` | test code | Verification routes; never retail or parity evidence by themselves. |
| `importer/openbfme_importer/`, `importer/blender/` | importer source | Deterministic private retail conversion, separate from its tests/config. |
| `content/` | public clean-room fixture | Test/mod fixtures only; never retail evidence or an implicit shipping fallback. |
| `tools/`, `.github/`, `config/` | tooling and automation | Gates, schemas, manifests, and Windows-native workflow. |
| `engine/`, `global.json` | non-shipping experiment | Comparison only until an accepted authority change. |
| `workspace/`, `dist/`, `game/.godot/` | private/generated | Never tracked. Retail bytes, packs, captures, logs, receipts, and caches stay here. |

The manifest uses exact roots and explicit prefix roots with narrow exclusions.
The checker applies every rule to every tracked path and fails on both zero
matches and multiple matches. Adding a file under an unclassified root—or
letting a broad rule swallow an exception—is therefore an architecture event,
not an invisible default.

## `game/data` quarantine

`game/data/base/assets` currently contains 563 tracked files and mixes
shipping-looking UI, test material, legacy donor assets, and duplicated bytes.
It is classified as `public-asset-debt`, not as an approved asset source.
`P0-ASSET-HYGIENE-001` owns its manifest-led split, attestation,
deduplication, and reference retargeting.

The tracked files `build_info.json`, `retail_ai_call_census.json`,
`retail_faction_sides.json`, `retail_module_census.json`,
`retail_w3d_chunk_backlog.json`, and `script_world_surface.json` are generated
or historical snapshots. They may help locate old implementation assumptions,
but they cannot satisfy current evidence. Their named work items must
regenerate or replace them against the sealed v9.7.7 effective corpus and the
accepted shipping runtime. This is active debt, not archival trivia: the map
currently finds 14 operational shipping, test, and generator consumers. Each
artifact pins only its exact rooted `game/data/...` and `res://data/...` tokens;
bare filename tokens are rejected because they create ambiguous consumers. The
checker extracts executable string literals (excluding comments and Python
docstrings) and statically composes common Python `pathlib`, `os.path`, `/`,
and `+` forms, then requires the discovered consumer set to equal the manifest.
A prose mention is not promoted to a load edge; a new code reference requires
an explicit disposition.

Evidence is resolved by ledger ID. Current tracked evidence may point only to a
class marked eligible; `sourcePointers`, input-evidence IDs, and stale-artifact
paths are checked too. A historical diagnostic may remain on an open item to
describe the red baseline, but a completed non-L0 item cannot cite it.

## High-conflict seams

The checker inventories every tracked `.py`, `.gd`, `.cs`, and `.ps1` file at
or above 5,000 lines. Every such file must have explicit seams and focused
test routes before an agent can touch it.

| Area | Current files | Owner route | Required seam |
|---|---|---|---|
| Unit conversion | `playable_unit_compiler.py` and its large test module | `P1-OBJECT-001` | parse -> normalized IR -> emit; fixtures/assertions remain separable |
| Module conversion | `module_contracts.py` and its large batch test | `P1-MODULE-001` | vocabulary -> partition -> receipt |
| General conversion | `pipeline.py` | `P0-SELECTION-002` | admission -> stages -> immutable publish |
| W3D conversion | `blender/w3d_to_glb.py` | `P1-ASSET-001` | decoded model -> Blender scene -> GLB |
| Match runtime | `retail_vertical_slice.gd`, `retail_slice_sim.gd` | `P1-SIM-001` | commands -> deterministic ticks -> state/events -> presentation |
| Script runtime | `retail_slice_script_world.gd` | `P1-SCRIPT-001` | vocabulary -> state mutation -> presentation sink |
| Skirmish HUD | `retail_hud.gd`, `retail_hud_apt_convert.py` | `P1-UI-001` | source/timeline -> neutral view model -> runtime/host bridge |
| WotR screen | `wotr_screen.gd` | `P2-WOTR-003` | strategic state -> command surface -> screen presentation |
| WotR map | `wotr_map_view.gd` | `P2-WOTR-002` | strategic geometry -> territory interaction -> map presentation |

The owner route is singular and the work item must list the file as an exact
literal candidate; a directory or wildcard is not enough. Test routes are exact
tracked test files too. Candidate paths remain planning hints, not ownership;
the checker separately rejects a conflict with any other currently assigned
lane's exact `ownedPaths`. Allocation still narrows the selected item to exact
paths. When a file crosses the threshold or a route disappears, the integration
owner must add a precise route or schedule a bounded split.

## Asset identity and naming

- Preserve every retail virtual key verbatim, together with winning archive,
  layer, and digest provenance. Do not make retail identifiers prettier.
- Put converted retail bytes only in immutable, content-addressed private
  packs. Human-readable labels are metadata, never the storage identity.
- Give public authored assets purpose-rooted canonical paths and record source
  or generator, license/attestation, SHA-256, and intended shipping/fixture use.
- Store one physical payload for one exact digest. Compatibility names are
  manifest aliases, not copied files.
- Treat `game/data/base/assets` as debt until the public-asset admission gate
  says otherwise. A resource resolving in Godot is not provenance.

The current boot splash and project icon both resolve into this unattested
asset class. The checker permits them only because each project key is named as
blocking debt owned by `P0-ASSET-HYGIENE-001`; an undeclared startup asset or a
path outside that quarantine fails.

## Named runtime debt

`run_game.bat` still injects WotR data directly from `workspace/retail-work`
and pins a legacy ranger overlay hash. Those exact tokens are both mapped to
`P0-DEFAULTS-001`, giving one planned owner to the batch file. The checker verifies
that the debt still exists at the declared path and that the owning work item
lists that file as an exact candidate. Removal is part of that item; silently renaming or
moving the bypass does not make it disappear.

## Agent use

Before implementation, an agent reads its single row in
`orchestration/work-items.json`, confirms the declared owner paths, and uses
this map to identify the shipping consumer and focused test route. If the
needed path is outside that ownership, the item returns to the integration
owner; the agent does not widen its lane. Any new top-level root, new tracked
`game/data` root, new shipping authority, or newly oversized conflict file is
supposed to stop autonomous work until the boundary is explicit.

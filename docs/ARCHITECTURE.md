# OpenBFME architecture

- Owner: integration owner
- Owns: component boundaries, authoritative state, runtime data flow, and accepted architecture decisions
- Does not own: current completion claims, task priority, or volatile gate results
- Target: Rise of the Witch-king Patch 2.02 v9.7.7
- Update trigger: an accepted change to an authority, boundary, protocol, content schema, or pack contract

## Product authority

The OpenBFME product target is a Godot 4.7 reimplementation of the complete
effective English Rise of the Witch-king Patch 2.02 v9.7.7 game. The effective
source is:

```text
BFME2 1.06 base
    + RotWK 2.01 expansion and patch precedence
    + Patch 2.02 official-2 v9.7.7 English overlay
```

The machine-readable authorities are:

- [2.02 v9.7.7 product scope](../contracts/rotwk-202-v9.7.7-product-scope.json)
- [2.02 v9.7.7 source baseline](../contracts/rotwk-202-v9.7.7-baseline.json)
- [2.02 v9.7.7 English overlay](../contracts/rotwk-202-v9.7.7-english-overlay.json)
- [canonical work-item ledger](../orchestration/work-items.json)

The product-scope contract defines what a complete claim means. The source
baseline and overlay contracts identify the exact private inputs. The work-item
ledger records current work and evidence; prose checklists are not a competing
backlog.

Retail-derived bytes stay below `workspace/` or a user-owned Godot data root.
They must never be committed. Tracked source may contain schemas, hashes,
recipes, tests, and independently created fixtures, but not retail payloads.

## Executable truth today

The current executable game path is a standard Godot project. It does not
currently load the standalone C# simulator as its gameplay authority.

```text
run_game.bat
  -> game/project.godot
  -> res://scenes/startup_boot.tscn
  -> res://src/ui/startup_boot.gd
  -> res://scenes/boot.tscn
  -> res://src/ui/main_menu.gd
  -> res://scenes/retail_loading_boot.tscn
  -> res://src/ui/retail_loading_boot.gd
  -> res://scenes/retail_vertical_slice.tscn
  -> res://src/retail_slice/retail_vertical_slice.gd
  -> RetailSliceSim.new()
  -> res://src/retail_slice/retail_slice_sim.gd
```

`game/project.godot` registers these global services:

| Service | Current responsibility |
|---|---|
| `DiagLog` | structured runtime diagnostics |
| `Events` | process-local event surface |
| `SimClock` | simulation timing support |
| `ModLoader` | selected pack and mod discovery |
| `ContentDB` | compiled runtime content |
| `GameState` | application and match-transition state |
| `GameAudio` | presentation audio service |

`RetailSliceSim` is the current authoritative gameplay implementation.
`RetailVerticalSlice` constructs it and currently combines presentation,
input, runtime integration, and some orchestration. The C# project in
`engine/OpenBfme.Sim` is a non-shipping experiment and comparison lane. It may
become authority only through an accepted architecture decision and exact
command/state/event trace equivalence; passing its standalone unit suite is not
a cutover.

Good and evil campaigns and tutorial missions are currently disabled in the
main menu. War of the Ring code exists, but availability of code or parsed data
does not establish full-mode parity.

## Import and content flow today

```text
user-owned retail installation
  -> tools/openbfme_import.py
  -> importer/openbfme_importer/cli.py
  -> importer/openbfme_importer/pipeline.py and pinned converters
  -> immutable hash-addressed private packs
  -> apply-selection-transaction
  -> workspace/content-packs/selection.json
  -> ModLoader
  -> ContentDB
  -> RetailSliceSim and Godot presentation consumers
```

Keep the immutable-address and atomic-selection design. A directory named by a
content digest is immutable: recooking into it is a contract violation. Publish
a new digest and atomically replace the complete selection instead.

The current developer launcher also injects WotR files directly from
`workspace/retail-work`. That is migration debt. Production runtime must read
all gameplay and presentation inputs from the selected, verified bundle set;
it must not read importer caches or source archives.

## Required target architecture

```text
pinned edition contracts
        |
archive catalog and precedence resolver
        |
versioned neutral content IR
        |
deterministic validation and cook
        |
small immutable bundle set + selection lock
        |
authoritative simulation
  commands -> ticks -> events -> state/hash
        |
read-only Godot projections
  world rendering / HUD / input / camera / audio / shell
```

### Import boundary

Only importer code and pinned conversion tools understand BIG, INI, W3D, APT,
WND, map, and other source formats. They resolve archive precedence,
inheritance, references, provenance, and source identity into versioned
OpenBFME schemas. Importer code does not invent gameplay defaults and does not
become a second simulation authority.

The long-term command surface is one edition-aware workflow with `doctor`,
`catalog`, `cook`, `verify`, `publish`, and `select` operations. Edition and
profile are explicit inputs. Root wrappers may provide convenient Windows
entry points but must not encode a second pipeline.

### Bundle boundary

A release selection should contain a small, stable set of independently
addressed bundles, such as gameplay/core, maps, media, and localization. A long
chain of repair and batch overlays is acceptable as build history, not as the
canonical product composition. The runtime identity includes the selection
bytes and every mounted bundle digest.

The legal-safe `openbfme-test` fixture supports public engine tests. It is not a
retail substitute, an implicit gameplay fallback, or parity evidence.

### Simulation boundary

The simulator exclusively owns authoritative entities, hordes, resources,
health, timers, production, combat, powers, upgrades, scripts, AI decisions,
pathing outcomes, RNG, save state, replay state, and the deterministic state
digest. It accepts canonical commands and emits immutable events and snapshots.

Godot presentation must not silently mutate authoritative truth. Input is
translated into commands. Rendering, audio, camera, selection highlight, local
menus, and accessibility state consume snapshots and events and remain outside
the authoritative hash.

Use stable identifiers, explicit RNG streams, stable collection order,
deterministic tie-breaks, and integer or otherwise specified deterministic
numeric representations. Cadence changes require oracle evidence and an
explicit protocol decision; they are not cleanup refactors.

### Missing-data behavior

The retail-parity profile fails closed. Missing source, unresolved reference,
unsupported behavior, absent asset, selection drift, or forbidden fallback is
a named failure. Synthetic definitions, placeholder art, guessed values, or
demo content cannot satisfy a 2.02 parity claim.

## Decomposition boundaries

Decomposition must preserve behavior and deterministic traces. Prefer extracting
stable interfaces over broad rewrites.

| Current concentration | Target boundary |
|---|---|
| `retail_slice_sim.gd` | command admission; world/entity state; economy/production; locomotion/pathing; combat/damage; powers/upgrades; AI/scripts; persistence/replay; deterministic digest |
| `retail_vertical_slice.gd` | match bootstrap adapter; input/camera; terrain and world projection; entity presentation; FX/audio; HUD adapter |
| `retail_slice_script_world.gd` | typed script queries; typed script actions; event subscriptions; scenario/objective state; refusal reporting |
| `main_menu.gd` | shell navigation; skirmish setup; multiplayer setup; load/save; WotR entry; content-readiness presenter |
| `wotr_screen.gd` and `wotr_map_view.gd` | strategic domain controller; map projection; tactical transition adapter; persistence; screen widgets |
| `pipeline.py`, `cli.py`, and large compilers | command parsing; source catalog; effective-tree construction; IR compilation; conversion adapters; validation; publication/selection |

Dependencies point inward: presentation depends on simulation contracts;
simulation does not depend on presentation. Domain modules depend on schemas
and narrow services, not on scenes or importer internals.

## Agent contribution contract

Every implementation packet must name one row in
`orchestration/work-items.json`, its owned files, exact 2.02 source evidence,
consumer, focused verification command, and completion evidence. Agents must:

1. Establish the current selected-pack identity before runtime work.
2. Trace source -> conversion -> mounted bundle -> runtime consumer -> behavior.
3. Avoid unrelated monolith edits and generated historical reports.
4. Add or retain tests only when they protect observable behavior, a reproduced
   defect, determinism, a schema/protocol, containment, or an oracle result.
5. Never turn a missing prerequisite into a pass or mint a new baseline merely
   to make a gate green.

Verification semantics and canonical commands are defined in
[VERIFICATION.md](VERIFICATION.md). Volatile progress belongs in the work-item
ledger and generated evidence, not in this architecture document.

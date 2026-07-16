# OpenBFME modding contract

> **Owner:** Content/mod API integration owner
> **Owns:** Mod manifests, dependency/load order, override rules, simulation compatibility, presentation overrides, validation, and authoring boundaries.
> **Does not own:** Retail extraction, authoritative simulation internals, public marketplace services, or legal approval for third-party content.
> **Last verified commit:** `efe6a6c1f7ab76ae84436faed4e9a02298a4a194`
> **Update trigger:** A pack category, manifest field, dependency rule, override rule, hash boundary, validation rule, or authoring workflow changes.
> **Validation:** `python tools/check-product-contracts.py --check`; pack-level validation remains unimplemented and is required before community content can load.

## Status and lanes

OpenBFME currently has a loose legacy pack loader for repository-authored and user
content. The production split between simulation and presentation packs described
below is an approved target and is **not yet fully implemented**. Do not claim
deterministic multiplayer mod compatibility until the separate digest and handshake
contracts pass.

Three lanes must remain distinct:

| Lane | Purpose | Fallback behavior | Parity evidence |
|---|---|---|---|
| Loose legacy | Local development and legal-safe user mods | Priority-based definition overrides may be accepted | Never BFME2 parity evidence |
| Strict private retail | Exact selected BFME2 1.06 compatibility pack | Fail closed; no loose/base substitution | Eligible when oracle and gates pass |
| Production mods | Versioned simulation and/or presentation packs | Manifest- and category-governed | Depends on declared profile, never silently retail parity |

Strict retail selection is controlled by the private content pipeline. A missing retail
definition, asset or capability must not be supplied by `game/mods`, `user://mods`, a
synthetic base pack or a presentation-only pack.

## Current loose-pack behavior

The existing loader recognizes pack directories in repository/user content roots and
an optional `OPENBFME_CONTENT` root. A loose pack has a `pack.json` plus data and assets;
higher priority definitions may override lower priority definitions by ID.

This surface remains supported for development while the production schema is built,
but it has limitations:

- priority alone is not a multiplayer compatibility contract;
- pack category is not yet a complete security boundary;
- loose overrides are not evidence of retail origin or parity; and
- an absolute external root is operator-provided content, not a publishable dependency.

New parity work must target the strict selected-pack contract rather than expanding
implicit loose fallbacks.

## Production pack categories

### Simulation packs

Simulation packs contain every value that can affect authoritative results, including:

- factions, objects, hordes, heroes and modules;
- weapons, armor, locomotion, economy, upgrades, powers and AI;
- map dimensions, terrain gameplay facts, collision, navigation and scripts;
- deterministic configuration and schema migrations; and
- authoritative event timing, attachment origins, root motion, and deterministic callbacks.

Server and clients must load identical ordered simulation dependencies and agree on one
canonical simulation digest. A simulation pack cannot be hot-reloaded during a match.

### Presentation packs

Presentation packs may contain:

- meshes, skeletons, animations and LODs;
- textures, materials, shaders and visual effects;
- UI layout/art, portraits and fonts;
- music, speech and sound effects; and
- localization that does not alter authoritative identifiers or rules.

Presentation packs may differ between peers. They cannot provide or override a field
used by simulation, navigation, targeting, visibility, collision, timing or command
validation. HD packs therefore replace audiovisual content without forking gameplay.

Presentation resources cannot drive simulation through animation notifies, root
motion, bone-derived launch positions, effect callbacks, or scripts. Import rejects or
strips that metadata; authoritative equivalents live in canonical simulation data.

A pack that contains both categories is treated as a simulation pack for compatibility
unless it is split into separately hashed manifests.

## Manifest contract

Every production mod manifest declares:

```text
schema and schema version
pack ID and semantic version
engine/mod API compatibility
category: simulation | presentation | mixed-map
dependencies with version constraints
conflicts and explicit load-order constraints
content roots and safe relative file declarations
canonical content digest
author/license/provenance metadata
redistribution policy
code_plugins (an empty array in modding v1)
```

Pack IDs and content object IDs are stable, case-normalized identifiers. Dependency
resolution is deterministic and rejects missing, cyclic, ambiguous or incompatible
graphs. Explicit dependency/load-order constraints take precedence over legacy numeric
priority. The resolved order is included in a canonical compatibility lockfile. That
lockfile also includes simulation protocol/schema and canonicalizer revisions, every
ordered pack manifest/content digest, deterministic configuration, map-simulation
digest, and the authoritative-plugin list, which is empty in modding v1.

## Overrides

- An override names the exact object and permitted category fields it replaces.
- Unknown objects, fields or schema versions fail with the source pack, object and field.
- Simulation overrides require identical multiplayer digests.
- Presentation overrides cannot change authoritative definitions.
- Deletes are explicit tombstones and cannot be inferred from a missing file.
- Asset paths are safe, normalized and contained below the owning pack root.
- A mod cannot reach another pack by path traversal; cross-pack reuse uses a declared
  dependency and stable asset reference.
- Last-writer-wins behavior is retained only for the documented loose legacy lane.

## Maps and scenarios

Map packages separate gameplay facts from presentation assets. Simulation map identity
covers topology, passability, buildability, water/naval rules, collision, starts,
waypoints, scripts, triggers and authoritative object placement. Presentation identity
covers terrain materials, prop models, sky, water rendering, ambience and other visual
or audio resources.

A custom map declares required simulation and presentation dependencies. Missing
simulation dependencies reject the match. Missing optional presentation dependencies
may use an explicit legal-safe presentation fallback only outside strict retail parity.

## Code plugins

Executable plugins are forbidden in modding v1. Packs containing native libraries,
managed assemblies, Godot/importer scripts, or other executable resources are
rejected. A future plugin protocol requires a separately reviewed contract and does
not inherit trust from a content-pack dependency.

## Authoring and diagnostics

Authoring tools should provide:

- schema validation with exact pack/object/field errors;
- resolved dependency and override views;
- simulation/presentation category audits;
- canonical digest calculation;
- provenance and redistribution warnings;
- map fact versus presentation validation; and
- safe-mode launch with the last-known-good pack set.

Mod discovery and dependency resolution may occur before launch. Authoring hot reload
is permitted only for presentation state outside a match. Invalid content must never be
silently ignored.

## Distribution

OpenBFME does not require Steam Workshop, a central account or a central mod service.
Packs may be installed from local folders or community-hosted indexes. A future mod
manager may provide dependency resolution, signatures, rollback and safe mode, but the
manifest and digest contracts remain independent of any one distribution service.

Retail-derived private packs are non-redistributable and are never mods for sharing.
See `RELEASE_POLICY.md` and `THIRD_PARTY.md`.

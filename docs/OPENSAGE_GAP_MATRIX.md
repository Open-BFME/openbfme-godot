# OpenSAGE comparison matrix

- Owner: integration owner
- Owns: bounded OpenSAGE research/tool comparisons
- Does not own: product scope, live status, or parity acceptance
- Target: OpenBFME RotWK Patch 2.02 v9.7.7
- Update trigger: an OpenSAGE capability materially changes a current research
  or conversion decision

Product authority remains the
[product scope](../contracts/rotwk-202-v9.7.7-product-scope.json),
[retail baseline](../contracts/rotwk-202-v9.7.7-baseline.json), and
[architecture](ARCHITECTURE.md). This comparison cannot change them.

## Purpose

[OpenSAGE](https://github.com/OpenSAGE/OpenSAGE) is an independent C#
reimplementation of SAGE-family technology. It can provide format research,
behavior hypotheses, test ideas, and external conversion tools. OpenBFME has a
different executable and product boundary: a Godot client consuming
deterministically converted, immutable content bundles for one pinned game.

OpenSAGE is therefore a research comparator, not an implementation-status
scoreboard and not an original-game oracle.

| Axis | OpenSAGE | OpenBFME |
|---|---|---|
| Product aim | General SAGE-family runtime/research | Exact RotWK Patch 2.02 v9.7.7 Godot product |
| Content access | May read supported install formats directly | Importer converts private source into versioned bundles; shipping runtime does not read retail archives |
| Runtime | OpenSAGE C# engine | Current authoritative GDScript simulation plus Godot presentation |
| Evidence | Project-specific tests and demos | Source, conversion, strict load, deterministic behavior, original-game visual/audio oracles, and end-to-end qualification |
| License boundary | OpenSAGE project licenses and terms | OpenBFME source license plus external-tool and private retail boundaries |

## Research matrix

Rows identify useful comparison areas. Their presence does not say either
project is complete.

| Surface | Potential OpenSAGE input | Required OpenBFME proof |
|---|---|---|
| Install discovery and BIG archives | Header, member, compression, and precedence research | Pinned three-layer catalog; exact archive/effective-path identity; malicious-input tests |
| INI parsing and inheritance | Grammar and field-behavior hypotheses | Exhaustive effective winners, reference closure, neutral IR, and source provenance |
| Map format and terrain | Chunk and coordinate-system research | Deterministic map IR, pathing/buildability, runtime consumption, matched map oracle |
| W3D hierarchy/materials/animation | External reader and format behavior | Deterministic conversion, preserved authored semantics, Godot runtime draw, visual oracle |
| APT/WND interface | Timeline/opcode/layout research | Cooked authored contracts, runtime consumers, interaction tests, matched shell/HUD captures |
| Object modules | Module vocabulary and behavioral hypotheses | Source-backed typed contracts plus authoritative runtime and behavior oracle |
| Weapons/projectiles/combat | Ordering and numeric hypotheses | Deterministic command/state/event traces against original-game scenarios |
| Scripts and AI | Opcode/action/query ideas | Exact target vocabulary denominator, fail-closed dispatch, live match-loop behavior |
| Particles and audio | Format and routing hypotheses | Converted target source, runtime timing/routing, visual/audio oracle |
| Networking | Protocol design comparison only | OpenBFME canonical serialization, digests, recovery, scale, and cross-platform qualification |

## Admission rule

An OpenSAGE observation enters OpenBFME work only through a row in
`orchestration/work-items.json` that names:

1. the exact target gap and effective 2.02 source;
2. the OpenSAGE file, version, commit, or demo used as a hypothesis;
3. the clean-room implementation boundary and license disposition;
4. the shipping OpenBFME consumer; and
5. the focused check plus original-game evidence required for acceptance.

Do not copy a donor runtime type merely because the data shape is convenient.
Do not convert a demo screenshot into parity evidence. The pinned retail source
and reproducible original-game observation remain authoritative.

## Tool boundary

The OpenSAGE BlenderPlugin is a pinned external importer tool. Its code is not
vendored into the Godot runtime or serialized into content bundles. Exact tool
identity and license obligations belong in generated provenance and
[THIRD_PARTY.md](THIRD_PARTY.md).

## Current work

Current OpenSAGE-related work, if any, is sequenced in
[ROADMAP.md](ROADMAP.md) and owned only by
[work-items.json](../orchestration/work-items.json). Do not maintain volatile
percentages, map totals, or "ahead/behind" scores in this document.

## Verification

All claims follow [VERIFICATION.md](VERIFICATION.md). OpenSAGE can help discover
or explain a requirement, but cannot replace exact source identity, live Godot
consumption, deterministic behavior, or original-game visual/audio approval.

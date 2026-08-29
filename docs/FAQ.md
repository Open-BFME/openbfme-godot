# FAQ

## What is OpenBFME?

An independent Godot 4.7 reimplementation plus a Python conversion pipeline for
*The Lord of the Rings: The Battle for Middle-earth II - The Rise of the
Witch-king*. The exact target is **Patch 2.02 v9.7.7**.

## What source installation does it require?

The pinned English target is built from three precedence layers: the Patch 2.02
official-2 v9.7.7 overlay, the underlying RotWK 2.01 installation, and BFME2
1.06. Users supply lawful copies locally. OpenBFME does not distribute retail or
converted retail content.

The [product scope](../contracts/rotwk-202-v9.7.7-product-scope.json) defines
the denominator and the [retail baseline](../contracts/rotwk-202-v9.7.7-baseline.json)
defines the accepted source identity.

## Is this already a 1:1 port?

No. It is a substantial developer alpha with an executable skirmish foundation,
but modes, behavior, assets, presentation, and oracle coverage remain open. A
launch, parser count, converted asset, or passing isolated runner is not a
whole-product claim. See [VERIFICATION.md](VERIFICATION.md) and the live
[work-item ledger](../orchestration/work-items.json).

## What does parity mean here?

The exact effective source must be identified, deterministically converted,
mounted by verified content address, consumed by the live runtime without a
forbidden fallback, behaviorally compared with the original, and protected by a
gate. Visual and audio fidelity require their own matched-condition oracle
evidence.

## What does fail closed mean?

Missing, ambiguous, unsupported, stale, or unsafe input produces a named
failure. Strict retail mode does not quietly invent rules, use generic art, or
mount another pack.

## Does SKIP mean a gate passed?

No. `SKIP` means the gate did not evaluate because a prerequisite was missing.
It never satisfies a required evidence lane.

## Which simulator is authoritative?

The current executable gameplay authority is the GDScript `RetailSliceSim`
path. The standalone C# simulator is an experiment/comparison lane and cannot
support product-completion claims. See [ARCHITECTURE.md](ARCHITECTURE.md).

## What is the current target or milestone?

[DIRECTION.md](../DIRECTION.md) owns the outcome,
[ROADMAP.md](ROADMAP.md) owns sequencing, and
`orchestration/work-items.json` is the only live task/status ledger. Historical
queues, reports, scores, and selected-pack receipts are not current authority.

## Where does OpenSAGE fit?

[OpenSAGE](https://github.com/OpenSAGE/OpenSAGE) is a research and comparison
source, and its Blender plugin is an external conversion tool. OpenBFME does not
vendor OpenSAGE as its runtime or treat an OpenSAGE demo as proof of Godot
parity. See [OPENSAGE_GAP_MATRIX.md](OPENSAGE_GAP_MATRIX.md) and
[THIRD_PARTY.md](THIRD_PARTY.md).

## Can I play without the retail game?

Public fixtures can exercise importer and engine code, but real target content
requires the lawful retail layers. The legal-safe fixture is not a parity
fallback.

## Can I mod it?

Mod contracts and examples exist; see [MODDING.md](MODDING.md). Mods are
separately identified from the strict retail profile and cannot silently change
its parity result.

## Is multiplayer complete?

No. Lockstep and lobby foundations exist, but full protocol, scale, recovery,
observer, and product qualification remain governed by the roadmap and work
items.

## How do I contribute?

Read [AGENTS.md](../AGENTS.md) and [CONTRIBUTING.md](../CONTRIBUTING.md). Work is
assigned as one bounded work-item row with owned files, source evidence, a
focused Windows command, and independent verification.

## What is licensed?

Repository source is [Unlicense](../LICENSE). Retail content, Tolkien and
Middle-earth material, trademarks, and third-party tools/assets retain their
own rights and licenses. This project is not affiliated with EA or the Tolkien
rights holders.

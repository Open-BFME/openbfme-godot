Owner: Integration owner
Owns: The parity evidence model, completeness-claim rules, and human-readable interpretation of generated compatibility reports.
Does not own: Hand-maintained content totals, current gate results, importer implementation, or product architecture.
Last verified commit: (see DIRECTION.md for policy ownership)
Update trigger: Scope policy, discovery rules, evidence lanes, staleness rules, or completeness criteria change.
Validation: `python tools/check-product-contracts.py --check`; once implemented, the private report generator must also reject unclassified, unresolved, stale, or unevidenced required rows.

# RotWK 2.01 retail parity (evidence model)

> **Naming note.** This file was historically titled for BFME2 1.06. The product
> parity baseline is now **RotWK 2.01** (`DIRECTION.md`, owner 2026-07-30).
> Keep this path for links; content below is RotWK-primary.

## What parity means

Parity is an evidence-backed match to observable **RotWK 2.01** behavior and
presentation for an explicitly scoped requirement. Parsed INI fields, converted
assets, compiler success, or a launchable scene are inputs to verification; none
alone proves parity.

Retail/original-game observation is the behavioral and audiovisual oracle.
Repository-authored contracts define OpenBFME requirements only where the retail
game has no corresponding feature, such as the modern mod-pack boundary.

BFME2 1.06 may be used as a **comparison** install. It cannot replace RotWK
evidence for RotWK completeness claims. Angmar and other expansion-only content
belong in RotWK denominators.

## Development model

Parity work is **systems-first iterative** (see `DIRECTION.md`). Vertical-slice
freeze is not the active strategy. Completeness claims still require identity-bound
evidence; systems work does not waive fail-closed rules.

## Machine-readable authority

`contracts/rotwk-201-product-scope.json` is the tracked product policy input. It
declares the game and patch, included/deferred domains, root-discovery queries,
required evidence lanes, claim profiles, and reasoned exclusions.

The historical file `contracts/bfme2-106-product-scope.json` is **superseded** and
is not what `tools/check-product-contracts.py` validates.

The product target is the full RotWK game. **RotWK skirmish compatibility** is the
near-term completeness claim profile (`rotwk-201-skirmish-complete`). Campaigns,
War of the Ring, and Create-a-Hero are in scope as **deferred** ladder domains
until skirmish systems support them.

The policy and its deterministic checker exist now. The full feature-graph/report
generator does not. Until that generator and current validated reports exist, the
contract is policy-and-validation-only and cannot support a completeness claim.

Private generator outputs (when present) live under:

```text
workspace/retail-work/reports/compatibility/
```

Human documents do not copy faction, hero, map, power, or asset denominators.
Generated views are projections of the effective retail catalog and evidence records.

## Evidence lanes (summary)

Parity rows require the lanes declared in the product contract (source provenance,
classification, reference resolution, conversion, runtime loading, gameplay/visual/audio
oracles, containment, and domain-specific lanes such as persistence or networking).

Unknown, ambiguous, unsupported, substituted, or unclassified requirements fail closed.

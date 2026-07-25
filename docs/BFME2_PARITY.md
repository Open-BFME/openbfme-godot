Owner: Integration owner
Owns: The parity evidence model, completeness-claim rules, and human-readable interpretation of generated compatibility reports.
Does not own: Hand-maintained content totals, current gate results, importer implementation, or product architecture.
Last verified commit: `efe6a6c1f7ab76ae84436faed4e9a02298a4a194`
Update trigger: Scope policy, discovery rules, evidence lanes, staleness rules, or completeness criteria change.
Validation: `python tools/check-product-contracts.py --check`; once implemented, the private report generator must also reject unclassified, unresolved, stale, or unevidenced required rows.

# Parity

## What parity means

Parity is an evidence-backed match to the observable behavior and presentation of the original game (RotWK 2.01, on the BFME2 1.06 base) for an explicitly scoped requirement. Parsed INI fields, converted assets, compiler success, or a launchable scene are inputs to verification; none alone proves parity.

Retail/original-game observation is the behavioral and audiovisual oracle. Repository-authored contracts define OpenBFME requirements only where the retail game has no corresponding feature, such as the modern mod-pack boundary.

## Machine-readable authority

`contracts/bfme2-106-product-scope.json` is the tracked policy input. It declares
the game and patch, included/deferred/excluded domains, reproducible
root-discovery queries, required evidence lanes, and reasoned exclusions. The
product target is RotWK 2.01 skirmish compatibility. War of the Ring is
outside project scope; the campaigns are in scope and sequenced separately.

The policy and its deterministic checker exist now. The full feature-graph/report
generator does not. Until that generator and current validated reports exist, the
contract is policy-and-validation-only and cannot support a completeness claim.

The private generator produces:

```text
.private/retail-work/reports/compatibility/
  bfme2-106-feature-graph.json
  bfme2-106-evidence-catalog.json
  bfme2-106-coverage-matrix.json
  generated-views/
```

Human documents do not copy faction, hero, map, power, or asset denominators. Generated views are projections of the effective retail catalog and evidence records.

## Feature graph

The feature graph contains every effective retail winner and every discovered reference assignment, including unknown or unresolved targets. Each node or edge records:

- Canonical source identity and kind.
- Archive precedence, source digest, and inheritance/override chain.
- Product-domain and semantic tags.
- Exact reference field and target.
- Resolution state and discovery/root-query reason.
- Evidence source.

Discovery cannot depend on an allowlist that makes unknown references disappear. Each effective definition must be classified or explicitly blocked.

## Evidence catalog

Each evidence record has stable requirement and evidence IDs, source/pack/tool/commit identities, a reproducible capture or test recipe, expected and observed metrics, tolerance and rationale, artifact digest, reviewer approval, staleness inputs, and pass/failure/blocker state.

Evidence becomes stale when an identified input changes. Approval without preserved identities and a reproducible recipe is not durable evidence.

## Coverage matrix

The matrix joins scope rows, reachable features, and all required evidence lanes. A row is green only when every applicable lane has current passing evidence. Asset presence is one possible lane, never the completion rule.

Generated views must keep the following independently visible:

- Factions, heroes, alternate and mounted forms, Ring Heroes, and Ring mechanics.
- Units, structures, upgrades, sciences, powers, and their behavior/animation/FX/audio chains.
- Naval units, docks, transports, water behavior, and map domains.
- Every winning map payload classified by its product role.
- Create-a-Hero source and behavior when that optional roadmap phase begins.
- Included shell, skirmish, replay, and observer UI/audio.
- The excluded War of the Ring domain, so it cannot be mistaken for
  missing work inside the skirmish-complete claim.
- Modern OpenBFME modding requirements in a separate OpenBFME-owned lane.

## Completeness claim

OpenBFME may claim skirmish compatibility only when:

1. Every effective retail winner is classified.
2. Every product-domain root query is frozen and reproducible.
3. Every reachable reference is resolved or explicitly approved as an exception.
4. No effective definition is unowned or unclassified.
5. Every required matrix row has current evidence for all applicable lanes.
6. Create-a-Hero, naval, Ring, and included shell requirements remain
   independently auditable where applicable.
7. Modern OpenBFME features are excluded from retail-parity completion calculations.

The target profile is the RotWK 2.01 catalog. War of the Ring
domains are excluded and cannot contribute to that claim. The contract retains a
fail-closed `bfme2-106-complete` profile only to make a whole-game claim
mechanically impossible while excluded retail domains remain. Create-a-Hero is
tracked separately and is not required to describe current skirmish progress.
A bounded milestone uses a separate named profile and never implies broader
completion.

Milestone completion applies the same rule to a bounded subset. The current subset and its exact acceptance belong in `docs/MILESTONE_CURRENT.md`; current outcomes belong in `STATUS.md` and generated reports.

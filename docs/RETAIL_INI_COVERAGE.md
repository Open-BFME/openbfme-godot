# Retail INI coverage ledger

`tools/retail-ini-coverage.py` is the reproducible BFME2 1.06 / RotWK 2.01
data-surface audit. It reads the effective INI and INC winners from the normal
retail catalogs, indexes the currently selected content packs, and compares
the authored vocabulary against the importer, live Godot runtime, and tests.
It also hashes every selected pack and records declared-versus-actual content
addresses; a drifted pack makes the source identity dishonest even when all
files remain readable.

Descriptor evidence is edition-scoped. BFME2 rows can only be satisfied by
selected `bfme2-*` packs; RotWK rows use the live layered mount (RotWK packs
plus its BFME2 supplements). The full selection is still address-checked as
one runtime mount. This prevents a RotWK-only descriptor from falsely closing
the corresponding BFME2 source row.

Descriptor emission is also source-site scoped. A feature signature is fully
emitted only when every authored `(sourceIni, line)` site has matching selected
descriptor provenance. Partial mappings expose mapped/unmapped site counts in
`descriptorSiteCounts` and per-category counts in
`descriptorCategorySiteCounts`; one mapped object cannot close the same field
on unrelated objects or factions.

Run it from the repository root with the pinned importer Python:

```powershell
$python = '.private\retail-work\tools\cpython-3.12.13\python.exe'
$env:PYTHONPATH = 'importer;.private\retail-work\tools\python-3.12-env\Lib\site-packages'
& $python tools\retail-ini-coverage.py
& $python tools\retail-ini-coverage.py --check
```

## Current measured baseline

The 2026-08-15 ledger is pinned to selection SHA-256
`e3cf65197b36fc855f852f18ce7e53a698fca34ff004d29e0b25d31f0326285c`.
Generation and `--check` both pass byte-for-byte. The strict completion command,
`--check --require-complete`, intentionally fails because the port is not 1:1:

- 10,983 of 11,119 feature signatures are incomplete.
- 1,075,196 of 1,114,761 authored semantic sites are incomplete.
- 174 of 245 object-module kinds remain below the strict completed state;
  71 currently have runtime-tested evidence.
- 6,260 of 8,947 effective objects are not emitted.
- 85,652 catalog-proven retail assets are absent from the selected packs.
- Unknown semantic lines and selected-pack address drift are both zero.

These figures come from the generated instrument. They are static evidence
accounting, not a gameplay-parity percentage, and must not be replaced by a
self-reported progress estimate.

The generated report is retail-derived and stays under
`.private/retail-work/reports/retail-ini-coverage/`. Its main entry points are:

- `SUMMARY.md`: corpus identity, totals, and evidence distribution.
- `GAPS.md`: usage-weighted mapping backlog.
- `CRITICAL-GAPS.md`: timers, scripts, assets, hero abilities, neutral mobs,
  ships, combat/effects, and spellbooks.
- `coverage.json`: complete feature and module matrices.
  Each feature carries `categorySites`, so a category total counts only the
  authored sites that actually belong to that category rather than every use
  of a field name that happens to be shared across unrelated definitions.
  Its top-level `completion` object keeps signature, exact authored-site,
  module, object, proven-asset, unknown-line, and pack-address denominators
  separate; `staticCoverageComplete` cannot be confused with behavioral parity.
- `unmapped-features.csv`: machine-sortable signature backlog with exact
  descriptor-mapped/unmapped site counts and separate importer, runtime, and
  test evidence flags.
- `definitions-*.jsonl`: one receipt per indent-zero definition header.
- `assignment-sites-*.jsonl`: one receipt per assignment. Authored values are
  represented by SHA-256 instead of being copied into the report. Every
  definition, assignment, nested block, script call, and directive receipt
  carries its own `descriptorSiteMapped` decision, `siteCoverageStatus`, and
  exact evidence files.
- `nested-sites-*.jsonl`, `script-call-sites-*.jsonl`, and
  `directive-sites-*.jsonl`: one receipt per remaining non-terminal semantic
  line, including embedded drawable scripts and preprocessor directives.
- `objects-*.jsonl`: every effective object identity and selected-descriptor
  evidence.
- `asset-references-*.jsonl`: every syntactic asset/effect reference candidate.
  Only catalog-proven physical BIG members enter the missing-asset completion
  denominator. INI definition IDs and unresolved compound-value tokens remain
  visible and are judged by their field/module receipts.
- `unknown-lines-*.jsonl`: anything the exhaustive lexer could not classify.
  A trustworthy run should report zero unknown semantic lines.

Structural terminators (`End` and drawable-script `EndScript`) remain counted
in `lineAccounting` so the corpus is exhaustive, but they do not become feature
rows or gaps. `BeginScript` and each command inside the script remain separate
receipts; this prevents thousands of closing tokens from inflating the backlog
without hiding any executable script operation.

## What the statuses mean

The evidence ladder is deliberately strict:

1. `unmapped`
2. `importer-mentioned`
3. `descriptor-emitted`
4. `runtime-mentioned`
5. `runtime-tested`

Each level requires the levels before it. Even `runtime-tested` is not a retail
parity claim: it proves exact static wiring evidence, not the original game's
timing, targeting, damage ordering, RNG, lockstep state, save/load behavior, or
presentation. Behavior parity still needs a focused retail oracle and an
authoritative match-loop test.

Module rows also preserve `consumed`, `refused`, and descriptor
`runtimeStatus` evidence from `retail_module_census.json`. A consumed or opaque
deferred module is never counted as functioning simulation.

Typed contracts become `runtimeStatus=executable` only through the closed
`EXECUTABLE_TYPED_MODULE_EVIDENCE` registry. Every entry names both a concrete
Godot consumer and a focused runtime runner; adding an importer parser alone
therefore cannot silently promote runtime coverage.

The neutral-mob category is bound to the same closed Object-family predicate
used by the neutral catalog (`Side`/source plus mob, creep, or lair identity).
The ledger resolves inherited `Side` and `KindOf` for `ChildObject` and
`ObjectReskin` rows before assigning neutral-mob, hero, and ship categories;
direct authored values remain separately visible in object receipts and the
ship summary reports direct-authored and effective-inherited populations.
Words such as `SendToNeutral`, `NeutralBanner`, and `Civilian` do not create a
mob receipt; ordinary fields inside an actual Warg, troll, spider, drake, or
lair do. This avoids both the old lexical false positives and the more serious
omission of most real neutral-mob fields.

Authored script commands may use a reviewed descriptor-operation alias only
when the shipping consumer and focused runner execute that exact operation.
Currently this closes ordinary `CurDrawableHideSubObject` and
`CurDrawableShowSubObject` through their typed `hide-sub-object` and
`show-sub-object` rows, plus `CurDrawablePlaySound` through ordered typed audio
intents consumed by the retail audio core. Permanent visibility, module
visibility, transition animation, continuation, and control-flow commands stay
gaps until their distinct semantics are implemented and tested.

`--require-complete` is the eventual closure gate. It intentionally exits nonzero
until every signature reaches the strict evidence chain; it must not be weakened
to make progress appear complete. Drifted pack addresses, unknown semantic
lines, non-emitted objects, catalog-proven physical retail assets missing from
the selected packs,
and non-runtime-tested module rows also keep this gate red.

## Complete requested object-family catalogs

Command reachability is too narrow for several requested families: summoned
heroes, map-owned creeps/lairs, and scenario ships often have no ordinary
`UNIT_BUILD` button. These commands inventory those full families without
inventing a producer route:

```powershell
$import = 'tools\openbfme_import.py'
& $python $import --state-root .private\retail-work compile-hero-catalog `
  --install $env:BFME2_INSTALL --game bfme2
& $python $import --state-root .private\retail-work compile-neutral-mob-catalog `
  --install $env:BFME2_INSTALL --game bfme2
& $python $import --state-root .private\retail-work compile-ship-catalog `
  --install $env:BFME2_INSTALL --game bfme2
```

Each catalog uses separate `catalog_complete` and `ready` results. Every member
is either a normal playable descriptor or a hashed authored-template receipt;
status remains family-specific rather than being inferred from inventory.
Non-buildable ships now carry explicit map/script/tutorial admission, expose no
build command, and have a focused runtime admission gate; hero summons and
neutral/template rows remain deferred wherever their own catalog receipt says
so. A complete catalog therefore proves inventory closure, not playable parity.

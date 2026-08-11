# Ring system importer contract

`compile_ring_system_descriptor(effective_ini_documents)` emits an
evidence-bearing `openbfme.ring-system-descriptor` v0 document. The descriptor
has `descriptorSha256`, importer corpus evidence, the executable art conversion
plan, and the semantic registration tables derived from retail INI.

`build_ring_system_runtime(descriptor)` emits the separate Godot-facing
`openbfme.ring-system-runtime` v0 document at `data/ring/system.json`. It has
`descriptorSha256`, `runtimeSha256`, and only the `registration` tables the game
reads. The RotWK Men host pack is its single owner (`pack.files["ring.system"]`);
supplemental faction packs must not publish a second copy.

The descriptor shape is:

```json
{
  "schema": "openbfme.ring-system-descriptor",
  "schemaVersion": 0,
  "runtimeOutputPath": "data/ring/system.json",
  "objects": {},
  "objectCreationLists": {},
  "upgrades": {},
  "delivery": {},
  "routes": {},
  "system": {},
  "artConversionPlan": {},
  "excludedObjects": {},
  "importerEvidence": {
    "compiler": "openbfme_importer.ring_system_compiler",
    "sourceDocumentCount": 0,
    "sourceCorpusSha256": "sha256"
  },
  "descriptorSha256": "sha256-of-every-field-above"
}
```

The runtime shape is:

```json
{
  "schema": "openbfme.ring-system-runtime",
  "schemaVersion": 0,
  "descriptorSha256": "descriptor-identity",
  "registration": {
    "objects": {},
    "objectCreationLists": {},
    "upgrades": {},
    "delivery": {},
    "routes": {},
    "system": {},
    "excludedObjects": {}
  },
  "runtimeSha256": "sha256-of-every-field-above"
}
```

Important nested contracts:

- `TheDroppedRing.ringMechanic.attach.filter` is
  `{mode,include,exclude,predicate}`. `mode` preserves the single authored
  `ANY`/`NONE` token, and `predicate` is always a list. Every authored token is
  preserved; `-NEUTRALGOLLUM` and `NOT_FLYING_UNITS` are required.
- The ring-hero object set is the case-insensitive union of every authored
  `PlayerTemplate.BuildableRingHeroesMP` route. Every referenced hero must
  resolve and compile its lifecycle; an absent mod object is a named hard
  failure. A conditional hero model is discovered from the hero's authored
  `ModelConditionUpgrade` plus matching model-condition draw state, never from
  a hero-name special case.
- `delivery.structures` has exactly 26 rows. Every row carries a boolean
  `dropsRingOnDeath` derived from its own ring-drop behavior or its authored
  `FortressRingFunc.inc` include. Retail `EreborThrone` therefore emits false
  plus `note: "retail-bug-accepts-ring-never-returns-it"`.
- `system.spawn` and `excludedObjects` are derived from
  `OCL_TheRingCarrier`/`OCL_TheRingStealer`. The compiler requires both OCLs to
  agree on team and waypoint family, requires the carrier object to be compiled,
  and requires the excluded stealer to resolve as a NeutralGollum child.
- `delivery.fortressModules.ringDrop.requiredUpgrades` is an ALL-of pair;
  `drawModels` is exactly `EXOneRing` and `EXOneRing_CR`.
- Ordinary playable-unit production routes keep `prerequisites: []`. Only
  `__engine__/BuildableRingHeroesMP` routes receive `Upgrade_RingHero` and
  `Upgrade_FortressRingHero` from `Command_RingHeroReviveSlot.NeededUpgrade`.
- `artConversionPlan.resources` is descriptor-only executable profile data; it
  is not run by compilation and is not shipped in the runtime. Conditional hero
  models are listed under `conditionalHeroModels` from their authored data.

Any missing module, unresolved route, malformed filter, inconsistent authored
spawn OCL, wrong count, ambiguous definition, or digest mismatch fails closed.

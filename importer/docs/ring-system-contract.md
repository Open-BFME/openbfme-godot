# Ring system importer contract

`compile_ring_system(effective_ini_documents)` emits one sealed
`openbfme.ring-system` v0 document at `data/ring/system.json`. The RotWK Men
host pack is its single owner (`pack.files["ring.system"]`); supplemental
faction packs must not publish a second copy.

The exact top-level shape is:

```json
{
  "schema": "openbfme.ring-system",
  "schemaVersion": 0,
  "outputPath": "data/ring/system.json",
  "objects": {
    "NeutralGollum": { "schema": "openbfme.playable-unit-style-object", "schemaVersion": 0, "objectId": "NeutralGollum", "side": "Neutral", "kindOf": [], "body": {}, "armor": {}, "camouflage": {}, "locomotors": {}, "ringMechanic": {} },
    "NeutralGollum_RingHero": { "schema": "openbfme.playable-unit-style-object", "schemaVersion": 0, "objectId": "NeutralGollum_RingHero", "parentObjectId": "NeutralGollum", "side": "Neutral", "animalAI": {}, "onDeath": {}, "ringMechanic": {} },
    "TheDroppedRing": { "schema": "openbfme.ring-object", "schemaVersion": 0, "objectId": "TheDroppedRing", "side": "Civilian", "kindOf": [], "shroudClearingRange": 100, "ringMechanic": {} },
    "MordorSauron_RingHero": { "schema": "openbfme.playable-unit-style-object", "schemaVersion": 0, "objectId": "MordorSauron_RingHero", "parentObjectId": "MordorSauron", "economy": {}, "onCreate": {}, "onDeath": {}, "experienceLevelOnCreate": 10 },
    "ElvenGaladriel_RingHero": { "schema": "openbfme.playable-unit-style-object", "schemaVersion": 0, "objectId": "ElvenGaladriel_RingHero", "parentObjectId": "ElvenGaladriel", "economy": {}, "onCreate": {}, "onDeath": {}, "experienceLevelOnCreate": 10, "terribleFuryModules": [], "darkSkin": {} }
  },
  "objectCreationLists": { "OCL_TheOneRing": {}, "OCL_TheOneRingCD": {} },
  "upgrades": { "Upgrade_RingHero": {}, "Upgrade_FortressRingHero": {} },
  "delivery": { "structures": [], "fortressModules": {} },
  "routes": { "commandButtonId": "Command_RingHeroReviveSlot", "prerequisites": [] },
  "system": { "modeToken": "ringheroes", "spawn": {}, "evaEvents": [], "ringHeroesByFaction": {} },
  "artConversionPlan": { "layout": "standard-unit-model-pack", "gollum": {}, "ring": {}, "fortressRingModels": [], "galadrielDarkSkin": {}, "resources": [] },
  "excludedObjects": { "NeutralGollum_RingStealer": "verified-dead-content-not-instantiated" },
  "systemSha256": "sha256-of-every-field-above"
}
```

Important nested contracts:

- `TheDroppedRing.ringMechanic.attach` is `{filter:{include,exclude,predicate}, scanRange, parentStatus}`. The filter preserves every authored token and requires `-NEUTRALGOLLUM` plus `NOT_FLYING_UNITS`.
- `delivery.structures` has exactly 26 rows of `{objectId,statusForRingEntry,upgradeForRingEntry,objectToDestroyForRingEntry,fxForRingEntry,enterSound}`. `EreborThrone` additionally has `note: "retail-bug-accepts-ring-never-returns-it"`.
- `delivery.fortressModules` is `{lossAnnouncement,ringDrop,modelCondition,drawModels}`. `ringDrop.requiredUpgrades` is an ALL-of pair; `drawModels` is exactly `EXOneRing` and `EXOneRing_CR`.
- `system.ringHeroesByFaction` is keyed by `PlayerTemplate` identity with the `Faction` prefix removed. Values are the authored `BuildableRingHeroesMP` token lists; unknown mod object ids pass through unchanged.
- Ordinary playable-unit production routes keep `prerequisites: []`. Only `__engine__/BuildableRingHeroesMP` routes receive `Upgrade_RingHero` and `Upgrade_FortressRingHero` from `Command_RingHeroReviveSlot.NeededUpgrade`.
- `artConversionPlan.resources` is executable profile data but is not run by compilation. It stages the BFME2 Gollum closure, TheRing, and both fortress ring models. Galadriel's `EUGaldrl_SKN` is converted by the normal unit recipe because it is a required `USER_1` Ring-Hero state, not `excluded-hero-form`.

Any missing module, changed sentinel value, wrong count, ambiguous definition, or digest mismatch fails closed.

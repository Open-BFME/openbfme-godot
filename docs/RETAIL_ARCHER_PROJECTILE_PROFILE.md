# Retail Gondor Archer projectile profile

`retail_archer_projectile_profile.py` closes the normal, unupgraded BFME II
1.06 Gondor Archer projectile and target-impact presentation without embedding
retail payloads in a public file.

The source chain is exact:

1. `GondorArcherBow` launches `GondorArcherArrow` with
   `GondorArcherBowWarhead`.
2. `GondorArcherArrow` reskins `GoodFactionArrow`.
3. `GoodFactionArrow` has no model. Its visible projectile is a
   `W3DStreakDraw` using `EXArrowStreak01.tga`, with the authored snowy weather
   variant `EXArrowStreak_Snow.tga`.
4. The warhead emits `GOOD_ARROW_PIERCE`. The struck object's `DamageFX` set
   decides whether that becomes `FX_GoodArrowHit`; the four retail mappings are
   retained independently.
5. `FX_GoodArrowHit` plays `ImpactArrow` and attaches `g_arrow`. The weapon's
   fire FX separately plays `ArcherWeapon`.

`EXArrowStreak01` is not a SAGE `ParticleSystem` or `FXParticleSystem`
definition. Treating it as one would invent a converter/runtime contract that
is absent from retail data.

## Exact closure

The generated plan binds 109 physical retail source files:

- 5 INI definition documents;
- `g_arrow.w3d`;
- `g_arrow.dds` and the two default/snow streak textures;
- 32 `ArcherWeapon` sound leaves;
- 68 `ImpactArrow` sound leaves.

The four-resource composition fragment selects 106 new files. It explicitly
reuses the existing complete-profile owners of `weapon.ini`, `soundeffects.ini`,
and `g_arrow.dds`. The standalone validation profile carries those three owner
resources, so it has seven resources and resolves 198 files; this larger number
is validation context, not the exact closure size.

The runtime document is `combat/gondor-archer-projectile.json`. It records the
streak, Bezier parameters, variable weapon speed, fire/impact audio pools,
damage-FX mappings, and impact model. It deliberately leaves these engine facts
unresolved:

- the exact attack-animation event frame that spawns a projectile;
- target `DamageFX` selection at runtime;
- audio random-pool seeds;
- `g_arrow` attachment orientation, lifetime, and animation playback;
- the separate ground-hit `FX_GondorArrowDeath` closure.

Runtime integration must fail visibly until an authoritative gameplay event and
the remaining original-engine semantics are supplied. It must not substitute a
generic line, particle, hit flash, or sound.

## Generate and verify

```powershell
$env:PYTHONPATH = 'importer'
$python = 'C:/Users/Jonathan/AppData/Local/Programs/Python/Python312/python.exe'
& $python -m openbfme_importer.retail_archer_projectile_profile `
  --effective-assets .private/retail-work/cache/effective-assets `
  --catalog .private/retail-work/catalog/bfme2.json `
  --base-profile .private/retail-work/profiles/men-fords-v0-complete.generated.json `
  --combat-report .private/scratch/combat-visual-parity/REPORT.md `
  --output .private/scratch/archer-projectile-profile/plan.json `
  --generated-profile .private/scratch/archer-projectile-profile/profile.json

& $python -m unittest importer.tests.test_retail_archer_projectile_profile -v
& $python -m ruff check `
  importer/openbfme_importer/retail_archer_projectile_profile.py `
  importer/tests/test_retail_archer_projectile_profile.py
```

The planner validates every read against the effective-assets manifest, checks
the current complete profile's dedupe owners, resolves the generated
ImportProfile against the real catalog with zero missing resources, and seals
the plan with canonical SHA-256. Retail and converted bytes remain under
`.private`.

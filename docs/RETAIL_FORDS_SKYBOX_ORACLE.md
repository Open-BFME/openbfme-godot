# Fords world-sky selection oracle

## Result

The exact BFME II 1.06 Fords of Isen II world-sky texture set is **not yet
proven**. The contract stays fail-closed: `selectedTextureSet=null`, no
candidate face files are added to the asset closure, and the four blockers are
retained explicitly.

This rejects both tempting shortcuts:

- `DefaultSky` is not selected merely because it is the first declared set;
- `Morning` is not selected merely because all five leaves exist or because
  their names occur in `game.dat`.

## Direct retail evidence

The bounded oracle binds the current effective-winner manifest and catalog to
these retail sources:

- Fords `.map`, SHA-256
  `fa3b460c23f72821c7e19127bb90f7d97c27a7a2c485a44936b9c291173c0800`;
- Fords `map.ini`, SHA-256
  `9555088bf9d0b6ebbe92d1591535dde532e3fbe8ca8db3ede582a5e9fdb14d46`;
- `data/ini/environment.ini`, SHA-256
  `124837dd4fd2a708a783cc36bb8602a6ca35e81306fe346f37dc3b14a60b826e`;
- `art/w3d/ne/new_skybox.w3d`, SHA-256
  `42761e75a105cece575583756393c2cde090e856b4cc87f285e77328eb4c775d`;
- BFME II `game.dat`, 10,969,600 bytes, SHA-256
  `f008b587570bad693981dc7218588c81d192a1e064b0f7f861539c51156a7640`.

The map has 20 top-level chunks. `SkyboxSettings` is not among them, and
`map.ini` contains no skybox token. `new_skybox.w3d` names exactly four side
placeholders: `SkyBox_01.tga` through `SkyBox_04.tga`.

`environment.ini` says that the `DefaultSky` scheme must be present and that
its names must match the textures assigned in `new_skybox.w3d`. That proves a
model-to-scheme name-compatibility contract. It does **not** say that a map with
no override selects `DefaultSky`. All five `DefaultSky` requested leaves
(`SkyBox_01.tga` through `SkyBox_05.tga`) also lack an exact or compiled-stem
counterpart in the effective tree.

The retail executable contains the `SkyboxTextureSet` parser vocabulary and
the five `TSMorningN/E/S/W/T.tga` literals. Bounded disassembly shows the
texture-set record constructor at virtual address `0x0060D4E3` initializing
five string fields from those literals; its observed construction call at
`0x0060D755` is on the parsed-record creation path. This is useful parser
default evidence, but it does not prove that the renderer chooses that record
when Fords has no map override. The generated contract deliberately records
only literal presence and requires a renderer control-flow trace or original
runtime trace before promotion.

## Secondary source check

A temporary OpenSAGE checkout at commit
`588ac477367a0022adf29f20a084e8873014e6ce` was kept strictly under
`.private/scratch/fords-skybox-oracle/OpenSAGE`. It independently recognizes
`SkyboxSettings` as an optional top-level map record containing position,
scale, rotation, and a texture-scheme string. It does not implement a renderer
selection path that could establish BFME II behavior. Therefore it supports
the chunk classification only and is not treated as retail proof.

## Reflection skydome is not the world sky

The map places exactly one `WaterReflectionSkydome_GapOfRohan`. Retail INI
data binds that type to `WtrSky_GRohan`; the W3D shader material binds
`WtrSkydome_GapOfRohan.tga`; and the effective tree resolves the compiled DDS
at `art/compiledtextures/wt/wtrskydome_gapofrohan.dds`.

That independently proven four-file evidence closure has canonical SHA-256
`0b253fb3b2153e12274133431ae3a1c007ffe80e430d9713ab94779ae739e226`.
It remains classified as a placed water-reflection skydome and cannot satisfy
the unresolved `new_skybox.w3d` world-sky contract.

## Blockers

1. No map-authored `SkyboxSettings` record or map-INI skybox override exists.
2. The global `DefaultSky` comment defines compatibility, not the no-override
   selection rule.
3. The five `DefaultSky` leaves are absent from the effective asset tree.
4. Executable Morning parser defaults have not been traced into the active
   Fords renderer.

The smallest acceptable closing proof is a BFME II 1.06 renderer trace at map
load showing the active named scheme and all five texture substitutions. An
independent original-game capture that identifies the exact five retail faces
would also close selection, but similarity to one candidate is not enough.

## Reproduce

From the repository root:

```powershell
$env:PYTHONPATH = 'importer'
$python = '<HOME>\AppData\Local\Programs\Python\Python312\python.exe'
$common = @(
  '--effective-assets-root', '.private\retail-work\cache\effective-assets',
  '--manifest', '.private\retail-work\cache\effective-assets\.openbfme\manifest.json',
  '--catalog', '.private\retail-work\catalog\bfme2.json',
  '--game-dat', '<BFME2>\game.dat'
)
& $python -m openbfme_importer.retail_fords_skybox_oracle @common `
  --output '.private\scratch\fords-skybox-oracle\contract-a.json'
& $python -m openbfme_importer.retail_fords_skybox_oracle @common `
  --output '.private\scratch\fords-skybox-oracle\contract-b.json'
Get-FileHash -Algorithm SHA256 `
  '.private\scratch\fords-skybox-oracle\contract-a.json', `
  '.private\scratch\fords-skybox-oracle\contract-b.json'

& $python -m pytest importer\tests\test_retail_fords_skybox_oracle.py -q
& $python -m ruff check `
  importer\openbfme_importer\retail_fords_skybox_oracle.py `
  importer\tests\test_retail_fords_skybox_oracle.py
```

The two contracts must be byte-identical. No retail payload is written outside
`.private`, and this bounded oracle does not edit profiles, converters,
runtime code, or gates.

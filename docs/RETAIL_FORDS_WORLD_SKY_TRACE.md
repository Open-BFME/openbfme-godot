# Fords world-sky activation trace

## Result

Static evidence still cannot name the active BFME II 1.06 Fords of Isen II
world-sky texture set. The result remains fail-closed:

- `worldSkySelectionProven=false`;
- `selectedTextureSet=null`;
- `selectedFaceClosure=[]`;
- neither `Morning` nor `DefaultSky` is promoted;
- the placed water-reflection skydome remains separate.

This trace does prove that the global `DrawSkyBox = Yes` setting is authored.
That enables the sky-background render mode; it does not select a named
texture scheme.

## Three different defaults that must not be conflated

### `TSMorning*` executable constructor defaults

The retail executable's `SkyboxTextureSet` record constructor begins at VA
`0x0060D4E3`. It initializes the five record string fields before INI parsing:

| Face | Record offset | Constructor literal | Push VA |
|---|---:|---|---:|
| N | `+0x04` | `TSMorningN.tga` | `0x0060D516` |
| E | `+0x08` | `TSMorningE.tga` | `0x0060D524` |
| S | `+0x0C` | `TSMorningS.tga` | `0x0060D530` |
| W | `+0x10` | `TSMorningW.tga` | `0x0060D53C` |
| T | `+0x14` | `TSMorningT.tga` | `0x0060D549` |

There is exactly one direct call to this constructor, at `0x0060D755`, on
the parsed-record creation path. These values are fallback initialization for
every newly allocated `SkyboxTextureSet` record. They are not an active set
selection.

### The named `Morning` INI record

`environment.ini` independently authors a named `SkyboxTextureSet Morning`
record. Its five authored values happen to match the constructor fallbacks.
That agreement proves the record's contents, not that a no-override map
selects the record.

The exact ASCII tokens `Morning`, `DefaultSky`, and `new_skybox` do not occur
as NUL-terminated selection names in this `game.dat`. Only the five
`TSMorning*.tga` constructor literals occur.

### The `DefaultSky` comment

The `environment.ini` comment says `DefaultSky` must exist and its names must
match the textures assigned in `new_skybox.w3d`. That is a model-placeholder
compatibility requirement. It does not state that a map lacking
`SkyboxSettings` selects `DefaultSky`. The existing skybox oracle also proves
that the five `DefaultSky` leaves are absent from the effective retail tree.

## Parser-to-registry trace

The executable parser descriptor at VA `0x00DB9850` binds the
`SkyboxTextureSet` type literal to callback `0x0060D6F5`. The callback creates
or finds records in the registry at `0x00DFF494`.

An exact scan of the retail `.text` section found only three direct absolute
references to that registry:

| Instruction VA | Classification |
|---:|---|
| `0x0060D720` | parser create/find record |
| `0x00BAE6E6` | static registry initialization |
| `0x00BB7AE1` | static registry teardown thunk |

There is no direct absolute registry reference at a renderer call site. This
rejects the claim that the constructor's `TSMorning*` values are statically
proven to reach the active Fords renderer.

This is an exact direct-reference result, not a proof that no indirect path
exists. An aliased registry pointer or a copied record could evade this bounded
scan. That is why the result remains unresolved rather than declaring that the
game draws no world sky.

## Draw-mode evidence is not selection evidence

`data/ini/gamedata.ini` line 6483 authors `DrawSkyBox = Yes`. `game.dat` also
contains `DRAW_SKYBOX_BEGIN` and `DRAW_SKYBOX_END` render-mode telemetry. These
prove that a sky-background draw mode exists and is globally enabled. None of
these values names `Morning`, `DefaultSky`, or five texture leaves.

## Secondary OpenSAGE check

OpenSAGE commit `588ac477367a0022adf29f20a084e8873014e6ce` stores and parses
`SkyboxTextureSet` assets and parses/writes the optional map
`SkyboxSettings` record. A complete production-source token scan at that
commit found no `SkyboxTextureSet`, `SkyboxTextureSets`, or `SkyboxSettings`
reference under its rendering or graphics code. This corroborates the parser
boundary but is secondary implementation evidence, not proof of retail
behavior.

## Water-reflection boundary

The separately attested `WaterReflectionSkydome_GapOfRohan` remains a placed
reflection source with contract aggregate
`9c70403da8aa2a4dbe9915b5855f056526ec7719b280df079f6464bafeccdfa2`.
It is not the main-camera `new_skybox.w3d` world sky and cannot close this
selection blocker.

## Smallest closing runtime trace

Use the installed x86 `cdb` or WinDbg against the real BFME II 1.06
`game.dat`. Two runs are necessary because x86 exposes only four hardware
watchpoint slots and the record has five face fields.

1. Break at `0x0060D76F`, immediately after each parsed
   `SkyboxTextureSet` insertion. Identify the `Morning` map key and retain its
   record pointer.
2. Before Fords loads, set read watchpoints on the record fields at `+0x04`,
   `+0x08`, `+0x0C`, and `+0x10`.
3. Run through the first rendered in-match frame. Record every reader
   instruction, copied destination, resolved texture name, and GPU resource
   bound to the main-camera sky draw.
4. Repeat with a watchpoint on `+0x14` for the top face.
5. Only if the same named record supplies all five main-camera bindings should
   the converter resolve those names to effective retail winners and record
   their virtual paths and hashes.

The success criterion is one named record and all five resolved retail
textures observed on the active main-camera sky draw for Fords. A similar
looking screenshot alone is not sufficient.

## Deterministic contract

The payload-free contracts are:

- `.private/scratch/fords-world-sky-trace/contract-a.json`;
- `.private/scratch/fords-world-sky-trace/contract-b.json`.

Both have file SHA-256
`78fe995a00384537569178e2549e9d26aab5f6c155a6d57d16483836f402473c`
and declared aggregate SHA-256
`b32056916fde73f3a182b29deee8dc50fc3c6522de4e5c7d4d429897e7fe683c`.

Reproduce from the repository root:

```powershell
$env:PYTHONPATH = 'importer'
$python = '<HOME>\AppData\Local\Programs\Python\Python312\python.exe'
$common = @(
  '--effective-assets-root', '.private\retail-work\cache\effective-assets',
  '--manifest', '.private\retail-work\cache\effective-assets\.openbfme\manifest.json',
  '--catalog', '.private\retail-work\catalog\bfme2.json',
  '--game-dat', '<BFME2>\game.dat',
  '--skybox-oracle', '.private\scratch\fords-skybox-oracle\contract-a.json',
  '--water-reflection-contract', '.private\scratch\fords-water-reflection\contract-a.json',
  '--opensage-root', '.private\scratch\fords-skybox-oracle\OpenSAGE'
)
& $python -m openbfme_importer.retail_fords_world_sky_trace @common `
  --output '.private\scratch\fords-world-sky-trace\contract-a.json'
& $python -m openbfme_importer.retail_fords_world_sky_trace @common `
  --output '.private\scratch\fords-world-sky-trace\contract-b.json'
Get-FileHash -Algorithm SHA256 `
  '.private\scratch\fords-world-sky-trace\contract-a.json', `
  '.private\scratch\fords-world-sky-trace\contract-b.json'

& $python -m pytest importer\tests\test_retail_fords_world_sky_trace.py -q
& $python -m ruff check `
  importer\openbfme_importer\retail_fords_world_sky_trace.py `
  importer\tests\test_retail_fords_world_sky_trace.py
```

No retail payload is written outside `.private`. This trace does not edit a
profile, converter pipeline, content pack, selection, renderer, or gate.

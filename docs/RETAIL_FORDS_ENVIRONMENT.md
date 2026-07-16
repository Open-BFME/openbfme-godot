# Fords retail environment evidence

`openbfme_importer.sage_environment` is a bounded oracle for the BFME II 1.06
Fords of Isen II environment. It reports source-authored rendering facts; it
does not convert, publish, or invent replacement values. The report must stay
under `.private/retail-work` because it contains decoded retail parameters.

## Exact inputs

The extractor reads these effective-winner files and records a SHA-256 and byte
count for each:

- `maps/map mp fords of isen ii/map mp fords of isen ii.map`
- `maps/map mp fords of isen ii/map.ini`
- `data/ini/weather.ini`
- `data/ini/water.ini`
- `data/ini/watertextures.ini`
- `data/ini/gamedata.ini`

The map is first accepted by the strict multiplayer map parser. The oracle then
decodes only the evidenced BFME II layouts: `WorldInfo v1`, `GlobalLighting v8`,
`EnvironmentData v3`, `NamedCameras v2`, empty `PostEffectsChunk v1`, and empty
`CameraAnimationList v3`. Wrong versions, truncated payloads, non-finite floats,
ambiguous duplicate chunks, malformed selected INI blocks, and duplicate
effective assignments fail closed.

## Retail findings

The current effective tree reports:

- active time `AFTERNOON` and map weather `NORMAL`;
- four time-of-day configurations, each retaining separate terrain, object,
  and infantry sun/accent light records;
- map-local hardware fog fields, reflection height/on overrides, and binary
  macro/cloud/water-alpha facts;
- four standing-water areas and eight river areas with exact material fields;
- two source named cameras plus map-specific camera bounds in `WorldInfo`;
- exact global shadow mode flags and map shadow color;
- one active missing asset reference: `SkyEnv.tga` has no exact filename or
  compiled-DDS stem counterpart in the effective tree.

Six items intentionally remain unresolved in the current report: the active
`SkyEnv.tga` leaf, the absent `FogSettings` chunk (fog is instead explicit in
map-local `Weather`), the unknown 44-byte BFME lighting field, the unnamed float
in each source named-camera record, the SAGE-to-Godot light/camera basis
transform, and exact Godot equivalence for retail shadow volumes/decals. Their
chunk/section evidence and hashes are retained; no values are inferred.

## Runtime handoff

| Runtime target | Authoritative report data | Gate before binding |
|---|---|---|
| Time/weather | `GlobalLighting.activeTimeOfDay`, `WorldInfo.weather` | Use the source enums exactly. |
| Terrain/object/infantry light rigs | Active `GlobalLighting.configuration` | Prove the coordinate-basis transform; do not collapse the three lighting domains silently. |
| Fog | Map `Weather` over global `Weather` | Bind the exact RGB, enabled flag, start, and end. |
| Sky/cloud/macro | `EnvironmentData`, global weather cloud controls, GameData sky flags | Resolve exact leaves and validate motion/scale against the retail oracle. |
| Water | Active `WaterSet`, effective `WaterTransparency`, `EnvironmentData`, and map water material rows | Require one source-proven asset candidate per active reference; water geometry stays in the existing cooked map. |
| Camera | `WorldInfo`, `NamedCameras`, GameData defaults | Prove basis and angle conventions before applying anchors. |
| Shadows | `GlobalLighting.shadowColor`, GameData shadow flags | Render-compare the chosen Godot implementation; the source does not assert equivalence. |

## Reproduce

From the repository root:

```powershell
$env:PYTHONPATH = 'importer'
$python = '.private\retail-work\tools\python-3.12-env\Scripts\python.exe'
$root = '.private\retail-work\cache\effective-assets'
& $python -m openbfme_importer.sage_environment `
  --effective-root $root `
  --output '.private\retail-work\reports\retail-fords-environment-a.json'
& $python -m openbfme_importer.sage_environment `
  --effective-root $root `
  --output '.private\retail-work\reports\retail-fords-environment-b.json'
Compare-Object `
  (Get-Content -Raw '.private\retail-work\reports\retail-fords-environment-a.json') `
  (Get-Content -Raw '.private\retail-work\reports\retail-fords-environment-b.json')
& $python -m unittest importer.tests.test_sage_environment -v
& $python -m ruff check `
  importer/openbfme_importer/sage_environment.py `
  importer/tests/test_sage_environment.py
```

Two reports from an unchanged effective tree must be byte-identical and carry
the same top-level `aggregateSha256`. This oracle does not modify a profile,
build a pack, change content selection, or write retail data outside `.private`.

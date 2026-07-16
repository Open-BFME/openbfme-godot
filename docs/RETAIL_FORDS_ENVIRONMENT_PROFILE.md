# Fords retail environment profile

`openbfme_importer.retail_fords_environment_profile` turns the bounded Fords
environment oracle into a deterministic, composable profile fragment. It does
not replace an unknown retail rule with a Godot approximation.

## Proven active contract

The current BFME II 1.06 effective-winner tree proves:

- time-of-day `AFTERNOON` and weather `NORMAL`;
- linear hardware fog enabled, RGB `(220, 226, 235)`, start `350`, end `2000`;
- nine separate terrain/object/infantry sun and accent rows from the map's
  `GlobalLighting v8` chunk, with their source ambient, color, and direction
  vectors preserved independently;
- packed shadow color `0x40000000`, shadow volumes enabled, shadow decals
  enabled, and shadow mapping disabled;
- `DrawSkyBox`, cloud-map, and cloud-plane flags enabled;
- active `TSCloudMed.tga` and `TSNoiseUrb.tga` references, each resolving to one
  compiled retail DDS winner.

The fragment therefore converts the active cloud and macro DDS inputs to PNG.
It also retains `art/w3d/ne/new_skybox.w3d` as a source-only pack input so the
eventual sky converter uses the exact retail geometry rather than reconstructing
it. That W3D's exact four authored placeholder references are retained in the
runtime contract.

## Zero-guess sky result

`data/ini/environment.ini` declares 11 named five-face `SkyboxTextureSet`
blocks. Eight sets resolve all five face assignments in the effective tree;
three do not. There are 40 resolved face assignments covering 37 unique DDS
files.

Fords authors no named skybox-set override. The retail map and INIs also do not
encode the executable's default selection rule. Consequently the plan sets
`worldSky.selectedTextureSet` to `null`, marks the source model as source-only,
and does not add any candidate face set to the profile. Selecting `DefaultSky`,
`MidDay`, `Sunny`, or a time-of-day-derived set would be unsupported.

Closing the world sky requires an original executable trace or an independent
retail render oracle that identifies the selected named set. Once proven, the
composer can add exactly those five face resources and convert
`new_skybox.w3d` with explicit texture overrides.

## Renderer blockers retained

The runtime document remains `complete=false` and `parityReady=false` for four
specific reasons:

1. named world-sky texture-set selection is unresolved;
2. the SAGE-to-Godot light-vector basis is not proven;
3. Godot's built-in environment fog does not implement SAGE's linear
   start/end contract;
4. Godot directional-light shadows are shadow maps, while this map explicitly
   enables SAGE shadow volumes and decals and disables shadow mapping.

The exact values are still included in
`maps/fords-of-isen-ii/environment.json`; consumers must fail closed rather than
silently collapsing light domains or enabling approximate renderer features.

## Reproduce

From the repository root:

```powershell
$env:PYTHONPATH = 'importer'
$python = '.private\retail-work\tools\python-3.12-env\Scripts\python.exe'
$common = @(
  '--effective-assets-root', '.private\retail-work\cache\effective-assets',
  '--manifest', '.private\retail-work\cache\effective-assets\.openbfme\manifest.json',
  '--catalog', '.private\retail-work\catalog\bfme2.json',
  '--environment-report', '.private\retail-work\reports\retail-fords-environment-c1f300fcf6fed6f2.json'
)
& $python -m openbfme_importer.retail_fords_environment_profile @common `
  --output '.private\scratch\fords-environment-profile\plan-a.json'
& $python -m openbfme_importer.retail_fords_environment_profile @common `
  --output '.private\scratch\fords-environment-profile\plan-b.json'
Get-FileHash -Algorithm SHA256 `
  '.private\scratch\fords-environment-profile\plan-a.json', `
  '.private\scratch\fords-environment-profile\plan-b.json'

& 'C:\Users\Jonathan\AppData\Local\Programs\Python\Python312\python.exe' `
  -m pytest importer\tests\test_retail_fords_environment_profile.py -q
& 'C:\Users\Jonathan\AppData\Local\Programs\Python\Python312\python.exe' `
  -m ruff check `
  importer\openbfme_importer\retail_fords_environment_profile.py `
  importer\tests\test_retail_fords_environment_profile.py
```

Both plan files must be byte-identical. The planner writes no retail payload
outside `.private`, does not publish or select a pack, and never edits the
completion profile.

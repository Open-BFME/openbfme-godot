# Retail Road conversion

`road-closure` is the bounded conversion boundary for SAGE `Road` records. It
reads the winning `data/ini/roads.ini` from an extracted effective-assets tree,
selects only explicitly requested Road IDs, and reports the exact physical
texture leaves needed by a Godot-side road renderer. The report contains hashes
and virtual paths, never retail payload bytes or an absolute source root.

## Run it

From the repository root, with the private importer Python on `PATH` or invoked
directly:

```powershell
$env:PYTHONPATH = 'importer'
python -c "from openbfme_importer.cli import main; raise SystemExit(main())" --json road-closure `
  --assets-root .private\retail-work\cache\effective-assets `
  --road Footprints `
  --road FtprintsDrk `
  --road FtprintsDrk02 `
  --road FtPrintGrass02 `
  --road FtPrintDrkGr02
```

The command writes a deterministic report beneath
`.private/retail-work/reports`. It returns exit code `0` only when every Road
definition is unique, all three required fields are valid, and every texture
has one exact physical leaf. It writes the evidence report and returns exit
code `6` when any requested closure has a gap.

## Exactness contract

- Road IDs are case-insensitive exact identifiers. Prefix, substring, and
  edit-distance matching are forbidden.
- A selected Road must have one unique definition and exactly one `Texture`,
  `RoadWidth`, and `RoadWidthInTexture` field.
- Both width fields must be positive finite decimal values. The report retains
  the authored value, a normalized decimal, and the source line.
- A texture resolves only by exact filename or exact extensionless stem. The
  only representation bridge is an authored `.tga` that is absent from the
  retail tree and has exactly one `.dds` with the same stem. No PNG/JPG guess,
  directory preference, prefix match, or first-candidate selection is allowed.
- The source INI and each selected physical texture are byte-counted and
  SHA-256 hashed. The complete canonical report also has an aggregate SHA-256.

Missing definitions, duplicate definitions, malformed fields, missing
textures, and ambiguous textures remain explicit diagnostics. A converter must
not replace any of them with a generic road material.

## Fords of Isen II findings

The five Road IDs exposed by the current Fords cook all close exactly. Retail
`roads.ini` is 12,876 bytes with SHA-256
`02dc8ef9b2f0a2088b4781c224606a54a3d06c3fd8bb38dd900d3d3c2d72a501`.
Every selected definition has `RoadWidth = 52` and
`RoadWidthInTexture = .95` (normalized to `0.95`).

| Road ID | Definition line | Authored texture | Exact retail leaf |
| --- | ---: | --- | --- |
| `Footprints` | 263 | `TRDirtRoad.tga` | `art/compiledtextures/tr/trdirtroad.dds` |
| `FtprintsDrk` | 269 | `TRFootPrintDark02.tga` | `art/compiledtextures/tr/trfootprintdark02.dds` |
| `FtprintsDrk02` | 275 | `TRFootPrintDarkSing.tga` | `art/compiledtextures/tr/trfootprintdarksing.dds` |
| `FtPrintGrass02` | 287 | `TRFtPrntGrssSing.tga` | `art/compiledtextures/tr/trftprntgrsssing.dds` |
| `FtPrintDrkGr02` | 299 | `TRFtPrntDrkSing.tga` | `art/compiledtextures/tr/trftprntdrksing.dds` |

The verified private report is
`.private/retail-work/reports/retail-road-closure-a6f4b4ef37263ff5.json`.
It has aggregate SHA-256
`45f557b171a18739e268c626e7be0f2aecadba4a0a527d809fdc7b3a1076fdc2`
and reports five resolved definitions, five resolved textures, and zero gaps.

## Map wire pairing contract

The 142 Fords cook records carrying `roadType` 2 or 4 are Road control-point
records, not Object placements and therefore not candidates for Object-to-GLB
conversion. Preserve their source order. The observed wire contract is strict:
each adjacent pair must be `roadType = 2` followed by `roadType = 4`, producing
the sequence `2, 4, 2, 4, ...`. An odd record count, a reversed pair, or any
other sequence is a conversion gap.

That pairing evidence does **not** establish spline type, tangent meaning,
texture UV direction, curve interpolation, or endpoint ownership. Do not infer
those semantics from the numeric tags. A Godot road implementation should
retain the exact paired control-point data and Road width/texture closure, then
settle rendering semantics through a BFME2 oracle comparison.

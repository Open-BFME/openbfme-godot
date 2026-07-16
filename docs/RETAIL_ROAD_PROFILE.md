# Retail Road profile generation

`retail_road_profile.build_road_profile` is the fail-closed bridge from a
validated `openbfme.retail-road-closure` report to a complete private importer
profile. It does not rescan `roads.ini`, search for textures, or choose a
substitute. It preserves the base profile and adds only the conversion inputs
and runtime facts proven by the report.

## Contract

The generator:

- validates the closure schema/version and recomputes its canonical aggregate
  SHA-256;
- requires a ready, gap-free summary whose counts agree with the Road records;
- requires every Road and texture leaf to have exact `resolved` status;
- accepts only safe, unique Road IDs and exact resolved DDS paths;
- adds one `texture`/`texture` resource per unique physical DDS, even if more
  than one Road intentionally shares that exact leaf;
- writes stable PNG outputs under
  `maps/fords-of-isen-ii/road-materials/textures/`;
- writes `maps/fords-of-isen-ii/road-materials.json`; and
- adds `roadMaterials: "road-materials.json"` to the one exact Fords
  `sage-map` resource's presentation metadata.

The base profile is loaded through the authoritative `ImportProfile` parser
before derivation. Unknown base fields, pack metadata, resource order, runtime
data, title, and version remain unchanged. The top-level profile ID and pack ID
also remain unchanged unless the caller supplies `profile_id` and `pack_id`
explicitly.

Generation rejects report digest/count drift, any diagnostic, unresolved
records, duplicate or case-ambiguous IDs and paths, unsafe paths, non-DDS
leaves, inconsistent hash/length facts for a shared path, unsupported widths,
resource/output/runtime collisions, and a missing or ambiguous Fords map
resource. It never falls back to a similarly named texture.

## Runtime document

The generated document has schema `openbfme.sage-road-materials`, version `0`,
the source report aggregate, an exact Road count, and one record per Road. Each
record retains:

- the exact Road ID;
- the authored `Texture` identifier;
- the converted pack-relative PNG path;
- the physical retail DDS virtual path, SHA-256, and byte length; and
- canonical decimal strings for `RoadWidth` and `RoadWidthInTexture`.

Widths remain canonical strings so profile generation does not introduce a
binary floating-point rewrite. The supported boundary is a positive
`RoadWidth` no greater than `1000000` and a positive
`RoadWidthInTexture` no greater than `1`. Consumers must parse those bounded
decimal strings explicitly.

## Generate the Fords private profile

Run from the repository root. Passing the five expected IDs makes their exact
spelling and membership part of the gate; omitting `expected_road_ids` leaves
the generator reusable for another explicitly closed Road set.

```powershell
$env:PYTHONPATH = 'importer'
@'
from pathlib import Path
from openbfme_importer.retail_road_profile import build_road_profile
from openbfme_importer.util import write_json_atomic

payload = build_road_profile(
    Path('importer/profiles/men-fords-v0.json'),
    Path('.private/retail-work/reports/retail-road-closure-a6f4b4ef37263ff5.json'),
    profile_id='men-fords-v0-roads-generated',
    pack_id='bfme2-men-vslice-roads-private',
    expected_road_ids=[
        'Footprints',
        'FtPrintDrkGr02',
        'FtPrintGrass02',
        'FtprintsDrk',
        'FtprintsDrk02',
    ],
)
write_json_atomic(
    Path('.private/retail-work/profiles/men-fords-v0-roads.generated.json'),
    payload,
)
'@ | .private\retail-work\tools\python-3.12-env\Scripts\python.exe -
```

The verified real generation adds five texture resources to the 80-resource
base, producing 85 total resources and five runtime Road material records. The
private generated JSON is regenerated whenever the tracked base profile
changes; its current byte count and SHA-256 are recorded by the composition
report rather than frozen in this document.
It was generated only; it was not built, published, or selected.

## Focused acceptance test

```powershell
$env:PYTHONPATH = 'importer'
.private\retail-work\tools\python-3.12-env\Scripts\python.exe -m unittest `
  importer.tests.test_retail_road_profile -v
```

The fixture covers determinism, base preservation, exact five-Road output,
shared-DDS deduplication, schema-valid profile loading, and every fail-closed
boundary listed above.

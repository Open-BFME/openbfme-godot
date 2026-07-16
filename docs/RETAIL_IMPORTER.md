# Private BFME II retail importer

> **Superseded as authority:** use `docs/CONTENT_PIPELINE.md` and
> `docs/MILESTONE_CURRENT.md`. Retained source-backed details are migration
> evidence only until represented by a contract, test, or generated report.

The importer converts BFME II 1.06 dependency closures into runtime-native private
Godot content packs. It never modifies the retail install. During private development,
all extracted and converted payloads are contained under the checkout's git-ignored
`.private` directory and are excluded from public/code-only exports.

For the model-family, hierarchy, equipment, terrain-layer, and Godot handoff
rules discovered while expanding the vertical-slice closure, see
[`GODOT_RETAIL_ASSET_CONVERSION.md`](GODOT_RETAIL_ASSET_CONVERSION.md).
The separately bounded official multiplayer-map compiler milestone and accountable
requirements are recorded in
[`MULTIPLAYER_MAP_CONVERTER.md`](MULTIPLAYER_MAP_CONVERTER.md).
The active complete-Men/five-map private compatibility expansion and its first
dependency census are recorded in
[`FULL_MEN_FIVE_MAP_MILESTONE.md`](FULL_MEN_FIVE_MAP_MILESTONE.md).

## One-command flow

```bat
run_importer.bat F:\BFME2
```

Equivalent explicit commands:

```bat
powershell -NoProfile -ExecutionPolicy Bypass -File tools\bootstrap-importer-python.ps1
set IMPORT_PY=.private\retail-work\tools\python-3.12-env\Scripts\python.exe
"%IMPORT_PY%" tools\openbfme_import.py bootstrap-tools
"%IMPORT_PY%" tools\openbfme_import.py doctor --install F:\BFME2 --deep
"%IMPORT_PY%" tools\openbfme_import.py index --install F:\BFME2
"%IMPORT_PY%" tools\openbfme_import.py census-faction --install F:\BFME2 --faction men
"%IMPORT_PY%" tools\openbfme_import.py plan --install F:\BFME2 --profile men-fords-v0
"%IMPORT_PY%" tools\openbfme_import.py build --install F:\BFME2 --profile men-fords-v0
```

Place `--json` before the subcommand for machine-readable output. `plan` resolves only
archive metadata. `build --no-publish` assembles/audits without changing Godot's active
selection.

`census-faction` does not extract payloads or modify the active pack. It writes the
deterministic private `reports\men-faction-leaf-census.json`, resolving the current
Men command graph through typed upgrade/science/special-power definitions, mapped-image
crops/compiled atlases, selected localization IDs, audio definitions, and exact audio
samples. The current BFME2 1.06 report resolves 41 upgrades, 26 sciences, 38 special
powers, 159 images, 380 text IDs, and 474 audio samples with zero converter gaps; one
retail-authored banner-carrier portrait remains an explicit source-null reference. This is input
to generated-profile work; it does not make the handwritten `men-fords-v0` profile or
Godot runtime complete.

## Private local locations

```text
.private\retail-work\
  catalog\bfme2.json            BIG directory index (metadata only)
  cache\sources\                exact extracted source entries
  jobs\                         isolated Blender work/config/temp
  tools\                        pinned tools + tool-manifest.json
  packs\bfme2-men-vslice\       last transactional build
  reports\                      plans and diagnostics

.private\content-packs\
  selection.json
  bfme2-men-vslice\<bundle-sha256>\
```

The immutable published directory is self-contained. Runtime does not read the BFME II
install, BIG/W3D/DDS/map files, Blender, OpenSAGE, FFmpeg, or the importer cache.

## What `men-fords-v0` proves

- Archive overlay selection including `_patch103`, `_patch101`, and language patch
  precedence.
- Streaming BIG parsing/extraction with bounds, containment, case-collision rejection,
  file/byte limits, atomic writes, and source-byte cache verification.
- Nine INI documents for the Gondor Fighter member/horde gameplay closure.
- `GUMAArms` model LODs, shared skeleton, and 23 core clips.
- One GLB with 650 exported vertices, 394 render triangles, one skinned mesh, 22 bones, three
  render materials, three source images, and every requested keyed action. W3D helper
  collision/volume geometry is filtered and recorded; ambiguous unmarked box geometry
  fails closed. The profile also requires proven right-hand weapon and left-hand shield
  semantic/attachment evidence. Missing/generated textures or either missing equipment
  role fail conversion. Rigid equipment attachments are canonicalized, restored after
  clip import, and semantically revalidated. Exact source-proven ONE/ONE additive W3D
  materials are converted to deterministic alpha while ordinary materials remain
  untouched, preventing the opaque square around the Soldier blade in Godot.
- Gondor Archer, Tower Guard, and Gondor Knight presentation closures, each with an
  exact model/texture dependency set and four core clips. Archer and Knight use their
  medium LOD because the high LODs contain unsupported secondary vertex/normal chunks;
  that substitution is explicit rather than silent.
- Intact zero-clip hierarchical GLBs for the Men Fortress, Farm, Barracks, Archery
  Range, and Stable. Their construction/damage/destruction states remain separate work.
- Deterministic Pillow conversion of all four 192x192 unit portraits, all four
  64x64 object/horde buttons, and all four 64x64 production buttons. Shared atlases
  are selected once and cropped through a bounded multi-output contract. PCM16
  conversion covers all 35 Soldier voice variants, with the six wildcard groups
  pinned independently at `9/10/6/6/3/1`. Music remains exact MP3 copies because
  Godot supports it.
- An independent, bounded EAR/RefPack + CkMp map cook. For Fords it proves `415x353`
  height samples, border 20, 18,325 impassable cells, 66 terrain symbols, four standing
  water areas, eight river strips, 1,526 objects, 14 waypoints/two starts, 14 empty
  script lists, zero triggers, and zero standing waves/waypoint paths. The source
  `.map` is never copied into the pack.
- Exact tile, blend, three-way blend, cliff, blend-description, and cliff-mapping
  payloads plus a 66-symbol/66-PNG terrain-material manifest. The first exact Fords
  prop binding converts `PTGrass15` and accounts for 31 placements; all other types
  remain explicitly logical or unresolved.
- Canonical whole-pack provenance for all 264 source entries, 12 attested retail
  archives, the exact profile, importer/adapter recipe tree, pinned portable Blender
  and Pillow trees, every unique conversion output, and no retail absolute path or
  timestamp.
- Unchanged repeat builds produce the same published bundle SHA-256.

## Tool pins

| Tool | Pin |
|---|---|
| Blender | 4.2.0; executable SHA-256 `80FB653019A0AFB3BDA0947EC74E84DC0A94D0D388F9B3849433C0E1A4EFDABE` |
| Blender portable tree | SHA-256 `81E0CFB0D56FF5E33C2C562B13CC88257B9B34E072EFA7AE054A6C87F13F2AA4` |
| OpenSAGE BlenderPlugin | `2de84023cb632a79a853b2a52f97c8002ed85142` |
| blender-addon-updater | `981aa2984117a1c686b7fa40d086794ce1c7665e` |
| FFmpeg | 8.1.1; executable SHA-256 `228D7A8556258DE907FDB55F36850078EBC7680B84EC30D84EA02E99BEC1D1EB` |
| Python runtime | 3.12.10; venv launcher SHA-256 `0B471133E110CFB53A061CAD528CE8E517D7B9AC41A0A396C39AD795A487FC14`; base DLL SHA-256 `9A0E3435AAA680D868150F87AB3E388AD2EEBC22F87E036155C7B4EDA8CD2120`; bounded 576-file runtime-tree SHA-256 `98348E31DA2E14C684372BF02FEE52B71984D28D8A91B82DBE0FE9AA2F6561D7` |
| Pillow | 12.2.0 in the selected Python environment |
| Pillow package tree | SHA-256 `18C02C91B31A5B2619EB1542144F0EF1F7AC4065EAB7C5924F2640B3010FD7B0` (bytecode caches excluded) |
| fontTools | 4.61.1; package-tree SHA-256 `7903DAA0E6E9BE7C6D7BED6E39EED52FE2BA0F17107F4E398CD806F54DAFECC3` (bytecode caches excluded) |
| defusedxml | 0.7.1; package-tree SHA-256 `4A5BC129BAD371FD21F6BB07621D2D331A1D2B192FEF9B2BF78656B928C7738D` (bytecode caches excluded) |

`bootstrap-tools` verifies existing pins. On a new machine it downloads official
Blender and OpenSAGE sources. Pass
`bootstrap-tools --ffmpeg C:\path\to\ffmpeg.exe` if the pinned FFmpeg build is absent;
`ffprobe.exe` must sit beside it and match its recorded hash.

## Verification

```bat
run_importer_tests.bat
run_retail_pack_tests.bat
run_retail_slice.bat --test
run_retail_pipeline_tests.bat
```

The first runs malicious synthetic archive/cache/precedence/profile tests plus the real
doctor/plan. The second loads the selected bundle in Godot 4.7 and checks containment,
data indexing, substantial GLB geometry, rig/all clips/materials, UI dimensions, music,
all PCM voices, cooked map invariants, provenance count, and absence of donor runtime
payloads. The retail pipeline gate performs two forced builds, requires the same bundle
hash, audits the complete tree and semantic source/recipe/tool provenance, runs a real
junction-escape probe and the playable retail battle, re-runs legacy tests, exercises
negative export fixtures, and scans the export boundary.

## Retail-slice implementation checkpoints

Stages 11-15 consume the private bundle but remain bounded proof work:

| Stage | Implemented contract | Not proved by that stage |
|---|---|---|
| 11 | Deterministic control groups 1-9, including stable filtering, pruning, reset, snapshot, and signature state. | Playable/control-group integration beyond Gondor Soldier. |
| 12 | Pending destination/route/cells/ford/order sequence in authoritative state, with a selected route line and destination flag in Godot. Rejected orders keep the prior valid route. | Building-aware/dynamic route invalidation or full navigation oracle parity. |
| 13 | Fail-closed non-render/ambiguous-box filtering, canonical/restored right-hand weapon plus left-hand shield proof, source-proven additive-material conversion, four unit presentation closures, and imported Soldier pre-attack/shot-interval values driving deterministic attack windup/cadence. | Full Archer/Tower Guard/Knight runtime, animation-state, simulation, production, and combat parity. |
| 14 | Symmetric Fortress, Farm, Barracks, Archery Range, and Stable authority; five intact imported structure GLBs; Farm payouts; resources/command points; and one Barracks production contract for a 15-member Gondor Soldier horde. | Construction/damage/destruction dependency closures, player placement, or Archer/Tower Guard/Knight production. |
| 15 | The Godot scene integrates the bounded base loop with the HUD, group controls, minimap, settings/audio, simple Soldier-producing enemy behavior, Fortress victory/defeat, and an outcome splash. | The complete Men-versus-Men AI build plan, viewport/performance matrix, or declared oracle scenarios. |

The five structure presentations prefer a contained imported bundle object if one is
available. The current `men-fords-v0` pack contains the five intact imported structure
GLBs, but the scene is not yet wired to their full construction/damage/destruction
capabilities and may still use its repository-authored procedural/legal-safe masonry
kit. That fallback is presentation evidence, not a completed BFME2 building lifecycle.

## Honest current boundary

The four-unit presentation gate, five-intact-structure gate, terrain-material closure,
and scoped full-Fords binary fact cook pass.
Godot now consumes the cooked facts directly: it renders a bounded decimated source
height/passability mesh, cooked standing-water and river geometry, derives battalion
spawns from the two exact player starts, routes river crossings through the three named
source ford strips, and projects the same transform into the minimap. A deterministic
72-placement subset is still shown mostly as generic vegetation/rock markers. One exact
prop binding (`PTGrass15`) is converted and accounts for 31 source placements; the
binding inventory explicitly leaves 71 of 91 types unresolved.

The exact 66 terrain images and source blend/cliff arrays are cooked, but Godot terrain-
material blending, buildability derivation, dynamic obstacle/rebuild
navigation, and object-model resolution are still unfinished. The current route layer
is a bounded static A* built once from the cooked passability and water topology.
All four unit presentation closures are imported, but the playable runtime still spawns,
produces, and simulates only Gondor Soldier battalions. The five intact structure models
are imported, but runtime placement/construction/damage/destruction integration is not
complete. Its economy and production are
limited to deterministic Farm payouts and one Soldier queue, while the enemy shares the
queue/attack contracts without implementing the complete Men build plan. Full
building-aware navigation, map object models/materials, and declared BFME2 oracle
coverage also remain incomplete. This proof is not the complete vertical-slice
definition of done; the profile remains `vertical_slice_complete: false`.

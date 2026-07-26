# BFME II retail assets to Godot

This is the working conversion guide for the private Men-versus-Men, Fords of
Isen II vertical slice. It records verified converter behavior and failures so
the next model is treated as a dependency closure, not as a loose W3D file.

Retail and converted retail payloads stay only under the checkout's ignored `.private`
workspace. The tracked repository
may contain virtual filenames, schemas, synthetic fixtures, converter code, and
sanitized capability reports. It must not contain source bytes, decoded retail
images, GLBs, audio, map binaries, screenshots of private generated media, or
API credentials.

## Current closure

The intended private pack contains:

- Gondor Soldier, Gondor Archer, Tower Guard, and Gondor Knight;
- Fortress, Farm, Barracks, Archery Range, and Stable;
- the Fords height, passability, water, terrain-layer, terrain-material, object,
  and waypoint facts;
- only the map props needed to render this Fords slice.

Do not replace this closure with a full-install dump. Every selected input must
belong to a named resource, have a deterministic output, and appear in the
private provenance manifest.

## Converter classes

### `w3d-bundle`

Use for rigged units with one skeleton and one or more explicitly selected
animations. The converter imports the model, restores rigid attachments after
each animation import, revalidates geometry/material/weight fingerprints, and
exports one GLB with named clips.

Important options:

- `model`: exact selected W3D basename;
- `animations`: exact selected W3D basenames in capability order;
- `inputResourceIds`: the texture or other resources that may be staged beside
  this asset; do not stage the whole profile;
- `required_equipment`: only equipment roles that can be proved before and
  after animation import;
- `excludedOptionalMeshes`: exact, bounded identifiers for deliberately
  unsupported optional visuals. Exclusion is rejected if the mesh is required
  or proven equipment, ambiguous helper geometry, unmatched, multiply matched,
  or the last character mesh.

Warnings containing unsupported W3D chunks, placeholder textures, ambiguous
boxes, missing attachments, changed geometry, or split actions are fatal. Do
not downgrade these to informational messages to make a model pass.

### `w3d-static`

Use only for an armature-free prop or structure state. Static conversion rejects
animations, armatures, skins, skeletal semantics, and equipment. It retains the
same helper filtering, material proof, render fingerprints, and provenance as
animated conversion.

A W3D that references an external `*_SKL`/`*_ASKL` hierarchy is not static even
when no gameplay animation is requested. Route it to the hierarchical class.

### `w3d-hierarchical`

Use for zero-clip structure or prop scenes that require exactly one hierarchy or
skeleton. This class permits skinning and exports skins, but forbids animations
and equipment. It is distinct from both static presentation and an animated unit
so the report cannot accidentally claim the wrong capability.

### `sage-map`

The map cook writes neutral, deterministic facts rather than packaging the
source `.map`. Fords currently produces exact height, impassability, water,
objects, waypoints, and these source terrain-layer payloads:

- tile indices (`u16`);
- blend cells (`u32`);
- three-way blend cells (`u32`);
- cliff cells (`u32`);
- blend descriptions;
- cliff mappings.

Every binary carries byte length, element count, encoding, dimensions where
applicable, and SHA-256 metadata in `terrain.json`. A short write, count mismatch,
or escaped output path fails conversion.

### `sage-terrain-materials`

This bundle receives one `terrain.ini`, the exact selected TGA closure, and an
explicit ordered symbol list. It resolves symbols case-insensitively, requires
one definition and one physical TGA per symbol, converts with the pinned Pillow
build, and emits contained PNGs plus `terrain-materials.json`.

It rejects duplicate definitions, ambiguous `Texture` declarations, missing or
extra TGAs, duplicate basenames, unsafe names, disguised image payloads, and
unsupported formats. Neither the INI nor TGAs are packaged.

## Verified unit findings

| Unit | Verified conversion result | Asset-specific finding |
|---|---|---|
| Gondor Soldier | Complete proof: textured rig, 23 selected clips, weapon and shield attachment restoration | The strict equipment and additive-material contracts originated here. Do not assume another rig shares them. |
| Gondor Archer | Medium LOD converts with idle, run, attack, and death clips | The high LOD emits unsupported secondary vertex/normal chunk warnings. The exact material closure also needs the fire-arrow sequence and arrow texture; missing either produces placeholders and fails. |
| Tower Guard | Complete presentation proof: textured rig plus idle, run, attack, and death clips | The forged-blade subobject is proved as right-hand equipment before animation, canonicalized, restored after clip import, and revalidated. It must not be hidden as an optional mesh. Its additive blade texture is also a required dependency. |
| Gondor Knight | Medium LOD converts with idle, run, attack, and death clips | The high LOD also exposes unsupported secondary vertex/normal chunks. The horse material is a separate texture dependency from the rider material. |

Medium LOD is a deliberate supported conversion when the high LOD contains an
unsupported chunk. Record that substitution in the capability/provenance report;
never silently relabel the medium mesh as the high mesh.

## Verified structure findings

The intact Men structure candidates are not armature-free statics. Their primary
`*_A.W3D` files reference companion `*_ASK` or `*_ASKL` hierarchies. Therefore:

- include the companion hierarchy in the same model resource;
- use `w3d-hierarchical` for an intact zero-clip proof;
- keep damaged, destroyed, construction, idle, banner, and subobject states as
  separate declared capabilities until each is converted and tested;
- do not mark a building closure complete merely because its intact GLB loads.

The vertical slice needs full placement, construction, damage, destruction, and
selection contracts for all five structures. An intact presentation proof is the
first gate, not the completion gate.

## Fords terrain findings

The source map contains 66 terrain symbols. All 66 resolve through the selected
retail terrain definition to 66 unique TGA inputs; the closure has zero unresolved
or ambiguous symbols. The private pack now has a deterministic path to preserve
the source layer arrays and convert the exact texture set.

Godot rendering remains a separate proof. It must interpret the source tile,
blend, three-way, and cliff encodings, preserve the map coordinate transform,
and reproduce the visual result in a contact-sheet comparison. Loading 66 PNGs
or painting a dominant texture per cell is not sufficient evidence of SAGE
material parity.

## Verified unit UI atlas findings

Do not find unit buttons by visually scanning an atlas. The unit or horde INI names a
logical `ButtonImage`; the matching mapped-image INI provides the exact texture and
rectangle; catalog precedence then selects the compiled DDS winner. Preserve unusual
coordinates exactly.

| Unit | Object/horde image | Compiled atlas | Crop `[x,y,w,h]` |
|---|---|---|---|
| Gondor Soldier | `WOR_GondorSoldier` | `art/compiledtextures/st/strategicimages_001.dds` | `[64,192,64,64]` |
| Gondor Archer | `WOR_GondorArcher` | same | `[192,128,64,64]` |
| Tower Guard | `WOR_GondorTowerGuard` | same | `[128,192,64,64]` |
| Gondor Knight | `WOR_GondorKnights` | same | `[0,129,64,64]` |

These mappings come from
`data/ini/mappedimages/aptimages/strategicimages.ini`. The Knight top coordinate is
really 129, not a value to normalize to 128. Because all four images share one source
atlas, select that DDS once and use a bounded multi-output crop converter; selecting
the same retail source in four resources would create ambiguous provenance.

The production buttons follow the same evidence chain through
`buildingradialbuttons.ini`:

| Unit | Train image | Compiled atlas | Crop `[x,y,w,h]` |
|---|---|---|---|
| Gondor Soldier | `BGBarracks_Soldiers` | `art/compiledtextures/bu/buildingradialbuttons_168.dds` | `[192,64,64,64]` |
| Gondor Archer | `BGArcheryRange_Archers` | `art/compiledtextures/bu/buildingradialbuttons_167.dds` | `[192,64,64,64]` |
| Tower Guard | `BGBarracks_TowerGuard` | `art/compiledtextures/bu/buildingradialbuttons_168.dds` | `[192,128,64,64]` |
| Gondor Knight | `BGStables_Knights` | `art/compiledtextures/bu/buildingradialbuttons_187.dds` | `[192,128,64,64]` |

The shared `_168` atlas likewise needs one multi-crop resource. Every crop must be
bounded by the decoded atlas dimensions and emitted as deterministic RGBA PNG.

## Full Men UI, localization, and audio leaf resolver

The `census-faction` command now follows the current command-reachable Men graph into
three typed leaf families. Its private payload-free report is
`.private\retail-work\reports\men-faction-leaf-census.json`; repeated
complete BFME2 1.06 runs produced SHA-256
`b425be539e7ebd39c0eacf0d9c89893612ec01fd0726ac2a52481eab631695cf`.

Mapped images are resolved across 36 effective definition documents. Strict whole-file
validation accepts 35 and identifies one document with a duplicate logical definition;
exact closure resolution ignores that unrelated duplicate but still rejects a duplicate
of any requested ID as ambiguous. Source-proven lexical quirks include internal spaces,
apostrophes, and one colonless coordinate label. Do not clean or round them. Of 156
current references, 155 resolve across 77 compiled textures. The missing
`UPGondor_Banner` definition is referenced only as a source-authored SelectPortrait and
is preserved as an explicit null; missing required ButtonImages still fail. The exact
compiled path rule is:

```text
logical <stem>.<source extension>
  -> art/compiledtextures/<lowercase first two stem characters>/<stem>.dds
```

All 77 paths exist uniquely under that rule. Basename searches are unnecessary and can
be ambiguous. A Godot crop record uses `x = Left`, `y = Top`,
`width = Right - Left`, and `height = Bottom - Top`; validate it against both the
declared texture dimensions and the decoded DDS dimensions before writing PNG.

The string parser understands the shipped CP-1252 `Category:Label`, value, `END`
grammar, including escapes, physical multiline values, and two bounded duplicate-quote
terminator anomalies. The English source contains 10,973 records and 10,899
case-insensitive winners. There are 74 later duplicate records across 72 IDs; 44 later
records conflict across 43 IDs. All 380 currently requested Men IDs resolve. Eleven
requested IDs are duplicated and ten conflict. Strict mode rejects all duplicates;
BFME2 compatibility mode explicitly uses source-order first-wins and carries those ten
IDs into oracle review. The neutral census stores IDs, character counts, and UTF-8
value hashes, not retail text. Private pack generation may write the decoded selected
values only to the ignored private content pack.

Audio resolution no longer uses filename prefixes. The source graph is:

```text
reachable object or command assignment
  -> AudioEvent or Multisound logical ID
  -> ordered Multisound subsounds
  -> ordered AudioEvent Sounds entries and optional weights
  -> unique data/audio media path with the same case-insensitive stem
```

The current closure has 105 roots, 115 events, ten multisounds, and 474 exact sample
leaves. Preserve repeated parameter assignments and source order because volume,
priority, pitch, delay, range, control, and concurrency behavior may depend on them.
Every referenced sample stem resolves uniquely; wildcard directory scanning is not an
acceptable runtime substitute.

The generated private profile groups source atlases, extracts each of the 474 samples
once, and emits bounded `ui`, `strings`, and `audio-event` manifests linked to
source-leaf provenance. Godot must bind those manifests to actual
controls and event contexts. A report that only proves IDs, dimensions, hashes, or file
readiness does not prove visible UI or audible parity.

## Model conversion checklist

For every new model family:

1. Add exact catalog patterns and expected counts to the private profile.
2. Give the W3D resource only its declared `inputResourceIds`.
3. Start with the intended LOD and a minimal required clip set.
4. Run a non-publishing incomplete diagnostic build.
5. Treat missing textures, unsupported chunks, hierarchy requirements, helper
   geometry, and attachment failures as dependency or converter findings.
6. Add the smallest fail-closed converter contract and synthetic regression test.
7. Rebuild twice and require identical bundle hashes.
8. Load the GLB in Godot and check skeletons, clips, materials, textures,
   subobjects, scale, handedness, team tint, and state transitions.
9. Add the runtime capability only after the conversion report is accepted, and bind
   its `conversionProof` to that resource's exact
   `provenance/conversion/<resource-id>.json` path. Do not retain a legacy generic
   filename after splitting a converter into per-resource reports.
10. Run the focused test, then `run_retail_pipeline_tests.bat`.

## Remaining conversion work

- add and prove the five structures' construction/damage/destruction states;
- extend the fail-closed Fords object-binding inventory from the first exact
  `PTGrass15` closure to the remaining renderable prop families;
- implement and visually verify the Godot terrain-layer renderer;
- generate the private UI/string/audio resources from the resolved leaf graph and make
  the Soldier/Barracks control bar consume them instead of hard-coded labels, authored
  palantir art, or filename-scanned voice buckets;
- integrate Archer, Tower Guard, Knight, and the imported structure lifecycles into
  the playable runtime rather than treating their GLBs as presentation-only proofs;
- keep resource/source/output/archive pins synchronized whenever the exact closure
  expands.

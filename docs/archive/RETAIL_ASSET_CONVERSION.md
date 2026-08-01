# Retail asset conversion

The single reference for how retail assets become Godot resources: the converter
classes and their verified behaviours and failure modes, the deterministic
dependency-closure command, the prop eligibility ladder (static, zero-clip
hierarchical, animated, proven-no-motion), roads, particles, projectiles, and
the pinned Blender toolchain attestation.

Two W3D animation edge cases are kept at the end as appendices because they
record retail authoring quirks that are not obvious from the format spec.

> **Consolidation note.** This document absorbs several former standalone
> documents, listed under their own headings below and preserved verbatim.
> Counts, blocker totals and hashes inside those sections are snapshots taken
> when that investigation was written and are **not** current status; they are
> kept because the surrounding evidence depends on them. Live gate results
> live only in [STATUS.md](../STATUS.md).

---

<!-- merged from docs/GODOT_RETAIL_ASSET_CONVERSION.md -->

## BFME II retail assets to Godot

This is the working conversion guide for the private Men-versus-Men, Fords of
Isen II vertical slice. It records verified converter behavior and failures so
the next model is treated as a dependency closure, not as a loose W3D file.

Retail and converted retail payloads stay only under the checkout's ignored `.private`
workspace. The tracked repository
may contain virtual filenames, schemas, synthetic fixtures, converter code, and
sanitized capability reports. It must not contain source bytes, decoded retail
images, GLBs, audio, map binaries, screenshots of private generated media, or
API credentials.

### Current closure

The intended private pack contains:

- Gondor Soldier, Gondor Archer, Tower Guard, and Gondor Knight;
- Fortress, Farm, Barracks, Archery Range, and Stable;
- the Fords height, passability, water, terrain-layer, terrain-material, object,
  and waypoint facts;
- only the map props needed to render this Fords slice.

Do not replace this closure with a full-install dump. Every selected input must
belong to a named resource, have a deterministic output, and appear in the
private provenance manifest.

### Converter classes

#### `w3d-bundle`

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

#### `w3d-static`

Use only for an armature-free prop or structure state. Static conversion rejects
animations, armatures, skins, skeletal semantics, and equipment. It retains the
same helper filtering, material proof, render fingerprints, and provenance as
animated conversion.

A W3D that references an external `*_SKL`/`*_ASKL` hierarchy is not static even
when no gameplay animation is requested. Route it to the hierarchical class.

#### `w3d-hierarchical`

Use for zero-clip structure or prop scenes that require exactly one hierarchy or
skeleton. This class permits skinning and exports skins, but forbids animations
and equipment. It is distinct from both static presentation and an animated unit
so the report cannot accidentally claim the wrong capability.

#### `sage-map`

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

#### `sage-terrain-materials`

This bundle receives one `terrain.ini`, the exact selected TGA closure, and an
explicit ordered symbol list. It resolves symbols case-insensitively, requires
one definition and one physical TGA per symbol, converts with the pinned Pillow
build, and emits contained PNGs plus `terrain-materials.json`.

It rejects duplicate definitions, ambiguous `Texture` declarations, missing or
extra TGAs, duplicate basenames, unsafe names, disguised image payloads, and
unsupported formats. Neither the INI nor TGAs are packaged.

### Verified unit findings

| Unit | Verified conversion result | Asset-specific finding |
|---|---|---|
| Gondor Soldier | Complete proof: textured rig, 23 selected clips, weapon and shield attachment restoration | The strict equipment and additive-material contracts originated here. Do not assume another rig shares them. |
| Gondor Archer | Medium LOD converts with idle, run, attack, and death clips | The high LOD emits unsupported secondary vertex/normal chunk warnings. The exact material closure also needs the fire-arrow sequence and arrow texture; missing either produces placeholders and fails. |
| Tower Guard | Complete presentation proof: textured rig plus idle, run, attack, and death clips | The forged-blade subobject is proved as right-hand equipment before animation, canonicalized, restored after clip import, and revalidated. It must not be hidden as an optional mesh. Its additive blade texture is also a required dependency. |
| Gondor Knight | Medium LOD converts with idle, run, attack, and death clips | The high LOD also exposes unsupported secondary vertex/normal chunks. The horse material is a separate texture dependency from the rider material. |

Medium LOD is a deliberate supported conversion when the high LOD contains an
unsupported chunk. Record that substitution in the capability/provenance report;
never silently relabel the medium mesh as the high mesh.

### Verified structure findings

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

### Fords terrain findings

The source map contains 66 terrain symbols. All 66 resolve through the selected
retail terrain definition to 66 unique TGA inputs; the closure has zero unresolved
or ambiguous symbols. The private pack now has a deterministic path to preserve
the source layer arrays and convert the exact texture set.

Godot rendering remains a separate proof. It must interpret the source tile,
blend, three-way, and cliff encodings, preserve the map coordinate transform,
and reproduce the visual result in a contact-sheet comparison. Loading 66 PNGs
or painting a dominant texture per cell is not sufficient evidence of SAGE
material parity.

### Verified unit UI atlas findings

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

### Full Men UI, localization, and audio leaf resolver

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

### Model conversion checklist

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
10. Run the focused test, then `run_retail_pipeline_tests.bat`, then
    `run_stage10_tests.bat`.

### Remaining conversion work

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

---

<!-- merged from docs/RETAIL_VISUAL_CONVERSION.md -->

## Retail visual conversion closure

`visual-closure` turns a bounded list of SAGE `Object` names into a
deterministic conversion dependency closure. It reads the extracted effective
asset tree, follows only the selected Object definitions, their parents, and
their reachable includes, then inspects only the exact W3D leaves selected by
that graph. Every authored visual and embedded texture dependency retains
source provenance.

The command does not convert or copy retail payloads. Its JSON report contains
virtual paths, source locations, hashes, header identifiers, and diagnostics;
it contains no INI text, W3D bytes, texture bytes, or absolute asset-root path.
The report is written beneath the configured private state root in `reports/`.

### Run it

Run `extract-all-assets` first. Then pass every Object required by the current
conversion batch as a repeated `--object` argument:

```powershell
$env:PYTHONPATH = "importer"
& ".private\retail-work\tools\python-3.12-env\Scripts\python.exe" `
  "tools\openbfme_import.py" `
  --state-root ".private\retail-work" `
  --json `
  visual-closure `
  --assets-root ".private\retail-work\cache\effective-assets" `
  --object "<first-object-id>" `
  --object "<second-object-id>"
```

The command exits `0` only when every target, inheritance edge, and authored
physical visual leaf resolves exactly. Exit `6` means a neutral report was
successfully written but still contains missing, ambiguous, or invalid source
evidence. Unsafe paths and ambiguous Object definitions are hard errors.
Targets are not restricted to units or structures: a map's full placement-type
set (including a 72-prop batch) can be supplied in one invocation. The bounded
maximum is 4,096 target Objects.

### Exact-resolution policy

The first pass catalogs W3D, DDS, TGA, JPG, and PNG files by virtual path only.
It does not open every W3D file. Models whose authored identifiers match an
exact physical stem or container resolve from that path evidence. Every exact
physical W3D leaf reached by the target graph is then scanned once, including
models that already resolved in the path-only pass. This is required to expose
their embedded material dependencies to the converter.

If a W3D hierarchy or animation remains unresolved, the command selects only
exact physical candidates:

- a raw identifier uses the same exact filename stem;
- a dotted animation identifier uses its animation/subobject component as the
  exact filename stem;
- candidates already exposed by the exact W3D resolver remain candidates.

The W3D read boundary is therefore the union of exact resolved target leaves
and exact unresolved candidates. Each path is opened at most once. Their
authored model, hierarchy, and animation header identifiers are added to the
index, and the typed Object graph is run a second time. Multiple exact
candidates remain ambiguous. A missing raw animation remains missing. There is
no prefix, substring, edit-distance, or nearest-file fallback.

An authored `.tga` texture may resolve to a unique compiled `.dds` with the
same exact stem. That bridge is retained as evidence in `exactLeaves`. It does
not permit PNG/JPG substitution or select among multiple DDS candidates.

The same rule is applied independently to texture identifiers embedded in each
scanned W3D. An existing explicitly named DDS/TGA/JPG/PNG resolves directly.
An absent authored TGA may bridge only to one exact-stem DDS. Missing and
ambiguous embedded dependencies remain in `w3dDependencyClosure` and make the
report non-ready; they are never dropped from the plan.

### Report contract

The private report records:

- requested targets and exact target definition locations;
- the Object/parent definition closure and any missing definitions;
- source documents and body fragments reached through includes, with hashes;
- lifecycle coverage, effective Draw modules, states, and reference counts;
- exact physical leaves with conditions, lifecycle phase, evidence, and source
  provenance;
- source-language semantic leaves such as an authored non-physical `None`;
- graph diagnostics and unresolved references without guessed replacements;
- only the W3D paths that were scanned, with byte length, SHA-256, exact header
  IDs, embedded model references, and scanner warnings;
- the exact W3D read boundary (ordered virtual paths, unique read count, and
  total bytes read);
- every embedded texture reference with W3D chunk provenance, resolution
  status, exact physical path/evidence, or retained missing/ambiguous
  candidates;
- per-status dependency counts and a canonical SHA-256 for the W3D dependency
  section;
- a canonical aggregate SHA-256 over the report content.

The same asset tree and target spellings produce the same ordered report and
aggregate hash regardless of target argument order.

### Godot conversion order

Use the report as a dependency boundary, not as a runtime asset manifest.

1. Freeze a green or intentionally reviewed closure report for the target
   batch. Do not begin with a hand-maintained list of likely filenames.
2. Convert every W3D in the reported read boundary. Preserve the logical ID to
   physical-path evidence and source SHA-256 in the generated pack provenance;
   do not add a filename-neighbour discovered outside the closure.
3. Convert both SAGE-authored texture leaves and each resolved embedded W3D
   texture dependency. Apply the authored-TGA-to-compiled-DDS bridge only when
   the relevant record proves one unique exact-stem DDS.
4. Bind materials, shader metadata, house colour, shadow, attached-model, and
   particle leaves from their reported roles. Do not silently install generic
   materials or effects for unresolved retail references.
5. Build Godot scenes for each reported lifecycle phase: intact, construction,
   damaged, really damaged, rubble, and post-rubble where authored. Missing
   phases remain an integration gap rather than aliases of the intact model.
6. Map animation states by the authored logical animation and hierarchy IDs,
   then verify skeleton compatibility and attachment points in the converter.
7. Publish the converted resources only through the private content-pack
   pipeline. Runtime selection must fail closed when a required converted leaf
   or provenance record is absent.
8. Compare rendered lifecycle/state captures against the original-game oracle,
   then run the focused importer tests and the repository retail gate.

UI images, localized strings, sound/music/VO, terrain, navigation, routing, and
AI are deliberately outside this Object visual closure. They need their own
typed dependency reports and runtime gates; a green visual closure is not a
claim of gameplay or 1:1 audiovisual parity.

### Men/Fords integration note

Current project evidence shows the primary Men roster and structure conversions
are present enough to exercise the private slice, while high-count prop binding
and complete building construction/damage/rubble presentation remain open
scoreboard items. UI/audio coverage and all-unit production/AI behavior are
separate gates. Run this command over the exact roster and structure Object IDs
to replace broad filename inventories with a reproducible conversion closure,
but do not describe the slice as 1:1 until the `DIRECTION.md` visual and play
checklists and the retail pipeline gate are all green.

#### Current 72-type Fords closure evidence

On 2026-07-13, the 72 non-logical records in the selected Fords
`object-bindings.json` (one bound type plus 71 unresolved render types) produced:

- private report
  `.private/retail-work/reports/retail-visual-closure-c5b4c1ed84af5d08.json`;
- report aggregate
  `06bacb549e4b13aaa79bb380e89c0ddb87d8b42fcff8046c81ded6dd49f5eb66`
  and W3D dependency aggregate
  `526a7dc1cf864ee6cf07a7448169929b153626dafec3fa5b9668ba74bed080bd`;
- 68 resolved target Objects and four missing footprint/decal definitions:
  `FtPrintDrkGr02`, `FtPrintGrass02`, `FtprintsDrk`, and `FtprintsDrk02`;
- 113 unique W3D reads totalling 6,711,319 bytes;
- 66 embedded texture references, all 66 resolved exactly;
- 198 exact SAGE leaves, 18 semantic leaves, and ten missing animation leaves:
  `CUBear_SKL.CUBear_IDLE`, `CUBear_SKL.CUBear_IDLC`,
  `CUDuck_SKL.CUDuck_ANTA`, `CUDuck_SKL.CUDuck_ANTB`,
  `CURabbit1_SKL.CURabbit1_IDLD`, `CURabbit1_SKL.CURabbit1_IDLC`,
  `CURaccoon_SKL.CURaccoon_ANTA`, `CURaccoon_SKL.CURaccoon_ANTB`,
  `CUWolf_SKL.CUWolf_SITA`, and `CUWolf_SKL.CUWolf_ATNA`;
- 18 retained `unsupported-chunk` scanner warnings across animal-skin and
  neutral-building W3Ds. These warnings are explicit inspection evidence, not
  guessed dependencies; the missing definitions and animations are the exact
  readiness blockers in this report.

Running the identical batch twice produced the same aggregate. Treat these
numbers as a dated baseline: regenerate the report whenever the extracted
effective tree, Object bindings, CST grammar, or W3D scanner changes.

---

<!-- merged from docs/RETAIL_BLENDER_TOOL_ATTESTATION.md -->

## Retail Blender and OpenSAGE tool attestation

OpenBFME treats the pinned Blender 4.2.0 portable directory as executable input.
Every file in that directory is covered by the exact full-tree SHA-256 pin; Python
bytecode is not ignored or accepted as equivalent to source.

Blender can create `__pycache__` directories and `.pyc`/`.pyo` files inside its
portable directory. Those generated files used to make the otherwise unchanged
tree alternate between valid and invalid after a direct Blender run. The importer
now has one bounded recovery operation for that specific condition:

- Cleanup is allowed only when the selected executable resolves to
  `<state-root>/tools/blender-4.2.0-windows-x64/blender.exe`.
- `OPENBFME_BLENDER` overrides and all other directories are read-only. They are
  attested, but never cleaned.
- The cleanup removes only standalone `.pyc`/`.pyo` files and complete directories
  named `__pycache__`. It has fixed file, directory, and byte ceilings.
- The entire tree is scanned and proved free of links, junctions, and Windows
  reparse points before any deletion. Every candidate is rechecked without link
  following immediately before removal, and containment below the resolved pinned
  root is mandatory.
- Any unsupported filesystem entry, traversal failure, containment failure, link,
  junction, reparse point, or exceeded cleanup bound fails closed before execution.

Cleanup runs when bootstrap reuses an installed pinned tree, immediately before
W3D-required build preflight asks for tool status, and immediately before every
W3D Blender execution. `tool_status()` itself is observational and never mutates
the tool tree.

After cleanup, both the Blender executable hash and exact portable-tree hash must
match their pins before Blender can run. Source edits, executable edits, extra
files, or unapproved bytecode therefore still fail attestation. After conversion,
the importer performs rejection-only link and bytecode checks and recomputes the
exact full-tree hash. It does not clean post-execution evidence.

The exact state-root-owned `OpenSAGE.BlenderPlugin` checkout uses the same bounded
cache cleanup at bootstrap reuse, W3D preflight, and immediately before Blender
execution. Plugin overrides are never cleaned, and `tool_status()` remains
observational. The plugin authority remains its exact main and updater-submodule
commits plus a clean Git worktree; `.git` is not assigned a new directory hash.
After execution, link, bytecode, commit, submodule, and worktree checks are
rejection-only.

Focused verification:

```powershell
$env:PYTHONPATH = "importer"
python -m unittest discover -s importer/tests -p "test_blender_tool_cache.py" -v
```

---

<!-- merged from docs/RETAIL_STATIC_PROP_BATCH.md -->

## Retail static-prop batch planning

`retail_visual_profile.py` turns a validated retail visual-closure report into
a deterministic, payload-free static-prop conversion plan. It does not scan
for filenames, convert assets, edit a content profile, build a pack, or publish
anything. The planner is intentionally usable when the full closure is not
ready: each requested Object type is evaluated independently, and every
ineligible type remains in the report with explicit reasons.

### Required inputs

The planner consumes two already-authored sources of truth:

- an `openbfme.retail-visual-closure` schema-version 1 report;
- the `openbfme.effective-assets-manifest` beside the extracted effective tree.

It verifies both the top-level visual-closure digest and the nested W3D
dependency digest. It also verifies the effective-manifest aggregate, totals,
case-unique paths, and every selected source record. A scanned W3D's size and
SHA-256 must agree with the effective manifest. Absolute, traversing,
noncanonical, duplicate, and case-colliding paths or target IDs are rejected.

### Static eligibility contract

A target Object is eligible only when all of these are proven by the closure:

1. Its definition exists and inheritance is complete.
2. Its resolved model leaves identify exactly one distinct physical W3D. The
   same W3D may appear in several authored states, and a semantic `Model = None`
   state is retained without becoming a physical model.
3. The target requires no hierarchy, animation, particle, or attached-model
   leaf. Missing or ambiguous leaves are never promoted from their candidates.
4. The selected W3D was scanned, has at least one model header, and has no
   scanner warning, animation header, hierarchy header, or unsupported embedded
   model-reference role. Internal HLOD `lod` references are supported.
5. Every texture named in the Object and every texture embedded in that exact
   W3D resolves to one physical DDS, TGA, JPG, or PNG. The planner accepts only
   the exact resolution evidence already recorded by the closure; it never
   searches for or infers a substitute.
6. The effective-assets manifest supplies an exact source archive, offset,
   precedence, byte length, and SHA-256 for the W3D and every texture.

This is deliberately narrower than a general W3D batch. Animated critters,
hierarchical props, buildings with lifecycle models, particles, and files with
unsupported scanner chunks stay in `ineligibleTargets` for their proper
converter path.

### Output contract

The `openbfme.retail-static-prop-plan` document contains:

- eligible and ineligible target Object types;
- one conversion group per distinct physical W3D, including all Object types
  that share it and exact source/hash records;
- one globally deduplicated texture resource per physical texture;
- one `w3d-static` resource per conversion group with exact
  `inputResourceIds`;
- deterministic `objectBindings.models` rows containing `typeName`, the exact
  `sourceVirtualModel`, the stable GLB output, and
  `matchMethod = exact-type-name`;
- placement-independent type counts and a canonical aggregate SHA-256.

The `profileFragment.resources` array satisfies the normal import-profile
resource contract, but the document is still only a fragment. Integration must
merge the resources and model-binding rows into a reviewed private map profile,
then run the ordinary pack build, audit, and runtime gates. No placement count
is consumed or inferred by this stage.

### Generate the private plan

The module currently remains separate from the public importer CLI so this
planning stage cannot accidentally publish a pack. A private one-off invocation
is:

```powershell
$env:PYTHONPATH = "importer"
@'
from pathlib import Path
from openbfme_importer.retail_visual_profile import (
    build_retail_static_prop_plan,
    load_retail_static_prop_plan_inputs,
    write_retail_static_prop_plan,
)

report, manifest = load_retail_static_prop_plan_inputs(
    Path(".private/retail-work/reports/<visual-closure>.json"),
    Path(".private/retail-work/cache/effective-assets/.openbfme/manifest.json"),
)
plan = build_retail_static_prop_plan(report, manifest)
write_retail_static_prop_plan(
    Path(".private/retail-work/reports/<static-prop-plan>.json"), plan
)
'@ | & ".private/retail-work/tools/python-3.12-env/Scripts/python.exe" -
```

Run the focused contract tests with:

```powershell
$env:PYTHONPATH = "importer"
& ".private/retail-work/tools/python-3.12-env/Scripts/python.exe" `
  -m unittest importer.tests.test_retail_visual_profile -v
```

### Current Men/Fords non-road result

On 2026-07-13, the corrected 67-type non-road Fords closure
`retail-visual-closure-0d51ad8d31ca6e6c.json` produced the private plan:

`.private/retail-work/reports/retail-static-prop-plan-0d51ad8d31ca6e6c.json`

The result is placement-independent:

- 67 target Object types considered;
- 38 eligible target types;
- 29 ineligible target types retained with reasons;
- 34 unique W3D conversion groups (the Banyan pair and four Evergreen types
  share exact W3Ds);
- 17 unique texture sources;
- 51 profile resources and 38 exact model-binding rows;
- plan aggregate SHA-256
  `60e08fc7e93b4f7151e4bcb67cc503a7766921622d1bbeca68f79db437673295`.

The plan was not built or published. Its ineligible set correctly retains
ambient logical objects without models, animated animals, CaptureFlag,
multi-model lairs and Inn, hierarchy-bearing props, W3Ds with scanner warnings,
and the ten unresolved animal animation references from the source closure.

---

<!-- merged from docs/RETAIL_HIERARCHICAL_PROP_BATCH.md -->

## Retail zero-clip hierarchical prop batch

`retail_hierarchical_profile.py` is the bounded follow-on to the static-prop
planner. The static planner correctly rejects every W3D with a hierarchy; this
planner admits only the narrower class that the importer can convert with
`w3d-hierarchical` without inventing animation state.

It produces a payload-free plan only. It does not copy retail files, invoke
Blender, build a pack, publish a pack, or edit map bindings.

### Selection contract

A target is eligible only when all of the following are proven by the sealed
visual closure, static plan, effective-assets manifest, and cooked map object
document:

- it was not already accepted by the static-prop plan;
- its Object definition and inheritance closure are complete and diagnostic
  free;
- all visual references for that target resolve exactly;
- it has one unique physical model W3D, not a filename neighbour or
  substitute;
- it has no authored animation, particle, or attached-model dependency;
- the model W3D was scanned, has model and hierarchy headers, has no animation
  headers, and produced no scanner warning;
- every embedded and SAGE-authored texture resolves to one exact physical
  image;
- there is at least one supported `lod` model reference, and every such
  reference names an exact render subobject present in the W3D model headers;
- any authored hierarchy W3D is the model W3D itself; external hierarchy
  bundles remain excluded;
- model and texture byte lengths and SHA-256 values agree with the effective
  manifest.

The input static plan is recomputed from the closure and manifest and must be
byte-for-byte equivalent as canonical JSON. Re-sealing a modified intermediate
plan does not make it acceptable. Closure and nested W3D-dependency digests,
manifest aggregate identity, unsafe paths, target/path case collisions, map
target case drift, and map object ordering are validated before selection.

Every rejected target remains in `rejectedTargets` with deterministic reason
records. The profile fragment is run through the real `ImportProfile` parser;
each model resource explicitly declares empty animation and equipment arrays
and exact local texture-resource dependencies.

#### Proven root-rigid exception

The scanner can prove a narrower source shape in which every supported render
reference is an `lod` reference at `boneIndex = 0`. OpenSAGE intentionally
omits that source root pivot, so these files import as rigid meshes parented to
one empty armature carrier. Only those exact groups receive
`options.provenRootRigidBake = true`; model names never participate in the
decision.

The Blender adapter accepts the corresponding CLI flag only for a hierarchical
conversion. It then requires zero animation actions, one empty unparented
armature carrier, at least one retained render mesh, every retained mesh
rigidly parented to that carrier, and no vertex groups, modifiers, bone parent,
or other deform ambiguity. It captures every mesh world matrix, unparents the
meshes, restores and verifies those matrices, removes the carrier, and verifies
the matrices again. Any mismatch is a conversion failure.

The adapter reports the requested/applied state, removed-carrier count,
baked-mesh count, transform proof, and deformation proof. The pipeline accepts
zero exported bones and skeletons only when the profile requested the bake and
that complete report agrees. A missing/false option, an unexpected applied
flag, or an incomplete proof still fails closed. Normal multi-pivot
hierarchies continue to require exactly one nonempty exported skeleton.

### Python use

There is deliberately no new CLI command for this batch. The existing importer
surface does not need to grow before the conversion contract has proved useful.

```python
from openbfme_importer.retail_hierarchical_profile import (
    build_retail_hierarchical_prop_plan,
    load_retail_hierarchical_prop_plan_inputs,
    write_retail_hierarchical_prop_plan,
)

inputs = load_retail_hierarchical_prop_plan_inputs(
    ".private/retail-work/reports/retail-visual-closure-0d51ad8d31ca6e6c.json",
    ".private/retail-work/reports/retail-static-prop-plan-0d51ad8d31ca6e6c.json",
    ".private/retail-work/cache/effective-assets/.openbfme/manifest.json",
    ".private/retail-work/packs/bfme2-men-vslice-roads-private/maps/"
    "fords-of-isen-ii/objects.json",
)
plan = build_retail_hierarchical_prop_plan(*inputs)
write_retail_hierarchical_prop_plan(
    ".private/retail-work/reports/"
    "retail-hierarchical-prop-plan-0d51ad8d31ca6e6c.json",
    plan,
)
```

Run the focused contract tests with:

```powershell
$env:PYTHONPATH = "importer"
& ".private\retail-work\tools\python-3.12-env\Scripts\python.exe" `
  -m unittest importer.tests.test_retail_hierarchical_profile -v
```

### Fords of Isen II correction, 2026-07-13

The earlier eight-group private report predates the render-subobject and
root-rigid checks and is superseded. Regenerate it from the current sealed
visual closure before composing another pack; its old digest and counts are
not current evidence.

The corrected classification is evidence-driven:

- `PMClotheLine01`, `RockGrey22`, `RockGrey23`, and `RockGrey24` each have only
  supported `lod` render references at `boneIndex = 0`, so their generated
  resources explicitly request the proven root-rigid bake;
- `FarmTemplate` and `FireCampfireNight` have render references on non-root
  pivots and remain normal nonempty hierarchical conversions;
- `WtrflHaze` and `WtrRiplsSmall` have hierarchy/model headers but no model
  references or render subobjects, so the planner now rejects them with
  `model-w3d-has-no-supported-render-subobject` instead of fabricating or
  requesting a mesh conversion.

Focused private scratch conversions of the four proven root-rigid files
produced one retained mesh each, zero exported bones, zero exported skeletons,
and complete adapter bake proofs. These scratch results validate the adapter;
they are not published content-pack evidence.

`OrcMeatRack01` is not in this batch. Its exact model has hierarchy headers but
also two animation headers, so treating it as a zero-clip hierarchy would
discard authored behavior. It remains rejected with
`model-w3d-contains-animation-headers` until the animated conversion path owns
it.

### Godot integration notes

When this plan is composed into a private profile, preserve its generated
resource IDs, `inputResourceIds`, outputs, and exact `objectBindings.models`
rows. Normal model conversion must report hierarchical asset kind, one
nonempty skeleton, and zero clips. A proven root-rigid resource instead must
report the exact requested bake proof and zero exported skeletons. A converter
failure, missing texture, unexpected clip, or contract mismatch is a hard
incomplete-pack result, not permission to retry as `w3d-static`.

After conversion, instantiate the generated bindings at the exact cooked positions
and yaw values. Keep the W3D hierarchy even when the object has no gameplay
animation: it can control authored pivots, HLOD subobjects, visibility pieces,
and material placement. The only flattening exception is the explicitly
proven pivot-zero carrier described above. In particular, do not flatten the
farm or campfire hierarchy merely because its animation header list is empty.

This report is conversion readiness evidence, not a claim that the assets are
already bound or rendered. Before calling the batch integrated, build and
audit the composed private pack, run the focused prop/runtime gates, inspect
rendered Fords captures, and then run `run_retail_pipeline_tests.bat`.

---

<!-- merged from docs/RETAIL_ANIMATED_PROP_BATCH.md -->

## Exact animated Fords prop batch

`retail_animated_prop_profile.py` is the fail-closed planning pass after the
static and zero-clip hierarchical prop planners. It does not convert or
publish retail content. It produces a private report and a standalone private
ImportProfile whose resources can later be composed into the main slice.

### Scope and result

The current Fords evidence has 67 visual-closure target types. The static pass
covers 38 types, the hierarchical pass covers six, and the base map profile
classifies seven more as logical ambient-audio emitters. The animated pass
therefore evaluates 16 remaining physical target types and 48 placements.

The exact animated pass currently promotes:

| Target | Placements | Model | Physical clips | No-clip transitions | Secondary-skin proof |
| --- | ---: | --- | ---: | ---: | --- |
| `Bear` | 1 | `CUBear_SKN` | 5 | 2 | yes |
| `CaptureFlag` | 2 | `CAPFLAG_SKN` | 4 | 0 | no |
| `Duck` | 2 | `CUDuck_SKN` | 5 | 2 | yes |
| `Egret` | 2 | `CUEgret_SKN` | 2 | 0 | no |
| `ElkFemale` | 1 | `CUELKF_SKN` | 7 | 0 | yes |
| `ElkMale` | 1 | `CUELK_SKN` | 7 | 0 | yes |
| `Fish` | 13 | `CUTuna_SKN` | 2 | 0 | no |
| `Rabbit` | 1 | `CURabbit1_SKN` | 4 | 2 | yes |
| `Raccoon` | 2 | `CURaccoon_SKN` | 4 | 2 | yes |
| `Wolf` | 1 | `CUWolf_SKN` | 4 | 2 | no |

This adds ten exact type bindings and 26 placements. The fragment contains ten
`w3d-bundle` model resources, 11 deduplicated texture-input resources, and one
shared `data`/`hash-only` W3D input resource. It is validated by the real
`ImportProfile` parser. The current plan aggregate is
`03d06180d7a9fd9cd64430d4644df160327d41a63c542d6c894ab9d7ed859194`.

`ElkFemale` and `ElkMale` use the same `NUHORSE_SKL` plus seven exact horse
actions. Those eight W3Ds have one source owner:
`animated-prop-shared-w3d-nuhorse-skl-4678722e3886` (`kind: data`,
`converter: hash-only`). Each elk model resource owns only its skin W3D and
declares the shared resource through `inputResourceIds`. The generated fragment
rejects any source pattern owned by more than one resource.

The ten transition names on Bear, Duck, Rabbit, Raccoon, and Wolf are not
filename guesses. The planner accepts only the exact authored TransitionState
rows and proves the ten physical header names absent from every one of the
12,751 manifest-attested retail W3Ds (1,049,935,816 bytes). That absence proof
is sealed into both the plan and the model-resource options as source-native
`immediate-no-clip` behavior.

Six target types and 22 placements remain deliberately unplanned:

- `WargLair`, `CaveTrollLair`, and `Inn`: multiple construction, intact,
  damage, collapse, rubble, and floor/bib models require a lifecycle-aware
  multi-model runtime binding. A single GLB binding would not be 1:1.
- `OrcMeatRack01`: its model contains one embedded 101-frame, 30-FPS animation
  header but no animation-channel chunks. The embedded-action adapter requires
  at least one exported channel, sampler, curve, and key, so this header-only
  no-motion action cannot yet pass the converter contract. It remains rejected
  until that exact case has a source-proven adapter policy.
- `WtrRiplsSmall` and `WtrflHaze`: these are particle/no-render water objects,
  not authored animated-model bundles. They belong to the particle/runtime
  path rather than a forced GLB binding.

### Conversion contract

An animated target is eligible only when all of these are true:

1. It is not already covered by the exact static or hierarchical plan and is
   not an exact base-profile logical binding.
2. It has one physical model W3D and either a complete set of split physical
   action W3Ds or one converter-compatible embedded action.
3. Fresh bytes below the private effective-assets root match the sealed
   manifest size and SHA-256.
4. The model has mesh/model headers and only supported `lod` references. A
   scanner warning is fatal except for the exact paired secondary skin streams,
   which require a separate semantic-equivalence proof before promotion.
5. The model header declares exactly one hierarchy ID. A split bundle uses the
   exact matching W3D in the model directory or the only manifest W3D with that
   basename. This is an authored header relationship, not a filename-near guess.
   An embedded bundle instead requires exactly one matching hierarchy header
   and nonempty pivots in the model itself.
6. Each split animation resolves to one animation-only W3D with one header, at
   least one key-channel chunk, positive timing, and the same hierarchy ID as
   the model. An embedded action must likewise have one matching header,
   positive timing, and a physical key channel.
7. An unresolved transition is accepted only if its target, logical name,
   condition, provenance, and source semantics match the closed no-clip policy,
   the complete expected transition set is present, and every corresponding
   physical header name is absent from the complete manifest W3D corpus.
8. Every explicit and model-embedded texture resolves to one exact image and
   its effective-tree bytes match the manifest.

The generated model resource selects model, skeleton, and animation W3Ds in a
single exact pattern set. Its `options.model` and `options.animations` use the
actual source basenames expected by the existing Blender adapter. Exact
source-native no-clip transitions are carried as inert, sealed planning
metadata; they are not fabricated animation files. A reused exact skeleton and
action closure is instead owned once by a shared `data`/`hash-only` input
resource and staged through `inputResourceIds`. Texture resources use the same
dependency mechanism so the original DDS/TGA bytes are staged beside the W3Ds;
the resulting GLB remains the runtime render asset.

### Rebuild the private plan

Run from the repository root with the pinned private Python environment and
`PYTHONPATH=importer`:

```powershell
$env:PYTHONPATH = "importer"
@'
from openbfme_importer.retail_animated_prop_profile import (
    build_retail_animated_prop_plan,
    generated_import_profile,
    load_retail_animated_prop_plan_inputs,
    write_generated_import_profile,
    write_retail_animated_prop_plan,
)

inputs = load_retail_animated_prop_plan_inputs(
    ".private/retail-work/reports/retail-visual-closure-0d51ad8d31ca6e6c.json",
    ".private/retail-work/reports/retail-static-prop-plan-0d51ad8d31ca6e6c.json",
    ".private/retail-work/reports/retail-hierarchical-prop-plan-0d51ad8d31ca6e6c.json",
    ".private/retail-work/cache/effective-assets/.openbfme/manifest.json",
    ".private/retail-work/packs/bfme2-men-vslice/maps/fords-of-isen-ii/objects.json",
    "importer/profiles/men-fords-v0.json",
)
plan = build_retail_animated_prop_plan(
    *inputs,
    ".private/retail-work/cache/effective-assets",
)
write_retail_animated_prop_plan(
    ".private/retail-work/reports/retail-animated-prop-plan-0d51ad8d31ca6e6c.json",
    plan,
)
write_generated_import_profile(
    ".private/retail-work/profiles/men-fords-v0-animated-props.generated.json",
    generated_import_profile(plan),
)
'@ | & .private/retail-work/tools/python-3.12-env/Scripts/python.exe -
```

Run the focused legal-safe fixture tests with:

```powershell
$env:PYTHONPATH = "importer"
& .private/retail-work/tools/python-3.12-env/Scripts/python.exe `
  -m unittest tests.test_retail_animated_prop_profile -v
ruff check importer/openbfme_importer/retail_animated_prop_profile.py `
  importer/tests/test_retail_animated_prop_profile.py
```

The standalone generated profile is a planning/build input only. A successful
plan is not a successful GLB conversion, runtime animation binding, or rendered
parity proof. Those remain separate fail-closed gates.

---

<!-- merged from docs/RETAIL_NO_MOTION_PROP_PROFILE.md -->

## Retail no-motion prop profile

`retail_no_motion_prop_profile.py` closes one deliberately narrow Fords of
Isen II gap: `OrcMeatRack01` is a renderable hierarchical W3D whose embedded
`PMMEATRACK01` animation container has a 101-frame, 30 Hz header but no key,
bit, compressed, motion, morph, or other animation channel. The object INI
does not author an `Animation` state. Treating the header as a looping action
would invent behavior; rejecting the whole model would discard a valid retail
mesh.

The planner therefore emits a normal `w3d-hierarchical` model with:

- `animations: []`
- `provenRootRigidBake: true`
- one exact `provenNoMotionAnimations` declaration for raw
  `PMMEATRACK01`, hierarchy `PMMEATRACK01`, model `PMMEATRACK01`, 101 frames at
  30 Hz
- the exact `PMMeatrack.tga` retail dependency resolved to
  `art/compiledtextures/pm/pmmeatrack.dds`
- one exact `OrcMeatRack01` map binding

This is not a general option for suppressing inconvenient animation failures.
The W3D preprocessor independently proves that the selected animation is a
top-level, header-only container and preserves every retained top-level byte.
Any child chunk, extra animation container, changed identifier, changed
timing, changed hierarchy/model binding, scanner warning, or source hash
mismatch aborts before output.

### Evidence gate

The planner requires all of the following inputs together:

1. the sealed retail visual closure;
2. the sealed static, hierarchical, and animated prop plans;
3. the effective-assets manifest and the private effective-assets directory;
4. the cooked Fords map object document;
5. the independent unresolved-object census.

It verifies the digest chain between the three upstream plans and requires the
same target history at each handoff: static rejects the hierarchy/animation,
hierarchical rejects the animation, and animated rejects it specifically
because the embedded animation has no key channel. It also requires that no
upstream resource or model binding already owns the target.

Fresh source bytes are then checked against the manifest. Fresh W3D metadata
must agree exactly with the census for mesh, model, hierarchy, pivot,
animation, model-reference, file-header, chunk-kind, and warning rows. The
single map placement must still be record 974 with unique ID
`OrcMeatRack01 551`. The exact embedded texture is read and hash-checked too.

The resulting standalone fragment has two resources and two source patterns:

- one texture conversion resource;
- one hierarchical model conversion resource;
- one model binding covering one placement;
- zero emitted animation clips.

Both the plan and generated profile are payload-free JSON. Retail and converted
payloads remain under `.private`.

### Focused verification

```powershell
python -m pytest -q importer/tests/test_retail_no_motion_prop_profile.py importer/tests/test_w3d_no_motion.py importer/tests/test_w3d_no_motion_pipeline.py
python -m ruff check importer/openbfme_importer/retail_no_motion_prop_profile.py importer/tests/test_retail_no_motion_prop_profile.py
python -m ruff format --check importer/openbfme_importer/retail_no_motion_prop_profile.py importer/tests/test_retail_no_motion_prop_profile.py
```

The integration owner should merge `profileFragment.resources` and
`profileFragment.objectBindings.models` only after checking for exact source,
resource-ID, output, and binding collisions. The shared composer remains the
authority for replacing the prior unresolved binding; this planner never
mutates a base profile or publishes a pack itself.

---

<!-- merged from docs/RETAIL_PARTICLE_DEFINITIONS.md -->

## Retail particle-definition parser

`openbfme_importer.sage_particles` is the bounded lexical bridge for the two
BFME2 particle-definition families:

- `ParticleSystem`: usually a flat sequence of scalar assignments.
- `FXParticleSystem`: nested sections, including assignment-shaped section
  headers whose selector chooses the SAGE section implementation.

The parser does not interpret or convert parameters. It preserves authored
entry order, nested structure, scalar text after comment removal, and exact
source spans/hashes so later conversion work can be evidence-driven.

### API

```python
from openbfme_importer.sage_particles import (
    parse_particle_definitions,
    select_particle_definition,
)

definitions = parse_particle_definitions(source_bytes)
record = select_particle_definition(
    definitions,
    "RequestedParticleId",
    kind="ParticleSystem",
)
```

Parsing all definitions intentionally retains duplicate evidence. Selecting a
single named definition is case-insensitive and fails if it is missing or
ambiguous. Supplying `kind` disambiguates the legacy and FX families, but never
disambiguates duplicate records within one family.

`ParticleDefinition.entries` retains the authored stream of
`ParticleAssignment` and `ParticleBlock` records. `assignments(recursive=True)`
and `blocks(recursive=True)` provide deterministic depth-first traversal.

### Provenance and containment

Every definition and nested block records inclusive line numbers, half-open
byte offsets, byte length, and SHA-256 of the exact raw span. Assignments carry
the same provenance for their source line. Returned records never contain
source bytes or host filesystem paths.

Retail values are still retail data. Private proof reports may serialize only
schema, counts, hashes, and field/block-name summaries. They must not serialize
assignment values or source excerpts. Retail inputs and proof artifacts remain
under `.private`.

### Fail-closed limits

The parser rejects non-byte input, oversized documents/lines/values/counts,
NUL or control bytes, unsupported encoding, unsafe identifiers and field names,
empty assignments, malformed quotes, unknown top-level input, bad indentation,
unbalanced `End`, excessive nesting, and unterminated definitions. Comment
markers inside quoted values are preserved; comment markers outside quotes are
removed before lexical parsing.

The parser is intentionally not a general SAGE INI interpreter. Expanding its
grammar should be driven by a contained retail proof and legal-safe regression
fixture, not by permissive fallback behavior.

---

<!-- merged from docs/RETAIL_FORDS_PARTICLE_PROFILE.md -->

## Fords particle/effects profile

`retail_particle_profile.py` is the exact private planning boundary for the
particle-bearing objects still needed by the Fords of Isen II slice:

- `CaveTrollLair`: 2 placements;
- `Inn`: 2 placements;
- `WargLair`: 4 placements;
- `WtrRiplsSmall`: 7 placements.

It consumes the sealed unresolved-object census, the effective-assets manifest,
the retail-binary particle-family oracle, and the matching private effective
tree. It does not publish a pack or select a content pack. Retail and converted
payloads remain below `.private`.

### Exact output

Against the current BFME2 1.06 census, the planner emits:

| Output | Count |
|---|---:|
| Exact target types | 4 |
| Exact Fords placements | 15 |
| Particle-system identifiers | 10 |
| `ParticleSystem` definition resources | 7 |
| `FXParticleSystem` definition resources | 10 |
| Total `sage-particle-definition` resources | 17 |
| Deduplicated exact texture-to-PNG resources | 9 |
| Hash-only `WtrRiplsSmall` W3D anchor resources | 1 |
| Total ImportProfile resources | 27 |
| Direct `ParticleSysBone` attachments | 11 |
| Object-to-FX-list roots | 13 |
| Unique FX lists | 4 |

Every definition resource has one exact source pattern, `limit=1`,
`expected_count=1`, and exact `kind` plus `name` options. The importer therefore
serializes only the selected definition block, not the full retail INI. Every
texture is converted once per exact physical source. The ripple anchor is
`art/w3d/p_/p_wtrriplssmall.w3d`, retained as a hash-only source dependency, and
its authored attachment bone is `waterRippleBone`.

The generated standalone profile writes the binding contract at
`effects/fords-particle-bindings.json`. That document records exact type names,
placement counts, attachment bones, attachment options, state/condition tokens,
FX-list triggers, definition candidates, texture resource IDs, source spans, and
hashes.

### Family-resolution finding

Seven identifiers have both a legacy and an FX definition:

- `BuildingContructDust`;
- `PCTMediumDust`;
- `RDTMediumExplosion`;
- `RDTMediumExplosionLight`;
- `SmokeBuildingLarge`;
- `SmokeBuildingMediumRubble`;
- `WaterRipplesSmall`.

The private BFME2 1.06 retail-binary oracle establishes the strongest honest
runtime contract now available:

- Object Draw `ParticleSysBone` and FXList `ParticleSystem` consumers call the
  same name lookup in global `0xDFDD04`, registered as
  `TheFXParticleSystemManager`;
- consumer references carry an unqualified system name, so there is one proven
  runtime namespace rather than per-family consumer namespaces;
- repeated declarations using `FXParticleSystem` syntax are proven
  last-definition-wins in retail;
- whether the legacy subsystem is active and which declaration wins a
  cross-family duplicate remain unresolved.

Both authored definitions and their family/source provenance are therefore
always retained. Six duplicate identifiers still carry this runtime resolution:

```json
{
  "status": "unresolved-cross-family-precedence",
  "selectedKind": null
}
```

`WaterRipplesSmall` is the sole explicit provisional selection:

```json
{
  "status": "provisional-explicit-runtime-selection",
  "selectedKind": "FXParticleSystem",
  "crossFamilyPrecedenceProven": false,
  "generalizesToOtherDuplicateIdentifiers": false,
  "visibleFieldsMateriallyEquivalent": true,
  "materialDiscriminator": "priority/culling"
}
```

That choice is bounded to the ripple: retail explicitly registers the FX
manager and the two ripple definitions have materially equivalent visible
fields, differing at priority/culling (`CRITICAL` versus
`VERY_LOW_OR_ABOVE`). It is not a retail precedence proof and must not be copied
to the other six duplicates. The plan summary therefore records exactly one
provisional runtime selection and six unresolved duplicate selections.

For an ID present in only one family (`BuildingDamaged`, `UntamedAllegiance`, and
`UntamedAllegiance2`), the sole exact authored family is recorded without making
a collision decision.

An authoritative close still requires a controlled retail `-mod` A/B oracle
that changes one family at a time with an unmistakable visible discriminator.

### Fail-closed checks

The planner rejects the input or private tree when any of these drift:

- census schema, version, or aggregate digest;
- effective-manifest schema, aggregate, identities, totals, or path case;
- census/manifest identity linkage;
- oracle source hashes/sizes, BFME2 executable version, claim grades, binary
  manager/lookup addresses, Water probe, or converter guidance;
- exact four target names and Fords placement counts;
- direct attachment order, bone, options, state family, or condition tokens;
- FX-root order, stage, state family, or condition tokens;
- definition family/source pairing;
- definition block line range, byte length, SHA-256, scalar-assignment count, or
  nested-block count;
- `ParticleName` order or exact compiled-texture stem resolution;
- FX-list block line range, length, hash, system edges, audio edge, or view shake;
- object-definition, particle-definition, FX-list, texture, or W3D source bytes;
- ripple W3D identity or anchor binding;
- generated resource IDs, outputs, or ImportProfile validity.

The private read boundary is included in the sealed report as paths, sizes, and
hashes only. No source bytes are included in the tracked planner or test fixture.

### Composer integration

Merge `profileFragment.resources` into the full profile and merge
`profileFragment.runtimeData` at `profileFragment.runtimeDataPath`.

There is one intentional source-pattern reuse rule: the seven legacy definition
resources all read `data/ini/particlesystem.ini`, and the ten FX definition
resources all read `data/ini/fxparticlesystem.ini`. This is required because the
strict converter selects one named block per resource. A composer may admit this
reuse only when all of the following are true:

- converter is exactly `sage-particle-definition`;
- source pattern is the one source required by the declared family;
- each `(kind, name)` pair is unique;
- every output JSON path is unique;
- the plan aggregate and source evidence remain valid.

Do not weaken the general duplicate-pattern check for other converters.

The binding document is an exact conversion handoff, not a claim that Godot
already renders the effects. The remaining runtime work is:

- implement the normalized particle-definition interpreter;
- run the family-isolated retail A/B experiment before selecting any of the six
  unresolved duplicate identifiers or treating the ripple choice as general;
- translate the four FX-list nugget parameters, not only their exact system/audio
  edges and hashed source spans;
- attach emitters to the named bones and lifecycle conditions;
- compare rendered timing, scale, blend, color, and lifetime against the retail
  game oracle.

No generic smoke, dust, or ripple fallback is acceptable in private parity mode.

### Focused verification

```powershell
$env:PYTHONPATH = "importer"
python -m pytest importer/tests/test_retail_particle_profile.py -q
python -m pytest importer/tests/test_sage_particles.py `
  importer/tests/test_sage_particle_pipeline.py -q
python -m ruff check importer/openbfme_importer/retail_particle_profile.py `
  importer/tests/test_retail_particle_profile.py
```

The real private plan can be produced without changing the shared composer:

```python
import json
from pathlib import Path

from openbfme_importer.retail_particle_profile import (
    build_retail_fords_particle_plan,
    generated_import_profile,
    write_generated_import_profile,
    write_retail_fords_particle_plan,
)

census = json.loads(
    Path(".private/scratch/fords-unresolved-census/census.json").read_text()
)
manifest = json.loads(
    Path(
        ".private/retail-work/cache/effective-assets/.openbfme/manifest.json"
    ).read_text()
)
oracle = json.loads(
    Path(".private/scratch/particle-family-oracle/evidence.json").read_text()
)
root = Path(".private/retail-work/cache/effective-assets")
plan = build_retail_fords_particle_plan(census, manifest, oracle, root)
profile = generated_import_profile(plan)
write_retail_fords_particle_plan(
    ".private/scratch/fords-particle-profile/plan.json", plan
)
write_generated_import_profile(
    ".private/scratch/fords-particle-profile/profile.json", profile
)
```

---

<!-- merged from docs/RETAIL_ROAD_CONVERSION.md -->

## Retail Road conversion

`road-closure` is the bounded conversion boundary for SAGE `Road` records. It
reads the winning `data/ini/roads.ini` from an extracted effective-assets tree,
selects only explicitly requested Road IDs, and reports the exact physical
texture leaves needed by a Godot-side road renderer. The report contains hashes
and virtual paths, never retail payload bytes or an absolute source root.

### Run it

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

### Exactness contract

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

### Fords of Isen II findings

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

### Map wire pairing contract

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

---

<!-- merged from docs/RETAIL_ROAD_PROFILE.md -->

## Retail Road profile generation

`retail_road_profile.build_road_profile` is the fail-closed bridge from a
validated `openbfme.retail-road-closure` report to a complete private importer
profile. It does not rescan `roads.ini`, search for textures, or choose a
substitute. It preserves the base profile and adds only the conversion inputs
and runtime facts proven by the report.

### Contract

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

### Runtime document

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

### Generate the Fords private profile

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

### Focused acceptance test

```powershell
$env:PYTHONPATH = 'importer'
.private\retail-work\tools\python-3.12-env\Scripts\python.exe -m unittest `
  importer.tests.test_retail_road_profile -v
```

The fixture covers determinism, base preservation, exact five-Road output,
shared-DDS deduplication, schema-valid profile loading, and every fail-closed
boundary listed above.

---

<!-- merged from docs/RETAIL_ROAD_RUNTIME.md -->

## Retail Road runtime

The Fords runtime consumes only the private cook's exact `roads.json` and
`road-materials.json`. It does not search for similarly named definitions or
textures and it does not manufacture a fallback road when a retail dependency
is missing.

### Trust boundary

`RetailMapData` requires the map's `roadMaterials` reference to remain inside
the selected map and pack. It validates:

- `openbfme.sage-road-materials` schema version `0`;
- exact Road ID membership and order against `roads.json`;
- the 64-character source closure aggregate;
- canonical positive `RoadWidth` and `RoadWidthInTexture` values within the
  profile's bounds;
- safe pack-relative PNG paths and decodable PNG payloads; and
- each PNG's byte length and SHA-256 against the exact path in
  `provenance/manifest.json` (`openbfme.retail-import-provenance-v1`).

The GPU builder re-reads and re-hashes each PNG after map validation. A file
changed between validation and material creation therefore fails closed. Road
materials preserve the retail alpha channel, use alpha blending, cull neither
side, depth-test without writing depth, repeat texture coordinates, and use
anisotropic mipmapped filtering.

### Fords topology evidence

The 142 wire records form 71 exact Start (`2`) to End (`4`) source edges. They
are not 71 unrelated quads. Exact endpoint identity produces:

- 120 unique endpoint positions;
- 21 shared nodes (20 degree-two and one degree-three across Road IDs);
- 18 nodes with exactly two edges of the same Road ID and therefore 18 curve
  candidates;
- no same-template degree-three/four crossing candidate; and
- no Angled, TightCurve, EndCap, bridge, or unknown modifier flag.

Applying OpenSAGE's `MinCurveAngle`, broad-curve radius, tangent length, and
overlap tests in source order generates 11 BroadCurve nodes and leaves seven
straight/angled fallbacks. The generated edge pairs are:

```text
2/3, 7/8, 11/12, 14/15, 19/20, 21/22,
24/25, 27/28, 34/35, 41/42, 50/51
```

The fallback pairs are:

```text
0/1, 28/29, 32/33, 46/47, 62/63, 66/67, 69/70
```

Six fall below the minimum curve angle. Pair `28/29` passes the angle test but
its computed tangent exceeds the available edge length. The battlefield keeps
per-node evidence (source node, edge pair, angles, source-unit tangent and edge
lengths, rejection reason, and generated strip count) so future conversions
can be audited without guessing from a rendered frame.

### Meshing rules

The runtime follows the proven OpenSAGE road rules needed by this map:

- connected edge orientation is aligned before UV generation;
- physical half-width is
  `RoadWidth * RoadWidthInTexture / 2`, then scaled by the map's uniform local
  transform;
- the straight atlas uses a longitudinal repeat length of `RoadWidth * 4` and
  the authored transverse span centered on `0.166` with half-span
  `0.125 * RoadWidthInTexture`;
- BroadCurve geometry is split into 30-degree atlas strips with the source
  overlap rule and uses the BroadCurve atlas crop;
- centerlines are sampled approximately every ten source world units;
- a cross-section is raised to the maximum sampled terrain height across its
  full width plus the source `0.1` height bias; and
- interior vertex pairs survive only when their height differs from linear
  neighbor interpolation by more than the source `0.001` threshold.

Consequently, a fixed four-vertices-per-edge assertion is incorrect. The
current exact Fords cook produces 24 curve strips and 95 total render strips,
with 534 adaptive vertices and 344 triangles across five retail-textured mesh
instances. The focused gate also proves the general identity
`triangles = vertices - 2 * render_strips` rather than relying only on those
snapshot counts.

Unsupported same-template crossings or nonzero Road modifier flags remain a
hard failure. They must be implemented from source evidence before a future
map containing them can be called source-driven.

### Focused acceptance

Point the runner directly at an audited, non-published private pack; it does
not depend on `ContentDB` selection order:

```powershell
$env:OPENBFME_ROAD_PACK = (Resolve-Path `
  '.private\retail-work\packs\bfme2-men-vslice-roads-private').Path

& 'C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe' `
  --headless --path game --script res://tests/retail_road_visual_runner.gd
```

The verified result is `27/27`, with no Godot `ERROR` or `WARNING`. The
integrated retail-slice runner independently requires the same material,
topology, curve-evidence, and adaptive-mesh facts once the integration owner
publishes/selects the combined audited pack.

---

<!-- merged from docs/RETAIL_ARCHER_PROJECTILE_PROFILE.md -->

## Retail Gondor Archer projectile profile

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

### Exact closure

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

### Generate and verify

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

---

<!-- merged from docs/RETAIL_W3D_NO_MOTION.md -->

## Source-proven header-only W3D animations

`openbfme_importer.w3d_no_motion` provides a deliberately narrow transformer
for retail W3Ds that contain an animation header but no motion data. It is not
an animation approximation and it is not currently integrated into the retail
pipeline.

### Accepted contract

`strip_proven_header_only_animations` accepts caller-owned W3D bytes, a safe
virtual `.w3d` path, and one exact `W3DNoMotionExpectation` for every animation
container in the file:

```python
from openbfme_importer.w3d_no_motion import (
    W3DNoMotionExpectation,
    strip_proven_header_only_animations,
)

result = strip_proven_header_only_animations(
    source,
    virtual_path="art/w3d/example.w3d",
    expectations=(
        W3DNoMotionExpectation(
            identifier="EXAMPLE",
            hierarchy_identifier="EXAMPLE",
            frame_count=101,
            frame_rate=30,
            compressed=False,
            model_identifier="EXAMPLE",
        ),
    ),
)
transformed = result.output_bytes()
proof = result.proof.neutral()
```

For a compressed animation, set `compressed=True` and provide its exact
16-bit `flavor` value. Frame count and frame rate must be finite positive
integers and must match the source header exactly.

The transformer succeeds only when all of the following are true:

- every raw (`0x00000200`) or compressed (`0x00000280`) animation container is
  top-level, selected exactly once, and carries the W3D container flag;
- each selected container contains exactly one 44-byte header of the matching
  kind and no other child;
- no raw channel, bit channel, compressed channel, compressed bit channel,
  compressed motion channel, morph animation, nested animation, or orphaned
  animation-related chunk exists anywhere else in the file;
- identifier, hierarchy identifier, frame count, frame rate, compression mode,
  and flavor match the explicit expectation with exact spelling;
- embedded hierarchy and HLOD model headers exactly match the expected
  hierarchy/model bindings, without duplicate or extra headers; and
- rescanning the output proves zero remaining animation headers and unchanged
  model, hierarchy, pivot, model-reference, and mesh identity records.

An unsupported or ambiguous source raises `W3DNoMotionError` before transformed
bytes are returned. The implementation has fixed source-size, chunk-count, and
nesting-depth limits.

### Mutation and proof boundary

The mutation is only a concatenation of the original top-level byte ranges
after omitting the proven containers. No retained chunk is decoded, rewritten,
or size-repaired. The canonical payload-free proof records input/output hashes
and lengths, removed container hashes/counts/bytes, exact header metadata,
identity hashes/counts, and its own `proofSha256`. It contains no W3D payload or
filesystem source path.

### PMMEATRACK01 retail backtest

The private proof at `.private/scratch/w3d-no-motion/REPORT.json` seals the
effective BFME2 source `art/w3d/pm/pmmeatrack01.w3d`:

- input: 32,466 bytes,
  SHA-256 `7f07ac4f7a8eb3812e06353a3ea54b9bebc0d5ccfef22893bf643e44b66cb0bf`;
- removed: one 60-byte raw animation container containing only the
  `PMMEATRACK01` header, bound exactly to hierarchy/model `PMMEATRACK01`, at
  101 frames and 30 fps;
- output: 32,406 bytes,
  SHA-256 `5fc73634b0c7506b8a662089eb2371bd3b2b906959a418493d6a169971923211`;
- output rescan: zero animation headers, zero warnings, and unchanged model,
  hierarchy, pivot, model-reference, and mesh identities; and
- proof digest:
  `9931d8209cf6c22bec97bd6dac340cee295f9198b51d829486521e4173e58fe8`.

The private transformed W3D remains under
`.private/scratch/w3d-no-motion/pmmeatrack01.no-motion.w3d`; neither it nor its
source is tracked.

### Verification

Run the focused legal-safe suite and style checks:

```powershell
$env:PYTHONPATH = "importer"
.private\retail-work\tools\python-3.12-env\Scripts\python.exe `
  -m unittest importer.tests.test_w3d_no_motion -v
ruff check importer\openbfme_importer\w3d_no_motion.py `
  importer\tests\test_w3d_no_motion.py
ruff format --check importer\openbfme_importer\w3d_no_motion.py `
  importer\tests\test_w3d_no_motion.py
```

---

<!-- merged from docs/RETAIL_W3D_EMPTY_ANIMATION_OUTPUT.md -->

## Retail W3D empty animation output

### Scope

This repair is limited to the Blender-side W3D adapter boundary. It does not
turn a missing animation into a static asset and does not synthesize animation
channels.

### Exact retail evidence

The generated completion profile resource
`men-fortress-door-closed-source` requests one self-contained W3D bundle:

- model: `gbfdoor_drc.w3d`
- animation: `gbfdoor_drc.w3d`
- output: `assets/models/structures/men-fortress/door-closed.glb`

The generated plan resolves it to `art/w3d/gb/gbfdoor_drc.w3d` in `w3d.big`,
offset `369627071`, size `6300`. The prepared source SHA-256 is
`f82cc85ba9d7e19d2f676bc230249942ac948b37c656ab8b13979723060177f8`.

Retail lifecycle evidence identifies `GBFDoor_DRC` as the default, damaged,
and really-damaged closed-door model. The separate `GBFDoor_DRCA` source owns
the `DOOR_1_CLOSING` animation (`GBFDoor_DRCA.GBFDoor_DRCA`). Therefore a
fabricated transform animation on `GBFDoor_DRC` would contradict the source.

The pinned importer seals `GBFDoor_DRC` as:

- one logical clip named `gbfdoor_drc`
- two owned actions: one object and one armature action
- shape `visibility-only`
- zero transform, material, and unsupported curves
- three visibility curves with three total keys
- zero NLA transform tracks

Blender consequently exports the skeletal door geometry without a glTF
`animations` array. That is truthful output, not evidence of missing transform
motion.

### Fail-closed rule

An absent glTF `animations` array is accepted only when every captured source
action shape is sealed as `visibility-only`, has zero transform/material/
unsupported curves, and has a non-empty exact visibility-channel payload whose
channel count matches the sealed shape. Those exact keys are retained in the
existing `openbfme.w3d-visibility-only-animations` root extras contract.

If any sealed shape requires a transform animation, absence of the glTF array
still raises `animation GLB has no required transform animations`. An empty or
inconsistent visibility proof also fails. No animation object, channel,
sampler, key, or motion is fabricated.

### Direct reproduction

The focused scratch reproduction uses copies of the prepared job input and the
pinned OpenSAGE plugin under `.private/scratch/w3d-door-closed-repair`; it does
not mutate or smoke-test the pinned plugin checkout.

```powershell
& .private/retail-work/tools/blender-4.2.0-windows-x64/blender.exe `
  --factory-startup -noaudio --background --python-use-system-env `
  --python-exit-code 1 --python importer/blender/w3d_to_glb.py -- `
  --plugin-root .private/scratch/w3d-door-closed-repair/plugin/OpenSAGE.BlenderPlugin `
  --model .private/scratch/w3d-door-closed-repair/input/gbfdoor_drc.w3d `
  --asset-kind animated `
  --output .private/scratch/w3d-door-closed-repair/repaired.glb `
  --animations .private/scratch/w3d-door-closed-repair/input/gbfdoor_drc.w3d `
  --required-equipment --excluded-optional-meshes
```

The repaired GLB is `831700` bytes with SHA-256
`08728c9240cba89c8b70f4206e37a10e51b9755fbefb95f97d16e2566c3de2ea`.
Three isolated conversions produced this same byte-for-byte digest.
It has two meshes, one skin, five nodes, no `animations` member, and one exact
visibility-only sidecar containing three channels and three keys. The adapter
report records zero exported animations/channels/samplers, two skeletal mesh
nodes, 32 triangles, and 56 vertices.

### Release locks

- Blender executable SHA-256:
  `80fb653019a0afb3bda0947ec74e84dc0a94d0d388f9b3849433c0e1a4efdabe`
- Blender tree SHA-256 required by bootstrap:
  `81e0cfb0d56ff5e33c2c562b13cc88257b9b34e072efa7ae054a6c87f13f2aa4`
- OpenSAGE plugin commit:
  `2de84023cb632a79a853b2a52f97c8002ed85142`
- Generated completion profile SHA-256:
  `cc5af254e0787cf135bd1cf8574b94dd19991741a6eda6ccc346aa304b78c588`
- Repaired adapter SHA-256:
  `3fba8cc0a3cfdb785b126a3e3bbe2de624dbc260661a504fd1c663fb2bcd2e28`
- Coordinator validator SHA-256:
  `7a2967bb9b325a7f1c2f8f49391565b4a36878392e3f19f7c6aeac2c08e3cbc9`
- Focused coordinator test SHA-256:
  `1f5b8b231a035d648f98a1625f3ea2495e9b6fad1a2bd9706d7af8891f5fb85f`

Focused verification:

```text
python -m pytest importer/tests/test_w3d_action_shapes.py importer/tests/test_w3d_pipeline.py -q
29 passed, 48 subtests passed

python -m ruff check importer/openbfme_importer/pipeline.py importer/tests/test_w3d_pipeline.py
All checks passed!

python -m py_compile importer/openbfme_importer/pipeline.py importer/tests/test_w3d_pipeline.py
PASS
```

The coordinator now accepts embedded/split core counts of zero animations, zero
channels, and zero samplers only when the sealed action shapes require zero
transform animations. Skin and skeletal-mesh proof remain mandatory. If any
transform animation is expected, its exact animation count and non-empty
channel/sampler proof remain mandatory.

The mixed CaptureFlag case is independently locked: `capflag_dn`,
`capflag_sup`, and `capflag_up` require three exported transform clips, while
`capflag_sdn` is the one visibility-only sidecar. The coordinator continues to
require `3 + 1 == 4` requested logical animations, all 36 visibility channels
and keys, 81 emitted core channels/samplers, and the skeletal export proof.

---


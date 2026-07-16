# Retail visual conversion closure

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

## Run it

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

## Exact-resolution policy

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

## Report contract

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

## Godot conversion order

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

## Men/Fords integration note

Current project evidence shows the primary Men roster and structure conversions
are present enough to exercise the private slice, while high-count prop binding
and complete building construction/damage/rubble presentation remain open
scoreboard items. UI/audio coverage and all-unit production/AI behavior are
separate gates. Run this command over the exact roster and structure Object IDs
to replace broad filename inventories with a reproducible conversion closure,
but do not describe the slice as 1:1 until the `DIRECTION.md` visual and play
checklists and the retail pipeline gate are all green.

### Current 72-type Fords closure evidence

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

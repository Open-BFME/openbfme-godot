# Fords neutral lifecycle conversion plan

`retail_neutral_lifecycle_profile.py` generates the private conversion batch for
the eight neutral lifecycle structures currently placed on Fords of Isen II:

- two `CaveTrollLair` placements;
- two `Inn` placements; and
- four `WargLair` placements.

The planner consumes the sealed unresolved-object census, the exact neutral
simulation-facts handoff, and the effective-assets tree. It verifies every
selected file against the census or simulation source evidence before emitting a
plan. It neither publishes retail data nor changes the shared slice profile.

## Exact output contract

The current contract is intentionally pinned:

| Item | Exact count |
|---|---:|
| Lifecycle structure types | 3 |
| Fords placements | 8 |
| GLB conversion resources | 22 |
| Unique W3D inputs | 24 |
| Model/weather texture inputs | 22 |
| Audio sample inputs | 40 |
| Total profile resources | 84 |
| Normalized lifecycle/model states | 26 |
| Source-authored no-render phases | 6 |

Each texture and audio leaf owns one exact pattern in one `hash-only` resource.
This is deliberate: a downstream composer can reuse an identical owner or reject
a collision without having to split a partially overlapping group. W3D resources
depend only on the exact one-source texture resources used by that conversion.
Snow replacements remain in the closure as evidence, but are not attached to the
normal-weather Fords conversions.

The twenty-two GLBs are:

- CaveTrollLair: construction, intact, damaged, really damaged, collapse, bib,
  and the `CaveTrollLairHole` visible rebuild-hole rubble;
- Inn: construction, intact idle, damaged, really damaged, collapse, rubble,
  post-rubble, bib; and
- WargLair: construction, intact, damaged, really damaged, collapse, bib, and
  the `WargLairHole` visible rebuild-hole rubble.

CaveTrollLair and WargLair retain the original object's authored `RUBBLE`,
`POST_RUBBLE`, and `POST_COLLAPSE` no-render conditions. They also spawn distinct,
visible rebuild-hole objects: `NBTrollLair_R` and `NBWargLair_R`. Each hole has
500 HP, zero health regeneration, fades in over exactly 2.0 seconds, and persists
until rebuild or explicit destruction. This preserves both objects instead of
replacing a no-render condition with an invented body state. Inn keeps the source
distinction between `POST_RUBBLE` (`GBGenRubble`) and `POST_COLLAPSE` (`NBInn_R`)
without converting a duplicate GLB.

## Generate the private plan

From the repository root with the private importer environment:

```powershell
$env:PYTHONPATH = "importer"
& .private\retail-work\tools\python-3.12-env\Scripts\python.exe `
  -m openbfme_importer.retail_neutral_lifecycle_profile `
  .private\scratch\fords-unresolved-census\census.json `
  .private\scratch\neutral-simulation-facts\handoff.json `
  .private\retail-work\cache\effective-assets `
  .private\scratch\neutral-lifecycle-profile\plan.json `
  --profile-output .private\scratch\neutral-lifecycle-profile\profile.json
```

The standalone profile is parsed through the real `ImportProfile` loader during
generation. The plan also records a canonical digest and a selected-source
aggregate, but never copies retail bytes into tracked files.

## Integration contract

`profileFragment.resources` can be merged into the composed private profile after
normal source-owner collision checks. `profileFragment.objectBindings.structures`
contains exactly three explicit records with:

- the source `typeName`;
- the intact `sourceVirtualModel`;
- the intact GLB path;
- the runtime `objectId`; and
- `matchMethod: exact-type-name`.

These bindings are lifecycle structures, not generic prop bindings. Map cooking
must route all eight placements to the structure path and must not also emit them
as ordinary GLB props or unresolved markers.

`structureLifecycles` is presentation metadata for lifecycle schema version 1. It
preserves source conditions, GLB/no-render mode, clip mode, next phase, audio event
IDs, direct particle attachment IDs, entering/collapse FX-list IDs, direct health
and threshold facts, and the separate rebuild-hole object states. Animation
completion is never simulation authority.

The proven health contract is:

| Type | Maximum health | Damaged threshold | Really-damaged threshold |
|---|---:|---:|---:|
| CaveTrollLair | 2000 | 1000 | 500 |
| Inn | 3000 | 2000 | 1000 |
| WargLair | 2000 | 1000 | 500 |

All eight map placements are proven to start at 100% health in the intact phase.
All three bibs are unconditional `W3DFloorDraw` modules: `StartHidden` is not
authored and `HideIfModelConditions` is empty, so `duringConstruction` is `true`.

## Deliberate blockers

The direct maximum-health and threshold fields are now bound. The SAGE-ancestor
inclusive threshold convention remains qualified until a BFME2 executable/oracle
check, rather than being silently treated as BFME2 proof.

Two death-timing gaps remain deliberately unassigned:

- BFME2's exact Cave/Warg `StructureCollapseUpdate` completion frame and original
  object removal ordering are not proven. The qualified Generals derivation is
  retained as evidence but is not promoted to runtime truth.
- Inn has no `StructureCollapseUpdate` and omits `KeepObjectDie.CollapsingTime`.
  Its D3 reachability and exact rubble/post-rubble timing remain blocked.

The separate exact particle plan now converts the selected definitions. This
standalone neutral plan still needs completion-composer cross-selection to attach
those converted definitions to its particle and FX-list IDs. The exact
Inn-to-CaptureFlag engine association rule remains unproven; the current pairs are
spatial/ID-adjacency evidence only.

Therefore this plan proves source selection and conversion intent, not completed
1:1 runtime parity. Integration still needs:

1. BFME2 oracle/executable proof for the two death-timing gaps above;
2. completion-composer handoff to the implemented and tested Godot lifecycle-v1
   route, including separate rebuild-hole objects;
3. cross-plan selection of the already-converted exact particle definitions;
4. real GLB conversion reports and Godot containment/preflight; and
5. rendered/audio comparison against the original game.

## Focused verification

```powershell
$env:PYTHONPATH = "importer"
python -m pytest importer/tests/test_retail_neutral_lifecycle_profile.py -q
python -m ruff check `
  importer/openbfme_importer/retail_neutral_lifecycle_profile.py `
  importer/tests/test_retail_neutral_lifecycle_profile.py
python -m ruff format --check `
  importer/openbfme_importer/retail_neutral_lifecycle_profile.py `
  importer/tests/test_retail_neutral_lifecycle_profile.py
```

The tests use repository-authored fixture bytes. They cover deterministic output,
the 22-from-24 model contract, one-source evidence ownership, `ImportProfile`
parsing, explicit no-render plus rebuild-hole states, exact health/bib facts,
census/simulation tampering, source-byte tampering, audio route tampering, and
semantic plan tampering.

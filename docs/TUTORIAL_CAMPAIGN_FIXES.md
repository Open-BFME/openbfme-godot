# Shared tutorial and campaign script fixes

This ports missing shared mission behavior onto upstream main, rather than
replacing the tutorial/campaign framework already supplied by upstream PR #137.
There are no map-name checks in the implementation: other campaign missions and
mods using the same actions or drawable shadow fields use these paths too.

## Behavior and editions

Both BFME2 and RotWK use the shared script/action and shroud paths; BFME1 keeps
its existing RotWK-rules compatibility mode (S-5017). No parser/table/startup
formats are changed. These fixes do not claim complete tutorial progression.

- `MAP_REVEAL_AT_WAYPOINT`, `MAP_REVEAL_IN_TRIGGER`, and their named permanent
  and undo forms now affect authoritative shroud instead of merely queuing an
  unhandled presentation request. They preserve player masks and overlapping
  lookers. Named records participate in hashing and OpenBFME snapshots.
- Playable campaign/tutorial loads draw unexplored shroud; bare viewers retain
  their explicit opt-out. This does not change `UseShroud` gameplay settings.
- Position-targeted DOZER_CONSTRUCT buttons use the normal dozer construction
  path, with AI idle and angle zero. This is in Object::doCommandButtonAtPosition,
  so named/team script commands retain upstream's normal command lookup,
  disabled gate, other abilities, and unsupported-command reporting.
- Explicit-size additive drawable shadow textures create terrain-only projected
  markers. Nodes follow visibility/position and are removed with the drawable.
  Godot node/resource work stays outside upstream's worker refresh lane.

## Sources and limits

Construction: RW 0x6979D9 / 0x697A57, script entry RW 0x7C6363;
BFME2 Object.cpp 0x297149, BuildAssistant.cpp 0x3952D8 and
ScriptActions_Rva003C57A9Do.cpp 0x3C58C9. The non-dozer buildObjectNow
branches remain S-5305, not silent substitutes.

Regional reveal donor: BFME2 ScriptEngineNamedMapReveal.cpp
0x3577DA/0x35789D, ScriptEngineRva00358853.cpp, Rva003BC9E0Do.cpp,
Rva003BCA12Do.cpp, Rva003BCA43Do.cpp, Rva00739BE0.cpp and
Rva00739AF0Siblings.cpp. Upstream identifies the RotWK trigger action at
RW 0x7C0BCF and reveal/unlook at RW 0xB4DCA0 / 0xB4DE50. Exact
cross-edition wrapper and polygon-edge equivalence remains explicitly
unverified (S-6102); this is not a retail-parity claim.

The reveal raster uses the existing deterministic grid conversion and circle
shape. Trigger cells use the existing point-in-trigger test at cell centers;
precise retail edge coverage remains S-6102. Each regional action reports that
stop. Named reveals use snapshot version 7, appended after upstream's version-6
attack-priority state; unchanged games retain version 5/6 writes and older
versions remain readable. These are OpenBFME save blocks, not retail formats.

Decal donor: BFME2 W3DShadowManagerAddShadow.cpp routes shadow types 0x40/0x800
to projected shadows; W3DProjectedShadowManagerFlushDecals.cpp selects additive
rendering. Godot emission is a device adaptation. Exact blending/projection,
ShadowOpacity timing, rotated offsets, automatic sizing, and per-pixel shroud
edges remain S-3304 and are exposed on each created node. No retail texture is
committed; textures are read from the mounted installation.

## Checks

Synthetic checks cover circle overlaps/player masks, concave trigger exclusion,
one-time exploration, named undo, and combined script/shroud snapshot round trips.
Mounted-data checks exercise ordinary construction and the tutorial's real
porters; the presentation check verifies black unexplored terrain, the opening
reveal's terrain texture, and marker creation/movement/removal. It accepts an
optional `SPOTLIGHT_TEST_SHOT` output path for a rendered capture.

Run from the repository root with the native extension built:

```sh
engine/build/openbfme_tests --test-case='shroud*,save4d*,mods3 retail*'
godot --headless --path godot --script res://tests/tutorial_builders_test.gd
godot --path godot --script res://tests/mission_script_presentation_test.gd
```

Runtime prerequisites and actual validation results are reported in the PR;
retail-dependent skips are not evidence that the retail scenarios passed.

### Validation on the upstream-based branch

- Release ARM64 native extension and test runner built successfully.
- Synthetic script/shroud/save/command suite: 162 cases, 4,713 assertions passed;
  mounted-data cases explicitly skipped in that run.
- With BFME2 1.06 and RotWK 2.01 archives present: 161 of 162 cases passed,
  36,188 of 36,189 assertions passed. The unchanged script4
  PLAYER_HAS_SCIENCEPURCHASEPOINTS assertion failed in the combined run, but
  its isolated case passed all 36 assertions. Suite-order investigation is
  still required; the broad suite is not claimed green.
- BFME2 edition setting: three focused regional-reveal/shroud/save cases,
  52 assertions passed with mounted data.
- Both Basic tutorial porters constructed and completed their authored towers.
  Running the construction check against the pre-change upstream extension
  fails because neither tower is created.
- Opening-reveal/terrain-texture and marker lifecycle checks passed headless
  and windowed, with the rendered before/after captured outside Git.
- BFME1 mounted-data setup/shell check was exercised. Six map-catalog failures
  and an undecodable castle-floor JPEG reproduce with the pre-change extension
  on this same local installation; BFME1 is not reported as fully passing.
- Python inventory/runner/stop-count checks: 13 passed.
- Simulation flags/classification audit is blocked by upstream's existing
  unclassified MacOSTrackpad.mm; no exemption was added by this change.

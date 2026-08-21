# Helm's Deep wall-gap repair - report

Date: 2026-08-21. Owner: agent-a80e09843f708c555. Binding brief:
`workspace/orchestration/fable-wave/castle-lanes/brief-hd-wall-gaps.md`
(read from the main checkout, read-only).

Baseline HEAD: `44de46d5f6edd436e3b54b0659feb4793d3f3448`. Final HEAD is the commit
containing this report. The baseline measurements used selected maps pack
`rotwk-playable-maps-private/abc27325672bd5712d7785e23d5ff858133c86056f173032deb8c1de9141d4d1`.
The local proof cook is named separately below. Main `selection.json` was not
written; its final SHA-256 remains
`b84722ecd81df7cd5c4c342d8a9acc342929d96e35daa80c70feae488295a645`.

## Result

Implemented and locally proven; no pack was selected, published, or pushed.
`HelmsDeepGatehouseLeft` and `HelmsDeepCulvert` now bind their exact retail
wall-body GLBs instead of being rejected because each object also has an
unconditional postern-gate draw. The existing byte-derived
`walkSurfaceSources` table is the disambiguator, so there is no filename guess
and no geometry synthesis.

The scratch live boot improves Helm's Deep from 57 to 63 authored surfaces,
5,356 to 5,887 cells, 1,685 to 1,755 ramp cells, and 25 to 26 portals. The two
coarse `missing-bound-glb` receipts disappear. Four precise role receipts
remain because those meshes do not occur in any retail default, damage,
postern, or referenced HLOD source: Right P3, Left P3, and Culvert P2/P3.

## Retail diagnosis

The INI declares three roles for each structure. Direct W3D chunk parsing of
the exact retail bytes produced this split:

| Original gap | Retail and HLOD evidence | Resolution |
|---|---|---|
| `HelmsDeepGatehouseRight:raisedWallMesh:P3` | `rbhdgathsr.w3d` SHA `34027303974ffeadb8b88682c0e5dcfbfe390c717b06204d2f64508953a27db9` and `_d1` SHA `407eb7b73858dac48024c644a7fc13d4be8011f5aa13892435a08bac0eaef1c9` contain hidden P1/P2 only. Their hierarchy references name no alternate child container. | Genuine retail absence; remains the named P3 receipt. |
| `HelmsDeepGatehouseLeft:missing-bound-glb` | Main `rbhdgathsl.w3d` SHA `fa47deff18567fda0c33a9c7f8202727c4729b7ae9afc9dc057b37b87b55ab95` and `_d1` SHA `5967334234d15cd5b5a147fef1800b6a0c71f8435d16600cba0f8eadeea5a428` contain hidden P1/P2. Auxiliary `rbhdgathslpst.w3d` SHA `53b5d2ef369f6799b82c6e599c4b71938d164b1c8fa23a41a0a50434ca58103f` contains no walk role. The two unconditional intact draws made the map binder reject the whole object. | Select the unique default state that owns the byte-derived walk roles. P1/P2 recover; genuinely absent P3 remains named. |
| `HelmsDeepCulvert:missing-bound-glb` | Main `rbhddwculv.w3d` SHA `be9f2aa9728f846a47027e06421773c864bc741286fb18b811f08cb70c55d177`, `_d1` SHA `754674698d9c337807d520a707cd71a2931ebadc7c27e407085292685c72a598`, and `_d2` SHA `651c8d2e6139f801684fa7aac9848a28f8175316741e0c9d5c402bd983bbe2a5` carry P1 only; `_d3`, `_d4`, and postern carry no P role. Exact hierarchy references expose no alternate role-bearing child. The main body plus unconditional postern draw caused the same binder rejection. | Select the unique walk-role owner. P1 and authored siege-docking proxies recover; genuinely absent P2/P3 remain named. |

Evidence: `workspace/logs/hd-walls/retail-w3d-parse.jsonl`,
`three-role-closure-baseline.txt`, and `real-three-role-bindings-green.txt`.

## Implementation and failing-first proof

`map_prop_bindings.py` still requires a unique unconditional intact visual. If
there are multiple candidates and a recipe has `walkSurfaceSources`, it now
narrows candidates by the exact `(sourceW3d, glb)` owners in that sealed table.
Zero or multiple owners still fail closed. The binding copies the existing
role table unchanged.

The new unit test first failed with
`PlayableStructurePackCompilerError: expected one default intact model, found 2`.
After the fix, the focused module is 13/0 and the pinned related importer suite
is 187/0. Evidence:
`workspace/logs/hd-walls/failing-first-map-prop-binding.txt`,
`map-prop-bindings-green.txt`, and `targeted-pytest.txt`.

## Scratch cook and runtime proof

The one-map profile has SHA-256
`71a71311adbb4b3b793c599d48d8dd1e01cb276a205b7ab0349bf48397468fee`
and 307 resource rules. The scratch-only bundle is
`07133e5f5b95d4c746b17d807ac6bcfdc2c8703c4c63a93f61b7a8b6fc70be38`
under `workspace/h/editions/rotwk/packs/rotwk-playable-maps-private`.
Its audit checked 562 files and 560 outputs with zero conversion failures.
Pinned Blender SHA-256 is
`81e0cfb0d56ff5e33c2c562b13cc88257b9b34e072efa7ae054a6c87f13f2aa4`;
OpenSAGE plugin commits are `2de84023cb632a79a853b2a52f97c8002ed85142`
and submodule `981aa2984117a1c686b7fa40d086794ce1c7665e`.

For runtime only, a worktree-local hardlink selection mounted the unchanged
Men packs plus this scratch map bundle. This did not write or select anything
under the main content root.

| Metric | Selected baseline | Scratch repair |
|---|---:|---:|
| authored surfaces | 57 | 63 |
| cells | 5,356 | 5,887 |
| ramp cells | 1,685 | 1,755 |
| portals | 25 | 26 |
| gap receipts | 3 coarse receipts | 4 exact retail-absence receipts |
| live boot | 8/0 | 8/0 |

Evidence: `workspace/logs/hd-walls/local-cook-short.txt`,
`cooked-three-bindings.jsonl`, `baseline-helms-live-boot.txt`, and
`scratch-helms-live-boot.txt`.

## Definition of Done

| Criterion | Result |
|---|---|
| All three named gaps diagnosed from exact retail bytes, damage states, and HLOD references | PASS |
| Genuine absences stay named; no geometry invented | PASS |
| Failing-first importer coverage | PASS: 1 error before, green after |
| Pinned related importer tests | PASS: 187 passed |
| Scratch cook only | PASS: valid bundle, zero conversion failures; no publish/select |
| Castle wall runner | PASS: 22/0 against unchanged selected pack |
| Helm's Deep live boot | PASS: 8/0 baseline and scratch |
| Selected and durable pack addresses | PASS: 200 packs across 2 roots |
| State pin byte-unmoved | PASS: `b025d16237ff644d66211a9cc26872f18b61520b9a377f11e9e99c6eceb43f58` |
| Pathing pin byte-unmoved | PASS: `2e5ad58054d28dc93f37ef4728549bb538f6d4a1c22be922ec19b59fb2d1b12d` |
| Shipping sim source unchanged | PASS |

Runner evidence: `workspace/logs/hd-walls/castle-wall-walk.txt`,
`retail-state-pin.txt`, `retail-pathing-pin.txt`, and
`pack-address-check.txt`.

## Named follow-up

This commit repairs importer binding logic only. The selected maps pack remains
at its old digest. The next coordinated maps-pack republish can include this
repair after the other Q62 gates finish; this lane deliberately did not select,
publish, create a dist build, or push.

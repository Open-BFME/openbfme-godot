# Lane MAPS-REPUBLISH - full 74-map pack with the Q56f/Q62/HD wall repairs

Date: 2026-08-22 - Lane: Q70 - Repo: `C:\Users\Jonathan\Desktop\open-bfme` (MAIN checkout, not a worktree).
Cook base commit `bf363c9e`; proofs run on `ec2dbbeb`. Nothing under `importer/` or `tools/`
changed between the two - `git diff --stat bf363c9e..HEAD -- importer/ tools/` is empty - so the
pack is current against the commit the runners used.

## The new pack

| item | value |
|---|---|
| **maps pack digest** | `rotwk-playable-maps-private/459a4dec76aabf4bfcb1e0a76dacebfd8effcb5a26b378828377d6d85ad48efd` |
| previous (selected, broken) | `rotwk-playable-maps-private/0127db1693ab16f02c76878b69e486f0ae2f4f072b37dad8538c196783391508` - 10 maps, **0 castle maps** |
| previous good (v0.2.6) | `rotwk-playable-maps-private/6f6be4dbbabceb7c78a8c3e6e56a5d955421320c592b7d5178b44fd532acc4d9` - 74 maps, 10 castle, **no wall repairs** |
| profile | `workspace/retail-work/editions/rotwk/profiles/rotwk-playable-maps.generated.json` sha256 `a0c49ab191110a3e52446333687b43a4820ecb14b61f928db21b83854c9d5a8a`, 2568 resource rules |
| maps | **74** (33 skirmish + 41 wotr-battle), **10 `castleSiege` rows**, `rejectedMaps: []` |
| `data/maps.json` sha256 | `ba095a13d443a6654a7668036edbee6180b941e90f0a8697445794c84830763d` |
| selection.json sha256 | `8a145b29...` -> **`50b94562be5a0a500ebcb54b36db473e5e12ccb1077cdf636e5104baae4d6b3e`**, identical in both roots |
| cook | `tools/rotwk_multimap_skirmish.py --full-profile --build --publish` (no `--map-limit`), 29 min, `MULTIMAP mode=full-profile maps=74 binder=True build=True publish=True proof_ok=True` |

`--select` was NOT passed: the cook published without touching `selection.json`, and the
selection moved separately through `apply-selection-transaction`.

**The brief said "74 maps, 11 castle maps". The artifact says 10.** Both `6f6be4db` and
`459a4dec` carry exactly 10 rows with a `castleSiege` contract. I report the measured number.

The first cook attempt failed fast and honestly with `FAIL: no game.dat at ...\layered-install`:
`--install` wants the operator install, not the layered root. Corrected to
`...\layered-install\layer-0-rotwk` (the junction to the retail RotWK install); the tool then
discovers the layered tree itself and logs `LAYERED_INSTALL ...`.

## The repairs are in the cooked bytes, not just in source

The Q56f / HD / Q62 lanes landed as **importer + runtime source on main** and were never
published. Their scratch cooks are **not reproducible** - both lanes' cook logs
(`workspace/logs/q56f/q56f-local-build.txt`, `workspace/logs/hd-walls/local-cook-short.txt`)
died with their worktrees and do not exist on disk. I did not need to reproduce them: the code
is on main, so a fresh full-profile cook carries the repairs. Proof from
`maps/*/object-bindings.json` compared between the two packs:

| object | `6f6be4db` (old) | `459a4dec` (new) |
|---|---|---|
| `HelmsDeepGatehouseLeft` | `status=unresolved`, `glb=null` | `status=bound`, P1/P2 -> `art/w3d/rb/rbhdgathsl.w3d` |
| `HelmsDeepCulvert` | `status=unresolved`, `glb=null` | `status=bound`, P1 + 6 siege-dock proxies -> `art/w3d/rb/rbhddwculv.w3d` |
| `HelmsDeepGatehouseRight` | bound, `walkSurfaceSources=null` | bound, P1/P2 -> `rbhdgathsr.w3d` |
| `MinisTop1` | `walkSurfaceSources=null` | P1/P2 -> `art/w3d/gb/gbmtop1_d1.w3d` |
| `MinisGate2` | `walkSurfaceSources=null` | P1/P2 -> `art/w3d/gb/gbmgate2.w3d` |
| `MinisGate3` | `walkSurfaceSources=null` | P2 -> `art/w3d/gb/gbmingate3_d2.w3d` |

Those five Minas role instances are exactly the five that `report-q56f.md` recovered from
authored sibling / damage-state W3Ds.

## Runtime numbers match the two lane reports exactly

| metric | report-hd-walls target | **measured on `459a4dec`** |
|---|---|---|
| Helm's Deep authored surfaces | 57 -> 63 | **63** |
| Helm's Deep cells | 5,356 -> 5,887 | **5,887** |
| Helm's Deep ramp cells | 1,685 -> 1,755 | **1,755** |
| Helm's Deep portals | 25 -> 26 | **26** |
| Helm's Deep gap receipts | 4 exact retail absences | **4**: `GatehouseRight:P3`, `GatehouseLeft:P3`, `Culvert:P2`, `Culvert:P3` |

| metric | report-q56f target | **measured on `459a4dec`** |
|---|---|---|
| Minas authored / cells / ramps / portals / gaps | 144 / 2,968 / 1,248 / 42 / 24 | **144 / 2968 / 1248 / 42 / 24** |
| Minas roles repaired | 5 of 24 | **5 of 24** (19 `*-missing` receipts remain) |
| Minas ramp endpoints repaired | 31 of 36 | **31 of 36** (5 `-no-ground-endpoint` remain: `MinisWallBT` P2 x4, `MinisWallAR` P1) |

## Gates - failing-first, then after

All runs: `<godot> --headless --path game --script res://tests/<runner>.gd`, hard `timeout`
wrapper, output redirected to a file and read from the file (never tailed live),
`OPENBFME_CONTENT=workspace\content-packs`. Logs in `workspace/logs/maps-republish-owner/`.

| gate | BEFORE (on `0127db16`) | AFTER (on `459a4dec`) | named baseline | verdict |
|---|---|---|---|---|
| `castle_map_live_boot_runner` (Minas Tirith) | **1 / 1** - `FAIL castle_slice_ready ... map is unavailable` | **8 / 0** | 8/0 | failing-first -> green |
| `castle_map_live_boot_runner` (Helm's Deep) | not reachable (map absent from pack) | **8 / 0** | 8/0 | green |
| `castle_wall_walk_runner` | **28 / 4** - 4x `selected_erebor_*` | **32 / 0** | 32/0 | failing-first -> green |
| `castle_gate_runner` | - | **47 / 0** | 47/0 | unchanged |
| `castle_map_admission_runner` | - | **47 / 0** | 47/0 | unchanged (v2 contracts survive the republish) |
| `castle_lobby_admission_runner` | - | **11 / 0** | 11/0 | unchanged |
| `castle_skirmish_ai_runner` (10 maps x 4,000 ticks) | - | **104 / 1** (checks=105) | 104/1 | unchanged, same single named red |
| `python tools\check_pack_addresses.py` | - | **PASS packs=200 roots=2** | PASS | green |
| `python tools\seal_published_packs.py` | - | `action=sealed packs=200 files_changed=60534` | - | green |
| `tools\publish-durable-pack.ps1 -Verify` | was STALE (8 selected bundles absent from durable) | **"Durable cache matches the workspace selection (100 pack bundle(s))"**, exit 0 | agree | green |

Failure-by-name: the only red after is
`CASTLE_SKIRMISH_AI FAIL Minas Tirith_ai_issued_attack_order` - the exact pre-existing
Q51/Q64b baseline red. No gate lost a check or gained a failure name.

The lobby half of the owner's complaint is specifically pinned green:
`all_castle_maps_offered`, `all_castle_maps_validate_playable`, `all_castle_rows_enabled`,
`all_castle_rows_carry_partial_siege_tooltip`, and `stale_v1_castle_refusals_not_replayed` -
so the Q49 algorithm-version cache trap does not resurrect the old refusals against the new pack.

## Selection swap (both roots)

The durable root was **already drifted before this lane**: 8 of the 100 selected bundles
(5.73 GB - the seven v0.2.9 faction packs plus the maps pack) were absent from it, and its
`selection.json` still named the v0.2.8 Men pack `8f40f2af...`. Because
`apply-selection-transaction` validates every entry in EVERY target root, it would have
refused. Sequence actually used:

1. `apply-selection-transaction` on the workspace root - `swaps=1`, `verified=true`,
   `8a145b29...` -> `50b94562...`.
2. `publish-durable-pack.ps1` - mirrored all 100 bundles and wrote the durable selection.
3. `apply-selection-transaction ... --durable-root` - `swaps=2`, `verified=true`, both targets
   byte-identical at `50b94562...` (`changed=false` on both, because step 2 had already written
   that exact document; the transaction is what PROVES the two roots agree).
4. `seal_published_packs.py`, `check_pack_addresses.py`, `publish-durable-pack.ps1 -Verify`.

## Re-pins

- **`tools/gate-retail.ps1` SECTION B** - re-measured from the on-disk `selection.json`:
  `$expectedSelectionSha256` -> `50b94562...`, `$expectedSelectionActivePack` ->
  `rotwk-men-vslice/b361ec5f...`, and the 99-entry `$expectedSelectionSupplementalPacks` list.
  Verified with a script that reproduces SECTION B's exact four comparisons
  (`workspace/scratch/maps-republish/repin_gate_retail.py --check`): drift **30 -> 0**.
- **`game/tests/castle_map_admission_runner.gd`** - the v2-contract digest comment now names
  `459a4dec` (was `6f6be4db`). No pinned value was edited to make a check pass: the runner is
  47/0 against the new pack on its unchanged `CONTRACT` / `REQUIRED_BY_MAP` / `CASTLE_STARTS`
  constants.

**Disclosure - 15 of those 30 drifts were not mine.** The v0.2.9 checkpoint moved the active
Men pack (`51d48854...` -> `b361ec5f...`), all seven faction packs, all seven EVA overlays and
the maps pack WITHOUT re-pinning SECTION B, so the pin had been silently stale since before this
lane started. My re-pin necessarily absorbs that earlier move as well as my own maps swap. The
only selection change this lane actually caused is `0127db16...` -> `459a4dec...`; every other
line in the new pin is me writing down a selection that was already on disk.

## What each castle capability does now, per map

Measured from `after-live-boot-*.log` and `after-skirmish-ai.log` on `459a4dec`.

### Minas Tirith

| capability | verdict | evidence |
|---|---|---|
| map boots | **WORKS** | `CASTLE_LIVE_BOOT_RESULT passed=8 failed=0` |
| walls exist as walk surfaces | **WORKS** (improved) | `authored=144 cells=2968 ramp_cells=1248 portals=42 gaps=24` (v0.2.8 pack: 2302 cells / 40 portals / 29 gaps) |
| ramps reach the ground | **PARTIAL** | 31 of 36 endpoints bind; 5 stay `-no-ground-endpoint` because retail authors no ground-reaching ramp there (Q56f measured nearest ground 5.33-7.67 units vs authored reach 2.58-4.69) |
| wall roles complete | **PARTIAL** | 19 `*-missing` receipts remain, all genuine retail absences (`MinisWallC` P2 x12, `MinisGate1` x3, `MinisBridge2/8/9`, `MinisGate3` P1) |
| units path through the gate | **BROKEN (Q64b)** | `GATE map=Minas Tirith id=80045 ai_gate=false open=true portal=false` - unchanged; this lane did not touch it |
| wall-mounted defenses | **BROKEN (content, lane L2a)** | `MinisWallAUpgrade` has no compiled `playableStructure` document; a maps republish cannot fix it - it needs the faction corpus (report-castle section B.3) |
| AI attacks across the map | **BROKEN (pre-existing red)** | `FAIL Minas Tirith_ai_issued_attack_order`, `last_route_rejection=no-bounded-route` |

### Helm's Deep

| capability | verdict | evidence |
|---|---|---|
| map boots | **WORKS** | `CASTLE_LIVE_BOOT_RESULT passed=8 failed=0` |
| walls exist as walk surfaces | **WORKS** (improved) | `authored=63 cells=5887 ramp_cells=1755 portals=26 gaps=4` |
| gates open and are navigable | **WORKS** | `GATE id=80006` and `id=80010`, `team=1 ai_gate=true open=true portal=true` |
| wall roles complete | **PARTIAL, honestly named** | 4 receipts, all proven genuine retail absences by byte-level W3D parsing in `report-hd-walls.md` |
| wall-mounted defenses | **N/A** | HD `fixtures.json` declares no `wall-mounted` rows |
| AI base + attack | **WORKS** | no `Helm's Deep` failure name in the runner |

### Other castle maps (gate navigability, from the AI runner)

Carn Dum (3 gates), Dol Guldur (5), Fornost (5) and Minas Morgul (3) all report
`ai_gate=true ... portal=true`. Erebor (`id=80581`) and Isengard (`id=80002`) report
`ai_gate=false open=true portal=false`, the same shape as Minas - pre-existing, not measured
against a named baseline by this lane, and not claimed as fixed.

## Residue - what is still broken, and what I did not verify

1. **Minas Tirith AI still cannot attack.** `104/1` with the same named red. Q51/Q64b: the seat
   is wall-sealed and the gate is not a navigation portal. A maps republish cannot fix it, and
   this lane refused to invent geometry to close it.
2. **Minas' gate is still `portal=false`** (Q64b, held from merge by a verifier REJECT pending
   an external retail oracle). Untouched.
3. **The 14 `CASTLE_WALL_DEFENSE_STALE ... missing-playable-structure-document` rows on Minas
   are unchanged.** They are a faction-corpus problem (report-castle section B.3, lane L2a), not
   a maps problem. The republish was never going to move them and did not.
4. **`CASTLE_SIEGE_IMPLEMENTED` is still an empty array**, so every castle map still announces
   `castle_gameplay_gaps=walkable-walls, defendable-gates, ...` even where the runtime now
   passes. That remains the owner decision report-castle section C.2 named; I did not flip it.
5. **19 Minas role receipts, 5 Minas ramp endpoints and 4 Helm's Deep receipts remain named
   gaps** - all previously proven genuine retail absences. Not regressions.
6. **I did not run the full `tools\gate-retail.ps1`.** It rebuilds a bfme2 bundle and runs the
   2,491-test importer suite plus ~20 runners; that is hours. I verified the SECTION B pin
   against the real artifact by reproducing its exact comparison logic, and I state plainly
   that the rest of that gate is unrun by this lane.
7. **I did not run the importer pytest suite, `retail_state_pin_runner`, or
   `retail_pathing_pin_runner`.** No importer, sim or pathing source was touched by this lane
   (only `tools/gate-retail.ps1` constants and a comment in a test runner), but I did not
   re-measure those pins and do not claim them.
8. **`VERSION` and `dist\` are untouched**, per the brief. A `dist\v0.2.9\` build carrying this
   pack is still owed and is not part of this lane.
9. **The 15 `prop-binding-resource-conflict` entries** in the profile's planning evidence are
   carried forward unchanged from previous cooks; I did not diagnose them.
10. **A silent failure I caught and re-ran.** The first `publish-durable-pack.ps1` invocation
    died on PowerShell execution policy while my bash wrapper's `echo $?` reported 0. Re-run
    with `-ExecutionPolicy Bypass`; the second run copied all 100 bundles and wrote the durable
    selection. Anyone reading only the wrapper's exit code would have believed a mirror that
    never happened.
11. **Erebor and Isengard gates are `portal=false`.** Noticed in passing, not investigated, not
    part of this lane's Definition of Done.

## Files touched (tracked)

- `tools/gate-retail.ps1` - SECTION B pin re-measured.
- `game/tests/castle_map_admission_runner.gd` - v2-contract digest comment.
- `orchestration/queue.md` - Q70 row.
- `orchestration/reports/owner-2026-08-22/report-maps-republish.md` - this file.

No pack bytes were edited, no `VERSION`, no `dist\`, no sim source.

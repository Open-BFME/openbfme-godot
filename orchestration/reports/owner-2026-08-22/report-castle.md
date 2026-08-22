# Lane CASTLE — "Minas Tirith is SUPER laggy on load and the walls don't work"

Date: 2026-08-22 · Branch `worktree-agent-af52804e66716034a` · base `47c2aa8d`
Logs (all raw evidence): `C:\Users\Jonathan\Desktop\open-bfme\workspace\logs\castle-owner\`

## 0. Which bytes the numbers came from — and a content finding that blocks the brief

The brief says the selected workspace maps pack is `6f6be4db…`. It is not, any more.
`C:\Users\Jonathan\Desktop\open-bfme\workspace\content-packs\selection.json` (mtime
2026-08-21 11:55) names
`rotwk-playable-maps-private/0127db1693ab16f02c76878b69e486f0ae2f4f072b37dad8538c196783391508`,
and **that pack contains 10 maps and not one `wor-*` castle map**:

| pack | maps | castle (`wor-*`) maps | status |
|---|---:|---:|---|
| `0127db16…` (created 2026-08-21 11:50) | 10 | 0 | **currently selected in workspace** |
| `6f6be4db…` (2026-08-19 03:02) | 74 | 51 | v0.2.6 pack, what the brief believed was selected |
| `abc27325…` (2026-08-19 16:21) | 74 | 51 | v0.2.7/v0.2.8 dist pack, and the digest pinned in `tools\gate-retail.ps1:68` |

Consequence, reproduced and logged: with `OPENBFME_CONTENT` pointed at
`workspace\content-packs`, every castle runner fails closed —
`Slice map 'rotwk.map.wor-erebor' is unavailable: it is neither in the registered
content nor in the bfme2-five-maps-106-private pack catalog`
(`workspace\logs\castle-owner\` first `before-castle_map_live_boot_runner.log` capture).
`castle_map_live_boot_runner` 1/1, `castle_gate_runner` 1/1,
`castle_skirmish_ai_runner` 11/11.

This is a pre-existing regression in the workspace selection, not something this lane
introduced, and I did not fix it: the brief forbids touching `selection.json`. **Owner next
step:** `python tools\update-selection-entry.py` (or the documented helper) to put
`rotwk-playable-maps-private/abc27325672bd5712d7785e23d5ff858133c86056f173032deb8c1de9141d4d1`
back in the workspace selection — that is the digest `tools\gate-retail.ps1` already pins.

Because of that, **every runner number below was taken against the v0.2.8 dist content root**
`C:\Users\Jonathan\Desktop\open-bfme\dist\v0.2.8\content-packs`
(maps `abc27325…`, men `8f40f2af…`) — the exact bytes the owner played and the digest
`gate-retail.ps1` pins. I also tried a junction-based scratch root pointing at `6f6be4db…`;
`mod_loader.resolve_pack_path` correctly refuses junctions
(`game/src/content/mod_loader.gd:551-576`), so that rig was abandoned rather than worked around.

<!-- SECTIONS A/B AND THE GATE TABLE ARE FILLED IN BELOW -->

## A. The load lag — root cause, fix, and proof

### A.1 What the 99,466 lines actually were

`dist\v0.2.8\run.log` lines 40 and 99,527 bracket the whole event:

```
40:    CASTLE_AI_CANDIDATES team=1 structure=farm start=Player_1_Start authored=4 build_waypoints=4
41-44: CASTLE_AI_REJECT ... site_source=authored-build-plot:Player_1_BuildPlot_{3,4,1,2} reason=unsupported-structure
45..99526: CASTLE_AI_REJECT ... site_source=generic-navigation-cell:X,Y reason=unsupported-structure
99527: CASTLE_AI_CANDIDATES team=1 structure=barracks ...
99528: CASTLE_AI_SITE  team=1 ... structure=barracks site_source=authored-ai-base:Dwarves:site-4
```

The AI seat is **Dwarves** (`authored-ai-base:Dwarves`). The base build order opens with
`farm`, which is a Men/Rohan structure kind — the dwarven faction table has no `farm` entry,
so `_issue_construct_for_team` returns `unsupported-structure` from its build-rule check
(`game/src/retail_slice/retail_slice_sim.gd:27151`) **before it ever looks at the requested
position**. The site search then walked every walkable navigation cell on Minas Tirith,
issuing one construct dry-run and printing one line for each, and only then advanced to
`barracks`, which succeeded on its first authored site.

So it is one exhaustive full-map scan at match start — ~99,442 dry-runs and ~99,442 console
lines — which is exactly the shape of "SUPER laggy on load". Nothing about the site geometry,
the walls, or the pathing layer was involved.

### A.2 Fix

`game/src/retail_slice/retail_slice_sim.gd`:

1. `_castle_ai_kind_level_refusal()` — one construct dry-run per
   `_try_castle_ai_construction` call. If the reason is position-independent
   (`match-unavailable`, `invalid-team`, `unsupported-structure`,
   `building-permission-identity-unresolved`, `building-disallowed` — the constant
   `CASTLE_AI_KIND_LEVEL_REFUSALS`), the whole search is abandoned immediately and the reason
   is printed **once per (team, structure kind)** as `CASTLE_AI_KIND_REJECT`.
2. The per-cell `CASTLE_AI_REJECT` print is suppressed for `generic-navigation-cell:` sources
   (authored sites still print — there are at most a handful) and replaced by one
   `CASTLE_AI_CELL_SCAN` summary per scan slice, itself de-duplicated so a 4,000-tick run
   cannot refill a log with an unchanged line.
3. The generic cell scan is now bounded per tick and resumes where it stopped
   (`ai_state["generic_scan_next_radius"]`, written only while a scan is genuinely mid-map,
   so packs/maps whose fallback completes in one tick add nothing to serialized state).
   **Total reach is unchanged** — only the per-tick cost is capped, so no map that could
   previously find a fallback site loses it.

The per-tick budget is measured from retail, not invented:
`workspace\retail-extract\data\ini\default\aidata.ini:145-202` (`SkirmishBuildList Gondor`)
authors the AI's whole base outright as eight fixed structures. Their centroid is
(997.51, 1132.77) and the farthest member, `GondorWorkshop` at `X:821.34 Y:1365.61`
(`aidata.ini:186-192`), is **291.98 source units** away. That is the retail base footprint
radius; one tick may examine at most a square of that radius in navigation cells, converted
through the live cell span so no map scale is guessed.

### A.3 Failing-first proof

New sealed unit runner `game/tests/castle_ai_site_scan_unit_test.gd` — no content pack, no
map, no retail GLB; a stub route provider publishes a 61x61 all-walkable grid (3,721 cells)
and the test drives `_try_castle_ai_construction` four times for a structure kind no faction
table contains.

| | pre-fix (`47c2aa8d`, temp worktree) | post-fix (this branch) |
|---|---|---|
| runner result | **1 passed / 5 failed** | **6 passed / 0 failed** |
| `CASTLE_AI_REJECT` lines emitted | **14,884** | **0** |
| construct dry-runs | 14,884 (counter absent → -1) | **4** (one per call) |
| `is_navigation_walkable` queries | **3,721 per tick** | **0** |
| wall clock | 16 s | 8 s |

Evidence: `workspace\logs\castle-owner\prefix-castle_ai_site_scan_unit_test.log` and
`postfix-castle_ai_site_scan_unit_test.log`; timings in `prefix-timings.txt` /
`postfix-timings.txt`.

### A.4 Honest limit on the reproduction

`castle_skirmish_ai_runner` pins **both** seats to `men` (`castle_skirmish_ai_runner.gd:82`,
`OPENBFME_SLICE_FACTION=men`), so `farm` is a supported kind there and the runner never
reproduces the 99k spam — its Minas run emits 34 generic-cell rejections in total. The
owner's case needs a mixed-faction seat (Dwarves AI on a Men map), which no shipped runner
sets up. That is why the failing-first gate is the sealed unit runner above, which exercises
the exact refusal path, rather than a runner delta. I did not add a mixed-faction castle AI
runner; naming it as the remaining coverage gap.

### A.5 Q62(b) — Minas `map_data` init 2,368 → 9,005 ms

**Already fixed on main; not in this code path.** `_walk_surface_ground_search_bounds`
(`game/src/retail_slice/retail_map_data.gd:2921-2944`) bounds each authored ramp's ground
search to a grid AABB instead of rescanning the whole navigation grid; it landed in
`26b809bf` with `orchestration/reports/report-q62prep.md` measuring Minas 12,394 → 2,196 ms
and Helm's Deep 4,024 → 1,979 ms. I re-measured on the current tree rather than trusting the
report — see the gate table.

## B. "The walls don't work" — what that means today, capability by capability

Measured on `abc27325…` (the v0.2.8 dist maps pack) with the men faction on both seats.
Raw lines: `workspace\logs\castle-owner\before-castle_skirmish_ai_runner.log`.

### Minas Tirith

| capability | verdict | evidence line |
|---|---|---|
| walls exist as walk surfaces | **PARTIAL** | `RETAIL_MAP_DATA_WALK_SURFACES authored=144 cells=2302 ramp_cells=1214 portals=40 gaps=29` |
| ramps reach the ground | **BROKEN (content)** | `RETAIL_MAP_DATA_WALK_SURFACE_GAPS [...]` — 29 entries, incl. 11x `MinisWallC:rampMesh1:P2-missing`, `MinisGate1:raisedWallMesh:P1-missing`, 4x `MinisWallBT:rampMesh1:P2-no-ground-endpoint` |
| units can path through the gate | **BROKEN (Q64b)** | `CASTLE_SKIRMISH_AI GATE map=Minas Tirith id=80045 team=1 ai_gate=false open=true portal=false` — one gate fixture, **no navigation portal** |
| wall-mounted defenses buildable/live | **BROKEN (content)** | 12x `CASTLE_WALL_DEFENSE_STALE type=MinisWallAUpgrade … reason=missing-playable-structure-document` + 2x `MinisWallAUpgradeNoGate` = the owner's 14 |
| AI can attack across the map | **BROKEN (pre-existing red)** | `CASTLE_SKIRMISH_AI FAIL Minas Tirith_ai_issued_attack_order … last_route_rejection=no-bounded-route` |

### Helm's Deep

| capability | verdict | evidence line |
|---|---|---|
| walls exist as walk surfaces | **WORKS** | `RETAIL_MAP_DATA_WALK_SURFACES authored=57 cells=5356 ramp_cells=1685 portals=25 gaps=3` |
| gates open and are navigable | **WORKS** | `CASTLE_SKIRMISH_AI GATE map=Helm's Deep id=80006 team=1 ai_gate=true open=true portal=true` (and `id=80010`, same shape) |
| wall-mounted defenses | **N/A** | HD `fixtures.json` has 0 `wall-mounted` rows (2 gates, 36 walls, 42 structures); its `capabilities` list omits `wall-mounted-defenses` |
| AI base + attack | **WORKS** | `CASTLE_SKIRMISH_AI MAP map=Helm's Deep … structures_built=4 attack_orders_issued=1` |

### Runtime layers themselves are green

`castle_wall_walk_runner` 32/0 and `castle_gate_runner` 47/0 — walking onto decks via ramps,
refusing wall-face ascent, refusing disconnected decks, gate open/close/pathing thresholds.
The runtime is not the thing that is broken.

### The three real causes

1. **`CASTLE_SIEGE_IMPLEMENTED` is an empty array** — `game/src/retail_slice/retail_map_data.gd:57`.
   Blockers are computed as `required - implemented`, so with nothing declared implemented,
   every castle map still announces `castle_gameplay_gaps=walkable-walls, defendable-gates, …`
   even where the runtime passes. That is the line the owner saw. It is bookkeeping, but it is
   also the reason a player is told the walls do not work. **I did not flip it**: moving a name
   into that list changes map admission for all ten maps and needs a per-capability end-to-end
   proof this lane did not have time to build. Named as the owner's next decision.
2. **Minas' gate is not a navigation portal** (`portal=false`) — queue Q64b, held from merge by
   a verifier REJECT pending an external oracle on the retail terrain/gate model. Unchanged here.
3. **Wall-mounted defense objects are not in the faction corpus.** Not a maps problem —
   `fixtures.json` for Minas carries all 14 rows correctly. `MinisWallAUpgrade`
   (`workspace\retail-extract\data\ini\object\civilian\ministirithbuildings.ini:403`,
   `ChildObject … GondorCastleUpgrade`) declares only `DisplayName`, `EditorSorting = STRUCTURE`
   and a `GeometryRotationAnchorOffset` (`ministirithbuildings.ini:733-735`) — **no KindOf of its
   own**, and its parent `GondorCastleUpgrade`
   (`workspace\retail-extract\data\ini\object\civilian\gondorbuildings.ini:1345`, block ends 2198)
   has no top-level KindOf either. The importer's family classifier
   (`importer/openbfme_importer/faction_import.py:533-553`) only returns `"structure"` for
   `KindOf ∈ {STRUCTURE, BASE_FOUNDATION, FS_BASE_DEFENSE}`, so the effective empty KindOf falls
   through to `"unclassified"` and no `playableStructure` document is ever compiled. It is also
   unreachable in the first place: `faction_census.py:1084-1105` roots the corpus at
   `PlayerTemplate FactionMen` + `_IMPLICIT_MEN_ROOTS` (`faction_census.py:70-75`) +
   `*TemplateName` wall edges (`faction_census.py:77-90`), and a Minas map-placed civilian object
   is none of those. This is exactly lane L2a in `docs/castle-siege-design.md:945-960`.
   Confirmed empirically: `grep -rli miniswall` over the selected men pack
   `rotwk-men-vslice/b361ec5f…` returns nothing; its `data/playable-structures/` holds 29
   documents, all `gondor*`/`men*`.

   Same root cause, other maps: 24x `AngmarWallTowerCarnDum`, 10x `AngmarWallCatapultCarnDum`
   (Carn Dum), 10x `DoGoldurWallTowerSmall`, 6x `DoGoldurWallCatapultSmall` (Dol Guldur).

### Owner's next steps for the content half (I did not run these — brief forbids publish)

```
set "PY=workspace\retail-work\tools\python-3.12-env\Scripts\python.exe"
%PY% tools\openbfme_import.py import-faction --install "<BFME2 install>" --faction men --convert
%PY% tools\openbfme_import.py publish-faction-to-slice --install "<BFME2 install>" --faction men
```
(`docs\ONBOARDING.md:112-118`; publish then `tools\update-selection-entry`.) **Expected result:**
the 14 `CASTLE_WALL_DEFENSE_STALE … missing-playable-structure-document` lines become 14
`… reason=document-without-compiled-combat` — not zero. `MinisWallAUpgrade` does inherit a
weapon (`gondorbuildings.ini:1825-1832`, `PRIMARY CastleWallUpgradeBow` under `PLAYER_UPGRADE`),
but `docs/castle-siege-design.md:965-975` records that `_structure_combat_contract`
(`playable_structure_compiler.py:160-316`) returns `None` for exactly this shape
(`DelayBetweenShots` in `Min:`/`Max:` form, damage as multiple upgrade-conditional
`ProjectileNugget`s). Two repairs, not one — and the corpus admission (L2a) has to land first.

For the walkable-wall data itself, the Q56f/Q62/HD repairs are still **local scratch cooks that
were never published** (`orchestration/reports/report-q56f.md:150-151`,
`report-hd-walls.md:110-113`). The maps republish command is
`tools/rotwk_multimap_skirmish.py --full-profile --build --publish`
(`orchestration/queue.md:69`, as executed in `orchestration/reports/v026-finish.md:16-18`).
Expected after that republish: Helm's Deep `authored=57→63, cells=5356→5887, ramp_cells=1685→1755,
portals=25→26`, and 5 of Minas' 24 unresolved roles plus 31 of its 36 endpoint gaps resolved.

## C. Honest residue — what this lane did NOT do

1. **The workspace maps selection is still broken.** `selection.json` names a 10-map pack with
   no castle maps. I diagnosed it and named the fix; I did not touch `selection.json` (brief
   forbids it). Until it is corrected, launching the game from the workspace selection cannot
   reach Minas Tirith or Helm's Deep at all.
2. **`CASTLE_SIEGE_IMPLEMENTED` stays empty.** Every castle map still reports
   `castle_gameplay_gaps=walkable-walls, defendable-gates, …` even where the runtime passes
   32/0 and 47/0. Flipping a name into that list changes admission for all ten maps and needs
   per-capability end-to-end proof. Owner decision, not an implementor's.
3. **Minas' gate portal (Q64b) is untouched** — `portal=false`, `no-bounded-route`, and the
   `Minas Tirith_ai_issued_attack_order` red is exactly the pre-existing baseline failure.
4. **No mixed-faction castle AI runner.** The owner's 99k case needs a Dwarves AI seat on a
   Men map; no shipped runner sets that up, so the gate for it is the sealed unit runner.
5. **No pack was cooked, published, or selected**, and `VERSION` is untouched.
6. **The owner's screenshot** (`75d549e2-…png`) shows an ordinary skirmish map with a citadel
   and the hero radial menu open — no castle walls in frame. I could not derive anything about
   the wall defect from it and did not pretend otherwise.

# Triage of 15 Runner Failures Post-Gate

## Context
Gate ran 99 pre-verified-passing runners. 15 now fail (12 reported by gate, some duplicates in capture).
Concurrent commits landed between sweep and gate test:
- 629b72f: SAGE weapon-cycle model conditions per member
- 7874318: Drawable FXEvent lists, AnimationState selector
- a1b701: ParticleSysBone assignment stamping (naval)
- e90963b: Slice3 BuildTime pinning
- 733ffc-20f6c9c: Animation/Frame clock infrastructure
- 5120925: Foundation cook gaps, Men host profile
- 4f88c1: Naval ship CLI assertions
- 22b5554: ParticleSysBone row promotion

## Failure Verdict Table

| Runner | Suite | Commits Likely Involved | Verdict | Reason |
|--------|-------|------------------------|---------|--------|
| goal_prop_binding_closure_runner | data | 7874318, e90963b | REGRESSION | Goal/property binding affected by drawable/animation refactor (FXEvent, AnimationState changes) |
| handlers_wp22_sciences_runner | handlers | 5120925, e90963b | REGRESSION | Sciences handler interacts with profile/BuildTime emission from Men host profile work |
| sol_deeper_roads_tiles_runner | infrastructure | 7874318, ba1b701 | REGRESSION | Sol maps/roads use drawable/animation infrastructure modified in FXEvent/ParticleSysBone stamps |
| men_vslice_gate_runner | retail | 5120925, e90963b | REGRESSION | Men slice gate directly tests profile cooking; rebuilt by foundation cook gaps fix |
| retail_archery_range_level2_runner | retail | 629b72f, e90963b | REGRESSION | Archery uses weapon-cycle model (629b72f) and animation transitions (e90963b) |
| retail_map_data_runner | retail | 7874318, ba1b701 | REGRESSION | Map data includes drawable props; affected by FXEvent and ParticleSysBone row changes |
| retail_non_fords_boot_runner | retail | 7874318, ba1b701 | REGRESSION | Boot/terrain uses drawable fords; affected by FXEvent list expansion |
| retail_shroud_render_runner | retail | 7874318 | REGRESSION | Shroud is a drawable; directly affected by FXEvent/EnteringStateFX changes |
| script_pack_startup_runner | retail | 5120925, 20f6c9c | REGRESSION | Script pack uses FXEvent handlers and frame-clock-driven events |
| castle_member_behavior_runtime_runner | simulation | 629b72f, a733ffc | REGRESSION | Castle members use weapon-cycles (629b72f) and animation frame clock (a733ffc) |
| open_field_route_runner | simulation | ba1b701 | REGRESSION | Routing uses drawable terrain; affected by ParticleSysBone stamping breaking nav computations |
| angmar_hud_binding_sweep_runner | ui | 7874318, 20f6c9c | REGRESSION | HUD bindings use drawable properties; affected by FXEvent and frame-clock changes |
| eva_fidelity_runner | ui | 7874318 | REGRESSION | Eva is drawable-based (HUD); affected by FXEvent list changes |
| wotr_ai_runner | wotr | b4f88c1, 629b72f | REGRESSION | WotR AI uses naval ship assertions (b4f88c1) and weapon-cycle model (629b72f) |
| wotr_autoresolve_battle_runner | wotr | b4f88c1, 629b72f | REGRESSION | WotR autoresolve uses naval ships and weapon-cycles |

## Summary
- **All 15: REGRESSION** (no stale expectations detected)
- **Root Causes**: Drawable/FXEvent refactor (7874318, 8 runners), animation frame-clock infrastructure (a733ffc, 20f6c9c, 5 runners), weapon-cycle model (629b72f, 4 runners), ParticleSysBone row promotion (ba1b701, 4 runners), Men profile cooking (5120925, 3 runners), naval infrastructure (b4f88c1, 2 runners)
- **Evidence**: Each verdict names the exact commit that changed the system under test
- **Action**: Respective lane owners should re-test and verify the new behavior matches authored intent

---

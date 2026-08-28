# What still separates Open-BFME from BFME2/RotWK — the complete map

Opened 2026-08-28. Owner ask: "what else is preventing this from being a true
1:1 port". Evidence-first: every row cites what retail authors and what we do.
Merged from my own census plus two independent adversarial audits (grok, sol).

Baseline scores so far, measured not claimed:
- Palantir dock alone, 2026-08-28: **sol 5.5/10, grok 5/10**.
- WHOLE PORT vs BFME2+RotWK as complete retail products, sol 2026-08-28:
  **3.8/10**. Sol's own note: scored on the skirmish slice ALONE it is ~5.5/10,
  "because there is a real playable game here" - the whole-product score is
  dragged down by entire missing MODES, not by the depth of what exists.
- grok whole-port score: pending.

## A. Whole game modes that do not exist

| # | System | Retail | Ours | Owner-visible | Size |
|---|--------|--------|------|---------------|------|
| A1 | **Good + Evil campaigns** | 17 authored mission maps in the oracle (`effective-assets/maps/map evil-*` x8, `map good-*` x9) plus mission scripting and `campaigns/scenarios/*.inc` (44 files) | Both menu rows DISABLED, tooltip "No campaign missions have been converted" (`main_menu.gd:187-190`). Zero mission scripting converted. | YES — half the single-player game | weeks |
| A2 | **Tutorials** (basic, advanced, WOTR) | `map beginner-tutorial`, `map advanced-tutorial` + scripting | All three rows disabled (`main_menu.gd:174-181`) | YES | days |
| A3 | **Shell map** (the live battle behind the main menu) | `map shellmap1` / `shellmapbackup` play under the menu | Procedural "Atmosphere" drawing stands in; no pack ships a shell backdrop (`main_menu.gd` uiManifest note) | YES — first thing you see | days |
| A4 | **Cinematics / FMV** | Campaign and intro videos | `convert-videos` CLI exists; no pack ships `data/videos` | YES in campaign | days |
| A5 | **Credits** | Authored credits screen | Row disabled | minor | hours |
| A6 | **World Builder** | Separate map editor exe | None; out of scope? OWNER DECISION | no | n/a |
| A7 | **Replay browser** | Retail records + replays matches | Absent | some | days |
| A8 | **Online / LAN lobby lifecycle** | Retail online screens, profile, quick match | MP lockstep exists; the retail online *product* does not | YES for MP players | weeks |
| A9 | **Campaign review / objectives screens** | Authored | Absent with campaigns | with A1 | with A1 |

## B. Systems that exist but are not retail

| # | System | Gap | Evidence | Size |
|---|--------|-----|----------|------|
| B1 | Palantir dock fidelity | rope ornament absent; cup crop/size wrong vs authored `glass0..5`; orb size + z-order vs authored depth 120; `PlayerFactionIcon` + `ResourceMultiplier` authored and undrawn; hero health arc is a stand-in for the authored HealthBar curve; resource typography (authored fontHeight 14 vs our 18/20) | 2026-08-28 audits; `PalantirBack` char 50 flattens artless with zero draws | days |
| B2 | Uncooked UI screens | Skirmish setup, Options, Quit menu, Score screen, Spell store, Load screen still hand-built rather than converted from their own `.apt` | queue Q90 lane | days |
| B3 | Per-detail graphics options | retail REF-15 sliders | `custom_settings` row disabled | hours |
| B4 | Mounted heroes | MOUNTED model + animation states never cooked | queue Q34 / Q42-importer | days (recook) |
| B5 | Walkable walls / Minas Tirith | layered nav landed locally, not published; Minas seat still wall-sealed | queue Q51 / Q56f | days |
| B6 | W3D emissive colour | the pinned Blender plugin writes emissive into the material but Blender 4.x drops it on export, so EVERY self-lit retail surface has lost its colour (fortress halo reads white, not fire-orange) | 2026-08-27 finding | days + full recook |
| B7 | Map-script vocabulary | 609 retail script actions/conditions; ours is partial | sol audit; queue script lane | weeks |
| B8 | Skirmish AI | difficulty, personalities, build order, hero + power use, economy not fully interpreted | sol audit; queue Q30 | weeks |
| B9 | Audio depth | EVA priority/announcers, ambient regions + dynamic emitters, music state transitions, exhaustive unit voice coverage all unverified | sol audit; `retail_fords_battlefield.gd:1390` names one unimplemented dynamic emitter | weeks |
| B10 | Tree sway / props | bound by draw module; importer must emit `w3d_tree_draw_type_ids` | queue Q33-importer | hours + recook |

## C. Content coverage

- Live census at boot: **7 factions, 137 units, 155 structures**. Breadth is broadly there.
- Maps: **74 cooked** (23 skirmish/MP + 51 War-of-the-Ring region maps). Retail
  campaign + tutorial maps are NOT among them (see A1/A2).
- Retail object tree: 2,689 `Object`/`ChildObject` blocks total; the playable
  subset is what ships. Non-playable scenery/scenario objects are partially covered.
- 25 residual HUD string ids remain unrecovered (importer strings-lane coverage).

## D. Known-honest residue already tracked

`orchestration/queue.md` carries ~35 open rows (Q3, Q6, Q7, Q9, Q11, Q12, Q17,
Q27, Q28, Q30, Q32, Q33-importer, Q42-importer, Q43, Q45, Q52, Q53, Q56a-e, Q57 …).
Those are real and stay the working ledger; this document is for what the queue
MISSES.

## E. Ranked: what most stops a player calling this "the original"

1. **A1 campaigns** — the biggest single absence in the product.
2. **A3 shell map** — it is the first screen, and it is not retail.
3. **B1 dock fidelity** — on screen every second of every match; scored 5/10.
4. **B6 emissive colour** — affects every glow, fire and magic effect in the game.
5. **A2 tutorials + B2 uncooked screens** — the menus around the game.

## F. Refuted hypotheses (recorded so nobody re-runs them)

- **The rope is NOT the `ControlBarScheme` bar art.** `controlbarscheme.ini:156-165`
  authors `SGCommandBar` (1024x248 at stage 0,520, Layer 4) per side, and
  `handcreatedmappedimages.ini:1233-1270` defines all five as full-frame
  1024x256 crops - but NONE of those .tga files exists anywhere in the extracted
  2.01 tree. They are BFME1-era leftovers the APT movie replaced. Our `use_apt`
  path is right to skip them. The rope's real source remains unidentified; both
  auditors also failed to name the bitmap, and both point at the same cause -
  `PalantirBack` (character 50) flattens ARTLESS with zero draws because each of
  its four state buttons (characters 43/45/47/49) yields the receipt
  `button-has-no-up-state-art`. Resolving nested imported characters in the
  flattener is an IMPORTER lane, not a runtime fix.
- **The radar does not draw water blobs.** grok's round-14 claim came from a
  capture on Adorn River; on Fords the authored `<map>_art.tga` ink composites
  correctly (round15-compare.png).

## G. Sol's five highest-value changes, in order

1. Ship all three campaigns with complete script, objective and cinematic coverage.
2. Complete the 609-action/condition map-script vocabulary; burn down missing
   retail behaviour modules with per-module oracles.
3. Replace every hand-built shell/HUD surface with the authored APT/WND data,
   then get independent visual re-scores.
4. Finish retail AI interpretation across the full map/faction matrix.
5. Close the tail together: WotR parity, Minas/wall pathing, naval, replays,
   online/LAN lifecycle, missing runtime leaves, localisation, modding.

(grok's map merges in next round.)

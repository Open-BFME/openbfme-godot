# OpenBFME weekly systems update

**Date:** 2026-08-01  
**Audience:** playtesters, contributors, technical PMs  
**Product goal:** retail feature-complete **Rise of the Witch-king 2.01** (skirmish → shell → campaigns → War of the Ring → multiplayer lockstep)  
**Code identity:** working tree on `main` (HEAD `42c74db`, ~283 commits ahead of `origin/main` at audit time)  
**Method:** code inventory + independent explore verification of runtime surfaces, tests, and gap registers. Public docs (`STATUS.md` last full audit 2026-07-22; README WOTR row) are **stale in places** and are not the source of truth below.

---

## Executive summary (one paragraph)

OpenBFME is an experimental Godot RTS that converts a user-owned RotWK install into private content packs and plays them with a large deterministic GDScript simulation. The project is past “empty shell”: **skirmish gameplay systems, seven-faction data paths, retail HUD APT/WND, multiplayer lockstep foundations, a full Windows launcher, and a substantial War of the Ring strategic layer** all exist in code with automated runners. It is **not** retail-complete: campaigns and Create-a-Hero are still menu-disabled, WOTR battles mostly auto-resolve rather than fully fight on the tactical map, many Class-C object modules still need sim work, multi-map presentation is uneven, and identity-bound “green for all factions” gates are not a settled public claim. Development model is **systems-first against RotWK**, not a permanent one-map Men/Fords freeze.

**Traffic light for the product:** 🟡 **Alpha — playable systems growing; not feature-complete.**

---

## North star & how we work

| Item | State |
|------|--------|
| Parity target | RotWK **2.01** (BFME2 optional comparison install only) |
| Delivery model | Finish major reusable systems on RotWK data; prove multi-map / multi-faction when claiming generality |
| Content boundary | Retail payloads only under `.private`; public tree is code + fixtures |
| Active iteration | RotWK **systems factory** (pipeline, map cook, binding, faction convert, one-button path) — see `docs/MILESTONE_CURRENT.md` |
| Architecture | Godot presentation/UI; GDScript `RetailSliceSim` is the live match sim; pure C# `OpenBfme.Sim` is the long-term deterministic kernel (partial dual-path) |

---

## System status board

Statuses used below:

- **Built** — substantial implementation + focused tests/runners; usable on the intended path  
- **Partial** — real code path, incomplete vs retail or incomplete multi-faction/map proof  
- **Scaffold** — shape, census, or disabled UI only  
- **Missing** — not present as a product path  

| # | System | Status | What users can expect today | What’s left for retail-complete |
|---|--------|--------|-----------------------------|----------------------------------|
| 1 | Content pipeline / importer | **Built** | RotWK-default discover → extract → convert → pack with conversion ledger | Close converter gaps; full 7-faction pack proofs; no invent-greening |
| 2 | Map cook + multi-map corpus | **Built** tools / **Partial** play | ~72 MP maps cook corpus; catalog/multi-map tooling; layered RotWK+BFME2 terrain | Connected starts everywhere; multi-map play polish; campaign/WOTR map packs as product |
| 3 | Asset conversion + object binding | **Partial** | W3D→GLB path; binding factory ~80% type bind over corpus (ledger burn-down) | Unbound props/types (flags, inns, lairs…); animation depth; FX completeness |
| 4 | Simulation core | **Built** (Godot) / **Partial** (C#) | Large battalion sim: teams, production, combat, creeps, CP, knockback, trample, AI tiers | Pack-descriptor-driven generality; C# cutover; Class-C module execution |
| 5 | Combat / economy / structures | **Built** (depth uneven) | Build, produce, fight, upgrades, spellbooks, capture/creeps under runners | Cross-faction signature green; garrison/FoW; naval; full structure lifecycle parity |
| 6 | Script engine + skirmish AI | **Built** / coverage **Partial** | Deterministic script VM; WP handlers for AI/economy/teams/units/CP/build permissions | Blocked FoW/garrison packages; remaining opcode gaps; campaign-scale coverage |
| 7 | Skirmish shell (menu/setup/options) | **Built** | Main menu, N-player skirmish setup, 7 factions (incl. Angmar), options, boot path | Tutorials off; stats stub; ring heroes/CaH toggles locked; map list vs full catalog |
| 8 | Retail HUD (APT/WND) | **Built** / visual parity **Partial** | Converted APT bytecode VM + WND runtime on slice path | Full chrome/oracle parity; multipack binding consistency |
| 9 | Multiplayer | **Partial** | ENet lockstep, lobby, LAN discovery, UPnP helpers, 2–8 peer smoke runners | Late join, production hardening, dedicated servers, ranked-free but solid listen/host |
| 10 | War of the Ring | **Partial** (strategic **strong**) | Setup screen, 3D living map, regions, AI turns, construction/treasury, autoresolve, handoff briefs | Recruit/CP economy; building nuggets; army merge/split; full tactical fight + outcome report |
| 11 | Campaigns (Good / Evil / Angmar) | **Scaffold** | Script foundation + map taxonomy; menu entries **disabled** | Cooked mission packs, mission shell, objectives UI, cinematics, saves |
| 12 | Create-a-Hero | **Scaffold** | INI census; **MY HEROES** disabled | Editor, runtime heroes, persistence, UI |
| 13 | Launcher + releases | **Built** | WPF launcher: discover BFME2/RotWK, import, signed update path, Play | Align default import with full RotWK systems path; channel feed for prereleases; public release policy |
| 14 | Module / behavior contracts | **Partial** | Census of 244 RotWK module kinds; contracts + some die/runtime consumers; C# module registry | Execute Class-C majority (~179 kinds classified C); shrink refused surface |
| 15 | Presentation (terrain/props/FX) | **Partial** | Fords-class terrain path strongest; roads, fog, particles, animated props with runners | Multi-map prop bind; particle ID coverage; water/env parity corpus-wide |
| 16 | One-button convert & play | **Built** entry / **Partial** completeness | `run_rotwk_one_button.bat` / systems gate; optional multi-map publish/launch | “Whole game converts clean” is not claimed; publish remains owner-gated |

---

## What is built (in more detail)

### Launcher (Windows)

Ship-shape product surface, not a stub:

- **Stack:** .NET WPF (`launcher/OpenBFME.Launcher`), companion tests, release installer/self-update  
- **Can:** detect BFME II and RotWK installs, headless flags (`--import-bfme2`, `--import-rotwk`, channel, verify-only), bootstrap pinned Python/tools, convert, set `OPENBFME_CONTENT`, launch  
- **Import reality check:** BFME2 path builds historical Men/Fords profile; RotWK path currently **import-faction Angmar + publish-to-slice** — a valid play path, **not** the full systems factory convert of every faction/map  
- **Operator full path (devs):** `run_rotwk_systems.bat` / `run_rotwk_one_button.bat` with doctor, map cook, binding factory, faction batch, optional multi-map  

### Content pipeline

- Default game edition **RotWK** in importer CLI  
- Conversion **ledger** (converted / failed / gap with detail)  
- Tools: `rotwk_map_cook_corpus.py`, `rotwk_binding_factory.py`, `rotwk_faction_convert_batch.py`, `rotwk_faction_pack_proof.py`, `rotwk_multimap_skirmish.py`, `rotwk_layered_install.py`  
- Offline gate: `tools/gate-rotwk-systems.ps1 -SkipLiveRetail`  

### Match simulation & skirmish

- Authoritative runtime: `game/src/retail_slice/retail_slice_sim.gd` (~14k lines) — battalions, structures, economy, command points, AI difficulty tiers, creeps, stances/formations, trample/knockback, N-team  
- Supporting: production queues, spellbooks, hero abilities path, upgrades, builder construction, capture buildings  
- **~150** focused Godot `*_runner.gd` tests across sim, HUD, scripts, MP, WOTR  
- Pure C# kernel `engine/OpenBfme.Sim` exists (fixed-point, modules, determinism tests) but is **not** yet the player’s match engine  

### Script & AI

- Script executor + fail-closed **gap log** (no silent skip of unimplemented ops)  
- Handler packages: core state, camera, audio/video, UI/radar, hero/objectives, AI core/economy/basebuilding/powers/pathing/teams/units/players, command points, build permissions  
- Explicitly **blocked** packages: fog-of-war, transport/garrison, misc missing — named, not faked  
- Script world surface map: **327** methods probed simulation-backed (callable ≠ full retail parity)  

### Shell & HUD

- Retail-inspired main menu with lazy load of heavy surfaces (measured boot work)  
- Skirmish setup with 7 factions; multiplayer lobby flyout  
- Retail **APT VM** + **WND** HUD runtimes; shell APT path  
- Options/pause surfaces with runners  
- **Stats screen:** honest stub (shows “—”; does not invent career stats)  

### Multiplayer

- Deterministic lockstep session over ENet (up to 8 seats in design/tests)  
- Lobby + LAN discovery + UPnP helpers  
- Status: **early but real** — not a finished online product  

### War of the Ring (major recent work)

Code volume ~1.2MB of WOTR GDScript alone; **24** `wotr_*` runners. Recent commits landed strategic map chrome, GAME SETUP, retail markers/regions, construction, AI, and **live auto-resolve** (retail numbers + Risk dice where retail is silent).

**Working / advanced:**

- Living-world document mount when packs provide it  
- Setup screen with authored bindings  
- 3D strategic map, region geometry, banners, build plots, HUD chrome  
- Strategic AI using retail AI template weights where bound  
- Treasury + building construction rules with named project-authored arithmetic where retail is silent  
- Neutral region claim path  
- Battle request → brief → auto-resolve path; handoff schema toward tactical  

**Open strategic gaps (code-named in `wotr_strategic_gaps.gd`):**

- Building nuggets (CP grants, armor tables, upgrade troops, spawn queues beyond treasury)  
- Army recruitment + true CP costs  
- Army merge/split  
- AI recruitment ratios (`Desired*Ratio` weights unspent)  

**Open tactical handoff gaps (`wotr_handoff.gd`):**

- Reinforcement schedule  
- Full tactical battle outcome report  
- Carried hero level / fallen heroes into match  
- Prebuilt fortress / standing buildings on tactical  
- Region bonus modifiers into combat  

### Module surface (honest denominator)

RotWK census (`game/data/retail_module_census.json`):

| Metric | Value |
|--------|------:|
| Distinct module kinds | 244 |
| Declaration sites | 21,044 |
| Objects | 5,176 |
| Importer “consumed” kinds | 226 |
| Refused | 19 |
| Class C (needs runtime systems) | **179** |
| Class A / B / D / E | 9 / 17 / 39 / 1 |

**Important:** “consumed” means the **importer names/extracts** the type — **not** that the behavior fully runs in match.

---

## What is still incomplete (roadmap by ladder)

Aligned with `DIRECTION.md` system ladder → retail feature-complete:

### Near-term (systems factory + skirmish floor)

1. **Burn converter + binding gaps** — UI images, damage scalars, unbound map types; raise multi-map bind %  
2. **Multi-map skirmish play** — catalog is ahead of “any official map feels like Fords”  
3. **Faction suite greening** — historical STATUS showed non-Men suites failing; re-measure after rewrite settles (docs stale)  
4. **Sim driven from pack descriptors** for RotWK generality (Angmar + 6 BFME2 sides equally)  
5. **Module Class-C burn-down** prioritized by skirmish-used behaviors  
6. **Script/AI coverage growth** — especially blocked FoW/garrison when subsystems exist  

### Mid-term (product shell completeness)

7. **Presentation/HUD** corpus-wide (not Fords-only oracles)  
8. **Create-a-Hero** full path  
9. **Saves, replays, observers, custom maps**  
10. **Multiplayer production** (no late join today; harden listen/host)  

### Later ladder (still in product scope)

11. **Campaigns** — Good/Evil + Angmar campaign content; mission cook + script coverage (plan docs claim script % progress historically; **play path still off**)  
12. **WOTR completion** — recruit/economy, nuggets, phased turns if required, **fought** tactical battles with full handoff  
13. **C# sim cutover** for lockstep authority  
14. **Modern product layer** — accessibility, mod management, safe mode, polished one-button UX  

### Explicitly not “done” claims

- Public “works on every map/faction like retail”  
- Campaign play  
- Create-a-Hero  
- Statistics tracking  
- Tutorials  
- Ring heroes as converted feature  
- Silent full visual parity  

---

## Menu feature map (player-facing truth)

| Menu entry | State |
|------------|--------|
| Skirmish | **Enabled** |
| War of the Ring (SOLO) | **Enabled when living-world + factions available** |
| Multiplayer | **Enabled** (early) |
| Options / settings quality presets | **Enabled** (per-detail sliders still disabled) |
| Good / Evil campaign | **Disabled** — “No campaign missions have been converted” |
| Tutorials (basic/advanced/WOTR tutorial) | **Disabled** |
| Load game | **Disabled** |
| MY HEROES / Create-a-Hero | **Disabled** |
| Credits | **Disabled** |
| Stats | **Visible stub** (no tracked stats) |

---

## Doc hygiene note (for maintainers)

Verification found these public claims **out of date vs code**:

| Doc claim | Code reality |
|-----------|--------------|
| README: “Campaigns and War of the Ring … not started” | **WOTR is substantially implemented** (strategic layer + tests). Campaigns remain non-playable. |
| STATUS.md (2026-07-22) primary blockers | Still useful as historical diagnostics; **does not reflect** post-07-25 WOTR and systems-factory work |
| Product contract campaigns/WOTR “deferred” | Means **ladder order / evidence policy**, not “zero code” for WOTR |

---

## How to verify this week (smallest honest checks)

```bat
:: Offline systems gate (no retail required for -SkipLiveRetail)
powershell -File tools\gate-rotwk-systems.ps1 -SkipLiveRetail

:: With install
set ROTWK_INSTALL=F:\RotWK
run_rotwk_systems.bat

:: One-button (optional launch)
run_rotwk_one_button.bat F:\RotWK --launch
```

Focused game suites (examples): `menu_skirmish_runner`, `retail_slice_runner`, `wotr_strategic_runner`, `wotr_playability_runner`, `retail_lockstep_determinism_runner`, handler `handlers_wp*_runner` set.

Launcher: build/run `OpenBFME.Launcher`; import BFME2 or RotWK from UI or `--import-rotwk --rotwk-path …`.

---

## Suggested narrative for community / Discord

**This week’s honest pitch:**  
“We’re building the **full RotWK product** systems-first. Skirmish systems, seven-faction conversion paths, a real launcher, and a **playable War of the Ring strategic layer** (setup, map, AI, build, auto-resolve) are in the tree with heavy automated coverage. Campaigns and Create-a-Hero are still off. Tactical WOTR fights and full multi-map presentation still need work. Not a finished game — a rapidly expanding alpha engine with fail-closed gaps instead of fake features.”

---

## Next week focus (proposed sprint themes)

Pick **one primary** (project rule: one major system per packet):

1. **WOTR tactical handoff consumers** — close `UNSUPPORTED_BY_TACTICAL_SIM` items so strategic battles can land on the slice sim with fort/heroes/outcomes  
2. **or** multi-map skirmish boot smoke across catalog maps (presentation + starts)  
3. **or** Class-C module slice with highest skirmish hit-rate  
4. **or** re-green faction runners under a stable code/pack identity and refresh `STATUS.md`  

Secondary hygiene: fix README WOTR/campaigns row so public messaging matches code.

---

## Appendix A — evidence scale (order of magnitude)

| Surface | Scale |
|---------|------:|
| Godot test runners (`*_runner.gd`) | ~150 |
| WOTR-specific runners | 24 |
| Script handler packages | 22 files |
| Retail slice sim size | ~700 KB source |
| WOTR source tree | ~1.2 MB GDScript |
| RotWK module kinds / Class C | 244 / 179 |
| Map cook corpus (operator docs) | 72 official MP maps |
| Binding factory (operator docs) | ~80% types bound over corpus |

## Appendix B — verification method

- Primary inventory: repository tree under `game/`, `importer/`, `launcher/`, `engine/`, `tools/`  
- Independent read-only explore agent cross-check of the same systems (2026-08-01)  
- Gap registers treated as authoritative incompleteness lists: `wotr_strategic_gaps.gd`, `wotr_handoff.gd`, `script_gaps.gd`, `retail_module_census.json`  
- Volatile gate pass/fail counts were **not** re-run end-to-end in this audit; do not treat this document as a green release certificate  

---

*Template for future weekly updates: keep the system board table, replace “what changed this week” with commit/PR highlights, re-measure one gate or gap %, and restate the single primary system for the next sprint.*

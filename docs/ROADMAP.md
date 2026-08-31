# RotWK 2.02 v9.7.7 parity roadmap

This is the navigable product map for an exact, clean-room Godot port of
**The Lord of the Rings: The Battle for Middle-earth II - The Rise of the
Witch-king, Patch 2.02 v9.7.7**. The machine-readable source of live status,
dependencies, ownership, and acceptance is
[`orchestration/work-items.json`](../orchestration/work-items.json). The
[product-scope contract](../contracts/rotwk-202-v9.7.7-product-scope.json),
[retail baseline](../contracts/rotwk-202-v9.7.7-baseline.json), and
[verification contract](VERIFICATION.md) outrank prose.

The roadmap deliberately separates four questions that older reports mixed:

1. **Structural source coverage:** was the exact retail requirement found and
   classified?
2. **Conversion coverage:** was that exact source deterministically converted
   with provenance?
3. **Runtime and behavior parity:** did the shipping Godot path consume it,
   and does state agree with the original game?
4. **Presentation parity:** do matched visual and audio observations agree?

A high count in an earlier column never proves a later column.

## Evidence register

Short evidence IDs below map one-to-one to `evidenceSources` in the work-item
ledger. Retail payloads and raw derived rows remain under ignored `workspace/`;
tracked files contain only identities, counts, methods, and sanitized results.

| ID | Authority | What it supports | Claim limit |
|---|---|---|---|
| `E-BL-202` | [`contracts/rotwk-202-v9.7.7-baseline.json`](../contracts/rotwk-202-v9.7.7-baseline.json) | official-2 v9.7.7 identity, three-layer archive policy, required package hashes, overlay and semantic census, red checkout snapshot | L1/SOURCE only |
| `E-SCOPE-202` | [`contracts/rotwk-202-v9.7.7-product-scope.json`](../contracts/rotwk-202-v9.7.7-product-scope.json) | included product domains, root discovery, evidence lanes, complete-claim rules | Policy/denominator only |
| `E-AUDIT-CORPUS-20260829` | Sanitized receipt in the canonical ledger, reproduced with `InstallCatalog`, `retail-ini-coverage.py`, and `map_census.py` against catalog `e1485aa8...` | effective winner, file-type, semantic, object-module, map-cache, and audio counts | Structural L1; no conversion/parity claim |
| `E-EFFECTIVE-TREE-20260830` | Accepted private artifact at implementation `55802bba`, SHA-256 `52fe8f2e...` | 53,433 archive records, 48,566 effective winners, 4,867 shadows, and precedence/provenance chains | L1 effective-tree closure only |
| `E-LEXICAL-CENSUS-20260830` | Accepted private artifact at implementation `68cbdde1`, SHA-256 `cdb25cf...` | exhaustive lexical accounting for 850 INIs, 36,940 definitions, 597,886 assignments, modules, scripts, and asset-reference sites | L1 lexical census only; no executed roots, semantic reference closure, domain routing, conversion, or parity |
| `E-AUDIT-CURRENT-20260829` | Baseline `currentCheckoutSnapshot`, `workspace/content-packs/selection.json`, and named private reports | audited implementation/selection boundary and stale-report diagnoses | Historical red baseline only |
| `E-AUDIT-REPO-20260829` | Read-only Git/worktree and ignored-payload inventory summarized in the ledger | cleanup risk and retention requirements | Repository hygiene only |
| `E-AUDIT-ASSETS-20260829` | Git-object/LFS census over two exact roots and eight exact extra paths at revision `f70f13eb...` | 611-record public-asset universe, eight disjoint disposition sets, Godot metadata debt, and literal candidate digests | L0 inventory and planning only; no deletion, load, or parity claim |

Any count not carrying one of these IDs is context, not a completion
denominator.

## Current proof boundary

The source identity is pinned. Product conversion and parity are not.

| Boundary | State on 2026-08-30 | Evidence | Consequence |
|---|---|---|---|
| Product identity | **Accepted for SOURCE work**: Patch 2.02 v9.7.7 English, package GUID `official-2` | `E-BL-202`; accepted source-baseline verification | Work may target this identity only; no lower patch or nearby install can substitute. |
| Archive policy | **Accepted**: policy `aaf30a92eacc76a8b11c0534235e569653f06fecb47400e28260981b5a04cf31` | `E-BL-202` | Nearby 2.01, BFME2-only, and arbitrary install scans are not substitutes. |
| Layered catalog | **Accepted**: catalog `e1485aa8af794e0d154d2f5ccb65fa24af937c4ac9731f27d62e0eef9753c748` | `E-BL-202` | The exact denominator is 53,433 raw records. |
| Effective tree receipt | **Sealed**: 48,566 winners + 4,867 shadows, SHA-256 `52fe8f2e...` | `E-EFFECTIVE-TREE-20260830` | Precedence and byte provenance are closed; feature semantics are not. |
| Lexical feature census | **Sealed with a strict claim limit**: SHA-256 `cdb25cf...` | `E-LEXICAL-CENSUS-20260830` | Syntax is exhaustively accounted, but 290,942 opaque assignments, 76,656 unresolved asset references, and 2,203 ambiguous asset references remain explicit. |
| Semantic requirement graph | **Absent** | `P0-CORPUS-003` | Product roots, typed target edges, domain membership, map/mode rows, and residual closure are not yet generated. |
| Owner routing policy | **Absent** | `P0-ROUTING-001` | No agent may infer terminal envelope, mutable files, prerequisites, checks, or evidence dimensions. |
| Current selected packs | **Invalid for 2.02 parity**: 100 mounted refs, zero audited 2.02 source markers | `E-AUDIT-CURRENT-20260829` | Old pack success cannot count toward 2.02. |
| Tracked public assets | **Quarantined debt**: 611 Git/LFS-aware records, 432,587,789 canonical bytes, eight exhaustive disposition sets | `E-AUDIT-ASSETS-20260829` | No tracked approximation is retail parity; deletion waits for an accepted immutable index and exact-path lane. |
| Static runtime coverage | **Stale 2.01 diagnostic only** | `E-AUDIT-CURRENT-20260829` | Regenerate after the 2.02 graph and clean recook. |
| Behavior parity | **Unproved** | `E-BL-202`, `E-SCOPE-202` | No subsystem may claim gameplay parity. |
| Visual parity | **Unproved** | `E-BL-202`, `E-SCOPE-202` | Asset presence, load, and screenshots without matched oracle conditions do not qualify. |
| Audio parity | **Unproved** | `E-BL-202`, `E-SCOPE-202` | Audio paths or playback alone do not qualify timing, routing, and mix. |
| Complete product | **Blocked** | `E-SCOPE-202` | Skirmish, shell, BASIC/ADVANCED/WOTR tutorials, Good/Evil/Angmar campaigns, War of the Ring, and Create-a-Hero all remain required. |

The current checkout snapshot reported 7 compiled factions, 137 units, 155
structures, and `369 passed / 61 failed` in the retail-slice runner. Its active
manifest explicitly marked asset conversion, full-faction completion, oracle
parity, vertical-slice completion, source closure, and denominator closure
false (`E-BL-202`). Those are useful implementation facts, not 2.02 acceptance.

## Frozen source denominators

### Layering and records

Lower order wins. Every complete source claim must bind all three layers.

| Order | Layer | BIG archives | Role | Evidence |
|---:|---|---:|---|---|
| 0 | official Patch 2.02 v9.7.7 overlay | 4 | gameplay/maps/art/audio, music, HD presentation, English localization overrides | `E-BL-202` |
| 1 | RotWK 2.01 English expansion base | 106 | expansion base, including `lang/EnglishAudio.big` | `E-BL-202` |
| 2 | BFME2 1.06 English base | 107 | base-game dependency layer | `E-BL-202` |
| **Total** | exact 2.02 effective source | **217** | **53,433 raw archive records; 48,566 case-folded effective winners** | `E-BL-202`, `E-AUDIT-CORPUS-20260829` |

### Effective corpus

These are discovery denominators, not promises that every file maps to one
runtime feature. The feature graph must classify relationships and product
roots before conversion can claim closure.

| Corpus surface | Exact audited denominator | Evidence |
|---|---:|---|
| Parseable effective INI documents | 850 | `E-BL-202`, `E-AUDIT-CORPUS-20260829` |
| INI bytes / meaningful lines | 36,072,803 / 849,017 | `E-AUDIT-CORPUS-20260829` |
| Top-level definition sites | 36,940 | `E-BL-202`, `E-AUDIT-CORPUS-20260829` |
| Assignment sites | 597,886 | `E-BL-202`, `E-AUDIT-CORPUS-20260829` |
| Unique object identifiers | 5,494 | `E-BL-202`, `E-AUDIT-CORPUS-20260829` |
| Asset-reference sites | 188,543 | `E-AUDIT-CORPUS-20260829` |
| Object-module declarations / kinds | 24,230 / 236 | `E-AUDIT-CORPUS-20260829` |
| Script-call sites | 4,384 | `E-AUDIT-CORPUS-20260829` |
| Effective W3D paths | 14,539 | `E-AUDIT-CORPUS-20260829` |
| Effective image paths (`dds`, `tga`, `jpg`, `png`) | 9,063 | `E-AUDIT-CORPUS-20260829` |
| Effective BSE effect paths | 233 | `E-AUDIT-CORPUS-20260829` |
| Effective audio paths (`wav`, `mp3`) | 19,194 | `E-AUDIT-CORPUS-20260829` |
| Effective INI/INC archive paths | 1,182 | `E-AUDIT-CORPUS-20260829` |
| Effective map paths | 419 | `E-AUDIT-CORPUS-20260829` |
| MapCache rows / multiplayer payloads | 349 / 299 | `E-BL-202`, `E-AUDIT-CORPUS-20260829` |
| Effective APT/WND paths | 86 / 18 | `E-AUDIT-CORPUS-20260829` |

`419`, `349`, and `299` answer different questions: effective `.map` archive
paths, MapCache records, and multiplayer payloads. The legacy 72/74 pack is a
selected implementation slice and is not any of these product denominators.

### Minimum 2.02 override churn

The overlay alone has 4,342 raw members and 4,337 effective paths: 2,437 are
new versus 2.01 and 1,900 replace a 2.01 path (`E-BL-202`). Until a clean
2.02 recook closes the graph, the following are minimum known retargeting
surfaces, not the whole work program.

| Overlay surface | Paths touched | Required treatment |
|---|---:|---|
| INI/INC | 1,126 | reparse, reclassify, re-resolve, recook, runtime/oracle requalify |
| W3D | 545 | reconvert or prove byte-identical resolved input, then load/visual qualify |
| Images | 1,903 | recook with provenance, then authored-use and visual qualify |
| Audio | 390 | recook/reroute, then matched timing and mix qualify |
| Map payloads | 317 | reclassify/cook and requalify scripts, nav, behavior, visuals, and audio |

The RotWK base English audio archive contributes 2,066 effective winners
(2,028 WAV and 38 MP3). A zero-size manifest sentinel must never exclude it;
the tracked archive policy is authoritative (`E-AUDIT-CORPUS-20260829`).

## Dependency phases

### P0 - make truth reproducible and agent-safe

P0 does not add a parity percentage. It prevents agents from optimizing stale
2.01 artifacts or destroying private evidence.

| Work item | Outcome | Exit condition |
|---|---|---|
| `P0-SOURCE-001` | Pin exact archive policy/catalog | Accepted: the exact v9.7.7 policy, package set, catalog, and SOURCE claim boundary are canonical. |
| `P0-SOURCE-002` | Independently accept the read-only baseline verifier | Accepted: the verifier and private receipt bind the current baseline identity. |
| `P0-CORPUS-001` | Seal effective winners | Content-addressed private winner manifest closes at 48,566 with provenance. |
| `P0-CORPUS-002` | Generate exhaustive lexical census | Accepted: all syntax sites account exactly; opaque/unresolved/ambiguous rows remain explicit and do not claim semantic closure. |
| `P0-CORPUS-003` | Close semantic requirement graph | All eight root queries execute; typed nodes/edges, domain and map/mode membership, provenance, and residuals close against the lexical census. |
| `P0-ROUTING-001` | Author closed owner routing policy | After semantic, code-map, and asset closure, every selector/evidence-lane class maps exactly once to one terminal envelope and a finite set of exact modify/delete tracked paths or exact classified absent create targets. |
| `P0-EVIDENCE-001` | Generate evidence catalog and coverage matrix | Product-contract generated outputs exist, validate, and fail closed on unknown/stale rows. |
| `P0-LANES-001` | Generate bounded worker lanes | One input-bound batch planner copies accepted coverage routing into full deterministic child rows for exactly 33 P1/P2 closure envelopes while three rollups remain owner-only. Its structured owner materializer commits exactly plan/ledger/boundaries, leaves the planner in verification, and requires a later reviewed ledger-only acceptance. |
| `P0-SELECTION-001` | Quarantine legacy selection claims | The 100-pack 2.01-derived stack is visibly historical and cannot satisfy 2.02 gates. |
| `P0-SELECTION-002` | Define and publish a clean 2.02 selection | One deterministic, provenance-complete v9.7.7 topology replaces accumulated supplements. |
| `P0-DEFAULTS-001` | Retarget stale defaults and pins | No `men-fords-v0`, five-map, 72/74, or old hash can pass a 2.02 gate. |
| `P0-GATES-001` | Independently accept fail-closed runtime admission | Implemented: the runner now requires `failed == 0` and has a focused static regression; final independent proof and canonical wrapper admission remain open. |
| `P0-ORACLE-001` | Build isolated 2.02 oracle harness | Exact executable/source identity and matched scenario recipes are reproducible without contaminating live installs. |
| `P0-AGENTS-001` | Implement autonomous worker lanes | Owner implementation in progress: the compact workflow keeps one item/worktree/commit, literal paths, closed commands, private receipts, and independent review without a custom process hypervisor. The focused admission check and hostile review must pass before the row advances. |
| `P0-HISTORY-INDEX-001` | Seal stale-history disposition | A read-only checker freezes exactly 91 deletion records and seven support mutations before any deletion is allocatable. |
| `P0-HISTORY-001` | Remove stale narrative/evidence authority | The accepted 91-record index is executed exactly; old briefs/reports and the frozen 2.01 coverage page remain recoverable only from Git history. |
| `P0-HYGIENE-001` | Restore publication-boundary hygiene | The focused gate stays green after integration and a durable sanitized receipt closes the item. |
| `P0-CODE-MAP-001` | Enforce repository/code ownership boundaries | In verification: the current 2,692-path map passes its focused checker; independent review and an owner acceptance transition remain open. |
| `P0-ASSET-HYGIENE-001` | Seal public-asset index and transition gate | The immutable 611-record index, consumer graph, exact disposition sets, and fail-closed transition checker land without mutating a baseline asset. |
| `P0-ASSET-PURGE-001` | Remove the sealed safe-purge set | Exactly 48 asset records and three dead guessed writers are removed; rooted sidebar consumers are closed in the same lane. |
| `P0-ASSET-FIXTURES-001` | Replace donor-looking test payloads | Six old records become three tiny purpose-named synthetic external-pack fixtures that are never export eligible. |
| `P0-ASSET-INPUT-001` | Remove unattested input glyphs | Forty image/sidecar records are removed while code-native input semantics and selected-retail hooks remain. |
| `P0-ASSET-AUDIO-001` | Remove public placeholder audio | All 106 placeholder audio records and their dead generator route are removed; selected private retail audio remains the parity path. |
| `P0-ASSET-ICONS-001` | Remove approximation icons | All 242 icon records are removed after consumer closure; no substitute raster is promoted to parity. |
| `P0-ASSET-MODELS-001` | Remove approximation models | All 150 model records and stale OBJ identity prose are removed; exact private conversion remains authoritative. |
| `P0-ASSET-SHELL-001` | Remove shell approximations | Sixteen splash/icon/backdrop records and every rooted fallback are removed while selected-retail shell routing remains. |
| `P0-ASSET-LAUNCHER-001` | Close launcher branding | Two unattested PNGs are removed; one byte-pinned C2PA banner remains explicitly non-parity project branding. |
| `P0-GODOT-IDENTITY-INDEX-001` | Seal Godot repair plan | After all eight dispositions, a read-only index freezes 635 scripts, 570 UIDs, 65 missing UIDs, one remaining import, and 67 exact mutation paths. |
| `P0-GODOT-IDENTITY-001` | Apply Godot repair plan | The 65 sealed UID sidecars are materialized, the last tracked import is removed, and two clean imports leave tracked bytes stable. |
| `P0-ASSET-EXPORT-001` | Unify export admission | Every Godot, bundle, release, CI, and launcher path uses one identity allowlist with pre-stage and post-package verification. |
| `P0-ASSET-CLOSE-001` | Close the asset debt graph | Eight overlays account for all 611 originals, export receipts admit only intended public media, and repository boundaries enter closed mode. |
| `P0-REPO-001` | Inventory worktrees before retirement | Every registered tree has merged/dirty/untracked/ignored/preservation disposition; no bulk deletion. |
| `P0-RETENTION-001` | Define private pack/workspace retention | Provenance roots and last-known-good packs are retained; deletion candidates have recoverable receipts. |

Critical P0 chain:

```text
P0-SOURCE-002 -> P0-SOURCE-001
P0-SOURCE-002 -> P0-CORPUS-001 -> P0-CORPUS-002 (lexical) -> P0-CORPUS-003 (semantic)
P0-CORPUS-003 + P0-CODE-MAP-001 + P0-ASSET-CLOSE-001 -> P0-ROUTING-001
P0-CORPUS-003 + P0-ROUTING-001 + P0-CODE-MAP-001 + P0-ASSET-CLOSE-001 -> P0-EVIDENCE-001
P0-EVIDENCE-001 + P0-SELECTION-001 + P0-GATES-001 -> P0-SELECTION-002

P0-DEFAULTS-001 + P0-GATES-001 + P0-ORACLE-001 must be green before any parity acceptance.
P0-REPO-001 -> P0-RETENTION-001 protects evidence; neither raises product parity.

P0-AGENTS-001 + P0-CODE-MAP-001
  -> P0-HISTORY-INDEX-001 -> P0-HISTORY-001 -> P0-HYGIENE-001
  -> P0-ASSET-HYGIENE-001 -> P0-ASSET-PURGE-001
       -> {FIXTURES, INPUT, AUDIO, ICONS, MODELS, SHELL, LAUNCHER}
       -> P0-GODOT-IDENTITY-INDEX-001 -> P0-GODOT-IDENTITY-001
       -> P0-ASSET-EXPORT-001 -> P0-ASSET-CLOSE-001

The braces abbreviate the seven `P0-ASSET-*` disposition IDs listed above.
Every deletion path is machine-authorized by a completed direct index dependency;
none of these L0 hygiene results proves retail asset parity.

P0-CORPUS-003 + P0-ROUTING-001 + P0-EVIDENCE-001 + P0-AGENTS-001 + P0-ASSET-CLOSE-001
  -> P0-LANES-001 -> bounded generated P1/P2 child rows -> closure envelopes
```

### P1 - complete 2.02 skirmish and its required shell

P1 works only from the frozen graph, accepted `P0-LANES-001` plan, and
clean selected identity. The broad rows below are closure envelopes, not
single worker commits. Their generated children own disjoint requirement and
file sets; integration-owner changes to the ledger, selection, canonical
evidence, or acceptance remain serialized.

| Track | Work items | Required result |
|---|---|---|
| Retail data and objects | `P1-DATA-001`, `P1-OBJECT-001` | All 850 documents, 36,940 definitions, 597,886 assignments, 5,494 objects, and their reachable references receive an explicit disposition. |
| Runtime module execution | `P1-MODULE-001`, `P1-MODULE-010` through `P1-MODULE-050` | `P1-MODULE-001` losslessly normalizes all 236 kinds and 24,230 sites; only `P0-LANES-001` allocates graph requirements, and bounded generated children close runtime/oracle behavior. |
| Simulation | `P1-SIM-001` | Economy, production, command points, combat, death, upgrades, powers, hordes, heroes/forms, ring mechanics, structures/walls, naval, neutral content, and deterministic outcomes match 2.02. |
| Assets and presentation | `P1-ASSET-001` through `P1-ASSET-003` | W3D, images, and BSE dependencies close without generic/placeholder fallback, then pass load and matched visual evidence. |
| Maps and navigation | `P1-MAP-001`, `P1-MAP-002` | All 419 effective paths are classified; all 299 multiplayer payloads have deterministic cook, boot, object binding, nav/buildability, scripts, and mode disposition. |
| Scripts and AI | `P1-SCRIPT-001`, `P1-AI-001` | Every reachable call and AI rule is handled or blocks; no dispatch-only completeness. |
| Shell, HUD, and authored UI | `P1-UI-001` | Every retail-reachable skirmish setup/HUD/loading/observer control is authored, interactive, and oracle-qualified. |
| Audio/music/localization | `P1-AUDIO-001`, `P1-AUDIO-002` | All reachable audio/localization references convert/load; matched playback, routing, timing, and mix are approved. |
| Independent qualification | `P1-ORACLE-001`, `P1-VISUAL-001`, `P1-QUAL-001` | Required skirmish/shell rows reach L5 and end-to-end qualification reaches L6 with zero severity-0/1 gaps. |

### P2 - close the full product and OpenBFME product layer

P2 cannot be used to excuse missing P1 evidence. It closes every remaining
included retail domain and qualifies OpenBFME networking/modding separately
from retail parity.

| Domain | Work items | Required result |
|---|---|---|
| Tutorials and campaigns | `P2-CAMPAIGN-001` through `P2-CAMPAIGN-003` | BASIC/ADVANCED/WOTR tutorial and Good/Evil/Angmar campaign roots, mission maps/scripts/objectives/cinematics/UI/audio, progression, and complete mission oracles qualify. |
| War of the Ring | `P2-WOTR-001` through `P2-WOTR-003` | Strategic data, turns, territories, armies, battle handoff, persistence, UI/audio, SP/MP behavior, and original-game comparisons qualify. |
| Create-a-Hero | `P2-CAH-001`, `P2-CAH-002` | Classes, parts, powers, progression, authoring UI, persistence, skirmish use, and oracle evidence qualify. |
| Complete retail shell | `P2-SHELL-001`, `P2-SHELL-002` | Main menu, options, loading, localization, save/load, replay, observer, credits/cinematics, and all domain transitions qualify. |
| OpenBFME networking | `P2-NET-001` | Two-to-eight player deterministic, server-refereed self-hosting, LAN/direct-connect, replay, reconnect, recovery, fault, scale, and security contracts pass. |
| Modern modding | `P2-MOD-001` | Pack schemas, simulation/presentation separation, compatibility hashes, overrides, limits, diagnostics, and dedicated-server policy pass. |
| Product/release | `P2-QUAL-001`, `P2-RELEASE-001` | Full denominator is current under one evidence identity; reliability, packaging, containment, and code-only distribution qualify. |

## Subsystem gap map

`Structural` describes discovery/conversion facts. `Parity` requires runtime
and original-game evidence. “Blocked” means prerequisite evidence is absent,
not that no code exists.

| Subsystem | Exact target and evidence | Structural state | Runtime/behavior state | Visual/audio state | Work items |
|---|---|---|---|---|---|
| Source/catalog | 217 archives, 53,433 records, 48,566 winners (`E-BL-202`, `E-EFFECTIVE-TREE-20260830`) | Policy/catalog and effective winner receipt accepted | not applicable | not applicable | `P0-SOURCE-001/002`, `P0-CORPUS-001` |
| Repository/code boundaries | all current 2,692 tracked paths, 27 rules, 14 generated consumers, 7 autoloads, and 13 high-conflict files | `P0-CODE-MAP-001` machine classification passes its focused checker but remains in independent verification; compact lane admission and later envelope splitting remain blocked on `P0-AGENTS-001`/`P0-LANES-001` | not applicable | not applicable | `P0-CODE-MAP-001`, `P0-AGENTS-001`, `P0-LANES-001`, `P0-HYGIENE-001` |
| Public non-retail assets | 611 exact Git/LFS records in eight disjoint sets (`E-AUDIT-ASSETS-20260829`) | census and machine plan exist; index, dispositions, identity repair, export admission, and closure are not implemented | rooted consumers are mapped but current approximations and `all_resources` export remain live | no public asset establishes retail visual/audio parity | `P0-ASSET-HYGIENE-001` through `P0-ASSET-CLOSE-001`, `P0-GODOT-IDENTITY-INDEX-001/001` |
| Requirement graph and routing | 850 docs, 36,940 definitions, 597,886 assignments, 188,543 asset refs (`E-LEXICAL-CENSUS-20260830`) | lexical census accepted; executed semantic roots/edges and owner routing absent | target consumption matrix absent | target oracle matrix absent | `P0-CORPUS-003`, `P0-ROUTING-001`, `P0-EVIDENCE-001`, `P1-DATA-001` |
| Factions/objects | 5,494 lexical object identifiers (`E-LEXICAL-CENSUS-20260830`) | faction/root reachability and domain membership await `P0-CORPUS-003`; current packs compile only 137 units/155 structures | broad 2.02 faction behavior unproved | broad authored presentation/audio unproved | `P0-CORPUS-003`, `P1-OBJECT-001`, `P1-SIM-001` |
| Object modules | 236 lexical kinds / 24,230 declaration sites (`E-LEXICAL-CENSUS-20260830`) | exact 2.02 census accepted; semantic ownership/execution mapping absent | 2.02 execution/oracles absent | module-driven draw/animation/effects unproved | `P0-CORPUS-003`, `P1-MODULE-*` |
| Maps | 419 effective paths; 349 MapCache rows; 299 MP payloads (`E-BL-202`, `E-AUDIT-CORPUS-20260829`) | old selected pack has 74 only; old reports pin 72/74 and 2.01 | nav, complete matches, scripts, AI, and mode parity absent | per-map visual/audio oracles absent | `P1-MAP-001/002`, `P1-SCRIPT-001`, `P1-QUAL-001` |
| Scripts/AI | 4,384 lexical script-call sites (`E-LEXICAL-CENSUS-20260830`) | AI roots, reference reachability, and domain membership await semantic closure | target call/state and authored AI outcome matrix absent | scripted cinematic/audio effects unproved | `P0-CORPUS-003`, `P1-SCRIPT-001`, `P1-AI-001` |
| W3D | 14,539 effective paths; 545 overlay-touched (`E-AUDIT-CORPUS-20260829`, `E-BL-202`) | target recook/closure absent | strict target bundle load absent | matched models, animation, material, lighting evidence absent | `P1-ASSET-001`, `P1-VISUAL-001` |
| Images/UI art | 9,063 effective image paths; 1,903 overlay-touched (`E-AUDIT-CORPUS-20260829`, `E-BL-202`) | target recook and authored-use graph absent | strict UI/object consumption absent | matched HUD/shell/terrain evidence absent | `P1-ASSET-002`, `P1-UI-001`, `P1-VISUAL-001` |
| Effects | 233 BSE paths (`E-AUDIT-CORPUS-20260829`) | target conversion/closure absent | trigger/timing/attachment behavior unproved | particles/decals/weather/effects unproved | `P1-ASSET-003`, `P1-VISUAL-001` |
| Audio/music | 19,194 effective paths; 390 overlay-touched; EnglishAudio contributes 2,066 winners (`E-AUDIT-CORPUS-20260829`) | target closure/recook absent | event/EVA/music routing unproved | timing, spatialization, priority, ducking, and mix unapproved | `P1-AUDIO-001/002` |
| APT/WND shell/HUD | 86 APT and 18 WND effective paths plus the required shell-root query (`E-AUDIT-CORPUS-20260829`, `E-SCOPE-202`) | some authored conversion exists, but the shell-root graph and target closure await `P0-CORPUS-003` | all controls/transitions and target selection identity unproved | complete matched shell/HUD visuals/audio unapproved | `P0-CORPUS-003`, `P1-UI-001`, `P2-SHELL-001/002` |
| Tutorials and campaigns | product policy requires BASIC/ADVANCED/WOTR tutorial and Good/Evil/Angmar roots and payloads (`E-SCOPE-202`) | the live menu still disables those entries (`game/src/ui/main_menu.gd:174-190`), no tracked `game/src/campaign/` runtime exists, and executed roots/membership/reachability await `P0-CORPUS-003` | tutorial/campaign missions, progression, and cinematics are unproved | tutorial/campaign presentation and audio are unapproved | `P0-CORPUS-003`, `P2-CAMPAIGN-*` |
| War of the Ring | product policy requires all strategic roots (`E-SCOPE-202`) | executed roots, membership, and reachability await `P0-CORPUS-003` | turns, handoff, persistence, SP/MP unproved | strategic UI/audio unapproved | `P0-CORPUS-003`, `P2-WOTR-*` |
| Create-a-Hero | product policy requires all CaH source/root families (`E-SCOPE-202`) | executed roots, membership, and reachability await `P0-CORPUS-003` | rules, progression, persistence, skirmish integration unproved | authoring UI/animation/audio unapproved | `P0-CORPUS-003`, `P2-CAH-*` |
| Networking | OpenBFME contract, 2-8 slots (`E-SCOPE-202`) | partial code is not qualification | cross-platform determinism/fault/recovery/scale/security incomplete | presentation is not the primary claim | `P2-NET-001` |
| Modern modding | linked modding policy (`E-SCOPE-202`) | policy exists; full implementation evidence absent | compatibility/override/server behavior incomplete | presentation-pack behavior incomplete | `P2-MOD-001` |

## Acceptance evidence ladder

Every accepted work item names the Git/dirty identity, source baseline, recipe,
bundle/selection address, command, artifact digest, and independent verifier.

| Level | Required evidence | Promotion rule |
|---:|---|---|
| L0 | tracked policy/schema validation | Cannot prove retail bytes exist. |
| L1 | exact source inventory, precedence, effective winners, provenance, and denominator | Must bind `rotwk-202-v9.7.7-en`, policy `aaf30a92...`, and catalog `e1485aa8...`. |
| L2 | deterministic clean conversion and immutable bundle validation | Repeat builds must produce the same addresses; no unresolved required row. |
| L3 | shipping Godot consumer mounts and uses that exact selection without fallback | Load/reachability only; no behavior or appearance claim. |
| L4 | deterministic focused behavior/state tests | Proves implemented rules, not original-game fidelity by itself. |
| L5 | approved matched-condition original-game comparison | Required for gameplay, visual, and audio parity rows. |
| L6 | end-to-end modes, complete matches, persistence/replay/network where applicable, reliability, containment, and packaging | Required for skirmish/full-product release claims. |

Verification is fail closed. Exit code zero is insufficient: the expected
marker must appear, contradictory markers and forbidden diagnostics must not,
the identity must remain fixed, and `SKIP` is not success. The audited
false-green path has been patched so the retail-slice runner requires
`failed == 0`, and a focused static regression exists. Acceptance remains
withheld until `P0-GATES-001` receives independent verification and the
canonical wrapper checks the exact result marker, both output streams, and the
full identity. `P0-DEFAULTS-001` separately keeps stale 2.01/72/74/hash routes
open.

## Cleanup and archival policy

Cleanup is evidence-preserving work, not a delete sweep.

- **Keep tracked:** current governing contracts, minimal current architecture and
  verification docs, the canonical roadmap/ledger, active code, focused tests,
  and sanitized receipts.
- **Supersede, do not compete:** a target contract replaced by an accepted exact
  contract is removed from the live authority surface and retained in Git
  history. `contracts/rotwk-201-product-scope.json` is superseded by
  `contracts/rotwk-202-v9.7.7-product-scope.json`; this is part of
  `P0-SOURCE-001`, not evidence deletion.
- **Replace or banner:** 2.01/vertical-slice narrative documents that are still
  useful history. They must not call a deleted queue or old state directory
  authoritative.
- **Remove after proof:** obsolete duplicate orchestration, generated reports,
  stale pins, dead wrappers, orphan runners, and synthetic product paths only
  after a focused caller/reference/history check and preservation receipt.
  The removed `docs/ONE_TO_ONE_GAP_MAP.md` is recorded by `P0-HISTORY-001` and
  remains available in Git history; its fixed 74-row narrative is not the exact
  product map.
- **Never ad hoc delete:** retail payloads, captures, cooked packs, source
  caches, worktrees, or unknown ignored files. They may be the only reproducible
  evidence or last-known-good bundle.
- **Worktrees:** the accepted 2026-08-30 inventory found 86 registered trees.
  Eighty have since been retired through exact state-bound archive/remove
  protocols. Six remain registered: main, the active retention lane, and four
  quarantined trees with tracked dirt, unmerged history, large physical
  payloads, or missing/external junction targets. They require the hardened
  recovery path; generic forced worktree removal remains forbidden.
- **Private packs/workspace:** retention and provenance pruning is a separate
  integration-owner operation after `P0-REPO-001`, never worker discretion.

## Explicit non-claims

As of this roadmap's audit identity:

- the selected game is **not** proved to use Patch 2.02 v9.7.7 content;
- 217 archives and 53,433 records prove source enumeration, not conversion;
- 48,566 winners, 850 INIs, or 5,494 objects do not prove runtime reachability;
- recognized/imported module names do not prove their rules execute;
- 74 selected maps, a 72-map runner, or 299 resolvable payloads do not prove a
  map booted, navigated, completed, or matched retail;
- converted W3D/images/audio do not prove correct usage or presentation;
- passing public or offline tests cannot substitute for private retail and
  original-game oracle evidence;
- existing BFME2/RotWK 2.01 captures, packs, reports, and hashes are regression
  history, not 2.02 acceptance;
- no behavior, visual, audio, skirmish-complete, or product-complete claim is
  currently accepted; and
- networking and modern modding are included OpenBFME product requirements but
  are counted separately from retail parity.

## Operating sequence

The integration owner should pull work in dependency order from the canonical
ledger. Repository-readiness lanes run before product lanes:

1. `P0-AGENTS-001` - use the one-time owner-bound and independently reviewed
   twelve-path foundation to land the pinned main-only control plane, then make
   allocation, sibling worktrees, focused checks, typed evidence, and handoff
   executable and fail closed.
2. `P0-REPO-001`, `P0-HISTORY-INDEX-001` -> `P0-HISTORY-001`, and
   `P0-HYGIENE-001` - inventory all legacy trees, freeze exact dispositions,
   retire competing instructions, and close publication hygiene without
   deleting private evidence.
3. `P0-ASSET-HYGIENE-001` -> `P0-ASSET-PURGE-001` -> the seven parallel
   fixture/input/audio/icon/model/shell/launcher lanes ->
   `P0-GODOT-IDENTITY-INDEX-001` -> `P0-GODOT-IDENTITY-001` ->
   `P0-ASSET-EXPORT-001` -> `P0-ASSET-CLOSE-001` - execute the sealed 611-way
   disposition, stabilize Godot identities, and prove export closure.
4. `P0-CORPUS-001` - seal the private effective-winner manifest.
5. `P0-GATES-001` - independently verify the implemented `failed == 0`
   admission repair; complete `P0-DEFAULTS-001` for all stale command and
   denominator routes.
6. `P0-CORPUS-002` - retain the accepted exhaustive lexical census with its
   strict claim limit.
7. `P0-CORPUS-003` - execute all product roots and close the semantic graph.
8. `P0-ROUTING-001` - author and validate the owner routing partition after
   code-map and public-asset closure.
9. `P0-EVIDENCE-001` - generate and validate the evidence catalog/matrix.
10. `P0-LANES-001` - bind the sealed graph/evidence/asset/code-map identities,
   copy the accepted coverage matrix's envelope/objective/file routing into
   capped exact-path children for all 33 envelopes, retarget the 12
   high-conflict rows currently owned by P1/P2 envelopes, and let only the
   integration owner materialize them;
   never allocate an envelope or rollup as a worker lane.
11. `P0-SELECTION-002` - clean-recook and atomically select exact 2.02 bundles.
12. Run P1 generated children in parallel, with `P0-ORACLE-001` supplying matched
   acceptance scenarios.
13. Accept `P1-QUAL-001` only when every required skirmish/shell row is current;
   then close P2 domains and the complete-product gate.

No worker should infer a task from this prose. The integration owner assigns
exactly one ready row and its exact owned paths through the lane allocator; the
worker returns only that row's declared evidence packet.

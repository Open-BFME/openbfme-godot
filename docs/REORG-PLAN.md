# OpenBFME reorganization plan (v3)

Written 2026-09-01 from a code survey. v1 and v2 were reviewed assuming they
were wrong. v2 rebuilt slice thinking in four places and sequenced the most
parallel work last; v3 fixes that. Findings are in section 10.

Goal: a good, playable Godot port of RotWK, the whole game, built by many
agents at once, with the launcher's download-and-convert flow intact. Not a
1:1 port. INI numbers stay exact; movement, AI, and timing may be better than
retail.

## 1. The product bar: the whole game, fail-loud denominators

No curated subsets anywhere. Every denominator is the corpus; anything that
does not pass is listed by name with a reason. `progress.py` prints this
table.

| Bar | Denominator | Pass |
|---|---|---|
| Modes | every main-menu entry: skirmish, multiplayer, WotR, CaH, Good/Evil/Angmar campaigns, tutorials | enabled and completes |
| Factions | 7 | every unit, structure, hero, power, upgrade loads |
| Maps | 419 effective map paths | cooks, boots, AI vs AI finishes |
| Campaign missions | every mission in the three campaigns plus tutorials | scripts run, objectives complete, next mission unlocks |
| AI | 7 factions x 3 difficulties | AI vs AI finishes on every map; win rate ordered by difficulty |
| Performance | new core | 144 fps with 10k members on a mid-range GPU at 1080p; sim tick under budget at 30 Hz |
| Loading | shell, match | under 5 s, under 30 s from a warm pack |
| Multiplayer | internet via relay or direct, 8 players | 30-minute match, zero desyncs, reconnect works, replay replays |
| Mods | any INI+art folder | layers, cooks, loads, or the validator names every failure |
| Assets | 14,539 W3D, 9,063 images, 19,194 audio, 233 particles, 86 APT, 18 WND | converts and passes the structural oracle |

## 2. Decisions

1. **Do not wait on the decompile.** Ghidra pseudo-C for BFME1 is in
   `workspace/reference/open-bfme-1/ghdec`; produce BFME2's the same way.
   Behavior reference exists now. Byte-exact source is a future oracle.
2. **The new core is the fleet's main target from week two.** The kernel
   skeleton is one serialized lane for about two weeks. Then 245 module types
   land in the new core in parallel, one file each, with golden traces from
   INI fixtures and pseudo-C. This work is never thrown away.
3. **The placeholder GDScript sim ships until the swap and is otherwise
   frozen.** Playtest bug fixes only. No module work, no red-test work on it.
4. **Two contracts come first**, because every parallel lane hangs on them:
   the Snapshot schema (sim to presentation, packed arrays) and the
   Match-launch contract (shell to sim). Bundle and Command schemas follow.
5. **The generic cook is on the core's critical path**, not after a beta. It
   parallelizes by INI block type with a round-trip oracle.
6. **Runtime fails open in tiers; conversion fails closed.** Structural
   modules required; cosmetic modules default and log. Determinism is the
   one runtime hard gate.
7. **Do not move working code.** Delete and consolidate only.

## 3. Delete list, two passes with an audit between

Tag `pre-reorg` and cut the next playtest release from that tag first.

**Pass 1: governance.** `orchestration/`, `PLAN.md`, `DIRECTION.md`,
`CONTRIBUTING.md`, `docs/` except build/release, modding, launcher,
third-party, patch notes, lessons, and this file; product-scope and
modding-contract JSON (archive-hash contracts move to
`importer/profiles/baselines/`); `config/repository-boundaries.json` and its
checker; `check-work-items.py`; `check-product-contracts.py`;
`tools/work-item.ps1`; ponytail installer and gate; `audit-worktrees.ps1`;
`lint-complexity*`; root `.bat` files except `run_game`, `run_launcher`,
`run_importer`, plus a new `run_tests`.

**Audit.** `python tools/fleet/audit_deps.py` lists every pass-2 candidate
referenced by a runner, a tool, or the launcher. Referenced items are kept
or their consumer is rewritten first.

**Pass 2: evidence tooling and approximation assets.** `tools/gate-*.ps1`
except `gate-hygiene` and `export-scan`; `m2-*`; `proof-gate-common`;
`q58_*`; module evidence and coverage tools; census JSON moves to
`importer/data/`; `game/data/base/assets/{models,icons,audio}` (~391 MB)
only after a no-pack boot and the surviving suite pass with the folder
emptied, since `res://data` is the always-mounted fallback pack;
`examples/mods/` once the validator ships a fixture.

## 4. Layout after the cut

```text
AGENTS.md            one screen: loop, queues, rules
README.md
contracts/           snapshot, match-launch, bundle, command schemas + golden fixtures
docs/                build/release, modding, launcher, patch notes, lessons.md
game/                Godot project, paths unchanged; surviving hubs split once
importer/            Python, paths unchanged; + data/, profiles/baselines/, cook/
launcher/            WPF launcher, unchanged
engine/              C# new core: kernel + modules/<Name>.cs, one file per module type
tools/fleet/         work.py, progress.py, lock.py, audit_deps.py, hook (Python, any OS)
tools/release/       PowerShell release pipeline, unchanged
content/             clean-room fixtures
workspace/           ignored: retail, packs, logs, reference, agents/<name>/
```

## 5. Runtime posture change on the placeholder

One lane. Timers round instead of refuse. Cosmetic unknown modules default
and record a gap on F1; structural unknowns are a named load error.
Missing geometry, animation, or art defaults once per template. Strict
parity profile becomes a debug flag. Seams: milliseconds at the data
boundary, no two-team assumptions, modules keyed by SAGE name.

## 6. Hardening for a fleet

- **Contracts before lanes.** Snapshot and Match-launch schemas with golden
  fixtures are the first Phase 0 deliverable. A synthetic snapshot generator
  lets the renderer lane run with no sim.
- **Split only the hubs that survive:** `wotr_screen.gd`, `retail_hud.gd`,
  `retail_hud_apt_runtime.gd`, `wotr_map_view.gd`, `main_menu.gd`.
  Mechanical, pin-verified. Placeholder hubs (`retail_vertical_slice.gd`,
  `retail_slice_sim.gd`, `retail_slice_script_world.gd`) are locked, one
  agent at a time via `lock.py`, never split.
- **Merge queue, not push races.** Bot rebases, runs the fast gate, lands
  serially.
- **Fleet tools in Python, cross-platform.** PowerShell only for release.
- **Per-agent workspace roots** under `workspace/agents/<name>/` over one
  shared read-only pack and retail install.
- **Fast gate on commit, slow gate nightly.** Commit: export scan, GDScript
  parse of touched files, pytest and dotnet test for touched modules, size
  cap. Nightly: full suites, headless skirmish hash pin, AI-vs-AI map sweep,
  perf benchmark, product-bar table.
- **No file over 1,000 lines may grow.** Registries from directory listings.

## 7. Queues and oracles

| Queue | Unit | Oracle | Strength |
|---|---|---|---|
| `core-modules` | one SAGE module type in `engine/modules/`, ranked by object count | golden trace from INI fixture + pseudo-C reference; twin-run hash | strong |
| `cook` | one INI block type in the generic cook | round-trip: parse, emit bundle, reload, compare to source values | strong |
| `render` | one presentation change against the synthetic snapshot | benchmark fps at fixed member counts | strong |
| `assets` | one W3D, texture set, audio file, particle | structural: bone/mesh/vertex counts, resolved textures, clip count and durations vs W3D source; audio duration and channels vs source | strong |
| `maps` | one of 419 | cook, boot, AI vs AI finishes, zero diagnostics | strong |
| `screens` | one APT/WND | convert, load, scripted click-through, no VM refusal | medium |
| `missions` | one campaign or tutorial mission | scripts run, objectives complete, next unlocks | medium |
| `ai` | one planner behavior in the new core | AI vs AI sweep, difficulty ordering holds | medium |
| `net` | one lockstep feature: relay, reconnect, replay, desync report | scripted multi-peer harness | strong |
| `bugs` | one playtest issue on the placeholder | reproduction runner green | strong |
| `mods` | one community mod through the validator | clean or every failure named | strong |

`work.py next` serves from the highest-priority non-empty queue; the
kernel lane and the two contracts are pre-assigned, not queued. Partial
attempts bank to `workspace/attempts/` with a line in `docs/lessons.md`.

## 8. Launcher and importer

Keep the launcher. Its four importer verbs are a frozen contract with a
test. Until the generic cook lands, point the launcher at the existing
all-faction, multi-map path that `run_rotwk_one_button.bat` already drives,
so players get seven factions now. When the cook lands, the same verbs call
it. The mod step in the launcher opens with the validator.

## 9. Phases

**Phase 0, two weeks, parallel.** Tag and ship. Pass-1 deletes. Contracts
with fixtures. Fleet tools, hook, merge queue. Audit, pass-2 deletes. Hub
splits. Runtime posture lane. Kernel skeleton lane starts. Synthetic
snapshot generator. Launcher on the all-faction path.

**Phase 1, fleet.** Open every queue. Point releases on a fixed cadence
from the placeholder; the product-bar table ships in every note.

**Phase 2, swap.** Dual-run harness agrees; shell points at the new core
through the match-launch contract. Perf bar measured on the core.

**Phase 3, whole-game close.** Missions, WotR, CaH, net, mods queues drain
against the whole-game denominators. Decompile pseudo-C and source refine
`core-modules` oracles as they arrive.

## 10. Review findings

v1: waiting on the decompile was a false dependency; per-behavior swap was a
fantasy; no product bar; delete list broke runners; push races and
PowerShell do not transfer; load-headless is a smoke test; fail-open needed
tiers; mods premature; perf absent.

v2: curated maps, modes, and a subset beta rebuilt the slice; core
serialized and modules pointed at the placeholder; generic cook after beta
though it is on the core's critical path; perf bar on the placeholder;
red-test effort on throwaway code; thumbnail oracle with no reference
renderer; unmeasurable AI bar; LAN-only net bar; contracts never scheduled;
hub splits spent on placeholder files.

## 11. Tripwires

- Any "curated", "representative", or "slice" set in a queue or release
  note: revert.
- More tooling commits than queue commits in a week: stop and cut.
- Module work landing on the placeholder sim: revert.
- A release note claiming anything the product-bar table does not show.

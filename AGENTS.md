# OpenBFME agent contract

OpenBFME is a Godot 4.7 port of *The Battle for Middle-earth II: Rise of the
Witch-king* (Patch 2.02 v9.7.7 as the data source). The goal is the complete
game, playable and better than retail where it makes sense: INI numbers stay
exact, movement, AI, performance, and multiplayer may improve. Not a 1:1 port.

Read `docs/REORG-PLAN.md` once. It is the plan; this file is the loop.

## The loop

```text
git pull --rebase origin main
python tools/fleet/work.py next            # serves one unit + its oracle
... do exactly that unit ...
<run the oracle command the unit printed>
git add <explicit paths>                    # never `git add -A`
git commit                                  # the hook runs the fast gate
git pull --rebase origin main && git push   # on rejection: rebase, retry
python tools/fleet/progress.py              # +0 means take another unit, not stop
```

Finish or revert each unit before taking the next. Bank a partial attempt
with `work.py bank <unit-id> --note "..."`; it lands in
`workspace/attempts/` and a line in `docs/lessons.md`, never in the tree.

An explicit request from the owner overrides the queue.

## Where things live

| Path | What | Who edits |
|---|---|---|
| `engine/` | the new C# core: kernel + `OpenBfme.Sim/Modules/<Name>.cs`, one file per SAGE module type | `core-modules` queue; kernel is one assigned lane |
| `importer/` | Python conversion of retail files; `importer/cook/` is the generic cook | `cook`, `assets`, `maps`, `screens` queues |
| `game/` | Godot project: shell, presentation, and the placeholder GDScript sim | `render`, `screens`, `bugs` queues |
| `launcher/` | WPF launcher: retail download, convert, pack selection, updates | assigned lanes only |
| `contracts/` | snapshot, match-launch, bundle, command schemas + golden fixtures | assigned lanes; a change bumps the version |
| `tools/fleet/` | queue, progress, lock, audit, hook | anyone, with tests |
| `workspace/` | ignored: retail bytes, packs, logs, per-agent scratch | never committed |

## Rules

1. **Retail bytes never enter git.** Original files, extracted data, packs,
   captures, audio, and raw logs stay under ignored `workspace/`. The hook
   and CI scan for them.
2. **The placeholder sim is frozen.** `game/src/retail_slice/**` accepts
   playtest bug fixes only. No module work, no coverage work. It is
   replaced by `engine/` at the swap.
3. **New code lands in new files.** No file over 1,000 lines may grow. The
   hook enforces it for `.gd`, `.py`, `.cs`, `.ps1`.
4. **Hubs are locked, one agent at a time.** Files in `tools/fleet/hubs.txt`
   need `python tools/fleet/lock.py acquire <path>` first. Set `FLEET_AGENT`
   to your agent name.
5. **Registries come from directory listings**, never from a hand-edited
   list. Adding a module, queue unit, or fixture is adding a file.
6. **Runtime fails open in tiers; conversion fails closed.** A converted
   asset that does not verify is refused. A cosmetic module the runtime does
   not know defaults and records a gap. A structural one is a named error.
7. **Determinism is the one runtime hard gate.** The per-tick state hash
   pins must not move without the unit saying why.
8. **Every fix ships a fast failing-first test.** Oracles are external to
   the code under test.
9. **Never bypass hooks.** No `--no-verify`, no `core.hooksPath` tricks. If
   the gate is wrong, fix the gate in its own unit.
10. **No curated subsets.** Denominators are the whole corpus. A unit that
    does not pass is listed by name with a reason; it is never dropped from
    the count.

## Oracles by queue

| Queue | Oracle |
|---|---|
| `core-modules` | golden trace from an INI fixture and the pseudo-C reference; twin-run hash equal |
| `cook` | round-trip: parse, emit bundle, reload, compare values to source |
| `render` | benchmark scene fps at fixed member counts; hash pin unchanged |
| `assets` | structural compare to the W3D/texture/audio source; digest recorded |
| `maps` | cook, boot, AI vs AI finishes, zero diagnostics |
| `screens` | convert, load, scripted click-through, no VM refusal |
| `missions` | scripts run, objectives complete, next mission unlocks |
| `ai` | AI vs AI sweep finishes; win rate ordered by difficulty |
| `net` | scripted multi-peer harness |
| `bugs` | reproduction runner added and green |
| `mods` | validator clean or every failure named |

## Reference material

- BFME1 decompile and Ghidra pseudo-C: `workspace/reference/open-bfme-1`
  (clone of github.com/Open-BFME/Open-BFME-1). Use for engine semantics.
  Copying and translating from it is allowed.
- Retail INI is the balance authority. When pseudo-C and INI disagree on a
  number, INI wins.

## Running things

```text
run_game.bat                 launch the game from source
run_tests.bat                importer tests + engine tests + headless slice runner
run_launcher.bat             launcher from source
run_importer.bat             importer CLI
run_rotwk_one_button.bat     convert all factions + maps from a RotWK install
run_sim_match.bat            launch the first playable match on the new core
```

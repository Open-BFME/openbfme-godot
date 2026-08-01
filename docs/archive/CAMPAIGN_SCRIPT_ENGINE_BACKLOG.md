# Campaign script engine — work queued out of the audit merge

Status: **queued, not lost.** Nothing here has landed. This file exists so that
a deliberate refusal is not mistaken later for an oversight.

## What was refused, and why

The audit lane (`claude/rts-codebase-audit-overhaul-0be13f`) built a campaign
script engine of roughly +2,439 lines. It was declined at the merge on
2026-07-28 for two independent reasons, either of which alone was sufficient.

**1. It was built inside a frozen file.** The whole engine lives in
`game/src/retail_slice/retail_map_scripts.gd`, which carries an explicit
`ARCHIVED REFERENCE - FROZEN, NOT A LIVE LANE. DO NOT EXTEND.` banner placed by
owner decision (`bd8733c`, 2026-07-26). That file is kept only because it is the
one worked example of a SAGE interpreter reaching into the simulation and
changing hashed state. Its hand-rolled opcode `match` statement is exactly the
shape the live lane exists to replace. Merging the engine would have re-opened
the file the day after it was closed and doubled the vocabulary in a second
place.

**2. It regresses a measured fix.** Commit `7ba4000` on this branch established,
from 1,510 decoded actions across 142 retail maps, that the argument to
`SET_MILLISECOND_TIMER` is **seconds** despite the opcode's name — a "Timer -
Daybreak" of 1800 is 30 minutes, not 1.8 seconds. The audit implementation reads
the same argument as milliseconds:

```gdscript
# audit side, retail_map_scripts.gd
_real_at(arguments, 1, ARGUMENT_REAL, 0.0) / (TICK_SECONDS * 1000.0)
```

That divides every retail timer by 1,000 and collapses a 30-minute campaign
timer into a single tick. Ours (`retail_map_scripts.gd`, `SET_MILLISECOND_TIMER`)
carries the measurement and the reasoning in place.

Alongside the engine, `game/tests/retail_map_script_runner.gd` gained
`_test_real_campaign_missions()`. It was dropped with the engine it exercises.

## Where the work belongs instead

The live script lane is `game/src/script/` — vocabulary-driven dispatch, 14
modules, absent entirely on the audit branch:

| module | role |
| --- | --- |
| `script_vocabulary.gd` | the opcode vocabulary (the single source of truth) |
| `script_dispatch.gd` | opcode → handler routing |
| `script_executor.gd` | evaluation loop, condition model, load-time fail-closed accounting |
| `script_action_table.gd` / `script_condition_table.gd` | table-driven opcode definitions |
| `script_args.gd` / `script_param_types.gd` | argument decoding and typing |
| `script_env.gd` | script-visible environment |
| `script_world.gd` / `script_world_query.gd` / `script_world_stub.gd` | the simulation seam |
| `script_handlers_core.gd`, `handlers/` | handler implementations |
| `script_gaps.gd` | unimplemented-opcode accounting |

Porting the campaign engine means adding handlers under
`game/src/script/handlers/` and rows to the action/condition tables — never a new
`match` statement, and never a second copy of the vocabulary.

## To do

- [ ] Read the audit engine as a specification, not as code to move:
      `git show 71d366a -- game/src/retail_slice/retail_map_scripts.gd`
      (commit `71d366a`, "Hero summons, evil veterancy, campaign script engine,
      map lane, APT shell").
- [ ] Port its campaign opcodes into `script_action_table.gd` /
      `script_condition_table.gd` plus handlers under `game/src/script/handlers/`.
- [ ] Keep the SECONDS reading for `SET_MILLISECOND_TIMER`. If the port
      reintroduces milliseconds, `7ba4000`'s 1,510-action measurement is the
      contradicting evidence and must be answered before the unit changes.
- [ ] Re-derive `_test_real_campaign_missions()` against the live executor
      (`game/tests/script_executor_runner.gd` is the right home) rather than
      restoring the dropped copy.
- [ ] The bottleneck is the world seam, not the vocabulary: the vocabulary is
      fully translated, the executor exists, and `script_world` covers 36 of 319
      surfaces. Campaign opcodes that need world surfaces will need those first.

## Do not

- Do not extend `game/src/retail_slice/retail_map_scripts.gd`. It is frozen. Its
  proof runner (`game/tests/retail_map_script_runner.gd`) is kept green so that
  rot in the reference is visible, and that is its whole remaining job.

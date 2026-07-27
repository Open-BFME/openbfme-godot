# Archived reference: the tier-1 map script interpreter

**Status: FROZEN REFERENCE. Not a live lane. Do not extend it.**

File: `game/src/retail_slice/retail_map_scripts.gd` (308 lines)
Proof runner: `game/tests/retail_map_script_runner.gd` (11 checks, kept green)

## Why it is kept

It is not dead code and it was not a false start. It is the only code in this
repository that ever demonstrated a SAGE script interpreter **reaching into the
simulation and changing it** — its `PLAYER_SET_MONEY` family writes
`team_resources`, which feeds `RetailSliceSim.state_hash()`. That makes it the
worked example for the seam the real interpreter now has to cross.

Deleting it would throw away the one thing it proved, to save 308 lines. So it
stays, frozen, with its tests running. If the runner ever goes red, the
reference has rotted and that is worth knowing.

## What supersedes it

`game/src/script/` — the vocabulary-driven system:

| | tier-1 (this file) | `game/src/script/` |
|---|---|---|
| Opcode knowledge | hand-rolled switch, narrow set | none; dispatches through the sourced vocabulary |
| Coverage | the tier-1 skirmish contract | 92.7% of retail AI call sites |
| Argument reading | positional, hand-written per opcode | `SageScriptArgs` against the declared signature |
| Unknown opcodes | counted, fail-closed | counted, fail-closed, plus structured gaps with identity |
| Reaches the simulation | **yes** | not yet — that is the open work |

## What was deliberately carried over

Three ideas from this file were kept, and they are the reason it earned its
place rather than merely being early:

1. **The condition model.** `OrCondition` records OR'd together, `Condition`
   records inside one block AND'd; `ScriptAction` on true, `ScriptActionFalse`
   on false. `script_executor.gd` implements exactly this.
2. **Load-time fail-closed accounting.** Every opcode occurrence in a loaded
   script is bucketed at LOAD time, not just when reached, so a script full of
   unimplemented opcodes is visible before it ever runs. The executor extends
   this to five buckets.
3. **The honesty stance in its own header** — "Nothing is skipped silently."

## What NOT to copy from it

- The hand-rolled opcode switch. Duplicating the vocabulary in a `match`
  statement is what the dispatch system exists to prevent.
- Its argument-type constants (`ARGUMENT_INTEGER`, `ARGUMENT_COUNTER_NAME`, …).
  `script_param_types.gd` is the sourced table and cites this file only as the
  origin of those observations.

## Convergence

The intended end state is one interpreter. This one is retired in place rather
than removed, so its proof stays inspectable and its tests keep asserting that
the simulation seam still behaves the way it documented.

Owner decision, 2026-07-26: **keep and archive rather than delete** — the work
was real and the reference is cheap to hold.

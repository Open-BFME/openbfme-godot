# Q64 Minas Tirith gate-nav and failed-route lag

Status: diagnosis complete; fixes and final gates pending.

## Failing-first product receipt

Run from the worktree with the selected read-only content root:

```powershell
$env:OPENBFME_CONTENT='C:\Users\Jonathan\Desktop\open-bfme\workspace\content-packs'
$env:OPENBFME_CASTLE_AI_MAP='rotwk.map.wor-minas-tirith'
& 'C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe' --headless --path game --script res://tests/castle_skirmish_ai_runner.gd
```

The pre-instrumentation control took 261,881 ms wall clock for one map and
4,000 deterministic ticks. The receipt-counter repeat took 226,853 ms and
measured **235 layered bridge queries consuming 94,435.970 ms** inside that
method, plus 19,047 ground queries. It remained red only at
`Minas Tirith_ai_issued_attack_order`: `attack_orders=0` and
`last_route_rejection=no-bounded-route`. Receipt:
`workspace/logs/minas-lag/before-castle-ai.txt` and
`workspace/logs/minas-lag/before-timing.txt`; instrumented repeat:
`workspace/logs/minas-lag/before-instrumented-castle-ai.txt` and
`workspace/logs/minas-lag/before-instrumented-timing.txt`.

## Retail/cook/runtime boundary

The selected maps pack is
`rotwk-playable-maps-private/6f6be4dbbabceb7c78a8c3e6e56a5d955421320c592b7d5178b44fd532acc4d9`.
Its `fixtures.json` row 459 preserves `MinisGateDoor` as a gate at source
position `(1045.5055, 426.21094, -2439.1736)`, with `openByDefault=true`,
`percentOpenForPathing=50`, and the exact Closed/OpenLeft/OpenRight geometry.

The pure retail oracle is
`data/ini/object/civilian/ministirithbuildings.ini`:

- lines 2811-2814: `GateOpenAndCloseBehavior`, `OpenByDefault = Yes`, and
  `PercentOpenForPathing = 50`;
- lines 2836-2842: the closed 7.2 by 58.4 box spans the passage;
- lines 2844-2856: opening moves collision to two narrow 25 by 3 side leaves.

The importer is therefore not losing the gate contract. The runtime seeds the
fixture `pathing_open=true`, but `RetailMapData._build_navigation()` constructs
ground A* only from the source impassability bitmap and never applies the gate's
authored dynamic pathing state. Collision correctly uses open geometry later in
`RetailSliceSim._castle_gate_blocking_discs`; route topology does not. The
honest A-layer fix is runtime gate-portal mutation of the ground grid, with
cache invalidation on every mutation (including breach/close/open), not an edit
to retail terrain bytes or the immutable selected pack.

## Pending proof

- B1 component-pair negative cache with mutation invalidation.
- B2 source-component portal budget.
- B3 deterministic repeated-order backoff.
- Open-gate ground portal and Minas AI green proof.
- Required pins and castle gates.

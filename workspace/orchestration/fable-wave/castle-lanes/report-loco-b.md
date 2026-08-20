# LOCO-B rework — authored close-turn arrival

Date: 2026-08-19

Rejected implementation: `14e5684`

Rework implementation: `657c43c`

Required main merged: `c0a4f2c` (merge commit `0811a7d`)

Content root: `C:\Users\Jonathan\Desktop\open-bfme\workspace\content-packs`

## Result

The lateral-final-waypoint orbit is fixed. `_step_route` now consumes the
already-cooked `slowTurnRadius` and `minTurnSpeed` locomotor fields. When a
waypoint lies inside the current heading-bounded arc, the unit slows to its
authored minimum turning fraction. At that speed, `SlowTurnRadius` owns the
arc; an authored zero pivots in place. The stall counter is no longer gated on
already facing the waypoint.

The runtime adapter preserves both fields, including an authored zero, and the
entity row carries them absent-unless-authored. No pack was changed or recooked.

The blocked-origin route seam is narrowed: only a provider with an explicit
`resolve_walkable_position` recovery may rescue the origin, and the recovered
point must still pass `parity.can_path_between`. Named tests cover both the
allowed and refused cases.

## Retail sources and selected packs

Authoritative root:
`workspace/retail-work/editions/rotwk/cache/effective-assets/data/ini`.

- Mounted Eomer: `object/goodfaction/units/men/eomer.ini:861-871` binds
  `HeroHorseLocomotor` and speed `NORMAL_CAVALRY_FAST_HORDE_SPEED`;
  `gamedata.ini:7817` resolves speed 90. `locomotor.ini:943-970` authors
  `TurnTime=1500`, `Acceleration=1500`, `SlowTurnRadius=0`, `Braking=2000`,
  and `MinTurnSpeed=10%`.
- Mumakil: `object/evilfaction/units/evilmen/mumakil.ini:1739-1743` binds
  `MumakilLocomotor` at speed 50. `locomotor.ini:1347-1367` authors
  `SlowTurnRadius=2`, `TurnTime=9000`, `Acceleration=750`, `Braking=1000`,
  and `MinTurnSpeed=12%`.
- Gondor Fighter control: `object/goodfaction/units/men/gondorfighter.ini:851-855`
  binds `HumanLocomotor`; `gamedata.ini:7903` resolves speed 55;
  `locomotor.ini:142-152` authors the 500 ms turn and 0% minimum turn speed.

The 0.1 runtime scale yields Eomer `9/150/0/200/0.10` and Mumakil
`5/75/0.2/100/0.12` for speed/acceleration/slow radius/braking/minimum fraction.
The fixture identities came from the selected packs, read without modification:
`rotwk-men-vslice/51d4885433869fa6290498eeac597b8cc8ac79e540d073dbf03c9c0d0184df3c`
and
`rotwk-mordor-vslice/975c3d6a618ad161e0b00884500aa03b6be8364309baa136064970e1e4ac4f29`.

## Failing-first evidence

Command:

```powershell
$env:OPENBFME_CONTENT='C:\Users\Jonathan\Desktop\open-bfme\workspace\content-packs'
& 'C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe' --headless --path game --script res://tests/retail_turn_model_runner.gd
```

On unchanged `14e5684`, `workspace/logs/loco-b-rework-failing-first.txt`
recorded `13/4`: mounted Eomer and Mumakil both had `done_tick=-1`, a nonempty
route, and `state=run` after 100 ticks. Both blocked-origin tests also failed
because the parity ledger received zero calls. On the fix,
`workspace/logs/loco-b-rework-retail-turn-model.txt` records `17/0`.

Secondary verifier findings are covered in the same tree:

- all turn-runner/report citations use the effective-assets oracle and current
  line numbers;
- authored-field labels now distinguish a missing rate from missing provenance;
- the wheel oracle computes its second heading from authored inputs, not the
  sim's output, and group togetherness checks final-position tolerance;
- blocked-origin recovery is parity-checked and tested in both directions.

## Merged-tree state pin

Old pre-LOCO-B pin:
`0e4bcdbf7e9a8579ccf559f0ac3d83284413e7196ad1249d2eafd3eafd1dcadc`.
Merged-tree pin:
`b025d16237ff644d66211a9cc26872f18b61520b9a377f11e9e99c6eceb43f58`.

The old-to-new movement is still caused by LOCO-B translating along bounded
facing rather than the raw waypoint vector. The pre-merge value was not reused:
after merging `c0a4f2c`, two independent runs produced the same digest in
`workspace/logs/loco-b-rework-state-pin-measure-1.txt` and
`workspace/logs/loco-b-rework-state-pin-measure-2.txt`. Main's castle/formation
additions and this rework's fields are measured pin-neutral in the non-castle
fixture; the final assertion is in
`workspace/logs/loco-b-rework-retail-state-pin.txt`.

## Complete sequential post-merge sweep

Every row used the Godot command above with the listed `res://tests/<runner>`
script and redirected combined output to the listed worktree log. Runners were
executed one at a time.

| Runner | Result | Evidence |
|---|---:|---|
| `retail_turn_model_runner.gd` | 17/0 | `workspace/logs/loco-b-rework-retail-turn-model.txt` |
| `retail_state_pin_runner.gd` | hash OK | `workspace/logs/loco-b-rework-retail-state-pin.txt` |
| `retail_lockstep_determinism_runner.gd` | 5/0 | `workspace/logs/loco-b-rework-retail-lockstep-determinism.txt` |
| `retail_formation_movement_runner.gd` | 34/0 | `workspace/logs/loco-b-rework-retail-formation-movement.txt` |
| `authored_field_consumption_runner.gd` | 14/0 | `workspace/logs/loco-b-rework-authored-field-consumption.txt` |
| `retail_member_combat_runner.gd` | 115/0 | `workspace/logs/loco-b-rework-retail-member-combat.txt` |
| `castle_gate_runner.gd` | 47/0 | `workspace/logs/loco-b-rework-castle-gate.txt` |
| `castle_skirmish_ai_runner.gd` | 104/1 expected | `workspace/logs/loco-b-rework-castle-skirmish-ai.txt` |
| `castle_map_live_boot_runner.gd` | 8/0 | `workspace/logs/loco-b-rework-castle-map-live-boot.txt` |
| `retail_slice_runner.gd` | 371/59 expected | `workspace/logs/loco-b-rework-retail-slice.txt` |

The sole castle-skirmish failure is the existing
`Minas Tirith_ai_issued_attack_order` case (`attack_orders=0`,
`last_route_rejection=no-bounded-route`). It is not a turning or closed-gate
freeze. The closed-gate sentinel is green at 47/0.

The slice comparison command extracts each `RETAIL_SLICE FAIL` label before the
first parenthesis, trims it, sorts unique labels, and compares it to
`C:\Users\Jonathan\Desktop\open-bfme\workspace\logs\v027fin-retail_slice_runner.txt`.
`workspace/logs/loco-b-rework-retail-slice-name-compare.txt` records
`BASE_UNIQUE=87`, `CURRENT_UNIQUE=87`, `CURRENT_ONLY=0`, `BASE_ONLY=0`.

## Not done / remaining red

- No content pack, `selection.json`, durable mirror, dist build, publish, or push
  was performed.
- No importer recook is owed: both fields were already compiled in the selected
  playable-unit documents; only runtime consumption was absent.
- `castle_skirmish_ai_runner.gd` remains at its named 104/1 baseline and
  `retail_slice_runner.gd` remains at its named 371/59 baseline.
- A fresh-context verifier has not yet rerun this report's Definition of Done.

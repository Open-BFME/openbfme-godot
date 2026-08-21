# Q63 locomotion parity report (2026-08-21)

## Result

The Q55 locomotion residue is closed in the commit containing this report.
Every compiled playable-unit movement block that authors `FastTurnRadius` now
admits it into the runtime rule. Turning selects the authored slow radius below
the authored `MinTurnSpeed` split and the fast radius above it; the selected
radius binds both heading and translation so the traced arc has that radius.

No pack, selection, durable mirror, recook, publish, dist build, or push was
performed. The code base was `44de46d5f6edd436e3b54b0659feb4793d3f3448`
and the live read-only selection supplied Men
`8f40f2af6bf8ea40cb6eb2e44ee262ad92da2364bbebd297fc122a4604dc5fa4`
and Mordor
`82143fea3daa8021704810e9a958519e6ea0b23677107aa639ae532b72ee5899`.

## Retail oracle and selected documents

The sole INI oracle was
`C:\Users\Jonathan\Desktop\open-bfme\workspace\retail-work\editions\rotwk\cache\effective-assets`.

- `data/ini/locomotor.ini:835-854` authors
  `NormalCavalryHordeLocomotor`: `TurnTime=1000`, `Acceleration=800`,
  `Braking=1500`, `SlowTurnRadius=0`, `FastTurnRadius=48`,
  `MinTurnSpeed=100%`, `MaxTurnWithoutReform=100`.
- `data/ini/locomotor.ini:1347-1367` authors `MumakilLocomotor`:
  `TurnTime=9000`, `Acceleration=750`, `Braking=1000`,
  `SlowTurnRadius=2`, `FastTurnRadius=50`, `MinTurnSpeed=12%`.
- `data/ini/locomotor.ini:2438-2446` authors `PorterLocomotor` without
  either turn-radius field. The selected `MenPorter` document is the negative
  kinematics identity fixture.

The runner pins three selected documents before testing their compiled movement:

- `RohanRohirrimHorde`, descriptor `d232ddc20de1be3ec6e83debf785e15ae0d59b54d37b907754ae5a8ba7c7b84d`.
- `MordorMumakil`, descriptor `50c65fb71ffb10725367afae9411fd4cb8223aff7d4b1289ba66cddf658a1bcc`.
- `MenPorter`, descriptor `a86465903f5699df909ff9a6cc850a7738ee5ff8458346b211748b80b5ca861f`.

## Kinematics

For a selected radius `r`, current speed `v`, and authored angular-rate cap
`w_authored`, the implementation uses:

`w_effective = min(w_authored, v / r)` and
`translation_step <= r * w_effective * tick_seconds`.

That is `v = r*w`, applied without a guessed constant. A radius wider than the
natural `v/w_authored` arc reduces heading rate; a tighter radius retains the
authored heading cap and shortens translation. An authored zero radius keeps
translation at zero while heading turns at the authored rate.

For Rohirrim, the source scale is 0.1: speed 100 becomes 10 sim units/s,
radius 48 becomes 4.8, and 360 degrees/s is `2*pi` radians/s. The old natural
radius was `10/(2*pi) = 1.59155` sim units, or 15.9155 source units. Because
retail's 4.8 is wider, the new effective rate is `10/4.8 = 2.08333` radians/s,
119.366 degrees/s. Each full-speed tick moves 1.0 while turning 0.208333
radians, so `1.0/0.208333 = 4.8`, the authored 48-source-unit arc. Seven moving
samples after the authored 179-degree reform measured 4.799996..4.800002; the
test tolerance is 0.02 sim units.

For Mumakil above 12%, speed 5 and fast radius 5 meet the authored 40-degree/s
cap. Translation is bounded to `5 * radians(40) * 0.1 = 0.349066`, giving an
implied radius of 5. Below 12%, slow radius 0.2 bounds the same turn step to
0.013963, giving 0.2.

For min-100% locomotors, slow-mode admission is now genuinely-low speed:
strictly below the first authored acceleration-ramp output. Cavalry acceleration
80 sim units/s^2 produces 8 sim units/s after one 0.1-second tick, so the first
accelerating tick is fast and no longer freezes. A waypoint already inside the
authored fast circle brakes on the authored ramp into slow mode, pivots, and
then advances; this preserves lateral and multi-leg arrival.

## Implementation

- `playable_unit_runtime_adapter.gd` now copies every authored
  `fastTurnRadius`, scaled and absent-unless-authored. Authored zero survives.
- `retail_slice_sim.gd` chooses the two-radius mode before turning, derives the
  effective heading cap from `v/r`, binds translation to `r*dtheta`, and brakes
  for waypoints inside the moving circle.
- The path for a compiled document with no authored fast radius retains the
  old MinTurnSpeed floor, heading, and translation arithmetic. The reusable
  MenPorter probe byte-compares the actual one-tick kinematics tuple against
  that pre-change formula.
- `retail_turn_model_runner.gd` also asserts the selected-document 48 arc,
  Mumakil fast/slow split, zero frozen accelerating-start ticks, real lateral
  and multi-leg arrival, and zero-distance pivot-crush safety.

## Failing-first and focused proof

Command shape:

```powershell
$env:OPENBFME_CONTENT='C:\Users\Jonathan\Desktop\open-bfme\workspace\content-packs'
& 'C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe' `
  --headless --path game --script res://tests/retail_turn_model_runner.gd
```

The final test-only red log is
`workspace/logs/q-loco-parity-turn-red-final.txt`: `39/5`. The five failures
were broad Mumakil admission, selected Mumakil admission, its fast-radius
behavior, the cavalry 48 arc, and the one frozen accelerating-start tick.

The final focused log is
`workspace/logs/q-loco-parity-turn-green-final.txt`: `44/0`.

## Sequential gate sweep

| Gate | Result | Log |
|---|---:|---|
| `retail_turn_model_runner.gd` | 44/0 | `workspace/logs/q-loco-parity-final-turn.txt` |
| `retail_formation_movement_runner.gd` | 34/0 | `workspace/logs/q-loco-parity-final-formation.txt` |
| `retail_lockstep_determinism_runner.gd` | 5/0 | `workspace/logs/q-loco-parity-final-lockstep.txt` |
| `retail_member_combat_runner.gd` | 115/0 | `workspace/logs/q-loco-parity-final-member-combat.txt` |
| `castle_gate_runner.gd` | 47/0 | `workspace/logs/q-loco-parity-final-castle-gate.txt` |
| `castle_map_live_boot_runner.gd` | 8/0 | `workspace/logs/q-loco-parity-final-live-boot.txt` |
| `retail_state_pin_runner.gd` | `b025d16237ff644d66211a9cc26872f18b61520b9a377f11e9e99c6eceb43f58` unchanged | `workspace/logs/q-loco-parity-final-state-pin.txt` |
| `retail_pathing_pin_runner.gd` | `2e5ad58054d28dc93f37ef4728549bb538f6d4a1c22be922ec19b59fb2d1b12d` unchanged | `workspace/logs/q-loco-parity-final-pathing-pin.txt` |
| `retail_projectile_pin_runner.gd` | `e6e053d44b8bbe58b22df87c236c8c0a725f559ed74795875255bf3b33e1ca9c`; re-mint measured twice | `workspace/logs/q-loco-parity-final-projectile-pin.txt` and `q-loco-parity-projectile-pin-green-{1,2}.txt` |
| `retail_slice_runner.gd` | 371/59; exact 87-name set | `workspace/logs/q-loco-parity-final-retail-slice.txt` |
| Slice name comparison vs `v027fin-retail_slice_runner.txt` | base 87, current 87, diff 0 | `workspace/logs/q-loco-parity-final-slice-name-compare.txt` |

The binding preamble's no-remint rule was applied to the current state pin:
it remained `b025d162...`. The pathing pin also remained unchanged.

The projectile pin's static unit-rule table intentionally moved when broad
fast-radius admission added authored keys. Its conscious in-file re-mint is
`709def7c... -> e6e053d4...`. Two independent pre-mint measurements
(`q-loco-parity-projectile-pin-before.txt` and
`q-loco-parity-projectile-pin-measure-2.txt`) produced the new hash and identical
coverage: maximum 16 projectiles; damage 2030/200/200/200. Two post-mint runs
then passed.

## Not done

- A fresh-context verifier has not rerun this lane.
- No pack/content mutation, recook, selection, publish, dist build, or push was
  authorized or performed.
- The known retail-slice acceptance aggregate remains red at 371/59; its exact
  87-name failure set has zero difference from the required baseline.

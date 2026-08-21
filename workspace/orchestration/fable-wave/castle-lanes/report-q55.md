# Q55 — cavalry FastTurnRadius / pivot-crush report (2026-08-21)

## Result

Q55 is fixed in the commit containing this report. A full-speed
`NormalCavalryHordeLocomotor` row no longer selects the zero-radius slow path
merely because `MinTurnSpeed` is 100%. While it is genuinely below that
threshold, the authored slow radius still owns the manoeuvre; at the threshold,
the authored fast radius owns the arc. Crush admission now uses displacement
actually achieved during the tick, so a zero-translation pivot cannot trample.

No pack was changed. No selection, durable mirror, publish, push, or recook was
performed.

## Retail oracle and compiled identity

The only retail tree consulted was:

`C:\Users\Jonathan\Desktop\open-bfme\workspace\retail-work\editions\rotwk\cache\effective-assets`

- `data/ini/locomotor.ini:835-854` defines
  `NormalCavalryHordeLocomotor`: `TurnTime=1000`, `Acceleration=800`,
  `Braking=1500`, `SlowTurnRadius=0`, `FastTurnRadius=48`,
  `MinTurnSpeed=100%`.
- `data/ini/object/goodfaction/hordes/men/menhordes.ini:1822-1825` binds
  `RohanRohirrimHorde` to that locomotor at
  `NORMAL_MOUNTED_MED_HORDE_SPEED`.
- `data/ini/gamedata.ini:7914` resolves that speed to 100.

The compiled proof came from the read-only selected pack
`rotwk-men-vslice/8f40f2af6bf8ea40cb6eb2e44ee262ad92da2364bbebd297fc122a4604dc5fa4`,
document `data/playable-units/rohanrohirrimhorde.json`, file SHA-256
`21DC6AC3F06B43BF7F52F93042A88865D92D8806CFF226CC67DE5BF0A5A24353`.
Its movement blocks cite `locomotor.ini:837/840/841/843/844/846` and compile
speed 100, turn rate 360 degrees/s, slow radius 0, fast radius 48, and minimum
turn speed 1.0. The runtime source scale 0.1 therefore yields
`10 / 80 / 150 / 0 / 4.8 / 1.0`; no movement constant in the Q55 fixture is
invented.

## Implementation

- `playable_unit_runtime_adapter.gd` validates authored `fastTurnRadius` and
  passes it through, scaled and absent-unless-authored, for the complete Q55
  `Slow=0 / Fast authored / Min=100%` contract. Authored zero is preserved.
  This exact admission boundary keeps unrelated locomotor metadata from moving
  the protected projectile pin, whose static rule table participates in its
  hash.
- `_add_battalion` carries the admitted value into the runtime row.
- `_step_route` records whether speed was genuinely below the minimum before
  applying the existing anti-orbit speed floor. Strictly below selects the slow
  radius; equality selects the fast radius. Both arc lengths are bounded by
  `radius * authored_turn_step`.
- `_should_attempt_crush` receives actual position delta divided by tick time,
  not the locomotor speed field after its minimum-turn-speed floor.

## Failing-first evidence

Command shape for every Godot runner below:

```powershell
$env:OPENBFME_CONTENT='C:\Users\Jonathan\Desktop\open-bfme\workspace\content-packs'
& 'C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe' `
  --headless --path game --script res://tests/<runner>.gd *> workspace\logs\q55-<name>.txt
```

`workspace/logs/q55-red-retail_turn_model_runner.txt` is the test-only red run:
`RETAIL_TURN_MODEL_RESULT passed=25 failed=7`. The 30/90/179-degree cases froze
1/3/5 of 1/3/5 turning ticks respectively; the row had no fast-radius field;
the zero-distance pivot wrote speed 10 and dealt one trample damage; the adapter
omitted authored 48 and authored zero.

`workspace/logs/q55-retail_turn_model_runner-after-pin-fix.txt` is the final
focused result: `passed=32 failed=0`. It proves all three corrections translate
on every turning tick, each heading step is at most 36 degrees, each translation
is fast-arc bounded, the lateral waypoint arrives and idles, the adjacent victim
takes no pivot crush, and adapter authored/zero/absence semantics hold.

## Final gates

| Gate | Result | Log |
|---|---:|---|
| `retail_turn_model_runner.gd` | 32/0 | `workspace/logs/q55-retail_turn_model_runner-after-pin-fix.txt` |
| `retail_formation_movement_runner.gd` | 34/0 | `workspace/logs/q55-final-retail_formation_movement_runner.txt` |
| `retail_lockstep_determinism_runner.gd` | 5/0 | `workspace/logs/q55-final-retail_lockstep_determinism_runner.txt` |
| `retail_member_combat_runner.gd` | 115/0 | `workspace/logs/q55-final-retail_member_combat_runner.txt` |
| `castle_gate_runner.gd` | 47/0 | `workspace/logs/q55-final-castle_gate_runner.txt` |
| `castle_map_live_boot_runner.gd` (default Erebor) | 8/0 | `workspace/logs/q55-final-castle_map_live_boot_runner.txt` |
| `retail_state_pin_runner.gd` | `b025d16237ff644d66211a9cc26872f18b61520b9a377f11e9e99c6eceb43f58` unchanged | `workspace/logs/q55-final-retail_state_pin_runner.txt` |
| `retail_projectile_pin_runner.gd` | `709def7cadaf6c91079697a343f437f71d6a2b10d71238248f57b171f8486e7f` unchanged | `workspace/logs/q55-retail_projectile_pin_runner-after-fix.txt` |
| `retail_pathing_pin_runner.gd` | `2e5ad58054d28dc93f37ef4728549bb538f6d4a1c22be922ec19b59fb2d1b12d` unchanged | `workspace/logs/q55-retail_pathing_pin_runner.txt` |
| `retail_slice_runner.gd` | 371/59; exact named set 87/87 | `workspace/logs/q55-retail_slice_runner.txt` and `q55-retail_slice_name_compare.txt` |

The slice name comparison used
`C:\Users\Jonathan\Desktop\open-bfme\workspace\logs\v027fin-retail_slice_runner.txt`
and reports `BASE_UNIQUE=87`, `CURRENT_UNIQUE=87`, `DIFF_COUNT=0`.

Honest caveat: after printing `RETAIL_SLICE_RESULT passed=371 failed=59` and the
expected acceptance failure, the Godot process exited with Windows code
`-1073741819` (access violation) during teardown. The named-failure acceptance
criterion is proven, but the runner process itself did not exit cleanly. This
lane did not diagnose or mask that teardown crash.

## Not done

- A fresh-context verifier has not rerun this report.
- No pack/content/publish work was authorized or performed.
- Fast-radius profiles outside Q55's complete `Slow=0 / Min=100%` contract stay
  byte-identical in runtime rules; broad admission would move the unrelated
  projectile pin because that pin hashes its full static rule table.

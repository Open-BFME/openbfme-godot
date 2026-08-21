# Q55 — cavalry FastTurnRadius / pivot-crush report (2026-08-21)

## Result

Q55 is fixed in the commit containing this report. A full-speed
`NormalCavalryHordeLocomotor` row no longer selects the zero-radius slow path
merely because `MinTurnSpeed` is 100%. While it is genuinely below that
threshold, the authored slow radius still owns the manoeuvre; at the threshold,
the authored fast radius owns the arc. Crush admission now uses displacement
actually achieved during the tick, so a zero-translation pivot cannot trample.

The round-2 verifier regression is fixed by runtime/test commit
`412bd31ebbff862bf3a439f2914d8adc72f421d6`, whose parent is the original Q55
commit `3ab3a1f5cdf5eabc35b9ce0d17e94f164c6b3b03`. The round-2 fallback is narrow:
when the adapter did not admit `fast_turn_radius`, `_step_route` applies the old
slow-radius clamp only when the pre-Q55 `current_speed <= minimum + epsilon`
condition would have applied. Min-below-100% rows therefore remain unchanged.

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
- Round 2 restores the pre-Q55 slow-radius fallback for rows without an admitted
  fast radius, guarded by the exact old minimum-speed condition. It does not
  broaden adapter admission or add state keys.

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

## Round-2 verifier repair

The test-only red run is
`workspace/logs/q55r2-turn-red.txt`: `RETAIL_TURN_MODEL_RESULT passed=33
failed=2`. The new Spiderling shape stepped `1.10000002384186` instead of the
pre-Q55 clamped `1.03672557568463`; the Baby Drake shape stepped
`1.20000004768372` instead of `0.08377580409573`. Both runtime rows correctly
lacked `fast_turn_radius`, proving the regression was the missing fallback, not
accidental admission.

The first broad fallback attempt is preserved honestly in
`workspace/logs/q55r2-turn-green.txt`: `31/4`. It restored the two new cases but
incorrectly clamped min-below-100% movement, including the Mumakil spot-check.
The final guard restores the exact old clamp condition. The final focused log is
`workspace/logs/q55r2-turn-green-final.txt`: `35/0`; Spiderling and Baby Drake
are green, and the selected `MordorMumakil` min-12% row remains byte-identical to
its pre-Q55 position bytes.

The selected-document census is rerunnable from
`workspace/logs/q55r2-residue-census-final.txt`: 118 playable-unit documents
author `FastTurnRadius` but remain outside Q55 adapter admission. Of those, 41
have `MinTurnSpeed=100%` and positive `SlowTurnRadius`; round 2 restores their
old clamp. The other 77 have `MinTurnSpeed<100%`; they are unchanged and still
ignore the authored fast radius (heroes, siege, fellbeasts, and Mumakil class).
There is no other bucket.

The four behavior-changing documents are all in the read-only selected pack
`rotwk-wild-vslice/2f477040a6125c0874acd9aab0d6c5e9e44384a1eedda83c6b677049343d8f86`:

- `wildspiderlinghorde`, `wildbannerspiderling`, and
  `wildbannerspiderrider` compile slow 33 / fast 33 / min 100% from
  `locomotor.ini:2944-2956`. Their pre-Q55 slow clamp was already the retail
  radius, so round 2 restores byte-identical movement.
- `wildbabydrakehorde` compiles slow 2 / fast 30 / min 100% from
  `locomotor.ini:178-188`. Round 2 restores its pre-Q55 slow clamp; consuming
  its authored fast 30 remains outside Q55 scope.

| Round-2 gate | Result | Log |
|---|---:|---|
| `retail_turn_model_runner.gd` | 35/0 | `workspace/logs/q55r2-turn-green-final.txt` |
| `retail_formation_movement_runner.gd` | 34/0 | `workspace/logs/q55r2-formation-movement.txt` |
| `retail_lockstep_determinism_runner.gd` | 5/0 | `workspace/logs/q55r2-lockstep-determinism.txt` |
| `retail_state_pin_runner.gd` | `b025d16237ff644d66211a9cc26872f18b61520b9a377f11e9e99c6eceb43f58` unchanged | `workspace/logs/q55r2-state-pin-final.txt` |
| `retail_projectile_pin_runner.gd` | `709def7cadaf6c91079697a343f437f71d6a2b10d71238248f57b171f8486e7f` unchanged | `workspace/logs/q55r2-projectile-pin.txt` |
| `retail_pathing_pin_runner.gd` | `2e5ad58054d28dc93f37ef4728549bb538f6d4a1c22be922ec19b59fb2d1b12d` unchanged | `workspace/logs/q55r2-pathing-pin.txt` |
| Selected-doc residue census | 118 = 41 clamp-restored + 77 unchanged | `workspace/logs/q55r2-residue-census-final.txt` |

Round-2 non-blocking findings retained for the queue:

- No protected pin or current gate covers compiled-locomotor kinematics. A
  kinematics gate over real compiled movement blocks is the durable coverage.
- Standing-start and braking ticks remain residue: for min-100% locomotors,
  genuinely sub-top-speed ticks still select the slow mode.
- The cavalry family's authored fast radius 48 gates successfully but never
  binds in the current fixture; retail cavalry would swing wider than the
  current 15.9 arc. Closing that parity gap is future work.

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
  byte-identical in runtime rules and, after round 2, in behavior: all 41
  min-100%/positive-slow documents have their pre-Q55 clamp restored, including
  the four selected Wild documents cited above; all 77 min-below-100% documents
  are unchanged. Broad fast-radius admission remains future work because it
  would alter both kinematics and the projectile pin's full static rule table.

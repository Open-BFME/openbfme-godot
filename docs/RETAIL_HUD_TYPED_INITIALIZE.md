# Retail HUD typed initialize effects

This slice closes only the 24 source-proven initialize events identified by
the BFME2 1.06 Palantir host-bridge oracle. It does not add an ActionScript VM,
general property access, reflection, host callbacks, or guessed lifecycle
ordering.

## Exact executable closure

Two unique retail byte programs are allowlisted:

| Program | Retail bytes | Repeated events | Typed effect |
|---|---:|---:|---|
| `ingamesidecommandbar:clip-event:13680` | 59 bytes, SHA-256 `782d8458e3a04ea8fc4a0563665053035b92d6bfd14e978f6e4b6d1f72873fbc` | 12 | Define local `SetFlashEffectState(state)` as the exact ancestor-indexed `flashEffects[_name].gotoAndPlay(state)` binding. |
| `libingameui:clip-event:56252` | 13 bytes, SHA-256 `0e6307bff26d6ffca7483353a04501e18d7acc5ee2dc60a50e5a75160ae81bb6` | 12 | Set Flash property 7 (`_visible`) on the empty-string/current target to `false`. |

Every event retains its exact record hash, target path, target clip identity,
program byte range, decoded instructions, and dispatch point:
`after-target-create-and-name-before-display-list-insert`.

The Godot runtime validates the exact program ID, movie, offsets, byte length,
SHA-256, and complete typed effect shape before accepting either program. It
then writes only bounded target state:

- `localMethods.SetFlashEffectState` receives the typed method body; it is data,
  not evaluated code.
- `_visible` becomes `false` for the property-7 program.

The three Palantir text initialization programs and the one unload/lifecycle
program remain blocked. No work from their separate oracles is folded into
this slice.

## Measured private result

The planner-backed conversion is byte-identical across two runs:

- contract file SHA-256: `22d2ad2ae72018cb7e97044dc9fd5a2438dd4d250934dcadd617659dd56d1766`
- contract aggregate: `7be22993067f5a88f11e1147051eae0f60b88ef20741237bc846b676ce536e12`
- blockers: 28 (the planner path does not add the WND observation blocker)

The sealed 189-source bundle conversion is also byte-identical:

- scene-contract file SHA-256: `3b1468f6b24cd06779d61efd44479cf9518806b995e04663f971aad2cdbe7016`
- contract aggregate: `5c848017922533e6ef7e90339066bbf27e88d0deae42564fb107728d3ad6018b`
- blockers: 29
- clip programs: 6 total, 2 executable
- clip events: 28 total, 24 executable, 4 blocked

Private artifacts live under `.private/scratch/hud-typed-initialize` and remain
gitignored.

## Verification

```powershell
$env:PYTHONPATH = 'importer'
python -m pytest importer/tests/test_retail_hud_apt_convert.py -q
python -m ruff check importer/openbfme_importer/retail_hud_apt_convert.py importer/tests/test_retail_hud_apt_convert.py

& '<HOME>\Downloads\godot47\Godot_v4.7-stable_win64_console.exe' `
  --headless --path game --script res://tests/retail_hud_apt_runtime_runner.gd

$contract = (Resolve-Path '.private\scratch\hud-typed-initialize\bundle-a\data\ui\palantir\scene-contract.json').Path
$pack = (Resolve-Path '.private\scratch\hud-typed-initialize\bundle-a').Path
& '<HOME>\Downloads\godot47\Godot_v4.7-stable_win64_console.exe' `
  --headless --path game --script res://tests/retail_hud_apt_runtime_runner.gd -- `
  "--retail-hud-apt-contract=$contract" "--retail-hud-apt-pack-root=$pack"

& '<HOME>\Downloads\godot47\Godot_v4.7-stable_win64_console.exe' `
  --headless --path game --editor --quit
```

The private runtime gate also mutates the allowlisted byte hash and the typed
property shape independently and proves that both changes fail closed.

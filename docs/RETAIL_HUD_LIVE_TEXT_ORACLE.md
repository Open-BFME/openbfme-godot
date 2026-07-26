# Retail HUD live-text oracle

The BFME2 1.06 `game.dat` code, not a placeholder UI, defines the three live
Palantir strings used by text characters 130, 132, and 134.

| APT variable | Inputs | Exact rendered value |
|---|---|---|
| `APT:PalantirResources` | resource integer | `%d`; one space when negative |
| `APT:PalantirCommandPoints` | current, cap | `%d/%d`; current only when cap is negative; one space when current is negative |
| `APT:PalantirResourceMultiplier` | multiplier float | `x%g`; one space when exactly `1.0` |

The three verified retail code ranges are `0x006D46AD..0x006D4748`,
`0x007FEECB..0x007FEF80`, and `0x007FEF80..0x007FF02E`. The private oracle
verifies the complete BFME2 1.06 `game.dat` identity plus a SHA-256 for each
routine before emitting a payload-free contract. Non-finite multipliers are
outside the gameplay-facing contract and fail closed.

Generate the private evidence contract with:

```powershell
$env:PYTHONPATH = 'importer'
python -m openbfme_importer.retail_hud_live_text_oracle `
  <BFME2>\game.dat `
  --output .private\scratch\hud-live-text\contract.json
```

The Godot bridge must feed the existing deterministic Men/Fords simulation
values into these formatters. It must not render the APT placeholders
`999999`, `x99`, or `999/999` as fallbacks.

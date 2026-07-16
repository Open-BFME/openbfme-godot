# Retail HUD side-command runtime

This slice binds three byte-exact `InGameSideCommandBar` ActionScript programs
to a typed Godot topology adapter. It does not introduce a general
ActionScript virtual machine.

## Supported programs

- `ingamesidecommandbar:6272` updates adjacent button frames in authored
  `next`, then `prior` order.
- `ingamesidecommandbar:6368` updates the current frame, then adjacent frames.
- `ingamesidecommandbar:7296` requires the boolean `InGame` input and, when it
  is true, sends `_show` to `Button1` through `Button15` in ascending order.

Support is conditional on the exact retail APT hash, the three program hashes,
all six helper-body hashes, the `Button0` through `Button11` local placement
topology, and the authored sprite-18 frame records. A mismatch falls back to
the existing unsupported-opcode path during conversion or fails contract
validation in Godot.

## Runtime semantics

`make_side_command_state()` creates the sealed twelve-button local topology.
Executing program 7296 performs each existing button's frame-10 placements
before its queued program 6368 effect. `Frame` is placed before `ButtonGlass`,
then the label is selected from the helper truth table:

| Next frame | Prior frame | Label |
| --- | --- | --- |
| absent | absent | `_topbottom` |
| present | absent | `_top` |
| absent | present | `_bottom` |
| present | present | `_middle` |

The local topology has no `Button12` through `Button15`. Their four authored
calls remain in the dispatch order and are consumed as no-ops, matching the
sealed native undefined-receiver behavior. `unresolvedRuntimeTraceCount` is
therefore zero.

## Conversion contract

The converter emits `sideCommandTopology`, sets
`renderPolicy.exactSideCommandTopologyBound`, and reports
`summary.typedSideCommandActionScriptCount = 3`. Those three scripts move from
unsupported to supported; the helper-library script at source offset 6264
remains blocked because it is evidence for the typed adapter, not executable
general bytecode.

For the private 261-source retail closure this changes the ActionScript totals
from 61 supported / 13 unsupported to 64 supported / 10 unsupported and
removes exactly three `action-script-unsupported-opcodes` blockers. Other HUD
blockers, including resource-flash and external-attachment gates, are unchanged.

## Acceptance

Run the focused converter test and Ruff checks, generate two fresh private
bundles, compare their bytes and hashes, then pass each bundle through
`game/tests/retail_hud_apt_runtime_runner.gd`. Finish with a headless Godot
editor import/compile check. Retail inputs and converted outputs stay under
`.private`.

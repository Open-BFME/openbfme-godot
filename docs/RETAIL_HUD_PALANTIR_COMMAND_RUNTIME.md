# Retail Palantir command registration runtime

The private retail HUD converter recognizes exactly two byte-identical
`Palantir.apt` ActionScript programs:

- `palantir:169224` declares six lifecycle callbacks.
- `palantir:169256` loops over numeric button frames `0` through `5` and
  installs three local methods on each frame.

Both programs are registration-only. Running either program does not invoke a
registered callback or method. The converter does not contain a generic
ActionScript virtual machine; any source hash, instruction shape, function
body, constant pool, operand, or authored placement change returns the program
to the existing unsupported-opcode blocker.

## Runtime contract

`palantir:169224` registers these functions in authored order:

1. `OnMovieClipFrameLoaded`
2. `OnMovieClipFrameUnloaded`
3. `OnCommandButtonSubMenuLoaded`
4. `OnCommandButtonSubMenuUnloaded`
5. `OnCommandButtonToggleFlashLoaded`
6. `OnCommandButtonToggleFlashUnloaded`

The contract preserves the exact native host intent and argument expression
for each function, but it does not dispatch those host calls during
registration. Native retained-slot and converted clip-handle ownership remain
behind the `command-child-lifecycle-host-result` retail trace gate.

`palantir:169256` registers, for buttons `0`, `1`, `2`, `3`, `4`, and `5`, in
that order:

1. `SetAutoAbilityOverlayState(state)` targets
   `this._parent._parent.AutoAbilityOverlays[this._name]`.
2. `SetFlashEffectState(state)` targets
   `this._parent.FlashEffects[this._name]`.
3. `SetGlassState(state)` targets
   `this._parent['glass' + this._name]`.

Each method performs the sealed `target.gotoAndPlay(state)` dispatch. The
runtime exposes only those three typed dispatches.

## Placement and scheduling

The `palantirCommandTopology` contract binds character `114` (`CommandButtons`)
and its exact `_hide = 0`, `_show = 9` labels, imports, root placements, numeric
button frames, glass targets, flash-effect children, and auto-ability overlay
children. The raw show-frame action record appears before the placement records
in the APT source. Retail scheduling defers that action until after the frame
record traversal, so the Godot runtime applies all authored frame placements
before executing `palantir:169256`.

## Deliberate blocker

`palantir:167296` remains unsupported. Its known call order is not enough to
execute it because the effects of `_root.UpdateSkillUpgradeButton` and
`_root.SetCommandButtonState` are absent from the converted APT closure and the
native name evidence. The retained gate is
`skill-upgrade-root-method-effects`; resolve it with a retail trace of one
`CommandUI` `_show` entry while `InGame` is true. Do not replace it with guessed
root mutations.

## Verification

Use the focused importer test and the HUD runtime runner. Fresh private A/B
conversion outputs must be byte-identical. The expected delta is two fewer
`action-script-unsupported-opcodes` blockers: 66 of 74 ActionScript programs
supported, 8 unsupported, and 20 total production blockers when the exact WND
companion is present.

# Retail HUD WND message runtime

`RetailHudWndRuntime` now validates the exact BFME2 1.06 WND message-semantics
oracle and exposes typed methods for all five callbacks declared necessary for
the Men-versus-Men slice:

- `ControlBarInput`
- `GameWinBlockInput`
- `PassSelectedButtonsToParentSystem`
- `LeftHUDInput`
- `ControlBarSystem`

The first three existing implementations are preserved. `LeftHUDInput` and
`ControlBarSystem` now follow their hash-pinned predicate and return-state
boundaries and emit ordered, address-pinned effect dictionaries for the opaque
branches. They do not use generic callback dispatch or assign guessed semantic
names to stripped retail types, controls, camera branches, or services.

The runtime requires both the exact 21-callback identity contract and the
five-handler message contract. It validates the private message-contract
aggregate, handler address ranges and SHA-256 values, typed interfaces, return
contracts, control bindings, state/dependency inventories, and unresolved-gate
counts. Changed handlers and malformed dynamic branch inputs fail closed.

## Current slice status

All five declared Men-versus-Men message callbacks have typed implementations.
The required-unimplemented count for this slice is therefore zero. This does
not mean every opaque semantic alias or retail service has been recovered.

The full WND runtime now implements 15 of 21 callback identities: ten sealed
draw callbacks and five slice-required message callbacks.

Two descriptor-known callbacks are retained outside the current slice rather
than being counted as Men-versus-Men requirements:

- `BeaconWindowInput`: event-dormant
- `ControlBarObserverSystem`: observer-only, outside declared player-versus-player
  play

Four runtime built-ins remain unresolved and are not claimed:

- `GameWinDefaultInput`
- `GameWinDefaultSystem`
- `GameWinDefaultTooltip`
- `W3DGameWinDefaultDraw`

## Remaining message alias and capture gates

`GameWinBlockInput` retains the stripped class names behind globals
`0x00e03220`, `0x00dfea3c`, and `0x00dfedf0`; the implementation uses only the
exact addresses and vslots.

`LeftHUDInput` retains three gates:

- semantic aliases for object field `+0x14` values
  `{0x0a,0x18,0x20,0x26}`;
- the selected-object branch choosing command ID `0x42f` or `0x430`;
- aliased selection/camera branches shared by messages `0x0011`, `0x0012`, and
  `0x0018`.

`ControlBarSystem` retains three gates:

- semantic names of the eight cached control handles;
- matched-service aliases for message `0x4031`;
- the message `0x400b` cached-control rejection and selected-button fallback
  branches. Their retail ordering is retained explicitly.

No generic dispatch, live HUD binding, or guessed built-in behavior is included.
Private oracle contracts remain under `.private`.

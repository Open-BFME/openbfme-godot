# Retail HUD WND draw runtime

`RetailHudWndRuntime` now validates the hash-pinned BFME2 1.06 WND draw oracle
and exposes typed command emitters for all ten sealed draw callbacks. It does
not render, resolve live game services, synthesize images, or bind the WND into
the live HUD.

Implemented draw callbacks:

- `W3DCommandBarBackgroundDraw`
- `W3DCommandBarForegroundDraw`
- `W3DCommandBarGenExpDraw`
- `W3DCommandBarGridDraw`
- `W3DCommandBarTopDraw`
- `W3DGadgetPushButtonImageDraw`
- `W3DLeftHUDDraw`
- `W3DNoDraw`
- `W3DPowerDraw`
- `W3DRightHUDDraw`

Every entry requires both the exact callback-control binding contract and the
draw-semantics contract. The latter pins all ten handler ranges and SHA-256
identities, authored control bindings, image literals, and ordered semantics.
Malformed controls, non-finite rectangles, over-four-cell grid state, missing
branch inputs, changed hashes, or a changed contract aggregate fail closed.

The emitters return ordered command dictionaries with
`renderingCommitted = false`. Exact retail addresses and vslots are preserved,
including the distinct background/foreground pairs, grid cell helper, three
push-button branches, radar service boundary, power image vslots, and the
RightHUD default-helper branch. `W3DNoDraw` remains a strict empty command list.

Seven draw handlers still carry bounded dynamic gates:

- background blend and parameter aliases;
- foreground mapped-image indirection;
- general-experience progress-source and blend aliases;
- grid cell semantic names and material parameters;
- top-tail draw parameters;
- left-HUD radar object plus blend/clipping structures;
- power counters and image blend/state aliases.

`W3DGadgetPushButtonImageDraw`, `W3DRightHUDDraw`, and `W3DNoDraw` have no
remaining draw-oracle dynamic gate. These gates do not prevent exact typed
command emission, but they do prevent claiming rendered parity or binding a
guessed rendering service.

The runtime now implements 13 of the 21 callback identities: the ten draw
callbacks plus the three existing typed message callbacks. Eight identities
remain unimplemented:

- `BeaconWindowInput`
- `ControlBarObserverSystem`
- `ControlBarSystem`
- `GameWinDefaultInput`
- `GameWinDefaultSystem`
- `GameWinDefaultTooltip`
- `LeftHUDInput`
- `W3DGameWinDefaultDraw`

The private oracle contract remains under `.private`; no retail payload or
contract was copied into the public tree.

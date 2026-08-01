# Retail ControlBar.wnd oracle

Reverse-engineered evidence about the **retail** `ControlBar.wnd` companion that
runs alongside the APT Palantir HUD in BFME2 skirmish.

The first section is the load-bearing one: it proves statically that the WND is
live in skirmish, refuting the earlier "candidate-dead" conclusion recorded in
the former `RETAIL_HUD_HOST_BRIDGE.md`. The remaining sections are the canonical
callback identity table and the draw / message / unresolved-builtin evidence that
hangs off it.

> **Consolidation note.** This document absorbs several former standalone
> documents, listed under their own headings below and preserved verbatim.
> Counts, blocker totals and hashes inside those sections are snapshots taken
> when that investigation was written and are **not** current status; they are
> kept because the surrounding evidence depends on them. Live gate results
> live only in [STATUS.md](../STATUS.md).

---

<!-- merged from docs/RETAIL_HUD_WND_ACTIVATION_ORACLE.md -->

## Retail HUD WND activation oracle

`ControlBar.wnd` is active in BFME2 1.06 skirmish beside the Apt/Palantir HUD.
It is not candidate-dead legacy payload. This is statically decidable from the
exact retail executable; no runtime trace is required for the activation
decision.

The decisive retail call route is one initialization method:

1. The BFME2 control-bar manager installs vtable `0x00BC7A88`.
2. Its init slot points to thunk `0x0048F0BB`, which tail-jumps to shared init
   `0x0069E2DE`.
3. That init calls `0x0069C23E` at `0x0069E5AD`. The callee passes the exact
   `ControlBar.wnd` string at `0x00BFD060` to the WND loader.
4. The same init dispatches vtable slot `0x1DC` at `0x0069E5F5`.
5. BFME2 vtable slot `0x1DC` is `0x0048F32C`, the Apt factory. Its constructor
   reaches `0x006D638E`, which consumes the global `Palantir.apt` name.

Therefore the correct model is **WND semantic/control companion plus Apt visual
HUD**, not an exclusive WND-or-Apt choice. OpenSAGE's `AptControlBarSource`
selection is useful observation of the visible layer, but it is not authority
for deleting retail WND behavior.

The exact private inputs are:

- `window/controlbar.wnd`: 289,881 bytes, 87 windows, 21 callback identities;
- `data/ini/controlbarresizer.ini`: 9,398 bytes, 84 exact
  `ControlBar.wnd:*` control references;
- `F:\BFME2\game.dat`: SHA-256
  `f008b587570bad693981dc7218588c81d192a1e064b0f7f861539c51156a7640`.

The payload-free A/B contracts and report live under
`.private/scratch/hud-wnd-activation-oracle`. Each executable code range,
asset, vtable slot, address, and OpenSAGE observation file is hash-pinned.

### Consequence

Do not delete `ControlBar.wnd`, its resizer closure, or the 21 callback
identities. Loading proves they belong to the live BFME2 control-bar runtime;
it does not prove that every event-dependent callback fires in every match or
define its parameters. Exact BFME2 callback semantics remain fail-closed.
Generals callback behavior and generic name dispatch are not acceptable
substitutes.

If a callback's semantics cannot be recovered statically, the smallest useful
dynamic trace is callback-specific: break on `0x0069C23E` to confirm the known
load, then record registration and invocation only for the selected callback
during one BFME2 1.06 skirmish. A broad frame capture or another activation
trace is unnecessary.

### Verification

```powershell
$env:PYTHONPATH = 'importer'
python -m pytest importer/tests/test_retail_hud_wnd_activation_oracle.py -q
python -m ruff check importer/openbfme_importer/retail_hud_wnd_activation_oracle.py importer/tests/test_retail_hud_wnd_activation_oracle.py
```

The focused gate is three passing tests, clean Ruff, and byte-identical
`contract-a.json` / `contract-b.json`.

---

<!-- merged from docs/RETAIL_HUD_WND_CALLBACK_ORACLE.md -->

## Retail HUD WND callback oracle

The active `ControlBar.wnd` closure contains 21 callback identities. Retail
`game.dat` has exact `{name, handler, null}` descriptors for 17 of them. Four
shared `GameWinDefault*` callbacks are authored in the WND but have no ASCII,
UTF-16, or descriptor identity in `game.dat`, `lotrbfme2.exe`, or the other
install executables. They remain WND-library built-ins with addresses requiring
one narrow dynamic trace; behavior from Generals or OpenSAGE is not substituted.

### Slice classification

- 19 callbacks are baseline-bound or frame-reachable in an ordinary
  BFME2 1.06 Men-v-Men player skirmish.
- `BeaconWindowInput` is event-dormant until the multiplayer beacon editor is
  opened.
- `ControlBarObserverSystem` is outside the declared player-versus-player slice.

This is an implementation order, not permission to remove the latter two.
All 21 identities and their exact control bindings remain in the converted
closure.

### Exact retail handlers

| Callback | Handler | Slice role |
|---|---:|---|
| `BeaconWindowInput` | `0x0090DBAA` | beacon-event dormant |
| `ControlBarInput` | `0x004D43D0` | bound exact no-op |
| `ControlBarObserverSystem` | `0x0090E6A6` | observer-only |
| `ControlBarSystem` | `0x00802455` | central selected-button/control-bar dispatcher |
| `GameWinBlockInput` | `0x0071435A` | active input blocking/reset |
| `LeftHUDInput` | `0x008020CE` | active radar/camera/order input |
| `PassSelectedButtonsToParentSystem` | `0x006C0978` | active typed parent forwarding |
| `W3DCommandBarBackgroundDraw` | `0x0049FA82` | frame-reachable draw |
| `W3DCommandBarForegroundDraw` | `0x0049FB63` | frame-reachable draw |
| `W3DCommandBarGenExpDraw` | `0x0049ED8D` | frame-reachable progress draw |
| `W3DCommandBarGridDraw` | `0x0049DE26` | frame-reachable command grid |
| `W3DCommandBarTopDraw` | `0x0049DF9A` | frame-reachable top overlay |
| `W3DGadgetPushButtonImageDraw` | `0x004A6019` | frame-reachable button image |
| `W3DLeftHUDDraw` | `0x0049DCEE` | frame-reachable radar/HUD draw |
| `W3DNoDraw` | `0x004B3FD0` | exact one-byte `ret` |
| `W3DPowerDraw` | `0x0049E365` | frame-reachable power draw |
| `W3DRightHUDDraw` | `0x0049DDE6` | frame-reachable production/right HUD |

The private contract records each descriptor VA and SHA-256, handler entry VA,
entry-prefix length and SHA-256, exact WND control names/indexes/types/status,
binding kind, resizer reference coverage, compared message constants, static
side-effect boundary, and proposed typed Godot interface.

The message callbacks use the retail cdecl-shaped boundary
`(window, message, data1, data2) -> handled`; draw callbacks use the observed
`(window, instanceData) -> void` shape. These are typed adapter boundaries, not
a generic name-to-callable map.

### Four built-in addresses

The unresolved built-ins are `GameWinDefaultInput`, `GameWinDefaultSystem`,
`GameWinDefaultTooltip`, and `W3DGameWinDefaultDraw`. Their smallest trace is:

1. Break after the exact WND load returns at `0x0069C25F` in one BFME2 1.06
   Men-v-Men skirmish.
2. Inspect only the callback slot for the exact controls listed in the private
   contract: root, `LeftHUD`, `LeftHUD1Input`, and `ProductionQueueWindow`.
3. Trigger one default input, system, tooltip-hover, or draw event. Record the
   indirect target, four entry stack words, return `EAX`, and touched services.
4. Hash the discovered handler entry before promoting its typed adapter.

No activation trace, broad frame capture, or observer match is required.

### Verification

```powershell
$env:PYTHONPATH = 'importer'
python -m pytest importer/tests/test_retail_hud_wnd_callback_oracle.py -q
python -m ruff check importer/openbfme_importer/retail_hud_wnd_callback_oracle.py importer/tests/test_retail_hud_wnd_callback_oracle.py
```

The focused gate is three passing tests, clean Ruff, and byte-identical private
`contract-a.json` / `contract-b.json`.

---

<!-- merged from docs/RETAIL_HUD_WND_DRAW_SEMANTICS.md -->

## Retail HUD WND draw semantics

This oracle freezes the ten descriptor-known draw handlers used by the active
BFME2 1.06 control-bar WND. Every complete handler body, WND control binding,
destination rectangle, authored image literal, draw order, downstream service,
and unresolved state alias is retained in a deterministic private contract.

Exact authored literals include `ControlBar.wnd:BackgroundMarker`,
`ControlBar.wnd:ButtonGeneral`, `GenExpBarTop1`, `GenExpBarBottom1`,
`GenExpBar1`, `PowerPointY`, `PowerPointG`, and `PowerBarSlider`. No substitute
images, inferred materials, OpenSAGE behavior, or Generals behavior are used.

Important command boundaries:

- Background and foreground use distinct pre-state/draw functions even though
  both resolve through the authored background marker.
- Grid emits up to four ordered cell draws from retail-computed rectangles.
- Push-button image draw preserves its three-way delegate/image/fallback branch.
- Left HUD delegates world/radar pixels and clipping to the exact radar service.
- Power and experience handlers preserve authored image order and clipped live
  progress geometry; stripped blend/state aliases remain trace gates.
- Right HUD is an exact conditional delegation to the retail default helper.
- `W3DNoDraw` at `0x004B3FD0` is exactly one byte, `C3` (`RET`). It performs no
  reads, writes, calls, draws, or state changes.

The proposed boundary is ten allowlisted typed functions returning ordered
`DrawCommand` values. Generic callback dispatch and procedural fallback visuals
are forbidden. Remaining traces break only on the exact handler needing an
alias, recording control/instance pointers, mapped-image identity, destination
rect, state/color words, and ordered downstream calls.

Verification:

```powershell
$env:PYTHONPATH = 'importer'
python -m pytest importer/tests/test_retail_hud_wnd_draw_semantics.py -q
python -m ruff check importer/openbfme_importer/retail_hud_wnd_draw_semantics.py importer/tests/test_retail_hud_wnd_draw_semantics.py
```

---

<!-- merged from docs/RETAIL_HUD_WND_MESSAGE_SEMANTICS.md -->

## Retail HUD WND message semantics

This oracle freezes the exact BFME2 1.06 state-machine boundary for the five
descriptor-known, non-draw callbacks needed first by the Men-v-Men slice. It
does not implement them and does not borrow semantic names from another SAGE
title.

Three handlers are statically complete:

- `ControlBarInput` at `0x004D43D0` is the exact three-byte `xor eax,eax; ret`
  function: every input returns unhandled and performs no side effect.
- `GameWinBlockInput` at `0x0071435A` returns unhandled only for messages
  `0x0015` and `0x0018`. Message `0x0006` clears exact input/camera service
  states; every other message is consumed.
- `PassSelectedButtonsToParentSystem` at `0x006C0978` forwards only
  `0x4006`, `0x4007`, `0x4008`, `0x4009`, `0x400B`, and `0x4031` to the exact
  parent through WND-manager vslot `0xE8`. Null control, missing parent, and all
  other messages return zero.

`LeftHUDInput` (`0x008020CE..0x00802455`) and `ControlBarSystem`
(`0x00802455..0x00802960`) are fully hash-pinned, with exact message predicates,
return paths, service addresses/vslots, field offsets, and call ordering in the
private contract. Stripped type aliases prevent honest names for several
selected-object and cached-control branches, so those branches remain bounded
and opaque rather than guessed.

The typed implementation boundary is five allowlisted methods. There is no
generic callback dictionary. `BeaconWindowInput` remains retained but
event-dormant; `ControlBarObserverSystem` remains retained outside the declared
player-versus-player slice.

The only remaining dynamic work is two handler breakpoints, `0x008020CE` and
`0x00802455`. Record message/data words, listed global pointers, exact call
target, return `EAX`, and written addresses for each opaque branch. Promote a
semantic alias only after two identical Men-v-Men observations.

Verification:

```powershell
$env:PYTHONPATH = 'importer'
python -m pytest importer/tests/test_retail_hud_wnd_message_semantics.py -q
python -m ruff check importer/openbfme_importer/retail_hud_wnd_message_semantics.py importer/tests/test_retail_hud_wnd_message_semantics.py
```

---

<!-- merged from docs/RETAIL_HUD_WND_BUILTIN_ORACLE.md -->

## Retail HUD WND built-in oracle

The four unresolved `ControlBar.wnd` built-ins remain blocked. Static analysis
of the exact BFME2 1.06 `game.dat` identifies their per-window callback slots
and setter functions, but it does not identify the target placed in any of
those slots. No callback is implementation-safe yet, and this oracle does not
substitute Generals, OpenSAGE, or a generic name dispatcher.

### Exact static result

| Authored callback | Exact controls | Window slot | Setter | Static result |
|---|---|---:|---:|---|
| `GameWinDefaultInput` | `ProductionQueueWindow` | `+0x1E0` | `0x00714147` | target unresolved |
| `GameWinDefaultSystem` | `LeftHUD`, `LeftHUD1Input` | `+0x1E4` | `0x00714134` | target unresolved |
| `GameWinDefaultTooltip` | `ProductionQueueWindow`, `LeftHUD1Input` | `+0x1EC` | `0x0071416D` | target unresolved |
| `W3DGameWinDefaultDraw` | `ControlBarParent` | `+0x1E8` | `0x0071415A` | target unresolved |

The slot setters are small and exact: the input, system, and draw setters only
store a non-null function pointer; the tooltip setter stores its pointer. Their
entry/end addresses and full SHA-256 hashes are sealed in the private oracle.
The authored names and exact bindings are independently sealed from the active
87-window WND.

The executable registry evidence is also closed:

- 35 exact draw descriptors at `0x00DB3D1C..0x00DB3EC0`;
- 21 exact system descriptors at `0x00DBC9C4..0x00DBCAC0`;
- 18 exact input descriptors at `0x00DBCACC..0x00DBCBA4`;
- the one exact `TOOLTIP` token descriptor at `0x00DBE308`;
- 473 callback-like descriptors in a structural scan of the complete initialized
  `.data` range.

None of the four authored names occurs in those descriptors or anywhere in
`game.dat` as ASCII or UTF-16. Consequently there is no static name-to-handler
edge to seal.

### No-op and delegation decision

No built-in is proven to be a no-op. No built-in is proven to delegate.

`0x0049DC32` is an exact default-draw helper: it tries the window's `+0x1E8`
override and otherwise delegates to the window draw object. Its body hash and
five direct callers are sealed. However, there is no static registry or loader
edge binding that helper to the authored `W3DGameWinDefaultDraw` identity. It
therefore remains a candidate, not an implementation target.

The remaining gates are callback-specific:

- input: resolved slot target, exact message/handled-return set, and touched
  window or service state;
- system: resolved slot target, exact message/handled-return set, and ordered
  parent/child or service dispatch;
- tooltip: resolved slot target, calling convention/result ownership, and text,
  image, or localization services;
- draw: resolved slot target, no-op versus delegation, and ordered draw,
  blend, and clipping operations, including whether `0x0049DC32` is actually
  involved.

### Minimal runtime capture

The activation route is already proven, so a broad trace is unnecessary.
For each callback:

1. Break at `0x0069C25F`, immediately after the exact `ControlBar.wnd` load.
2. Resolve only the control identities in the table above and read the exact
   four-byte callback slot.
3. If the slot is zero, hardware-watch that slot and reload. If it is nonzero,
   set an execute breakpoint on the observed target.
4. Trigger only that callback kind. Record the target VA, entry stack words,
   `EAX` return where applicable, memory writes, calls, and ordered draw calls.
5. Recover and hash the complete handler range before promoting any typed
   adapter.

This is the minimum evidence needed to change `implementationSafe` for an
individual callback. A similar-looking helper or another SAGE title is not
sufficient.

### Verification

```powershell
$env:PYTHONPATH = "importer"
python -m pytest importer/tests/test_retail_hud_wnd_builtin_oracle.py -q
python -m ruff check importer/openbfme_importer/retail_hud_wnd_builtin_oracle.py importer/tests/test_retail_hud_wnd_builtin_oracle.py
```

The byte-identical private A/B contracts live under
`.private/scratch/hud-wnd-builtin-oracle`.

- Contract aggregate: `459f94f95014dc7e3cddd6b4af705c584eef0b18dad93af4b98db9aab6efab71`
- File SHA-256: `804a5c310c501524dc54c896cc622923a62dd7c9dd700886aa4df2031ca86d31`
- Focused tests: 3 passed; Ruff clean

---


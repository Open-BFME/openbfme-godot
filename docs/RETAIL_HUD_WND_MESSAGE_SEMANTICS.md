# Retail HUD WND message semantics

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

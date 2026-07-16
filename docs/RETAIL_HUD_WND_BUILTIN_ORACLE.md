# Retail HUD WND built-in oracle

The four unresolved `ControlBar.wnd` built-ins remain blocked. Static analysis
of the exact BFME2 1.06 `game.dat` identifies their per-window callback slots
and setter functions, but it does not identify the target placed in any of
those slots. No callback is implementation-safe yet, and this oracle does not
substitute Generals, OpenSAGE, or a generic name dispatcher.

## Exact static result

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

## No-op and delegation decision

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

## Minimal runtime capture

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

## Verification

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

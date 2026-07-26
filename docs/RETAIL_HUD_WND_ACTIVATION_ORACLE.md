# Retail HUD WND activation oracle

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
- `<BFME2>\game.dat`: SHA-256
  `f008b587570bad693981dc7218588c81d192a1e064b0f7f861539c51156a7640`.

The payload-free A/B contracts and report live under
`.private/scratch/hud-wnd-activation-oracle`. Each executable code range,
asset, vtable slot, address, and OpenSAGE observation file is hash-pinned.

## Consequence

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

## Verification

```powershell
$env:PYTHONPATH = 'importer'
python -m pytest importer/tests/test_retail_hud_wnd_activation_oracle.py -q
python -m ruff check importer/openbfme_importer/retail_hud_wnd_activation_oracle.py importer/tests/test_retail_hud_wnd_activation_oracle.py
```

The focused gate is three passing tests, clean Ruff, and byte-identical
`contract-a.json` / `contract-b.json`.

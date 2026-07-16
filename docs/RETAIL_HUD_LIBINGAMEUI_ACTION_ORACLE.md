# Retail HUD libInGameUI MovieClipFrame action oracle

`retail_hud_libingameui_action_oracle.py` seals the 294-byte BFME2 1.06
program `libingameui:37332`. It reads the exact private retail APT/CONST/DAT
triplets and `game.dat`, then emits hashes and typed semantics only. No retail
payload is copied outside `.private` and no generic ActionScript VM is added.

## Exact program

The program is frame 0 of exported `libInGameUI.MovieClipFrame` character 6.
Its authored order is:

1. Install `CreateContent(contentType, contentName)`.
2. Install `DeleteContent()`.
3. Test `Boolean(initialized)`.
4. On first entry, install an `onUnload` closure.
5. Call `_parent.OnMovieClipFrameLoaded(this)`.
6. Set `initialized = true`.

It is not declaration-only: the first-entry path invokes the parent callback.

`CreateContent` calls `attachMovie(contentType, contentName, 0)`, resolves the
new child as `this[contentName]`, and branches when that lookup is undefined.
For a defined child it copies `placeholder._x`, `_y`, `_width`, and `_height`
in that order. It then writes the child path to
`extern[String(this) + "_ContentName"]`.

`DeleteContent` calls `contentClip.removeMovieClip()` only when `contentClip`
is defined. The unload closure calls
`_parent.OnMovieClipFrameUnloaded(this)`.

The three exact branch inputs are:

| Offset | Input | True path |
| --- | --- | --- |
| `53760` | `contentClip == undefined` | End `CreateContent` at `53824` |
| `53858` | `contentClip == undefined` | End `DeleteContent` at `53869` |
| `53873` | `Boolean(initialized)` | Skip first-entry work at `53949` |

## Parent and placement closure

Two retail parent contexts are source-proven:

- `InGameSideCommandBar` imports the symbol as character 1. Its root frame 0
  places `ButtonSet`; that sprite places `Button0` through `Button11`; each
  button places the imported `MovieClipFrame` as `Button` at its frame 0.
  Parent load/unload calls become
  `OnAptInGameSideCommandBarButtonFrameLoaded` and
  `OnAptInGameSideCommandBarButtonFrameUnloaded`.
- `Palantir` imports the symbol as character 108. Root frame 0 places
  `CommandButtons`; its `_show` frame 9 places six instances in authored order
  `1, 2, 3, 4, 5, 0`. Parent calls become
  `PalantirCommandUI::OnButtonFrameLoaded` and
  `PalantirCommandUI::OnButtonFrameUnloaded`.

The oracle pins both parent ActionScript bodies, all four native callback
handlers, both callback registries, native frame scheduling, and the native
content adapter. The native adapter reads
`_level%u.%s_ContentName`, calls `CreateContent`, retains the resulting clip
handle, calls `DeleteContent` during teardown, and clears its initialized
state in that order.

## Implementation decision

The program is implementation-safe as a bounded typed adapter. A generic
ActionScript VM and a new runtime trace are not required for these semantics.
The fail-closed implementation rule is:

- Resolve `contentType` only through the converted retail movie/export
  allowlist.
- If it is absent, preserve retail's undefined branch: do not copy geometry,
  do not register an extern path, and do not invent fallback content.

One concrete-content gate therefore remains: a requested `contentType` cannot
render until that exact retail export exists in the converted closure. This
oracle adds static evidence only. It does not implement runtime support,
authorize profile-blocker removal, or claim any concrete dynamic child is
already converted.

## Verification

```powershell
$env:PYTHONPATH = "importer"
python -m pytest importer/tests/test_retail_hud_libingameui_action_oracle.py -q
python -m ruff check `
  importer/openbfme_importer/retail_hud_libingameui_action_oracle.py `
  importer/tests/test_retail_hud_libingameui_action_oracle.py

python -m openbfme_importer.retail_hud_libingameui_action_oracle `
  .private/retail-work/cache/effective-assets `
  F:/BFME2/game.dat `
  --output .private/scratch/hud-libingameui-action-oracle/contract.json
```

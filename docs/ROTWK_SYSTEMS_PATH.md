# RotWK systems path

Operator commands for the systems-first factory. Product ladder:
`DIRECTION.md`. Volatile numbers: `STATUS.md`.

Validate offline:

```powershell
powershell -File tools\gate-rotwk-systems.ps1 -SkipLiveRetail
```

With install: `-RotwkInstall C:\Path\To\RotWK`.

## Quick start

```bat
set ROTWK_INSTALL=C:\Path\To\RotWK
run_rotwk_systems.bat
```

```powershell
powershell -File tools\rotwk-systems.ps1 -RotwkInstall C:\Path\To\RotWK
```

## What `run_rotwk_systems` does

1. Bootstrap pinned importer Python env
2. `doctor --game rotwk`
3. `census-maps` / `census-factions`
4. `tools/rotwk_map_cook_corpus.py` - map cook + conversion ledger
5. `tools/rotwk_binding_factory.py` - binding burn-down
6. Faction plans (`import-faction --plan-only`) or `-ConvertFactions`
   (`rotwk_faction_convert_batch.py`; `publicationReady` stays false until pack proof)
7. Pack proof: `tools/rotwk_faction_pack_proof.py` (no `selection.json` unless `--select`)
8. `tools/check-product-contracts.py --check`

Ledgers land under `.private/retail-work/reports/` (JSONL + summary %).

OpenSAGE comparison checklist: [OPENSAGE_GAP_MATRIX.md](OPENSAGE_GAP_MATRIX.md).

## One-button

```bat
run_rotwk_one_button.bat C:\Path\To\RotWK
run_rotwk_one_button.bat C:\Path\To\RotWK --launch
run_rotwk_one_button.bat C:\Path\To\RotWK --convert-factions --binding-limit 5 --map-limit 10
run_rotwk_one_button.bat C:\Path\To\RotWK --multi-map
run_rotwk_one_button.bat C:\Path\To\RotWK --multi-map --build --publish --launch
```

| Flag | Meaning |
|---|---|
| `--launch` | `run_game.bat` after convert path |
| `--multi-map` | `tools/rotwk_multimap_skirmish.py` profile/catalog |
| `--build` | Cook multi-map pack |
| `--publish` | Owner-only: rewrite `selection.json` |
| `--full-profile` | Terrain-closed profile via layered RotWK+BFME2 install |

Without `--publish`, the game uses the already-selected pack.

## Layered install

RotWK `terrain.big` is thin; multiplayer terrain mostly lives in BFME2 base.
`tools/rotwk_layered_install.py` builds junctions under
`.private/retail-work/editions/rotwk/layered-install/`.

## Related scripts

- `tools/rotwk_map_cook_corpus.py`
- `tools/rotwk_binding_factory.py`
- `tools/rotwk_faction_convert_batch.py`
- `tools/rotwk_faction_pack_proof.py`
- `tools/rotwk_multimap_skirmish.py`
- `tools/rotwk_layered_install.py`

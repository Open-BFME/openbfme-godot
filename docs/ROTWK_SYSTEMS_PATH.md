# RotWK systems-first operator path

**Owner:** integration owner  
**Owns:** day-to-day RotWK systems factory commands  
**Does not own:** product ladder (see `DIRECTION.md`) or volatile gate numbers (`STATUS.md`)  
**Last verified commit:** dirty working tree (systems-first RotWK operator path, 2026-07-30)  
**Update trigger:** operator entry points, map-cook corpus schema, or gate contract changes  
**Validation:** `powershell -File tools/gate-rotwk-systems.ps1 -SkipLiveRetail` and, with install, `-RotwkInstall <path>`

## Quick start

```bat
set ROTWK_INSTALL=F:\RotWK
run_rotwk_systems.bat
```

Or:

```powershell
powershell -File tools\rotwk-systems.ps1 -RotwkInstall F:\RotWK
```

## What it does

1. Bootstraps the pinned importer Python env  
2. `doctor --game rotwk`  
3. `census-maps` / `census-factions`  
4. `tools/rotwk_map_cook_corpus.py` — strict cook + conversion ledger (% connected)  
5. `tools/rotwk_binding_factory.py` — visual-closure / object-binding burn-down + % bound  
6. Faction plans (`import-faction --plan-only`) or `-ConvertFactions` for real convert  
   (`rotwk_faction_convert_batch.py` writes durable coverage + object artifacts; convert
   coverage keeps `publicationReady=false` until pack proof)  
7. Pack/runtime receipt: `tools/rotwk_faction_pack_proof.py` (compose + cook + audit;
   no `selection.json` rewrite unless explicit `--select`)  
8. `tools/check-product-contracts.py --check`  

Cross-project presentation oracle: `docs/OPENSAGE_GAP_MATRIX.md`.  


**Conversion logging:** every systems tool writes JSONL under
`.private/retail-work/reports/*-ledger*.jsonl` plus a summary with
`percentConvertedLike` / `percentFailedLike` / `percentGapLike` and up to 50
detailed failure records (error + traceback tail).

Does **not** rewrite `selection.json` unless you later pass an explicit build with publish.

### One-button path

```bat
run_rotwk_one_button.bat F:\RotWK
run_rotwk_one_button.bat F:\RotWK --launch
run_rotwk_one_button.bat F:\RotWK --convert-factions --binding-limit 5 --map-limit 10
run_rotwk_one_button.bat F:\RotWK --multi-map
run_rotwk_one_button.bat F:\RotWK --multi-map --build --publish --launch
run_rotwk_one_button.bat F:\RotWK --profile .private\retail-work\profiles\rotwk-skirmish-maps.generated.json --publish --launch
```

- `--launch` starts `run_game.bat` after the convert path.
- `--multi-map` runs `tools/rotwk_multimap_skirmish.py` (generate skirmish profile +
  catalog proof). Add `--build` to cook the pack; add `--publish` only with owner
  authority (rewrites `selection.json`).
- `--full-profile` builds a terrain-closed skirmish profile via the **layered
  install** (`editions/rotwk/layered-install`: RotWK + BFME2 base). Required for
  multi-map terrain cook; RotWK-only `terrain.big` is too thin.
- Without `--publish`, the game uses whatever pack is already selected.

## Focused gate

```powershell
powershell -File tools\gate-rotwk-systems.ps1 -RotwkInstall F:\RotWK
```

Offline CI-friendly:

```powershell
powershell -File tools\gate-rotwk-systems.ps1 -SkipLiveRetail
```

## Map cook report

Default output:

```text
.private/retail-work/reports/rotwk-map-cook-corpus.json
```

Verdicts include `cooked-and-connected`, `cooked-but-starts-disconnected`,
`under-two-player-starts`, `cook-rejected`, `cook-error`,
`registry-stale-missing-payload`. Fatal cook errors exit nonzero.

## Relation to legacy wrappers

| Wrapper | Role |
|---|---|
| `run_rotwk_systems.bat` | **Preferred** RotWK systems path |
| `run_importer.bat` | Legacy BFME2 Men/Fords profile build |
| `import_faction.bat` | Uses RotWK when `ROTWK_INSTALL` is set; else BFME2 |
| `run_m2_acceptance.bat` | Historical M2 oracle gate only |

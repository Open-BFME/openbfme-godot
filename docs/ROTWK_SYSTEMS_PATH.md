# RotWK Patch 2.02 v9.7.7 operator path

This document distinguishes the exact target baseline from historical RotWK
diagnostic scripts. It does not own live status or authorize publication.

## Authority and live work

- [Product scope](../contracts/rotwk-202-v9.7.7-product-scope.json)
- [Retail baseline](../contracts/rotwk-202-v9.7.7-baseline.json)
- [English overlay](../contracts/rotwk-202-v9.7.7-english-overlay.json)
- [Architecture](ARCHITECTURE.md)
- [Verification](VERIFICATION.md)
- [Roadmap](ROADMAP.md)
- [Live work items](../orchestration/work-items.json)

Only a current work item may name a cook, publish, or selection command as
target work. Old reports and commands below remain implementation diagnostics;
they are not a second authority.

## Start here: prepare the exact three-layer baseline

Keep each source outside the repository. This command creates private junctions
under `workspace\retail-work` and verifies the resulting catalog against the
pinned Patch 2.02 v9.7.7 policy.

```bat
set BFME2_INSTALL=C:\Games\BFME2
set ROTWK201_INSTALL=C:\Games\RotWK
set PATCH202_OVERLAY=C:\Games\RotWK-Patch-202-v9.7.7

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass ^
  -File tools\prepare-rotwk-202-baseline.ps1 ^
  -Bfme2Install "%BFME2_INSTALL%" ^
  -Rotwk201Install "%ROTWK201_INSTALL%" ^
  -Patch202Overlay "%PATCH202_OVERLAY%"
```

The effective source order is:

```text
layer 0: Patch 2.02 official-2 v9.7.7 English overlay
layer 1: RotWK 2.01 installation
layer 2: BFME2 1.06 installation
```

Baseline preparation proves source/catalog identity only. It does not prove
conversion, bundle publication, runtime loading, gameplay, visual, audio, mode,
or whole-product parity.

The preparation command calls the standalone read-only verifier. It can be
repeated without touching the layer layout:

```bat
workspace\retail-work\tools\python-3.12-env\Scripts\python.exe tools\verify-rotwk-202-baseline.py
```

Do not use `-ReplaceExisting` simply to clear a refusal. It archives a
nonmatching private layered root; investigate the source mismatch first.

## Current cook and publication rule

After baseline preparation, use only the command and owned paths declared by
the relevant row in `orchestration/work-items.json`. That row must bind:

- the v9.7.7 product, baseline, overlay, catalog, recipe, and tool identities;
- deterministic output and complete provenance;
- newly published immutable bundle addresses;
- the complete selection transaction; and
- the focused verification and required oracle level.

Only the integration owner may publish or select a bundle. A script that can
write `selection.json` is not thereby an approved target pipeline.

## Historical 2.01-era systems diagnostics

> **NON-TARGET EVIDENCE.** The commands in this section were built around the
> earlier RotWK source route and bounded packs. They can still expose importer,
> map, binding, faction, or runtime defects, but a green result is not Patch
> 2.02 v9.7.7 evidence. Do not publish or select their output for the target
> unless a current work item has explicitly retargeted and verified that exact
> command.

Offline diagnostic:

```powershell
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File tools\gate-rotwk-systems.ps1 -SkipLiveRetail
```

`-SkipLiveRetail` means the private/live stages are `SKIP`, never `PASS`.

Historical systems wrapper:

```bat
set ROTWK_INSTALL=C:\Path\To\RotWK
run_rotwk_systems.bat
```

It bootstraps the importer environment and exercises earlier doctor, census,
map-cook, binding, faction-plan, and pack-proof surfaces. Its reports belong
under ignored `workspace\retail-work\reports\` and must retain their historical
source identity.

### Historical full-content command

```powershell
python tools/rotwk_full_content.py --install C:\Path\To\RotWK --bfme2-install C:\Path\To\BFME2
```

This command's former faction/map closure and `PLAYTEST` card describe its
historical input and assertions. They must not be relabeled as v9.7.7 coverage.
Do not add `--select` for target work.

### Historical one-button commands

```bat
run_rotwk_one_button.bat C:\Path\To\RotWK
run_rotwk_one_button.bat C:\Path\To\RotWK --launch
run_rotwk_one_button.bat C:\Path\To\RotWK --convert-factions --binding-limit 5 --map-limit 10
run_rotwk_one_button.bat C:\Path\To\RotWK --multi-map
run_rotwk_one_button.bat C:\Path\To\RotWK --multi-map --build
```

Historical flags such as `--publish` or a publish-and-launch combination can
rewrite canonical private state. They are intentionally omitted. The commands
above may diagnose old profile behavior, but neither a built pack nor a launch
proves it used the exact 2.02 source.

### Historical component scripts

- `tools/rotwk_map_cook_corpus.py`
- `tools/rotwk_binding_factory.py`
- `tools/rotwk_faction_convert_batch.py`
- `tools/rotwk_faction_pack_proof.py`
- `tools/rotwk_multimap_skirmish.py`
- `tools/rotwk_full_content.py`
- `tools/rotwk_layered_install.py`

The older layered-install helper is superseded for target setup by
`tools/prepare-rotwk-202-baseline.ps1`. Reuse any component only after its work
item proves the new source policy, inputs, outputs, and consumer path.

## Result interpretation

Follow [VERIFICATION.md](VERIFICATION.md): `PASS` requires fixed identity,
required acceptance markers, and zero unexpected diagnostics; `FAIL` blocks;
`SKIP` does not satisfy a prerequisite. Source preparation, conversion,
loading, behavior, visual, audio, and end-to-end qualification remain separate
evidence levels.

# Onboarding

Get the source, set a few paths, convert content from a game you own, then
launch. Windows first. This is an experimental alpha - first convert can take
a long time.

If something is missing, the tools usually **stop with an error** instead of
guessing. That is intentional (see [Glossary](#glossary)).

## Requirements

- Windows 10 or 11, Git, PowerShell
- **Rise of the Witch-king 2.01** (needs BFME2 base underneath - the tools
  layer them). Optional: BFME2 alone for comparison only
- Godot **4.7** for Windows (console build preferred so you can see log text)
- Python 3.12 on PATH once so tools can bootstrap a **private** env under
  `workspace/` (after that, the private env is used; first gate may download
  pinned packages over the network)
- Lots of free disk for `workspace/` (full multi-faction work is tens of GB)

Retail-derived files live under `workspace/`; use them freely. Git ignores
`workspace/` and the publication-boundary CI scans tracked files for retail
bytes and machine-absolute paths — that is the whole policy.

## Glossary (words we use a lot)

| Term | Plain meaning |
|---|---|
| **RotWK** | *Rise of the Witch-king* (the expansion). Our main target. |
| **BFME2** | *Battle for Middle-earth II* base game. Required under RotWK. |
| **Pack** | Converted game content the Godot client loads (factions, maps, assets). Lives under `workspace/content-packs` after you convert. |
| **Fail closed** | If required data is missing or invalid, **stop and report an error**. Do not invent fake art or silent placeholders for "parity" paths. |
| **Gate** | Automated check script (headless). Green = that check passed. |
| **Doctor** | Install / tool health check (`run_doctor.bat` or importer `doctor`). |
| **Selection** | Which pack is "active" for launch (file `selection.json`). Only rewritten when you explicitly publish/select. |
| **Layered install** | RotWK folder + BFME2 folder joined so the importer sees one catalog. |
| **Systems factory** | Scripts under `tools/rotwk_*.py` that census, cook maps, convert factions, and prove packs. |

## Environment variables

Set these in Command Prompt before running tools (or put them in a `.bat` you keep outside the repo).

| Variable | Purpose |
|---|---|
| `OPENBFME_GODOT` | Full path to Godot 4.7 `.exe` (preferred). Also accepted: `GODOT_CONSOLE`, `GODOT_EXE`, `GODOT`. |
| `ROTWK_INSTALL` | Folder that contains RotWK `game.dat`. |
| `BFME2_INSTALL` | BFME2 install when layering / Men wizard needs it. |
| `OPENBFME_CONTENT` | Where packs live (default: `workspace\content-packs`). |
| `OPENBFME_IMPORT_ROOT` | Importer workspace (default: `workspace\retail-work`). |

How Godot is found (`tools/resolve-godot.bat`):

1. Those env vars  
2. Else a local drop under `.tools\godot\` (gitignored)  
3. Else `godot` on your PATH  
4. Else error (fail closed)

## 1. Clone

```bat
git clone https://github.com/Open-BFME/openbfme-godot.git
cd openbfme-godot
```

## 2. Path A - RotWK (recommended)

Matches the product baseline and `tools/rotwk_*.py`.

```bat
set OPENBFME_GODOT=C:\Path\To\Godot_v4.7-stable_win64_console.exe
set ROTWK_INSTALL=C:\Path\To\RotWK

:: Check tools without a game install (may download pinned Python packages once)
powershell -File tools\gate-rotwk-systems.ps1 -SkipLiveRetail

:: Full systems path (census, map cook plans, etc.)
run_rotwk_systems.bat

:: Convert only - does NOT pick which pack the game launches
run_rotwk_one_button.bat "%ROTWK_INSTALL%"

:: First-time: convert multi-map pack, SELECT it, then launch
:: (--publish writes workspace\content-packs\selection.json)
run_rotwk_one_button.bat "%ROTWK_INSTALL%" --multi-map --build --publish --launch
```

More operator flags: [ROTWK_SYSTEMS_PATH.md](ROTWK_SYSTEMS_PATH.md).

## 3. Path B - Men pack wizard (legacy tests)

Builds the older "Men" pack used by classic headless tests
(`retail_slice_runner`, `menu_skirmish_runner`).

```bat
python tools\onboard.py --install "C:\Path\To\BFME2" --rotwk "C:\Path\To\RotWK" --godot "%OPENBFME_GODOT%" --yes
```

Or interactive: `python tools\onboard.py`.

Exit codes: `0` = setup + tests green, `1` = setup ok but a test failed,
`2` = setup stopped early.

## 4. Launch

```bat
run_game.bat
run_retail_slice.bat
run_retail_slice.bat --test
```

## Manual Men convert (same idea as the wizard)

```bat
run_doctor.bat
set "PY=workspace\retail-work\tools\python-3.12-env\Scripts\python.exe"
%PY% tools\openbfme_import.py doctor --install "C:\Path\To\BFME2"
%PY% tools\openbfme_import.py import-faction --install "C:\Path\To\BFME2" --faction men --convert
%PY% tools\openbfme_import.py publish-faction-to-slice --install "C:\Path\To\BFME2" --faction men --select
```

Other factions: `--faction elves`, `dwarves`, `isengard`, `mordor`, `wild`,
or `--game rotwk --faction angmar`.

### Exit codes you will actually see

The importer separates "this broke" from "I refuse to ship this", because they
need different reactions.

| Exit | Meaning | What to do |
|---|---|---|
| `0` | Success | Nothing |
| `3` | The pack was built but failed its own audit | Real failure — read the audit output |
| `6` | A **convert** step is reporting its own gaps | Some objects did not convert. The publish step will refuse this coverage (see 7) |
| `7` | A **publish gate refused** — nothing was published | Not a crash. Fix and re-run; see below |

**Exit 7 is a deliberate refusal.** `publish-faction-to-slice`, `build`, and
`import-unit` all run the same gates, and any of three things triggers one:

- **incomplete coverage** — the conversion report records converter gaps, so
  the cook would ship a known-short faction;
- **stale coverage** — the report is clean but does not describe the tree being
  cooked (its catalog identity or its compiler identity token disagrees with
  what is on disk), so it cannot vouch for this cook;
- **roster regression** — the cook drops playable-unit ids that the already
  published bundle of the same pack id ships. Checked by *name*, so swapping
  one unit for another refuses too.

The reason list is always printed to stderr. The normal fix is to re-run the
conversion and publish again:

```bat
%PY% tools\openbfme_import.py import-faction --install "C:\Path\To\BFME2" --faction men --convert
```

Each gate has an override, and each one means "I know this ships something
worse": `--allow-incomplete-coverage`, `--allow-stale-coverage`,
`--allow-fewer-playable-units`. They print what they let through.

## If something breaks

| Symptom | What to do |
|---|---|
| Doctor rejects install | Point at the **install root** (folder with `game.dat` or `lotrbfme2.exe`), not a random subfolder |
| Godot not found | Set `OPENBFME_GODOT` or copy the exe under `.tools\godot\` |
| Gate / test fails | Read the error; check [orchestration/queue.md](../orchestration/queue.md) and [docs/state/](state/). Fail closed is normal when data is incomplete |
| Convert stopped mid-way | Run the **same** command again (resumes). Avoid deleting `workspace` as a first step |
| Before you publish code | `powershell -File tools\export-scan.ps1` |

## Related

- [CONTENT_PIPELINE.md](CONTENT_PIPELINE.md) - import / packs / workspace  
- [MODDING.md](MODDING.md) - simple pack example from the repo  
- [FAQ.md](FAQ.md)  
- [DIRECTION.md](../DIRECTION.md) - product goals  

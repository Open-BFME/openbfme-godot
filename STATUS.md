# Status

Volatile. Prefer focused gate output over anything written here.

**Product:** RotWK 2.01, systems-first (`DIRECTION.md`).
**Repo:** public source â€” https://github.com/Open-BFME/openbfme-godot
**Not the strategy:** permanent Men/Fords vertical-slice freeze.

## Current surface (code)

| Surface | Entry |
|---|---|
| Offline systems gate | `tools/gate-rotwk-systems.ps1 -SkipLiveRetail` |
| RotWK operator path | `run_rotwk_systems.bat` / `run_rotwk_one_button.bat` |
| Godot resolve | `tools/resolve-godot.bat` / `.ps1` |
| Containment | `tools/export-scan.ps1` |
| Classic Men onboard | `tools/onboard.py` |
| Launch | `run_game.bat` |

Importer default game: `rotwk` (`importer/openbfme_importer/cli.py`).

## Code inventory (not gate evidence)

Scripts that exist in-tree for the systems factory:

- `tools/rotwk_map_cook_corpus.py`
- `tools/rotwk_binding_factory.py`
- `tools/rotwk_faction_convert_batch.py` / `rotwk_faction_pack_proof.py`
- `tools/rotwk_layered_install.py` / `rotwk_multimap_skirmish.py`

Open items: converter-gap burn-down, multi-map play smoke, THIRD_PARTY
procedural asset redistribution attestation.

Re-run focused gates after material changes; do not treat this file as a
pinned pack hash ledger.

## Known open product work

- Converter-gap burn-down and multi-map runtime play smoke
- Simulation/AI driven more fully from pack descriptors
- Campaigns / WOTR ladder steps (not started as shipped play)
- Hardened multiplayer and installer

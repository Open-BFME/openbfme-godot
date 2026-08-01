# Current objective

Systems-first RotWK iteration. Full ladder: `DIRECTION.md`. Evidence: `STATUS.md`.

## Context

- Parity baseline: **RotWK 2.01** (not BFME2-only freeze)
- Not the strategy: permanent Men/Fords vertical-slice freeze
- Policy: `contracts/rotwk-201-product-scope.json`

## Active work (systems factory)

Pull from the top; prefer >=2 maps or >=2 factions before calling a system general.

1. RotWK content pipeline - entry exists (`run_rotwk_systems.bat`)
2. Map cook IR + official map corpus - entry exists (`rotwk_map_cook_corpus.py`)
3. Asset closure + batch convert - entry exists (`rotwk_faction_convert_batch.py`)
4. Object binding - entry exists (`rotwk_binding_factory.py`)
5. Navigation / buildability - partial
6. Simulation from pack descriptors - next
7. Script / AI packaging - open
8. Skirmish shell over RotWK factions/maps - partial (`rotwk_multimap_skirmish.py`)
9. One-button convert+play - entry (`run_rotwk_one_button.bat`; publish only with `--publish`)

Done enough: reusable on RotWK data, fail-closed on gaps, focused automated check.

Not the objective: one-button UX before systems 1-8 are real; inventing retail
behavior; silent placeholder art in parity mode.

## Legacy tooling

`run_m2_acceptance.bat` and Men/Fords oracle IDs may remain for regression. They
do not define product completion.

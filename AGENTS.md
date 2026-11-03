# Agent and contributor rules: game editions

OpenBFME is growing two editions of the game, each with its own rules:

- **BFME2 1.06**: the base. Its rules come from the clean BFME2 1.06
  `game.dat` (md5 `34391b253b0ee12d6cec1178f46d3bf9`); the Open-BFME-2
  decompilation is the reading aid, not the authority.
- **RotWK 2.01**: BFME2 plus its differences. Its rules come from the RotWK
  2.01 `game.dat` (cited as `RW 0x...`).

Today every game runs RotWK's rules. The BFME2 edition exists only behind a
developer switch (`OPENBFME_EDITION=bfme2`, see `docs/EDITIONS.md`). BFME1
keeps its current mode (RotWK's rules on BFME1 data, stop S-5017).

## Rules for every change

1. **State the editions.** Every simulation change says in its commit /
   pull request / report which editions it affects ("RotWK only", "both",
   "BFME2 only").
2. **Cite both sources where the games differ.** A function whose behaviour
   differs between the editions cites the RotWK address (`RW 0x...`) and the
   BFME2 one (the BFME2 1.06 `game.dat` address, `BFME2 0x...`, or the
   decomp's file / `Rva` row). `tools/re/rw2decomp.py` and
   `tools/re/pair_diff.py` find the pair; `tools/re/edition_census.py` lists
   which functions differ.
3. **Never fall back silently.** A rule that one edition has and the other
   lacks is a named error, an `unverified` report line or a registered stop
   (`docs/STOPS.md`), never the other edition's rule used quietly.
4. **Per-system variants, not scattered checks.** Edition differences live
   in one variant per system (AI, hordes, castles, War of the Ring, ...)
   reached through one edition accessor, not in per-function `if` tests on
   strings or install paths spread through the code.
5. **Mods declare their edition.** A mod is loaded for the edition it names.

Everything else (sources, numerics, tests, the commit gate, privacy) is in
`CLAUDE.md` and `docs/PLAN.md`.

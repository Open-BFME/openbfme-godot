# tools/re: RotWK 2.01 -> BFME2 1.06 -> Open-BFME-2 function map

RotWK's `game.dat` is BFME2's engine plus the expansion. Open-BFME-2 is a byte-matching C++
decompilation of BFME2 1.06's `game.dat`. These tools map every RotWK function to its BFME2
counterpart and to the decomp's source for it, so a lane can read C++ instead of disassembly
wherever the code is unchanged.

Lanes only need the lookup (`rw2decomp.py`); the rest rebuilds the map.

## Lookup

From the repository root with Python 3 and Git. The lookup requires the generated
map below; the donor clone adds source line numbers and is optional. Binary
comparison/rebuilding also requires `capstone` and `pefile`, plus the local retail
binaries. These POSIX-shell examples work on
Linux/macOS; set `ROTWK_INSTALL` and `BFME2_INSTALL` to the install roots first:

```sh
python3 tools/re/rw2decomp.py 0x66B3FD                  # RotWK VA, any address inside a function
python3 tools/re/rw2decomp.py privateMoveToPosition     # RotWK Ghidra name or decomp name
python3 tools/re/rw2decomp.py --bfme2 0x66B487          # BFME2 VA -> RotWK
python3 tools/re/pair_diff.py --rotwk "$ROTWK_INSTALL/game.dat" --bfme2 "$BFME2_INSTALL/game.dat" \
    --map workspace/rebuild/xmap/rw_map.csv 0x66B3FD   # where a tier-B pair differs
```

It reads `workspace/rebuild/xmap/rw_map.csv` and the clone `workspace/reference/open-bfme-2` of
the main checkout (the current decomp: keep it pulled, and rebuild the map when it moves) (found through git, so this works from any lane worktree), or
`$OPENBFME_XMAP` / `$OPENBFME_DECOMP2`.

Tiers:

| Tier | Meaning | How to use the decomp source |
|---|---|---|
| A | identical bytes after masking absolute addresses, external branch targets and `__FILE__` paths | describes RotWK exactly (modulo addresses); `amb..-far` = identical code, uncertain which BFME2 instance |
| B `same-shape` | same instructions, some operand values differ (struct offsets, constants, frame size) | the logic is the source's; check the changed offsets/constants in RotWK |
| B `edited` | similarity >= 0.9, instructions added or removed | read the source, then the RotWK differences (`pair_diff.py`) |
| C | anchored by a string only both functions use | a starting point |
| D | similarity 0.6-0.9 in the anchored gap or by call graph | a starting point |
| RotWK-only | no counterpart | disassembly only |

Decomp status of the counterpart: `authored` / `vendored` / `generated` = byte-matched C++
(the ledger row starting at the function), `attempt` = recovered C++ that does not match yet
(`reverse/attempts/`; the folder is absent while nothing is banked), `tu-only` = no code, but the
decomp assigns the function to a file, `assembly` = byte-matched reviewed assembly (not C++),
`data` = verified data in `.text`, `library` / `dump` = no source, `partial-*` = ledger rows cover
part of the extent, `none`. A ledger lane the tools don't know stops the rebuild: add it to
`LANE_RANK` in `match_rotwk.py` (the clone's `tools/progress.py` `SOURCE_LANES` lists them).

Always cite the RotWK address; BFME2 and the decomp are donors (PLAN source priority).

Known limit: where BFME2's Ghidra list merged two functions into one entry and RotWK's did not
(BFME2 0x2FB997 is FindSafePath + InternalFindPath, 0x2DBCCA is GameState::xfer + its neighbour,
0x3CA4BE is executeAction + initConditionTemplates), the RotWK half that pairs with it gets the first
function's name and the other half is RotWK-only or pairs with something else. Check the
counterpart's `bfme2_size` against the ledger row's size when a name looks wrong.

## Rebuilding the map

Outputs contain retail-derived addresses and names only (no bytes, no decompiled code) and stay
under the ignored `workspace/rebuild/xmap/`.

The commands below are argument sketches, not directly executable shell blocks:
replace `X` with `workspace/rebuild/xmap`, create that directory first, and replace
each `<...>` with a quoted local path. Use the same Python/Git/binary-analysis
prerequisites as above; the inventory fetch additionally needs a running local
GhidraMCP server with the named program loaded.

1. Function entries. RotWK: from the headless GhidraMCP server (read-only
   `/list_functions_enhanced`). BFME2: the decomp's own Ghidra list, `reverse/ghidra_functions.csv`
   (no second Ghidra analysis needed). Both sides use Ghidra's entries and the same extent rule
   (to the next entry, minus int3 padding): adding the ledger's EH-funclet entries to BFME2 only
   made the partitions disagree and cut tier A from 61.7% to 53.9% of the bytes.

       python tools/re/inventory.py --bin <RotWK game.dat> --ghidra-url http://127.0.0.1:8089 \
           --program rotwk201_game.exe --save-json X/rotwk_ghidra_raw.json --out X/rotwk_functions.csv
       python tools/re/inventory.py --bin <BFME2 game.dat> \
           --csv <open-bfme-2>/reverse/ghidra_functions.csv --out X/bfme2_functions.csv

   (`ghidra/export_inventory.java` exports Ghidra bodies, string xrefs and vtables where Ghidra
   scripting is available; its extents are Ghidra bodies, so don't mix it with inventory.py.)
2. The decomp ledger with provenance lanes: `python tools/re/ledger_lanes.py --clone <open-bfme-2> --out X/bfme2_ledger.csv`
3. The map: `python tools/re/match_rotwk.py --config X/config.json` with keys `rotwk`, `rotwk_inv`,
   `bfme2`, `bfme2_inv`, `bfme2_ledger`, `decomp` (the clone), `work` (feature cache), `out_csv`
   (`rw_map.csv`), `out_json` (`rw_summary.json`: totals, per subsystem, RotWK-only islands and
   strings, tier A/B samples). About 3 minutes and 0.4 GB on an idle machine (the first run
   extracts features into `work`; 25+ minutes on a loaded one). The last run's config is kept as
   `workspace/rebuild/xmap/config.json`; back the folder up (`xmap.bak-<date>`) before a rebuild.
4. Stops: `python tools/re/stops_xmap.py --out X/stops_xmap.md` ranks the open `docs/STOPS.md`
   rows whose cited code is tier A (or B) with byte-matched decomp source.

`edition_census.py` (lane BFME2MODE-0) classifies every `RW 0x...` cite in `engine/src/GameLogic` and `engine/src/Common`:
same behaviour / real divergence / RotWK-only, plus the BFME2-only GameLogic functions. It writes the generated section of
`docs/EDITIONS.md`, which describes the method and its limits.

`text_patches.py` lists `.text` references into the community-added sections of the installed
RotWK `game.dat` (stop S-001).

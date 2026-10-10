# tools/re: RotWK 2.01 -> BFME2 1.06 -> Open-BFME-2 function map

RotWK's `game.dat` is BFME2's engine plus the expansion. Open-BFME-2 is a byte-matching C++
decompilation of BFME2 1.06's `game.dat`. These tools map every RotWK function to its BFME2
counterpart and to the decomp's source for it, so a lane can read C++ instead of disassembly
wherever the code is unchanged.

Lanes only need the lookup (`rw2decomp.py`); the rest rebuilds the map.

## Lookup

    python tools/re/rw2decomp.py 0x66B3FD                  # RotWK VA, any address inside a function
    python tools/re/rw2decomp.py privateMoveToPosition     # RotWK Ghidra name or decomp name
    python tools/re/rw2decomp.py --bfme2 0x66B487          # BFME2 VA -> RotWK
    python tools/re/pair_diff.py --rotwk <RotWK game.dat> --bfme2 <BFME2 game.dat> \
        --map <rw_map.csv> 0x66B3FD                        # where a tier-B pair differs

It reads `workspace/rebuild/xmap/rw_map.csv` and the clone `workspace/reference/open-bfme-2` of
the main checkout (found through git, so this works from any lane worktree), or
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
(`reverse/attempts/`), `tu-only` = no code, but the decomp assigns the function to a file,
`library` / `dump` = no source, `partial-*` = ledger rows cover part of the extent, `none`.

Always cite the RotWK address; BFME2 and the decomp are donors (PLAN source priority).

## Rebuilding the map

Outputs contain retail-derived addresses and names only (no bytes, no decompiled code) and stay
under the ignored `workspace/rebuild/xmap/`.

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
   strings, tier A/B samples). About 3 minutes and 0.4 GB.
4. Stops: `python tools/re/stops_xmap.py --out X/stops_xmap.md` ranks the open `docs/STOPS.md`
   rows whose cited code is tier A (or B) with byte-matched decomp source.

`text_patches.py` lists `.text` references into the community-added sections of the installed
RotWK `game.dat` (stop S-001).

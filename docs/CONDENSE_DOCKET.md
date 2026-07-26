# Condense docket

Musk-algorithm pass over the codebase, 2026-07-25. Steps 1–2 only: **requirements
questioned and deletions proposed.** Nothing here has been deleted. Owner approves
rows; deletion then happens via git, recoverable, in batches with gates re-run
between each.

Measured size: **358,241 tracked lines** of `.gd/.py/.cs/.ps1/.mjs`.

| area | lines | share |
|---|---:|---:|
| `importer/openbfme_importer` | 145,046 | 40% |
| `importer/tests` | 76,070 | 21% |
| `game/src` | 73,225 | 20% |
| `game/tests` | 39,139 | 11% |
| `tools` | 10,380 | 3% |
| `engine` | 8,539 | 2% |

**The importer is 61% of the codebase.** The instinct that the game code is the
bloat is wrong; `game/src` is a fifth of it.

## Step 1 — requirements questioned

| requirement | verdict | reasoning | owner |
|---|---|---|---|
| "Vertical slice" as the product frame | **DROP** | Legacy of a one-faction/one-map parity goal the project has outgrown — 7 factions and 195 maps now convert. The name is load-bearing in code (`retail_vertical_slice.gd`, `RetailVerticalSlice`, pack id `bfme2-men-vslice`, `OPENBFME_SLICE_*` env vars) and misleads every reader about scope. | DIRECTION.md |
| BFME2 1.06 as parity target | **REWRITE** | Owner moved the baseline to RotWK 2.01. `contracts/bfme2-106-product-scope.json` still declares `1.06` and `retail-campaigns: excluded`, contradicting the rewritten DIRECTION.md. The contract is machine-readable and wins by default — it must be reconciled deliberately. | contracts/ |
| Campaign + WotR out of scope | **REWRITE** | Campaign is in scope and at 62.7% script coverage. WotR remains out and that is correct — it is a separate living-world project sharing almost nothing. | DIRECTION.md |
| stage1–15 proof ladder | **KEEP for now** | Still reachable: `gate-stage10.ps1` chains stage9→stage1, `boot.tscn` has Stage1–9 buttons, and `main_menu.gd::_collect_stage_buttons()` uses `get_node` so it *crashes* if they are missing. Retiring it needs a four-step order starting in the UI. Already filed separately. | PLAN.md |
| Fords-only and Men-only profile modules | **REWRITE** | ~7.6k lines of `retail_fords_*` and `retail_men_*` modules encode single-map/single-faction assumptions. The generic replacements now exist (`map_prop_bindings.py` reproduces the hand-composed Fords profile exactly). These should collapse into the generic lane rather than persist beside it. | — |

## Step 2 — deletion docket

### DELETE — modules whose only reference is their own test

Code that exists solely to be tested. Verified by grepping every `.py/.ps1/.bat/.md/.json`
in the repo for each module name, excluding the module and its own test file.

| path | what it is | evidence | risk | verdict |
|---|---|---|---|---|
| `map_native_corpus.py` (+test) | map-format corpus analysis | 1 ref: `test_map_native_corpus.py` | low | **DELETE** — 2,674 |
| `support_corpus.py` (+test) | support-matrix corpus analysis | 1 ref: own test | low | **DELETE** — 2,483 |
| `map_script_closure.py` (+test) | map script closure probe | 1 ref: own test | low | **DELETE** — 1,913 |
| `w3d_decode_corpus.py` (+test) | W3D decode corpus survey | 1 ref: own test | low | **DELETE** — 1,843 |
| `scb_native_corpus.py` (+test) | SCB corpus analysis | 1 ref: own test | low | **DELETE** — 1,573 |
| `w3d_support_matrix.py` (+test) | W3D support matrix | 1 ref: own test | low | **DELETE** — 1,266 |

**Subtotal: 11,752 lines.** These are one-off investigation tools whose findings
have already been absorbed into the converters. If a future investigation needs
one, git has it.

### KEEP — flagged by the same sweep, but load-bearing or uniquely capable

| path | why it survives |
|---|---|
| `edition_overlay.py` (+test, 1,920) | The **only** implementation of expansion-wins overlay, and nothing else builds the RotWK layered install. With RotWK now the baseline this is required capability, not dead code. It is *unwired*, not dead. |
| `sage_csf.py` (+test, 576) | The only CSF localization-table parser. Localization is unimplemented, so it has no consumer yet — but deleting the sole implementation of a needed capability is exactly what the ground rules forbid. |

### Needs owner decision before docketing

| item | question |
|---|---|
| `retail_fords_*` (7.6k lines, 8 modules) | The generic prop-binding lane now reproduces the Fords profile exactly. Collapse these into it, or keep Fords as a pinned reference implementation? |
| `retail_men_*` (2.7k lines, 3 modules) | Same question for the Men-only lifecycle/damage profiles. |
| `contracts/bfme2-106-product-scope.json` | Update in place to RotWK, or freeze 1.06 and add a RotWK contract beside it? Freezing preserves banked evidence. |

## Steps 3–5 — not started

Deliberately. Steps 3 (simplify), 4 (accelerate) and 5 (automate) only apply to
survivors, and nothing has been cut yet. Note step 4's headline target is already
partly done: the conversion pipeline went 532 s → 386 s, with publish 15× faster.

## Calibration note

The skill's rule is that if you never want ~10% back, you did not cut enough.
This docket proposes ~12k lines of a 358k codebase — **3%** — which is
deliberately conservative for a first pass, because the evidence standard here is
"referenced by nothing but its own test" and that is provable. The larger prizes
(Fords/Men module collapse, the stage ladder, the vertical-slice rename) are all
*rewrites* rather than deletions and need the owner's call on sequencing.

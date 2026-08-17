# Q5 — `retail_state_pin` drift: diagnosis

Lane: `opus-q5`, 2026-08-17. Brief: `orchestration/briefs/q5-state-pin-drift-diagnosis.md`.
Read-only lane: no runtime, sim, runner or gate file was modified. All historical
runs were done in a detached `git worktree` at `%TEMP%\q5-bisect`, now removed.

## Headline

**The `retail_state_pin` hash is GREEN at HEAD (`e1ba04c`) and needs NO re-mint.**
The drift had exactly one cause and it has already been fixed, by `3dbbc9f`
(2026-08-17 05:17, *"fix(sim): keep lazy contract misses state-neutral"*), which
landed 34 minutes after the last red log was captured. Every observation of
`f5579dd9…` in `workspace/logs/` predates that commit.

Two other things are true and are NOT the pin's hash:

1. The gate step `retail_state_pin` in `tools/gate-retail.ps1` **still fails at
   HEAD** — not on the hash, on `$forbiddenDiagnostics`. The runner prints four
   `ERROR: [RetailSliceSim] structure armor: kind '…' has no compiled armor
   contract` lines and the gate's forbidden-diagnostic regex matches `\bERROR\b`.
   Measured, not assumed (see "Gate emulation" below).
2. The **sibling** pin `retail_scripted_state_pin` is red at HEAD against the
   gate ratchet, and its drift is only *partly* explained by the same cause. The
   unexplained remainder is inside the `3c67c7c` mega-commit. New work item.

## Method

- Runner: `<godot> --headless --path game --script res://tests/retail_state_pin_runner.gd`,
  hash read from the `RETAIL_STATE_PIN ticks=3000 hash=…` line the runner emits
  *before* its own assertion, so a commit's own `EXPECTED_HASH` never influences
  the measurement.
- Content dependence was eliminated as a variable first: at HEAD the pin emits
  the identical hash with today's selection mounted (`rotwk-men-vslice/4f92c8a4…`,
  7 factions / 136 units) and with `OPENBFME_CONTENT` pointed at a nonexistent
  path (loader fails closed, `packs=0 units=0`). Every historical run below was
  therefore executed content-free, which isolates code.

## Commit → hash table

`retail_state_pin` (`PIN_TICKS=3000`), all runs content-free, worktree:

| commit | date | hash | vs pin `0e4bcdbf…` |
|---|---|---|---|
| `b79ca1d` (parent of the mint) | 08-15 | `a436bb5989a026ee…` | n/a — this is the *superseded* value the 08-15 mint note names |
| `3c67c7c` **the mint commit** | 08-15 20:39 | `f5579dd90fedea8b…` | **RED at the moment of minting** |
| `5c700d8` | 08-16 00:43 | `f5579dd9…` | RED |
| `099abc8` | 08-16 00:46 | `f5579dd9…` | RED |
| `0067dad` | 08-16 03:56 | `f5579dd9…` | RED |
| `996ca18` | 08-17 01:55 | `f5579dd9…` | RED |
| `604fc71` | 08-17 03:58 | `f5579dd9…` | RED |
| `ebc60aa` | 08-17 05:00 | `f5579dd9…` | RED |
| `3dbbc9f` (= `ebc60aa`'s child) | 08-17 05:17 | `0e4bcdbf7e9a8579…` | **GREEN — flip** |
| `8cf4cd2` | 08-17 07:09 | `0e4bcdbf…` | GREEN |
| `e1ba04c` **HEAD** | 08-17 | `0e4bcdbf…` | GREEN (worktree *and* main tree, with content and without, 3 runs) |

Exactly **one** distinct drifted value across the 105 commits since the mint.
The flip is single-commit: `git rev-parse 3dbbc9f^` = `ebc60aa`.

## Single-hunk attribution (the pin header's own standard)

At `3dbbc9f`, with the rest of that commit intact, **only** the removed memo hunk
was put back into `_attach_structure_module_contracts` (re-adding
`row["structure_module_contracts_attempted"] = true` and its early-return arm):

| runner | `3dbbc9f` as committed | `3dbbc9f` + memo re-added |
|---|---|---|
| `retail_state_pin` | `0e4bcdbf…` (green) | `f5579dd9…` (the drifted value, exactly) |
| `retail_scripted_state_pin` | `b0a3b163…` | `5ec18b4e…` (the pre-fix value, exactly) |

So the memo is the sole cause on both pins, and `3dbbc9f`'s other hunks (the
weather/horn `source_key` change from `_next_event_sequence` to `tick_index`, and
moving `next_event_sequence` into `snapshot()` only) are pin-neutral for both.

Logs: `workspace/logs/q5-3dbbc9f-memo-readded-statepin.txt`,
`workspace/logs/q5-3dbbc9f-memo-readded-scriptedpin.txt`.

## Culprit classification

| commit | classification | why |
|---|---|---|
| `3c67c7c` *feat: compile and simulate typed retail contracts* | **(c) incidental state-shape change** | `_attach_structure_module_contracts` wrote a derived memo key `structure_module_contracts_attempted` into structure rows on a *miss*. Structure rows are hashed, so a bookkeeping flag with no gameplay meaning entered the authoritative state and moved the pin. No entity, economy, combat or position value changed. |
| `3dbbc9f` *fix(sim): keep lazy contract misses state-neutral* | **the fix** | Removes the memo; a no-contract lookup is byte-inert again. Restores `0e4bcdbf…` **exactly** — i.e. the value minted on 08-15 was correct all along. |
| `819b3e3` *fix(combat): close structure armor fallback defect* | **(b) defect, pin-neutral but gate-fatal** | Introduced `_record_structure_armor_provisionals`'s `push_error`. The hash does not move (`5c700d8`, after it, equals `3c67c7c`, before it), but the four `ERROR:` lines make the *gate step* fail. See below. |
| `629b72f`, `604fc71`, `2bbcfe6`, `ebc60aa`, `d78a8f3`, `6fa4bb6` (brief's other suspects) | pin-neutral | All measured inside the flat `f5579dd9…` run; none of them moves the hash. |

### The mint itself is the root cause of the incident

`3c67c7c` is a squash of ~11k lines in `retail_slice_sim.gd` that **both** minted
`EXPECTED_HASH = 0e4bcdbf…` *and* introduced the memo hunk that moved the real
hash to `f5579dd9…`. The mint's stated WHY (retiring four inert creep keys) is
verifiably honest — the parent `b79ca1d` measures `a436bb59…`, exactly the
"superseded value" in the mint note — but it was measured against a scratch
diagnostic rather than against the committed tree, so **the pin landed red in the
same commit that minted it** and stayed red for 33 hours. That is the process
defect worth naming: a mint must be proved by running the runner on the commit
being pushed.

## The armor floods (brief step 5)

They ARE in the pin fixture's path — backtrace
`setup → _apply_gameplay_rules → _record_structure_armor_provisionals` — and they
are **content-independent**: the fixture's `_harness_rules()` supplies no faction
manifest, so `_structure_armor` falls back to `DEFAULT_STRUCTURE_ARMOR`, which
contains only `fortress`. The four kinds the fixture uses (`barracks`, `farm`,
`archery_range`, `stable`) have no compiled table, so the guard fires four times
in every run — with a pack mounted and with none.

They are **not** a culprit for the hash: the pin's scripted log issues no attack
against a structure, so no structure damage is ever refused; the hash is green
with the floods present.

They **are** the reason the gate step is red. Gate emulation of
`Invoke-ProofChecked` (`tools/proof-gate-common.ps1:82`) at HEAD:

```
exit=0  markerMatch=True  forbiddenMatch=True  gateWouldPass=False
```

with exactly four matches of the forbidden token, all four the structure-armor
`ERROR:` lines (`workspace/logs/q5-gate-emulation.txt`). Fix is one of:
give the frozen fixture compiled armor tables for its four kinds (this would be a
behaviour change and therefore a re-mint), or have the provisional recorder log
at a level the gate does not forbid while keeping the refusal itself loud.
**Do not "fix" this by loosening `$forbiddenDiagnostics`.**

`_add_battalion`'s *"missing selected-pack unit rule"* flood named in the brief
does **not** occur in this runner at all (0 occurrences in every pin log); it
comes from other runners.

## Sibling pin: `retail_scripted_state_pin` — still red, partly unexplained

Gate ratchet `$expectedScriptedStatePinObserved` = `0ae2055f…` (set 2026-08-05,
`c5eb584`). Content-free measurements:

| commit | hash |
|---|---|
| `b79ca1d` (pre-mint) | `0ae2055fc76eac23…` — matches the ratchet |
| `3c67c7c` | `5ec18b4ea81bc4d2…` |
| `ebc60aa` | `5ec18b4e…` |
| `3dbbc9f` | `b0a3b163289930cb…` |
| `e1ba04c` HEAD | `b0a3b163…` (also `b0a3b163…` with content mounted — content-independent) |

The memo explains only the `5ec18b4e ↔ b0a3b163` step. The remaining move
(`0ae2055f → b0a3b163`, i.e. the memo-free effect of `3c67c7c`) is authored
behaviour change inside that mega-commit and is **undiagnosed**. `3c67c7c` cannot
be bisected internally; it needs hunk-level reverts on the scripted fixture. This
is separate work — proposed as a new queue row, not folded into Q5.

## Verdict

**RE-MINT-READY — and, for `retail_state_pin`, NOT NEEDED AT ALL.**

- No `(b)` defect moved the `retail_state_pin` hash. The only cause was `(c)`, and
  it is already fixed in the tree by `3dbbc9f`; the committed constant
  `0e4bcdbf…` is the correct current behaviour.
- The guard that would have prevented it — "a derived/bookkeeping key must not be
  written into hashed rows" — is what `3dbbc9f` implements. If a future lane needs
  a lazy-miss memo, it must live in a non-hashed side table keyed by structure id,
  not in the structure row.
- Q5 as written ("red, un-re-minted owner decision") is **stale**. What remains
  under it is the gate-diagnostic defect and the scripted-pin remainder.

## Exact re-mint procedure (for the projectile lane, if that lane moves the hash)

Only if a future change legitimately moves the hash. One constant, one file:

1. File `game/tests/retail_state_pin_runner.gd`, constant `EXPECTED_HASH`
   (currently at line 177): `const EXPECTED_HASH := "<new 64-hex>"`.
2. Immediately above it, append a new `## ---` block in the same shape as the
   four existing ones, and it must state, in the runner's own required words,
   that this is a conscious mint — the header demands: *"A DIFFERING HASH IS A
   DEFECT, NEVER A NEW BASELINE. Re-minting this value is the owner's decision
   alone and must be stated explicitly as minting a new pin."* The block must
   carry: `Superseded value:`, `New value:`, and a measured WHY that names the
   commit/hunk earning the move and cites the log path — not a claim.
3. The new value must be produced by running the runner **on the commit being
   pushed**, twice, content-free and with the live selection, and both logs cited.
   (That step is what `3c67c7c` skipped and is the reason this lane exists.)
4. Nothing else changes: `PIN_TICKS`, `SUBMIT_THROUGH_TICK`, `_harness_rules()`,
   `_scripted_log()` and the fixture stay frozen — changing any of them is
   minting a *different* pin, and the header requires saying so.
5. The gate needs no edit for the state pin (it asserts the runner's own OK
   line). `retail_scripted_state_pin` is different: its observed value lives in
   `tools/gate-retail.ps1` (`$expectedScriptedStatePinObserved`, line 244), and
   `$scriptedStatePinAuthoredExpectation` (line 245) must keep matching the
   runner's own still-red 2026-07-27 constant — the gate deliberately fails if a
   re-mint arrives silently.

## Evidence paths

All under `C:\Users\Jonathan\Desktop\open-bfme\workspace\logs\`:

- `q5-pin-HEAD-run1.txt`, `q5-pin-HEAD-run2.txt` — HEAD, live selection, green twice
- `q5-pin-HEAD-nocontent.txt` — HEAD, loader failed closed, same hash
- `q5-pin-604fc71-nocontent.txt` — pre-fix commit reproducing `f5579dd9…`
- `q5-sweep-results.txt` — the eight-commit sweep
- `q5-pin-sweep-HEAD-e1ba04c.txt` — HEAD measured inside the worktree (environment cross-check)
- `q5-3dbbc9f-memo-readded-statepin.txt`, `q5-3dbbc9f-memo-readded-scriptedpin.txt` — single-hunk attribution
- `q5-scripted-sweep.txt`, `q5-scripted-pin-HEAD.txt` — sibling pin
- `q5-gate-emulation.txt` — `Invoke-ProofChecked` emulation showing the forbidden-diagnostic failure

Historical red logs (both predate `3dbbc9f`; the second is an *archived* file
whose 08-17 04:43 mtime is its archive time, but its `.private/content-packs`
paths and `sim.gd:2458` frame place the run before the workspace rename):
`workspace/logs/lane-logs/retail_state_pin_runner.txt.err`,
`workspace/logs/root-archive-2026-08/.lane-cloudbreak-state-pin.txt`.

## What I did not verify

- I did not reproduce `f5579dd9…` under the *exact* 08-16 pack selection
  (`rotwk-men-vslice/3be646b0…`); content was shown irrelevant at HEAD and at
  `604fc71`, so the reproduction was done content-free instead.
- I did not diagnose the scripted pin's residual drift (`0ae2055f → b0a3b163`).
- I did not run the full `tools/gate-retail.ps1`; the gate finding is an
  emulation of `Invoke-ProofChecked`'s exact predicate against a real run, not a
  full-gate execution.
- No fix was applied. This lane changed only `orchestration/queue.md` and this
  report.

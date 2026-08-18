# orchestration/ — how work moves through this repo

This directory is the whole coordination surface for humans and agents. If a
piece of work is not visible here, it does not exist.

| Path | What it is |
|---|---|
| `queue.md` | THE live work queue. One row per item, claim-before-start, close-by-evidence. |
| `briefs/` | One file per lane: the contract an implementor executes (goal, oracle facts with anchors, steps, tests-first list, Definition of Done). |
| `reports/` | One file per lane: what the implementor did, verbatim DoD outputs, honest reds. Verifier reports live here too (`<lane>-verify.md`). |
| `TEMPLATE-brief.md`, `TEMPLATE-report.md` | Copy these. A brief without a measurable DoD is not a brief. |

## The lane lifecycle (every lane, no exceptions)

1. **Claim** — write your lane name into the `owner` column of a `queue.md` row
   (or add a row) and commit that first. Unclaimed rows are fair game; claimed
   rows are not. One tree-mutating lane at a time on this machine.
2. **Brief** — `briefs/<lane>.md` from the template. Anchors are `file:line`
   as of a named commit; oracle facts cite retail INI paths; the DoD lists
   commands whose output can be pasted verbatim.
3. **Execute** — read `AGENTS.md` first (ten rules; rule 7: no find-replace
   sweeps). Logs go ONLY under `workspace/logs/`. Stage by explicit path.
   Long jobs run detached and are polled by log mtime; a tool timeout is not
   evidence of anything.
4. **Report** — `reports/<lane>.md` from the template: per DoD item PASS/FAIL
   with the measured output, judged failure-by-NAME against the named
   baseline, plus everything left undone. Honest reds stay red.
5. **Verify** — a fresh-context verifier (opus-medium) re-runs the DoD and
   adversarially reviews the diff; verdict ACCEPT / FIX-FIRST / REJECT in
   `reports/<lane>-verify.md`. Implementor self-reports are unproven until then.
   FIX-FIRST → a fix brief (`briefs/<lane>-fixes.md`), then re-verify.
6. **Close** — the queue row goes CLOSED with the evidence (commit shas, runner
   numbers, digests, dist version if content changed).

## Which implementor for which lane (measured on this repo, 2026-08-17)

- **sol-medium (Codex gpt-5.6-sol)** — bulk implementation from a precise
  brief with a failing-first test list. Never let it sweep/rename mechanically;
  it corrupted identifiers twice.
- **grok 4.6 CLI** — surgical, fully-specified fix lanes and document/ops-heavy
  lanes (pack activation, docs overhaul, releases). Flawless on exact specs.
- **kimi k3 CLI** — long-context triage/ledger work (promoting ledgers,
  purge dockets, evidence verification). `-p` mode only, no approval flags.
- **opus-medium** — verifier/reviewer, diagnosis/bisect lanes. Every lane's
  acceptance gate.

## Pins are oracles, not knobs
Hash pins (`retail_state_pin_runner`, contract `policy_digest`, generated
profile identities, selection pins in `tools/gate-retail.ps1`) are never edited
to make a check pass. Selection pins move only in the same change that moves
the selection, re-measured, with a dated comment. The state pin is re-minted by
the owner alone, explicitly, following the procedure in
`reports/q5-state-pin-drift.md`.

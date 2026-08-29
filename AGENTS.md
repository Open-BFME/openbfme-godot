# Agent contract

OpenBFME has one product target: an exact, clean-room Godot port of **The Lord
of the Rings: The Battle for Middle-earth II - The Rise of the Witch-king,
Patch 2.02 v9.7.7**. Approximation, a nearby patch, BFME2 behavior, and a
plausible-looking substitute are not parity.

This file governs every human and agent contribution. Stop and ask the
integration owner when another document conflicts with it.

## Sources of truth

- [Product scope](contracts/rotwk-202-v9.7.7-product-scope.json) defines what
  the product is and is not.
- [Retail baseline](contracts/rotwk-202-v9.7.7-baseline.json) identifies the
  only accepted original-game baseline.
- [DIRECTION.md](DIRECTION.md) states the product outcome.
- [PLAN.md](PLAN.md) maps the systems needed to reach it.
- [Work items](orchestration/work-items.json) are the only live task ledger.
- [Verification](docs/VERIFICATION.md) defines gates and acceptable evidence.

The deleted `orchestration/queue.md` remains available in Git history. Old
reports, captures, counts, and prior passing logs are also history, not current
authority.

## Ownership

The integration owner alone may change canonical state, including:

- `contracts/**`, `DIRECTION.md`, `PLAN.md`, and
  `orchestration/work-items.json`;
- any active or durable `selection.json`, selected-pack identity, gate pin, or
  published content-pack state; and
- milestone acceptance, release state, and completion declarations.

Workers edit only paths assigned by one work item. They never work directly on
`main`, change canonical state, publish/select packs, or expand their own scope.
When scope is wrong or evidence is missing, return the item to the integration
owner.

## One bounded lane

Each lane has exactly one work-item ID, one short-lived sibling worktree, and
one focused commit. The integration owner creates the branch and worktree
beside the checkout (for example, `..\open-bfme-lanes\<work-id>`), records its
owned paths and acceptance check, and removes it after integration. Do not
create nested worktrees under this repository. Do not start a second item,
perform opportunistic cleanup, or edit files owned by another lane.

Stage explicit paths. Do not use `git add -A`, `git reset`, `git restore`,
`git clean`, `git stash`, or `git commit --amend`. The handoff must be one
reviewable commit containing only the assigned change and its focused tests.

## Retail and private material

Retail bytes and raw derived evidence belong only under the Git-ignored
`workspace/` tree. This includes extracted data, cooked private packs, images,
audio, captures, logs, and oracle output. Never track them, paste them into a
report, copy them into a lane, or encode a machine-absolute path in a tracked
file. Tracked evidence is limited to portable digests, source locators,
measurements, and sanitized receipts. Do not hand-edit content-addressed packs.

Use Windows-native commands and paths: PowerShell, `.ps1`, `.bat`, `.cmd`, and
`py -3`. A tracked instruction or receipt must not depend on Bash, WSL, a Unix
path, or one operator's installed-tool path. Put command output in
`workspace\logs\<work-id>\`, never in the repository root or tracked docs.

## Evidence levels

Every parity claim names the Git revision, retail-baseline identity, conversion
profile, bundle/selection identity, command, and private artifact digest. The
levels are strict and cumulative:

1. **SOURCE** - exact Patch 2.02 v9.7.7 retail/effective source or reproducible
   original-game observation is identified.
2. **CONVERT** - the required source bytes were deterministically converted
   with provenance; this proves conversion only.
3. **LOAD** - the shipping Godot path loaded that exact converted identity with
   no fallback; this proves reachability only.
4. **BEHAVIOR** - deterministic runtime/original-game comparison proves the
   required rules, state transitions, and outcomes.
5. **VISUAL** - approved matched-condition retail/Godot captures prove the
   required rendered result.
6. **AUDIO** - approved matched-condition retail/Godot capture and measurement
   prove the required sound, timing, routing, and mix.

Never promote a lower level into a higher one. SOURCE is not conversion,
CONVERT is not loading, LOAD is not behavior, and behavior does not prove
visual or audio parity. A work item is complete only at every evidence level
and output dimension its acceptance contract requires.

## Verification and integration

Start with a focused test or reproduction that fails for the named gap. Make
the smallest implementation, run the work item's focused Windows-native check,
and hand off the commit plus sanitized evidence. A self-report is provisional.
An independent verifier in a clean context must inspect the source evidence,
review the diff, and rerun the declared check against the same identities.

Only the integration owner may run final gates, update canonical records,
publish or select a pack, merge the commit, and declare the item complete. A
failure, warning, fallback, stale identity, or unverifiable original-game fact
keeps the item open.

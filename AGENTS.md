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
- The tracked `orchestration/feature-lane-plan.json`, when present, is the
  accepted raw-byte plan for generated children; it has no authority before
  the ledger's owner-only materialization transition accepts it.
- [Verification](docs/VERIFICATION.md) defines gates and acceptable evidence.

The deleted `orchestration/queue.md` remains available in Git history. Old
reports, captures, counts, and prior passing logs are also history, not current
authority.

## Ownership

The integration owner alone may change canonical state, including:

- `contracts/**`, `DIRECTION.md`, `PLAN.md`, and
  `orchestration/work-items.json`;
- `orchestration/feature-lane-plan.json`, generated work-item rows, boundary
  owner transitions, typed completion receipts, and accepted plan state;
- `config/repository-boundaries.json`; workers may propose its exact bytes
  privately but never own or commit the canonical boundary map;
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

Only a ledger row whose `allocationClass` is `worker-lane` may become a worker
lane. `closure-envelope` and `rollup` rows are integration-owner aggregation
state: they remain unassigned and are materialized or accepted only through
the canonical planning and verification rules in the ledger.

Every worker check and handoff revalidates its complete assignment-authority
slice against current `main`: target, assignment policy, selected diagnostic
policies, transitive evidence-source rows and tracked source blobs, the complete
work-item row, and any accepted-plan slice. A mutation of that slice revokes the
assignment; an unrelated `main` commit with an identical slice may advance. The
current branch, fetched-main ancestry, index/ref transition checkpoints, index
flags, authority/plan digests, finite protected identities, and exact assigned
targets are rechecked around every step and at the final decision point.

The private repository binding is part of assignment authority. It binds the
main/lane Git pointer topology, stable administrative files and exact absence
sets, directory identities, closed Git environment, empty hook sink, and the
literal stored plus held working identities of both lifecycle files. Normal
work never executes a worker copy of the control plane: the clean canonical-main
launcher receives one explicit bound lane path. No-share handles remain open
through child-process drain, then every bound path, identity, byte set, and
semantic Git checkpoint is revalidated. Drift revokes the assignment.

Focused commands are the ledger's closed, portable, length-bounded
executable-plus-argument vectors. They never contain a free-form shell command,
machine-absolute or expandable argument, encoded command, response file,
`eval`, or `py -3` launcher step. Direct executables and every non-standalone
native runtime tree (including Git), cache-free Python runtime/environment
trees, the canonical pure-module archive or retail import-canary receipt, Git
LFS, tracked/owned targets, and wrapper-spawned auxiliary tools are content-bound
in private receipts. A finite protected-state set and ignored-write monitor reject
undeclared private output. Windows Job containment and change monitoring are
not a kernel filesystem sandbox, so the lane states that limit and revalidates
observable state after every step.

The ledger names one self-hosting exception for `P0-AGENTS-001`: the integration
owner may synthesize its exact twelve-path foundation as one direct-child commit
from the committed pending/unassigned authorization row, derive its owner-side
bootstrap repository binding only after that commit exists, run the provisional
candidate admission gate, and obtain independent review of the exact binding,
revision, and receipt. A revoked experiment may supply inspected bytes but
grants no authority. No other row can use this bootstrap.

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

Use Windows-native commands and paths: PowerShell, `.ps1`, `.bat`, and `.cmd`.
The normal lifecycle control plane enters only through the clean main-worktree
`tools\work-item-lane.ps1`, which verifies the repository binding and directly
launches the held main `tools\work-item-lane.py` with the pinned private CPython
runtime; `py -3`, ambient `python`, and worker lifecycle copies are forbidden
there and in focused steps. The generated private brief provides the exact
machine-local invocation. A tracked
instruction or receipt must not depend on Bash, WSL, a Unix path, or one
operator's installed-tool path. Put command output in
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

Every acceptance row carries all six dimension names in order and marks each
`REQUIRED` or `NOT_REQUIRED`; omission and inference from `L0`-`L6` are
forbidden. A provisional handoff may say `UNPROVED`, but a completion or
independent acceptance may not. SOURCE uses its own typed receipt bound to the
baseline or a sanitized hash-only source locator, verifier command, private
artifact digests, and an independent review. The focused implementation check
cannot substitute for SOURCE evidence.

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

Prose completion claims have no authority. `complete` requires the ledger's
typed completion receipt, exact implementation revision and artifact digests,
fresh focused receipt, typed dimension/source receipts, and a distinct typed
independent-review receipt. Generated feature plans are proposed under ignored
`workspace/` only. The owner materializer rehashes all inputs and atomically
commits exactly the accepted plan, ledger, and repository-boundary files while
leaving the planner in verification; after independent review, a later owner
ledger-only transition records completion.

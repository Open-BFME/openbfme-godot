# OpenBFME agent contract

OpenBFME has one product target: an exact clean-room Godot port of **The Lord
of the Rings: The Battle for Middle-earth II - The Rise of the Witch-king,
Patch 2.02 v9.7.7**. A nearby patch, BFME2 behavior, or a plausible substitute
is not parity.

This file is the short operating contract for every contributor. If another
document conflicts with it, stop and ask the integration owner.

## Read these first

- [DIRECTION.md](DIRECTION.md) — product boundary and non-goals.
- [PLAN.md](PLAN.md) — system map and dependency order.
- [docs/ROADMAP.md](docs/ROADMAP.md) — remaining work and current evidence.
- [orchestration/work-items.json](orchestration/work-items.json) — the only
  live task ledger.
- [docs/VERIFICATION.md](docs/VERIFICATION.md) — evidence levels and gates.
- [docs/ONBOARDING.md](docs/ONBOARDING.md) — the five-minute worker path.

Old reports, captures, branches, worktrees, and passing logs are historical
context. They are never current authority.

## Seven rules

1. **One item, one sibling worktree, one implementation commit.** Work only on
   the assigned item and paths. Never work directly on `main`, create a nested
   worktree, or start a second item.
2. **Canonical state is owner-only.** Workers do not edit `contracts/**`,
   `DIRECTION.md`, `PLAN.md`, `orchestration/**`,
   `config/repository-boundaries.json`, or any selected-pack/`selection.json`
   state unless the work item explicitly says it is an owner task.
3. **The path boundary is literal.** Every changed path must be inside the
   item's `ownership.ownedPaths`. Stage explicit paths; never use `git add -A`.
4. **Retail material stays private.** Original files, extracted data, derived
   packs, captures, audio, images, and raw logs live only under ignored
   `workspace/`. Track only code, portable digests, locators, and sanitized
   receipts. Never hand-edit a content-addressed pack.
5. **Run the ledger command.** A check is the exact structured command stored
   on the work-item row. Do not replace it with a convenient command, a shell
   string, `eval`, an encoded command, an ambient interpreter, or a different
   game baseline.
6. **Worker success is provisional.** A different reviewer must inspect the
   source evidence and diff and rerun the same check. Only the integration
   owner merges and changes canonical status.
7. **Ponytail reviews every normal commit and push.** Install and verify the
   repository hooks before work. The official Grok
   `/ponytail:ponytail-review` must approve the exact staged tree before a
   commit and the exact outgoing commit set before a push. Findings, missing
   tools, malformed output, or Git-state drift fail closed. Never use
   `--no-verify`, override `core.hooksPath`, delete/replace the hooks, or use
   plumbing to evade them. Ponytail reviews complexity only; it never replaces
   the ledger check, correctness/security review, retail evidence, or
   independent acceptance.

These rules are enforced by a small Windows-native work-item tool and Git.
They are a review boundary, not an operating-system sandbox.

## Evidence means exactly one thing

Every acceptance map names all six dimensions and marks each `REQUIRED` or
`NOT_REQUIRED`:

1. **SOURCE** — exact v9.7.7 effective source or reproducible original-game
   observation is identified.
2. **CONVERT** — those source bytes were deterministically converted.
3. **LOAD** — the shipping Godot path loaded that converted identity without
   fallback.
4. **BEHAVIOR** — matched deterministic runs prove rules and state changes.
5. **VISUAL** — matched-condition retail and Godot captures prove rendering.
6. **AUDIO** — matched-condition captures and measurements prove sound,
   timing, routing, and mix.

Evidence is cumulative but never interchangeable. SOURCE does not prove
CONVERT; CONVERT does not prove LOAD; LOAD does not prove BEHAVIOR; behavior
does not prove VISUAL or AUDIO. `UNPROVED` is allowed in a worker handoff, not
in final acceptance for a required dimension.

Every parity claim records the Git revision, baseline ID, conversion profile,
pack/selection identity, command, and private artifact digest. A fallback,
warning, stale identity, skipped check, or unverifiable retail fact keeps the
item open.

## Worker flow

Use Windows PowerShell from the clean main checkout:

```powershell
tools\work-item.ps1 ready
tools\work-item.ps1 create -Id P1-EXAMPLE-001 -Assignee agent-name
```

The owner command assigns the row and creates
`..\open-bfme-lanes\P1-EXAMPLE-001`. In that sibling worktree:

```powershell
tools\work-item.ps1 check
# edit only owned paths, then stage them explicitly and make one commit
tools\work-item.ps1 handoff
```

The generated private brief names the exact scope, paths, check, and evidence
requirements. If any of those are wrong, stop and return the item to the owner.

Do not use `git reset --hard`, `git checkout --`, `git restore`, `git clean`,
`git stash`, or `git commit --amend`. Do not create or execute Bash/WSL helper
scripts for this workflow. The private two-line Git-for-Windows hook launchers
installed by `tools\Install-PonytailHooks.ps1` are the sole shell exception;
all review logic runs in the tracked Python gate with the pinned interpreter.
Do not let `cherry-pick`, `revert`, or `rebase` sequencers create commits;
apply a single change with the porcelain's no-commit mode, then use ordinary
`git commit` so the staged tree is reviewed. Worker lanes never rebase.
Put command output under
`workspace/logs/<work-item-id>/`.

Install after the pinned private Python exists, and verify at the start of
every owner or worker session:

```powershell
powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File tools\Install-PonytailHooks.ps1 -Install
powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File tools\Install-PonytailHooks.ps1 -Verify
```

The client hooks are the strongest normal Git-path enforcement available;
Git's explicit `--no-verify` escape cannot be removed by a client hook, which
is why bypass is also a contribution-policy violation. Shared-branch policy
must still reject unreviewed changes independently.

## Handoff and integration

A valid handoff contains exactly one non-merge implementation commit after the
assignment base, a clean worktree, only owned changed paths, a passing exact
check receipt, and artifact digests. The receipt remains private under
`workspace/logs/<work-item-id>/handoff.json`.

An independent reviewer reruns the same command against the same commit and
writes `independent-review.json`. Review checks the code and evidence; matching
counts alone are not acceptance. The integration owner resolves conflicts,
merges, reruns the focused command on the integrated tree when needed, and
updates the ledger. Prose such as "done" or "looks good" has no authority.

## Ownership and cleanup

`ownership.candidatePaths` is planning information, not permission. Only a
non-empty `ownership.ownedPaths` on an assigned worker-lane row grants worker
write scope. Closure-envelope and rollup rows are owner aggregation tasks and
are never self-assigned by workers.

Historical worktrees are not a backlog. Before removal, classify each one as
clean/dirty and merged/unmerged. Preserve every dirty or unmerged state under
ignored `workspace/archive/` with metadata, a binary diff, and untracked files;
then the owner may remove the exact registered worktree and branch. Never bulk
delete an unresolved path set.

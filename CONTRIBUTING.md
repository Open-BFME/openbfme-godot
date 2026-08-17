# Contributing

Small, tested changes beat large rewrites.

## Pick work

Claim a row in [orchestration/queue.md](orchestration/queue.md) before you
start. Brief in `orchestration/briefs/`, report in `orchestration/reports/`.
Product strategy: [DIRECTION.md](DIRECTION.md). Agent contract and lanes:
[AGENTS.md](AGENTS.md).

## Definition of done

A failing test first, then the fix, then the named baseline delta, then say
which pack/commit the numbers came from. Baselines live in `docs/state/` and
`orchestration/queue.md`. If a lane is red, say so with its output.

## Git

Stage by explicit path. Banned: `git add -A`, `git reset`, `git restore`,
`git clean`, `git stash`, `git commit --amend`. Do not create branches or
worktrees. All logs go to `workspace/logs/`.

Retail-derived files live under `workspace/`; use them freely. Git ignores
`workspace/` and the publication-boundary CI scans tracked files for retail
bytes and machine-absolute paths — that is the whole policy.

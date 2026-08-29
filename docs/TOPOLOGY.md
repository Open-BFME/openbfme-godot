# Repository topology

This document answers one operational question: **what may an agent trust,
change, or clean?** It does not define product scope or parity. Those decisions
come from [AGENTS.md](../AGENTS.md), the
[v9.7.7 product contract](../contracts/rotwk-202-v9.7.7-product-scope.json),
the [retail baseline](../contracts/rotwk-202-v9.7.7-baseline.json), and the
[work ledger](../orchestration/work-items.json).

## Tracked repository

| Path | Role | Agent rule |
|---|---|---|
| `game/` | Shipping Godot runtime and focused runtime tests | Change only through an assigned work item. GDScript reached from `game/project.godot` is shipping authority. |
| `importer/` | Retail-to-private-pack conversion, catalog, profiles, and tests | Preserve source provenance and deterministic identities. A successful conversion is not runtime parity. |
| `launcher/` | Launcher source | Treat as production code, but not proof that the Godot gameplay path works. |
| `engine/` | C# simulation and design experiments | Reference/oracle work only unless the Godot runtime explicitly consumes it. It is not the shipping gameplay authority today. |
| `contracts/` | Product, source, and publication contracts | Integration-owner only. Exact 2.02 v9.7.7 contracts supersede the removed 2.01 product contract. |
| `docs/` | Current instructions plus clearly bannered history | Current authority must link to the roadmap/ledger. Old measurements are history unless regenerated against the exact baseline. |
| `orchestration/` | Agent templates, historical reports, and the one live ledger | `work-items.json` is the only live task/status ledger. Briefs and reports are evidence history, not a queue. |
| `tools/` and root `*.bat` | Windows-native operators and gates | A tool may be stale even when tracked; use only the command named by the claimed work item. |

The 24 tracked `game/data/base/assets/models/**/*.obj.import` files are
deliberate Godot resource-identity records and are explicitly unignored. Other
generated `.obj.import` files remain ignored.

## Private source baseline

The accepted local source root is below
`workspace/retail-work/editions/rotwk/layered-install/` and has exactly this
precedence:

```text
layer-0-patch202/  official Patch 2.02 v9.7.7 English overlay
layer-1-rotwk/     RotWK 2.01 English installation
layer-2-bfme2/     BFME2 1.06 English installation
```

These are user-owned/non-redistributable inputs. Never copy them into Git,
another worktree, a prompt, or a tracked report. Verify them with
`tools/verify-rotwk-202-baseline.py`; do not infer identity from a directory
name or nearby executable.

## Private generated state

Everything below `workspace/` is ignored but **not automatically disposable**.

| Path | Meaning | Retention rule |
|---|---|---|
| `workspace/content-packs/` | Content-addressed cooked bundles and active `selection.json` | Immutable. Preserve selected identities and all unclassified identities. Selection is runtime routing, not parity proof. |
| `workspace/retail-work/reports/` | Private catalogs, censuses, compatibility evidence, and receipts | Preserve until the producing identity and replacement are recorded. Old 2.01 reports remain historical. |
| `workspace/retail-work/oracle/` and `workspace/retail-oracle/` | Original-game observations and recipes | Irreplaceable or expensive evidence; never clean automatically. |
| `workspace/captures/` and `reference/` | Visual/audio evidence and user references | Preserve unless a retention work item proves an exact replacement and recovery path. |
| `workspace/logs/` | Per-item raw output | Keep private; sanitize only the identity/result receipt that belongs in Git. |
| `workspace/orchestration/` | Historical lane output and private evidence | Not current authority. Preserve unclassified data even when its tracked wrapper was removed. |
| `workspace/retail-work/tools/` | Pinned local tool environments | Private reproducibility dependency; do not publish or silently replace. |

The current selected pack stack is a legacy, accumulated 2.01-derived stack.
It contains no accepted 2.02 marker and cannot satisfy a v9.7.7 completion
claim. `P0-SELECTION-001` and `P0-SELECTION-002` govern quarantine and
replacement.

## Worktrees and cleanup boundary

The 2026-08-29 audit found 83 registered worktrees. Forty-five were
branch-merged and tracked-clean but still held ignored payloads; 38 were dirty,
including two with unmerged commits. Therefore:

- never bulk-remove worktrees;
- never treat `git status` cleanliness as proof that a tree is disposable;
- inventory tracked, untracked, ignored, hardlink, branch, and unique-commit
  state before assigning a disposition; and
- execute deletion only through `P0-REPO-001` and `P0-RETENTION-001` after an
  integration-owner decision.

The same rule applies to the large private pack store and capture/report
trees. Size is not provenance.

## What has been retired

The obsolete `orchestration/queue.md`, the tracked `docs/state/` snapshots, the
stale 74-row gap map, six tracked-but-ignored orchestration reports, the frozen
orphan-runner CSV, and the superseded 2.01 product contract were removed from
the live authority surface. They remain recoverable from Git history. No
private retail payload, content pack, capture tree, or worktree was deleted.

Any future topology measurement belongs in a dated private report and a
bounded ledger evidence row. Do not paste changing multi-gigabyte counts back
into this document as timeless truth.

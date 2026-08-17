# Stage 1 fixes, round 2 — report

Lane executed `orchestration/briefs/stage1-fixes-r2.md` exactly. Four items,
no sweeps, no edits outside the named lines. Files staged by path. Commit
`a1059de`.

## Edits performed

1. `contracts/rotwk-201-product-scope.json` — `policy_digest.value` replaced
   `fb82e9d2474d0b28280888fc10e1687cdc8bc2946d7b350c3bb25d10a747ae52` →
   `2a4f3fa12aecadbe64ca73064d696e0d67fe0c33ab3499decdc663cd7d8108c5`.
   Nothing else in the file changed.
2. `tools/seal_published_packs.py` — path literal at line 41
   `REPO_ROOT / ".private" / "content-packs"` →
   `REPO_ROOT / "workspace" / "content-packs"`. Identifier
   `default_workspace_root` left untouched. Whole-file scan: no other
   `".private"` path literals.
3. `tools/test-private-workspace.ps1` — lines 93 and 95:
   - check-ignore probe `.private/retail-work/private-workspace-test.probe`
     → `workspace/retail-work/private-workspace-test.probe`
   - throw message dropped `.private` (per verifier: do not double the word)
     → `The repository workspace is not ignored by git.`
4. Deleted repo-root debris file whose name is the collapsed redirect
   `C<U+F03A>UsersJonathanDesktopopen-bfmeworkspacelogsstage1-fixes-final-report.txt`.
   It was untracked. Post-delete scan found no remaining match.

## Definition of Done

### 1. Product-contract digest — PASS

Command:

```
workspace\retail-work\tools\python-3.12-env\Scripts\python.exe tools\check-product-contracts.py
```

Verbatim output:

```
PRODUCT_CONTRACTS PASS product_policy_sha256=2a4f3fa12aecadbe64ca73064d696e0d67fe0c33ab3499decdc663cd7d8108c5 modding_policy_sha256=7ef975b4b7facc1df2c4f3225236c19c76b0cfea88c4d41b8bd4ea378cef8cb5
```

`DOD1_EXIT=0`. Stored digest matches the verifier-computed value. No digest mismatch.

### 2. `test_product_contracts.py` — PASS

Command (pinned interpreter, `BFME2_INSTALL` set to
`<repo>\workspace\retail-work\editions\rotwk\layered-install\layer-1-bfme2`):

```
workspace\retail-work\tools\python-3.12-env\Scripts\python.exe -m pytest importer/tests/test_product_contracts.py
```

Verbatim output:

```
============================= test session starts =============================
platform win32 -- Python 3.12.13, pytest-9.1.1, pluggy-1.6.0
rootdir: C:\Users\Jonathan\Desktop\open-bfme
collected 2 items

importer\tests\test_product_contracts.py ..                              [100%]

============================== 2 passed in 0.18s ==============================
```

`DOD2_EXIT=0`. 2 passed, 0 failed.

### 3. `test-private-workspace.ps1` — PASS

Command:

```
powershell -ExecutionPolicy Bypass -File tools\test-private-workspace.ps1
```

Verbatim output:

```
PRIVATE_WORKSPACE_TEST PASS
```

`DOD3_EXIT=0`.

### 4. `py_compile` + no `".private"` path literals — PASS

Command:

```
workspace\retail-work\tools\python-3.12-env\Scripts\python.exe -m py_compile tools\seal_published_packs.py
git grep -n -F '".private"' -- tools/seal_published_packs.py
```

Verbatim output:

```
PY_COMPILE_EXIT=0
GIT_GREP_EXIT=1
```

`py_compile` produced no output and exited 0. `git grep -n -F '".private"'`
printed no hits and exited 1 (no match). Reconfirmed after the commit:
`GIT_GREP_RECONFIRM_EXIT=1`, no printed lines.

### 5. One commit, three files, debris gone, tree otherwise clean — PASS

Command sequence (explicit paths only; no `git add -A` / reset / restore /
clean / stash / amend):

```
git add -- contracts/rotwk-201-product-scope.json tools/seal_published_packs.py tools/test-private-workspace.ps1
git commit -m "fix(layout): reseal rotwk-201 digest, finish path fixes"
```

Verbatim commit output:

```
[main a1059de] fix(layout): reseal rotwk-201 digest, finish path fixes
 3 files changed, 4 insertions(+), 4 deletions(-)
COMMIT_EXIT=0
```

`git show --stat --format=fuller HEAD`:

```
commit a1059de1692f953a4d0778ad9dcc3a85cee5815b
Author:     Ancalgonn <1760627+Ancalgonn@users.noreply.github.com>
AuthorDate: Mon Aug 17 13:52:32 2026 -0400
Commit:     Ancalgonn <1760627+Ancalgonn@users.noreply.github.com>
CommitDate: Mon Aug 17 13:52:32 2026 -0400

    fix(layout): reseal rotwk-201 digest, finish path fixes

 contracts/rotwk-201-product-scope.json | 2 +-
 tools/seal_published_packs.py          | 2 +-
 tools/test-private-workspace.ps1       | 4 ++--
 3 files changed, 4 insertions(+), 4 deletions(-)
```

`git show --name-only HEAD` lists exactly those three files.

Post-commit: `git diff --name-only` and `git diff --cached --name-only` are
empty (no remaining tracked modifications). Debris-file scan after delete
printed no matches (`DEBRIS_SCAN_DONE` with no preceding names). Remaining
untracked files (`.codex-brief.md`, `.codex-log.txt`, `FAILURE_TRIAGE_TABLE.md`,
`FINAL_STATUS.txt`, `PHANTOMS.md`, `gate-wiring-final-brief.md`, the
orchestration briefs, `orchestration/reports/stage1-verify.md`) were already
present before this lane and were not staged.

## Verdict

All five DoD items **PASS**.

# Stage 1 fixes, round 3 — report

Lane executed `orchestration/briefs/stage1-fixes-r3.md` exactly. Four items,
no sweeps, no edits outside the named lines and the three `virtualPath`
strings. Files staged by path. Commit `db3491c`.

## Edits performed

1. `importer/tests/test_livingmap_regions.py` — definition at `:405` is
   `workspace_root`. Call sites at `:434` and `:472` were still
   `_private_workspace()`; both now call `workspace_root()`. No other
   `_private_workspace` remains in the file.
2. `importer/tests/test_living_world_autoresolve_bindings.py` — definition at
   `:44` is `workspace_root`. Call site at `:669` was still
   `_private_workspace()`; now `workspace_root()`. No other
   `_private_workspace` remains in the file.
3. `workspace/retail-work/profiles/men-fords-v0-complete.generated.json` —
   reverted exactly three `virtualPath` strings
   `workspace/retail-work` → `.private/retail-work`. Pin file
   `importer/profiles/men-fords-v1.json` was not edited. File is untracked
   (gitignored under `workspace/`) and was not staged. Measured sha256 after
   the revert:
   `0bc2e76708d3c13b0aeac45afe375e4f120acdf329344b79d683f42e5d667c9d`
   (`MATCH` against the verifier-proved pin).
4. `git rm -- gate-wiring-final.log workspace-rename-baseline-status.txt` —
   untracked and deleted from disk. Both were designated stray artifacts.

## Definition of Done

### 1. Targeted pytest on the three affected modules — PASS

Verifier round-3 named `test_livingmap_regions`,
`test_living_world_autoresolve_bindings`, and `test_m3_pack_expansion` (the
profile-identity failure is `test_m3_pack_expansion.py::test_effective_trebuchet_contract_and_object_bindings_are_source_complete`,
not `test_men_fords_profile.py`).

Command (pinned interpreter, `BFME2_INSTALL` set to
`<repo>\workspace\retail-work\editions\rotwk\layered-install\layer-1-bfme2`):

```
workspace\retail-work\tools\python-3.12-env\Scripts\python.exe -m pytest importer/tests/test_livingmap_regions.py importer/tests/test_living_world_autoresolve_bindings.py importer/tests/test_m3_pack_expansion.py --tb=short
```

Verbatim output:

```
============================= test session starts =============================
platform win32 -- Python 3.12.13, pytest-9.1.1, pluggy-1.6.0
rootdir: C:\Users\Jonathan\Desktop\open-bfme
collected 67 items

importer\tests\test_livingmap_regions.py ......................          [ 32%]
importer\tests\test_living_world_autoresolve_bindings.py ............... [ 55%]
.............                                                            [ 74%]
importer\tests\test_m3_pack_expansion.py .................               [100%]

============================= 67 passed in 9.87s ==============================
```

`DOD1_EXIT=0`. 67 passed, 0 failed, 0 errors. The `_private_workspace`
`NameError` failures/errors and the `private base profile identity changed`
failure are gone.

### 2. Stray artifacts untracked — PASS

Command:

```
git ls-files | grep -iE "gate-wiring-final|baseline-status"
```

Verbatim output:

```
GREP_EXIT=1
```

No printed hits. `gate-wiring-final.log` and
`workspace-rename-baseline-status.txt` are absent from the index and from
disk (`LOG_DELETED`, `STATUS_DELETED`).

### 3. Working tree clean after the commit — PASS

`git diff --name-only` and `git diff --cached --name-only` are empty (no
remaining tracked modifications). `git status --porcelain` shows only
pre-existing untracked files that were already present before this lane
(orchestration briefs/reports from prior Stage-1 rounds, and prior-lane
debris: `.codex-brief.md`, `.codex-log.txt`, `.grok-stage1-r2.log`,
`.grok-stage1-r3.log`, `FAILURE_TRIAGE_TABLE.md`, `FINAL_STATUS.txt`,
`PHANTOMS.md`, `gate-wiring-final-brief.md`). None were staged. Ignored
`workspace/` paths (including the restored generated profile) do not appear.

### 4. One commit, exact file set — PASS

Command sequence (explicit paths only; no `git add -A` / reset / restore /
clean / stash / amend):

```
git add -- importer/tests/test_livingmap_regions.py importer/tests/test_living_world_autoresolve_bindings.py
git rm -- gate-wiring-final.log workspace-rename-baseline-status.txt
git commit -m "fix(layout): repair stale helper call sites, restore pinned profile identity"
```

Verbatim commit output:

```
[main db3491c] fix(layout): repair stale helper call sites, restore pinned profile identity
 4 files changed, 3 insertions(+), 3 deletions(-)
 delete mode 100644 gate-wiring-final.log
 delete mode 100644 workspace-rename-baseline-status.txt
COMMIT_EXIT=0
```

`git show --stat --format=fuller HEAD`:

```
commit db3491c53f24521ecc377108169de1925b7e2fb5
Author:     Ancalgonn <1760627+Ancalgonn@users.noreply.github.com>
AuthorDate: Mon Aug 17 14:40:59 2026 -0400
Commit:     Ancalgonn <1760627+Ancalgonn@users.noreply.github.com>
CommitDate: Mon Aug 17 14:40:59 2026 -0400

    fix(layout): repair stale helper call sites, restore pinned profile identity

 gate-wiring-final.log                                | Bin 10104990 -> 0 bytes
 .../tests/test_living_world_autoresolve_bindings.py  |   2 +-
 importer/tests/test_livingmap_regions.py             |   4 ++--
 workspace-rename-baseline-status.txt                 | Bin 10988 -> 0 bytes
 4 files changed, 3 insertions(+), 3 deletions(-)
```

`git show --name-only --pretty=format: HEAD` lists exactly:

```
gate-wiring-final.log
importer/tests/test_living_world_autoresolve_bindings.py
importer/tests/test_livingmap_regions.py
workspace-rename-baseline-status.txt
```

The generated profile is not in the commit. This report is untracked and
was written after the commit.

## Verdict

All four DoD items **PASS**.

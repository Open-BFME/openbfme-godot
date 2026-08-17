# Stage 1 fixes, round 2 — three surgical edits + one deletion

Context: `orchestration/reports/stage1-verify.md` (round-2 section) has full
evidence. This lane is FOUR items, exactly as specified. No sweeps, no wholesale
file restores, no edits outside the named lines. Stage explicitly by path;
`git add -A`, `git reset/restore/clean/stash/amend` are BANNED.

## Edits

1. `contracts/rotwk-201-product-scope.json` — the `policy_digest` `value` for the
   product envelope is wrong. Replace the stored value
   `fb82e9d2474d0b28280888fc10e1687cdc8bc2946d7b350c3bb25d10a747ae52`
   with the verifier-computed correct value
   `2a4f3fa12aecadbe64ca73064d696e0d67fe0c33ab3499decdc663cd7d8108c5`.
   Change nothing else in the file.

2. `tools/seal_published_packs.py` — the file was wrongly restored byte-identical
   to pre-rename commit 7b503ea. Keep its `workspace_root` identifiers as they
   are (they are correct), but update the PATH literals that reference the old
   directory: line ~41 `REPO_ROOT / ".private" / "content-packs"` must become
   `REPO_ROOT / "workspace" / "content-packs"`. Check the whole file for any
   other `".private"` path literal and update those too (path literals ONLY —
   identifiers stay untouched).

3. `tools/test-private-workspace.ps1` — lines ~93 and ~95 reverted to
   `.private` while the rest of the file uses `workspace`. Update those two
   lines to `workspace` paths so the script is internally consistent.

4. Delete the repo-root debris file literally named
   `C:UsersJonathanDesktopopen-bfmeworkspacelogsstage1-fixes-final-report.txt`
   (a collapsed-redirect artifact). It is untracked; just remove it from disk.

## Definition of Done (all outputs pasted in your final message)

1. `workspace\retail-work\tools\python-3.12-env\Scripts\python.exe tools\check-product-contracts.py`
   → no digest mismatch (exit 0).
2. Same interpreter, `python -m pytest importer/tests/test_product_contracts.py`
   with `BFME2_INSTALL=<repo>\workspace\retail-work\editions\rotwk\layered-install\layer-1-bfme2`
   → all pass.
3. `powershell -ExecutionPolicy Bypass -File tools\test-private-workspace.ps1`
   → exit 0.
4. `py_compile` on `tools/seal_published_packs.py` → clean, and
   `git grep -n -F '".private"' -- tools/seal_published_packs.py` → no hits.
5. One commit, message `fix(layout): reseal rotwk-201 digest, finish path fixes`,
   containing exactly the three edited files. Debris file gone, tree otherwise
   clean.

Write `orchestration/reports/stage1-fixes-r2.md` with each DoD item PASS/FAIL
and verbatim output.

# Retail Blender and OpenSAGE tool attestation

OpenBFME treats the pinned Blender 4.2.0 portable directory as executable input.
Every file in that directory is covered by the exact full-tree SHA-256 pin; Python
bytecode is not ignored or accepted as equivalent to source.

Blender can create `__pycache__` directories and `.pyc`/`.pyo` files inside its
portable directory. Those generated files used to make the otherwise unchanged
tree alternate between valid and invalid after a direct Blender run. The importer
now has one bounded recovery operation for that specific condition:

- Cleanup is allowed only when the selected executable resolves to
  `<state-root>/tools/blender-4.2.0-windows-x64/blender.exe`.
- `OPENBFME_BLENDER` overrides and all other directories are read-only. They are
  attested, but never cleaned.
- The cleanup removes only standalone `.pyc`/`.pyo` files and complete directories
  named `__pycache__`. It has fixed file, directory, and byte ceilings.
- The entire tree is scanned and proved free of links, junctions, and Windows
  reparse points before any deletion. Every candidate is rechecked without link
  following immediately before removal, and containment below the resolved pinned
  root is mandatory.
- Any unsupported filesystem entry, traversal failure, containment failure, link,
  junction, reparse point, or exceeded cleanup bound fails closed before execution.

Cleanup runs when bootstrap reuses an installed pinned tree, immediately before
W3D-required build preflight asks for tool status, and immediately before every
W3D Blender execution. `tool_status()` itself is observational and never mutates
the tool tree.

After cleanup, both the Blender executable hash and exact portable-tree hash must
match their pins before Blender can run. Source edits, executable edits, extra
files, or unapproved bytecode therefore still fail attestation. After conversion,
the importer performs rejection-only link and bytecode checks and recomputes the
exact full-tree hash. It does not clean post-execution evidence.

The exact state-root-owned `OpenSAGE.BlenderPlugin` checkout uses the same bounded
cache cleanup at bootstrap reuse, W3D preflight, and immediately before Blender
execution. Plugin overrides are never cleaned, and `tool_status()` remains
observational. The plugin authority remains its exact main and updater-submodule
commits plus a clean Git worktree; `.git` is not assigned a new directory hash.
After execution, link, bytecode, commit, submodule, and worktree checks are
rejection-only.

Focused verification:

```powershell
$env:PYTHONPATH = "importer"
python -m unittest discover -s importer/tests -p "test_blender_tool_cache.py" -v
```

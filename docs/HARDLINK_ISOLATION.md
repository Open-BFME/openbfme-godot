# Hardlink isolation

No Git-tracked file may share an NTFS file identity with any path in disposable evidence. This is a byte-integrity boundary, not a naming convention.

On this volume, both hardlink failure modes were measured: an in-place write propagates changed bytes into the evidence, while a temp-file-plus-rename write silently detaches the live path and freezes the evidence copy. Either behavior makes evidence unreliable.

**Forward rule: copy, do not link.** Every future evidence capture must create real copies. WotR packets 0007 and later are the precedent: they were captured as copies.

Run the Windows-authoritative gate from PowerShell at the repository root:

```powershell
python tools/gate-hardlink-isolation.py
python tools/gate-hardlink-isolation.py --json
```

Use `--evidence-root PATH` to select an evidence tree; repeat the option to scan additional roots. Run the isolated temporary-fixture checks with `python tools/gate-hardlink-isolation.py --self-test`.

Exit codes:

- `0`: enumeration genuinely completed, no identity intersection exists, and no tracked path is a reparse point.
- `1`: at least one tracked/evidence identity intersection or tracked reparse point was found.
- `2`: the gate could not perform an authoritative check (for example missing evidence, Git or `fsutil`; non-NTFS storage; or an enumeration error). A skip is never a pass.

No tracked hardlink-producing tool was found. Outside this gate's deliberate TEMP-only self-test fixture, `os.link` appears only in importer test fixtures, and the production importer actively refuses hardlinks. This gate prevents recurrence without claiming that a producer was fixed.

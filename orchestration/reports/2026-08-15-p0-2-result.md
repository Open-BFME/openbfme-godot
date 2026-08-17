# P0-2 result

## Summary

- Added a read-only structured validator for `selection.json`, immutable active-pack keys, matching `pack.json` identity, and the complete named supplement set.
- Gated both Play buttons on a valid engine install and valid selection, with the same player-facing reason in status/checklist UI.
- Revalidated the resolved selection at the process-start boundary before setting `OPENBFME_CONTENT` live.
- Made explicit runtime content fail closed: missing/corrupt selections, missing supplements, and missing explicit roots mount no ambient, sibling, workspace, or durable packs.
- Extended the focused launcher and Godot boundary tests.

## Files changed

- `docs/LAUNCHER_UX_AND_RELEASE_READINESS.md`
- `game/src/content/mod_loader.gd`
- `game/tests/external_pack_runner.gd`
- `launcher/OpenBFME.Launcher.Tests/Program.cs`
- `launcher/OpenBFME.Launcher/ContentPackCatalog.cs`
- `launcher/OpenBFME.Launcher/LauncherService.cs`
- `launcher/OpenBFME.Launcher/MainWindow.xaml.cs`

## Proof

Launcher build:

```text
OpenBFME.Launcher -> ...\bin\Debug\net10.0-windows\OpenBFME.Launcher.dll
Build succeeded.
0 Warning(s)
0 Error(s)
```

Launcher tests:

```text
PASS content pack catalog
PASS content selection validation
LAUNCHER_TESTS_PASS
```

The sandbox denies .NET's read of the user-local Windows SDK probe directory, so the successful build/test invocation supplied the installed SDK root explicitly and used `--no-restore`. The requested unmodified command was also attempted and stopped at that sandbox denial before compilation.

Focused Godot boundary runner:

```text
EXTERNAL_PACK PASS external_missing_selection_mounts_nothing
EXTERNAL_PACK PASS external_missing_selection_refuses_sibling
EXTERNAL_PACK PASS external_corrupt_selection_mounts_nothing
EXTERNAL_PACK PASS external_missing_supplement_mounts_nothing
EXTERNAL_PACK PASS external_missing_supplement_refuses_active
EXTERNAL_PACK_RESULT passed=72 failed=0
```

## Residual risks

- The launcher UI was not visually exercised; build and logic tests cover the gating paths.
- The validation-to-`Process.Start` race is minimized by immediate revalidation but cannot make several filesystem reads atomic against a hostile concurrent writer.
- The requested local commit could not be created because this session cannot create `.git/worktrees/launcher-p0-selection/index.lock`; the shared parent Git metadata is mounted read-only. Nothing was pushed.

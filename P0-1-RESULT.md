# P0-1 result

## Summary

- Added a persistent release-channel preference under the install root.
- Effective precedence is explicit CLI `--channel`, saved preference, embedded default.
- Added Stable, Playtest, and Nightly selection to Settings with pre-release warnings.
- Channel changes update the footer and re-resolve candidate presentation.
- Feed discovery filters zero-score wrong-channel releases.
- Missing candidates are presented honestly instead of as ready to install.

## Files changed

- `config/release-source.json`
- `docs/LAUNCHER_UX_AND_RELEASE_READINESS.md`
- `launcher/OpenBFME.Launcher.Tests/Program.cs`
- `launcher/OpenBFME.Launcher/GitHubReleaseFeed.cs`
- `launcher/OpenBFME.Launcher/LauncherOptions.cs`
- `launcher/OpenBFME.Launcher/LauncherPreferences.cs`
- `launcher/OpenBFME.Launcher/LauncherService.cs`
- `launcher/OpenBFME.Launcher/MainWindow.xaml`
- `launcher/OpenBFME.Launcher/MainWindow.xaml.cs`

## Test output excerpt

```text
PASS options
PASS channel preferences
PASS release source
PASS release channel filtering
...
PASS content pack catalog
LAUNCHER_TESTS_PASS
```

Release build:

```text
OpenBFME.Launcher -> ...\\bin\\Release\\net10.0-windows\\OpenBFME.Launcher.dll
Build succeeded.
1 Warning(s)
0 Error(s)
```

The warning was `NU1900` because the sandbox could not reach NuGet vulnerability metadata; compilation and tests succeeded.

## Residual risks

- No live GitHub release/feed or visual double-click smoke test was performed; feed tests are hermetic.
- Local commit could not be created because the worktree Git metadata is mounted read-only in this session.
- The requested sibling scratch path was outside the writable sandbox; this fallback report is at the worktree root.

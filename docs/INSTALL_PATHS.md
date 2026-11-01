# User-selected disk paths

Engine boundary strings are UTF-8. `PathIO::native` converts complete disk paths to native filesystem paths. On Windows it uses strict UTF-8/UTF-16 conversion and extended absolute paths at the Win32 length boundary; relative archive members remain relative UTF-8 components. Environment and registry values use dynamically sized wide-character APIs, not the ANSI code page or a fixed buffer.

## Flow inventory

| Source | Consumers and disk boundary |
|---|---|
| Launcher folder picker | `games_screen.gd::set_folder` keeps the chosen string; `aio_install.gd` uses Godot `DirAccess`/`FileAccess` and writes downloaded-install markers. `paths.gd` reads marker/config values without trimming them. |
| Launcher version/user-data directory | `updater.gd` joins `versions/<version>/game/<executable>` with Godot paths; state/locks use `ConfigFile`/`FileAccess`. Both versioned and bundled launches use `OS.create_process(executable, arguments)`: no shell and no added quoting. |
| First-run entries and folder dialogs | `first_run.gd` passes the same raw text to check, mount and remember. `GodotInstallSetup` and `GodotRetailFileSystem` use explicit `String::utf8()` conversions. `InstallLocator::remember` writes UTF-8 `install-paths.cfg` through native paths; `ResolveInstallPath` trims legacy horizontal padding after `=` and the CR line terminator, keeping internal/trailing spaces. Environment overrides are not trimmed. |
| Discovery / overrides | `InstallLocator::host` reads wide Windows environment values, wide drive types and dynamically sized `RegQueryValueExW`/`ExpandEnvironmentStringsW` results (both registry views). Unix/Wine discovery uses UTF-8 filesystem paths and preserves symlink choices. |
| Engine executable arguments | `PathIO::runMain` ignores Windows CRT ANSI argv, parses `GetCommandLineW()` with `CommandLineToArgvW`, and converts each argument strictly to UTF-8 once before entering the existing parser. It preserves argument storage and the null sentinel through the callback. The path-taking targets are `openbfme_peer`, `openbfme_tests` (including native `--out` streams), `audio_decode_cli`, `openbfme_process_probe` and MSVC-only `openbfme_crash_probe`. `openbfme_lua_probe` accepts only a numeric seed shift; Godot shader `main` functions are not executable entry points. Unix argv passes through unchanged. |
| Mount / BIG disk reads | Install validation and `MountRetailArchives` validate the selected spelling; MD5/cache, `Win32BIGFileSystem`, `ArchiveHandle` and the mandatory `LooseFileScan` use native paths. The loose scan emits UTF-8 relative filenames without filesystem canonicalization (which failed for long roots under Wine). `_wfsopen` remains the protected Windows archive opener. Archive members are unchanged. |
| Session logs / crash artifacts | Godot globalizes `user://session.log`; the C++ logger opens its native path with `_wfopen` on Windows. `LogPrivacy` gets Unicode home/user values. Launcher log scrubbing shares `Paths.scrub` (including username components outside the home path and Windows case variants). Crash dumps/desync copies use native paths. |
| Saves / replays / options | Godot joins user-data/save/replay directories without shell quoting. `SaveGameFile` uses native streams and atomic temporary-save replacement. Replay and relay-log disk streams, hero lists/statistics, skirmish preferences and the continue-save probe use native paths; recorder formats, byte generation and playback/simulation logic are unchanged. |
| Other install consumers | Cursor/movie/campaign enumeration serializes paths as UTF-8; VP6 and `gi.dat` disk readers, the Godot movie reopening, audio event logs, hero import and peer diagnostics use native paths. Separator normalization precedes extended-path handling, including already-prefixed drive/UNC inputs. No decoding, simulation, replay-format or save-format changes. |

Windows components ending in a space or dot are explicitly refused with the supplied folder in the error: Win32/Godot must not normalize a selection into another folder. Unix keeps those characters. NUL/CR/LF and empty install roots cannot round-trip through the line-oriented config and are refused. Missing/unwritable locations report the offending path, not a different default install.

## Regression commands

```sh
cmake --build engine/build --target openbfme_tests
engine/build/openbfme_tests --test-case='install paths:*'
python3 tools/release/test_install_paths.py --godot /path/to/godot
python3 tools/release/test_install_paths.py --godot /path/to/godot --first-run-ref c4ee740e5b8cf1f060aec0fad9ad4c9c5bb1ae64
bash tools/release/build_windows.sh --no-export
```

All payloads are generated synthetic data. No game install is needed. The headless first-run harness substitutes its native services; the native tests separately exercise real install checking, config persistence, MD5, disk BIG mount/read, log output/privacy and save/replay streams. Launcher tests substitute signed-package verification, not process dispatch: synthetic executables report their actual argv for both versioned and bundled launch paths. They also exercise the real folder selector's storage callback. These are headless callback tests, not interactive UI tests. The existing signed fresh-install-and-play regression is checked separately, with its temporary root under a synthetic Unicode/OneDrive/app-bundle-style path longer than 260 characters.

## Round 2 verification

Based on `664f111fb28c900c2bb989bf8930f7457e548a78`. The three reviewer reports are covered by the same cross-platform path suite: synthetic mount followed by the mandatory loose scan; nonempty Unicode loose filenames; legacy config padding; Windows extended drive/UNC separator normalization through streams and stdio; and the production movie lookup/reopen below an extended Unicode root. Profile helper tests cover statistics replacement/load, hero list save/load/remove, skirmish preference persistence and the continue-save probe. The Windows-only cases also exercise strict conversion failures and long paths. All files are generated, never retail bytes.

- Linux focused path suite: **10 cases / 681 assertions**.
- Linux selected regression suite: **44 cases / 1,103 assertions**, including the focused suite.
- Windows executable under Wine 6.0.3, ACP **1252**: focused **15 cases / 628 assertions**; selected suite **47 cases / 1,029 assertions**, including the focused suite.
- Headless Linux first-run: **66 assertions**; launcher: **127 assertions**; eleven synthetic folder cases each.
- llvm-mingw: core, peer, tests and GDExtension build passed (`--no-export`).
- Simulation compiler/link/classification/AST audit passed; its Python regressions: **101 passed / 1 skipped**.

The selected suite uses source filters for `test_install_paths.cpp`, `test_big_archive.cpp`, `test_archive_order.cpp`, `test_release1_install.cpp`, `test_win32path.cpp`, `test_opts2_options.cpp` and `test_cah_stats.cpp`. Retail-dependent cases and the Microsoft shlwapi runtime oracle are excluded; Wine does not provide that oracle. Counts above are not a full-suite run. No native Windows or macOS runtime, real retail mount or running OneDrive service was tested.

The remaining path-boundary audit hits are intentional native `filesystem::path` variables (iterator/temporary paths and wide Win32 handles), relative UTF-8 archive members/filename components, the platform implementations inside `PathIO`, and disabled Lua patch text. No complete UTF-8 disk-root string is passed to a narrow Windows filesystem constructor or stream opener in `engine/src`.

Round 1 historical evidence: a consumer-only baseline probe rebuilt eleven disk/config consumer translation units from the original base against the then-current harness: 320/328 assertions passed; eight config assertions failed. This was not a full base build. Together with the first-run comparison, twelve baseline assertion failures were reproduced. Round 2 intentionally restores leading config padding trimming for compatibility.

The first-run base-revision command is expected to fail: it reproduces four assertions for a selected Linux folder ending in a space. Both headless harnesses include an install symlink; their summaries count assertions, not test processes. A cross-build is not Windows runtime validation; the Wine runtime results above are separate from native Windows testing.

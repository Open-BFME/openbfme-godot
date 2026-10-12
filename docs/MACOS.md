# macOS build (lane MAC-1)

The game and the launcher run natively on Apple Silicon (arm64, Metal). Intel Macs are not supported: the engine is built for
arm64 only, and the application bundle carries an arm64-only Godot (`tools/release/macos_template.py` thins Godot's universal
template). Stops S-3710, S-3711 and S-3712 in `docs/STOPS.md` record what is assumed or not yet measured.

| | macOS, Apple Silicon |
|---|---|
| Host | a Mac with Apple Silicon, macOS 11 or newer (built and tested on an M4, macOS 26); Xcode command line tools; `brew install cmake ninja lame mpg123`; Godot 4.7.2 in `/Applications` with its export templates installed |
| Build | `cmake -S engine -B engine/build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build engine/build --target openbfme openbfme_tests` |
| Output | `godot/bin/openbfme.macos.template_debug.arm64.dylib` (the GDExtension, listed by `godot/openbfme.gdextension` for `macos.debug.arm64` and `macos.release.arm64`), `engine/build/openbfme_tests` |
| Package | `tools/release/package.sh --platforms macos --out <dir>` (on a Mac): `openbfme-<version>-macos-arm64.zip` and `openbfme-launcher-<version>-macos-arm64.zip` |

## Building and testing

```sh
GODOT=/Applications/Godot.app/Contents/MacOS/Godot
cmake -S engine -B engine/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build engine/build --target openbfme openbfme_tests
export ROTWK_INSTALL=/path/to/RotWK BFME2_INSTALL=/path/to/BFME2 RW_GAME_DAT=/path/to/RotWK/game.dat
engine/build/openbfme_tests                                   # the doctest suite (the retail tests skip without the folders)
python3 tools/sim/sim_audit.py --build engine/build --ast=skip  # the flags, link and classification checks (the AST check: S-230)
$GODOT --headless --path godot --import                       # once: registers the extension (.godot/extension_list.cfg)
$GODOT --headless --path godot --script res://tests/smoke_test.gd
$GODOT --path godot                                           # the game (Metal)
```

The game folders can also live in `user://install-paths.cfg` (the first-run screen writes it); the first start looks for them in
CrossOver bottles (`~/Library/Application Support/CrossOver/Bottles/<name>`, a Wine prefix with its `system.reg`) and PlayOnMac
prefixes, besides every location the Linux build searches (`Common/InstallLocator.h`).

The MP3 test fixtures are generated at build time by `tools/audio/gen_mp3_fixtures.py` with the installed `lame` and `mpg123`. Each
lame release encodes the synthetic signals to other bytes, so the committed golden numbers are per release:
`engine/tests/data/audio/mp3_golden-lame-3.100.tsv` (the Linux and Windows builders) and `mp3_golden-lame-4.0.tsv` (Homebrew's
lame on macOS). A lame without a golden file stops the build and names the command that writes one after review.

## What was made to match x86

- **The Lua NaN.** A NaN that an operation itself produces is x86's "indefinite" (the quiet NaN with the sign bit set, what the
  scripts' `tostring` prints as `-1.#IND`) on arm64 too (`engine/src/Libraries/Lua/leanumeric.cpp`); arm64 hardware makes the
  positive default NaN instead. Before that, the AI-vs-AI engine pins of `test_skirmish_ai_vs_ai.cpp` ended every game early on
  the Mac (the Lua scripts' numbers differed); with it they agree with x86's pins.
- **Paths inside Wine prefixes.** `childCaseInsensitive` returns the folder's own spelling of a registry path on a case-insensitive
  file system too (APFS says every spelling exists), and `~` stands for a home whose canonical path differs (`/var` is
  `/private/var`).
- **The compiler warnings.** `-Winvalid-offsetof` is off for GCC and Clang (`engine/CMakeLists.txt`): the SAGE field-parse tables
  take `offsetof` of classes with virtual functions as the Zero Hour source does, 267 times.

## The package

`package.sh --platforms macos` exports the "macOS arm64" preset of `godot/export_presets.cfg` through the arm64 template, unsigned
(`codesign/codesign=0`: Godot's built-in signer gives the extension a random identifier, so two exports never match), then signs
the extension and the bundle ad hoc with `codesign` (a fixed identifier each, the hardened runtime, the entitlement of
`tools/release/macos_entitlements.plist`, no timestamp): the same bytes every time, which `package.sh --verify` proves. The
bundle holds exactly what `tools/release/audit_package.py`'s `macos` allowlist names: Godot's `Info.plist`, `PkgInfo`, icon and
privacy manifest, the thin arm64 executable, the extension, the pack and the signature's resource seal. The archive is a zip whose
folders exist only through its file names (no directory entries), like the Windows one.

`tools/release/test_export.sh <openbfme-...-macos-arm64.zip>` runs the packaged bundle: the skirmish start, the smoke test on the
packaged pack and extension (the editor binary in the executable's place), the crash test and the fresh-user test, with Godot's
user data under `~/Library/Application Support/Godot/app_userdata/OpenBFME`.

A release built on the Linux builder takes the macOS archives a Mac made with `package.sh --macos-dir <that Mac's out folder>`
(`docs/RELEASE.md`).

## The launcher

`build_launcher.sh --platforms macos` exports `OpenBFMELauncher.app`. Godot's macOS export ignores `embed_pck`, so the bundle holds
Godot's arm64 template binary, the same bytes for every launcher version built with the same Godot, next to its pack
(`Contents/Resources/OpenBFME Launcher.pck`). The self-update keeps its one-file contract by swapping the pack
(`launcher/scripts/core/self_update.gd`; stop S-3711). The launcher's platform is `macos-arm64`, its manifest file on a release
`manifest-macos-arm64.json` (`manifest.json` keeps exactly Linux and Windows: every launcher published before refuses any other
key), its data folder `~/Library/Application Support/OpenBFMELauncher`.

`tools/release/test_launcher.py` plays the host's own launcher: on a Mac it builds the macOS launcher, installs macOS packages (a
shell script inside `OpenBFME.app/Contents/MacOS/`) and runs the self-update killed at every step; the published launchers' tests
and the GLIBC test are Linux-only.

## Gatekeeper

Nothing is notarized (stop S-3710). A bundle a browser downloaded is refused on its first open until the user allows it in System
Settings > Privacy & Security; what the launcher downloads has no quarantine attribute and starts at once, and the self-update
removes the attribute from the files it replaces. README_TESTERS.txt and the launcher's README say the steps.

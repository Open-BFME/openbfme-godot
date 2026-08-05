# OpenBFME Launcher on Linux (via Wine)

The launcher is a **Windows WPF** app (`net10.0-windows`, `win-x64`). There is no
native Linux GUI. The supported Linux path is:

**self-contained Windows build → Wine (or Proton / CrossOver)**

WPF talks to Direct3D. Wine must provide a working D3D translation layer
(usually **DXVK**). That is a compatibility layer, not a second product port.

## What to run

Always use the **self-contained** publish output from the release zip or:

```bash
# on a Windows build host / CI
dotnet publish launcher/OpenBFME.Launcher/OpenBFME.Launcher.csproj \
  -c Release -r win-x64 --self-contained true \
  -o dist/launcher-win-x64
```

Ship / copy `OpenBFME.Launcher.exe` from that folder (single-file extract layout).

**Do not** ship the framework-dependent `bin/Release/net10.0-windows/OpenBFME.Launcher.exe`
(~160 KiB). On a clean prefix without the .NET 10 Desktop runtime it exits with no UI.

## Wine setup (outline)

```bash
# Example: Wine Staging + DXVK (distro packages vary)
wineboot -u
# Install DXVK into the prefix (see dxvk releases)
# Then:
wine OpenBFME.Launcher.exe
```

Recommended:

| Piece | Why |
|-------|-----|
| Wine 9+ / Staging, or Proton Experimental | Newer D3D + .NET host behaviour |
| **DXVK** | WPF rendering path |
| 64-bit prefix (`WINEARCH=win64`) | Matches `win-x64` publish |
| Self-contained launcher | No separate .NET Desktop install in the prefix |

Optional env when debugging:

```bash
export WINEDEBUG=-all
# or for crashes:
export WINEDEBUG=+loaddll,+seh
```

## What works / soft-degrades

| Feature | Native Windows | Wine |
|---------|----------------|------|
| UI chrome / Play / status | Yes | Yes (with DXVK) |
| GitHub release check | Yes | Yes (needs network) |
| Workshop BFME2/RotWK download | Yes | Yes |
| Browse folder | OpenFolderDialog | Falls back to “pick game.dat”, or paste path |
| All-in-One UAC install | Yes | Often broken — use in-launcher **Download** |
| EA registry discovery | Yes | Soft-fail if no registry |
| Convert / importer | Needs bundled Python tools | Same under Wine if prefix has enough Win APIs |

The footer channel line shows `· Wine` when a Wine prefix is detected so players
know they are on the compatibility path.

## Install paths under Wine

Game folders are Windows-style paths inside the prefix, e.g.:

```text
Z:\home\you\games\BFME2
C:\Program Files (x86)\Electronic Arts\...
```

Paste those into the path boxes if Browse fails. The install must still contain
`game.dat`.

OpenBFME state lands under the Wine equivalent of:

```text
%LOCALAPPDATA%\OpenBFME\
```

which maps to something like:

```text
~/.wine/drive_c/users/<you>/AppData/Local/OpenBFME/
```

## Honest limits

- This is **not** a native GTK/Qt Linux launcher.
- GPU drivers, Wine version, and DXVK version matter; black windows or instant
  exit are usually D3D/Wine, not OpenBFME logic.
- Self-update handoff (relaunch a new `OpenBFME.Launcher.exe`) can be flaky under
  Wine — prefer replacing the build manually and restarting.
- Full convert (Blender/ffmpeg) under Wine is heavier than “download + check
  update”; treat conversion as best-effort on Linux hosts.

## Quick verification

1. `wine OpenBFME.Launcher.exe` shows the window and stays open.
2. Footer may show `Wine`.
3. **Check for update** either finds a release or prints the honest empty-feed message.
4. **Download** for BFME II writes under the Wine LocalAppData tree and produces `game.dat`.

If the window never appears, fix Wine/DXVK first — the lifecycle log under
`AppData\Local\OpenBFME\launcher-lifecycle.log` (in the prefix) only helps after
the process actually starts.

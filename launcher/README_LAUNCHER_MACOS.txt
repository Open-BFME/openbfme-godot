OpenBFME Launcher @VERSION@
==========================

This package contains the OpenBFME game and launcher for macOS. OpenBFME
rebuilds The Battle for Middle-earth II 1.06 and The Rise of the Witch-king
2.01. It contains no retail game files; use your own installs.

Start it: @START@
Keep OpenBFME.app and OpenBFMELauncher.app together in the same folder.
Launcher Play requires a valid signed payload inventory. A missing or invalid
seal disables Play; this does not prevent opening OpenBFME.app directly to run
the bundled game. Choose Game files in the launcher to check or select your
existing BFME2 and RotWK folders. Discovery checks the environment, remembered
folders, and downloaded-install marker in that order; each source must name two
existing folders.

Automatic updates and launcher self-updates are not available in this macOS
package. Install a newer package manually by replacing the entire package
folder, including both app bundles, VERSION, README files, and any payload
metadata. Do not replace only the app bundles or an executable inside a bundle.
Starting the game directly uses the packaged game and does not update either
app. The launcher cannot download or install game packages on macOS.

macOS may warn that the apps are unsigned or cannot be verified. This
credential-free package is ad-hoc signed, not Developer ID signed or
notarized. If you trust the source and verified the archive checksum:
1. Double-click the app in Finder, then dismiss the blocked-open warning.
2. Apple menu > System Settings > Privacy & Security > Security: click
   Open Anyway beside the app's warning (normally available for about an hour).
3. Confirm Open and authenticate with an administrator account or Touch ID
   when asked; the order varies by macOS version. Repeat for each app if needed.
On older macOS, Control-click/right-click the app > Open > Open may suffice.
If that shortcut is blocked, use System Settings instead. Managed Macs may
require IT approval. Do not disable Gatekeeper, strip quarantine, or override
a malware-detected/damaged-app warning. See docs/MACOS.md in the source tree
for more detail; it is not part of the exported package.

Release-update checks do not send account information or game data. The online
player count also asks the configured lobby for anonymous totals by default;
turn off "Show how many players are online" in Settings > General to stop it.
Multiplayer contacts the lobby for sign-in, friends, invitations and game
sessions, and linked-account sign-in opens its provider in your browser.
The launcher does not send telemetry.

Replay files
------------
Use Play > Open replay..., drop one .replay file onto the window, or use Finder > Open with > OpenBFMELauncher.
The app declares .replay as a Viewer document type. Finder Get Info > Open with > Change All selects the default;
the launcher does not force itself over an existing default. It starts only the installed/bundled build whose version
and commit match the replay. A missing-build message names the required build; replace/install that package manually.
Formats 5-8 did not record a release version; use the matching game's --replay option directly. New recordings use format 10.
Settings > General > Open .replay files with OpenBFME can refuse Finder file opens (or use -- --replay-association=off).
To change the OS default, choose a different application in Finder. The signed Info.plist is never rewritten.
Explicit Open replay / command-line / window drop still work when disabled; the game needs your own retail files.

Where it stores files
---------------------
The launcher stores its settings and logs in its macOS application-support
folder. OpenBFME stores its own settings and logs in Godot's app-user-data
folder. Both app bundles should remain together and in a writable folder.

LICENSE
-------
OpenBFME is licensed under GPL-3.0. See LICENSE and NOTICE.

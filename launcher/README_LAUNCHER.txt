OpenBFME Launcher @VERSION@
==========================

The launcher installs, updates and starts OpenBFME, a rebuild of The Battle for Middle-earth II: The Rise of the
Witch-king 2.01. OpenBFME runs on YOUR OWN copies of The Rise of the Witch-king 2.01 and The Battle for Middle-earth II
1.06; the launcher does not download or need any game files. When OpenBFME starts the first time, it asks where they are.

Start it: @START@
Press Play to start the newest installed version. At every start the launcher looks for a newer version of your
channel (Stable, or Preview for test builds), downloads it, checks it and installs it.

What it contacts
----------------
Only GitHub, over HTTPS with certificate checks:
  * api.github.com - the list of releases of @REPO@ (about one request per start;
    unchanged answers are cached and do not count against GitHub's limit of 60 requests per hour);
  * github.com and *.githubusercontent.com - the release files (the manifest, its signature, the packages).
It sends a User-Agent naming the launcher and its version. Nothing else: no telemetry, no account, no game data.
Offline, or when GitHub refuses (for example its hourly limit), it says so and plays the newest installed version.

How it checks a download
------------------------
Every release carries manifest.json, signed with the OpenBFME release key (Ed25519). The launcher accepts only a
manifest whose signature matches the key built into it, then checks every downloaded package's size and SHA-256
against the manifest before unpacking it. A package that fails a check is deleted and nothing is installed.
It never installs an older version by itself; to go back, pick an installed version or install an older release.

Where it stores files
---------------------
  Linux:   ~/.local/share/OpenBFMELauncher
  Windows: %APPDATA%\OpenBFMELauncher
    versions\<version>\   the installed versions (the two newest are kept, and the one you picked)
    downloads\            downloads in progress (resumed next time)
    cache\                the last release list and the checked manifests
    launcher.log          what the launcher did (attach it to a bug report; your home folder is written as ~)
OpenBFME itself keeps its settings and logs in its own folder (see the README_TESTERS.txt of an installed version).
Updates of the launcher itself are placed next to it (in update) and replace it at the next start; the previous
launcher is kept as OpenBFMELauncher(.exe).old until the new one has started once. If the launcher ever does not
start after an update, rename that .old file back to the launcher's name. Keep the launcher in a folder you can
write to.

Remove everything: delete the launcher's folder and the data folder above.

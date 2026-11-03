# Installing BFME2 without RotWK

RotWK 2.01 remains the launcher's default selection and still uses both
RotWK and BFME2 installations. To install only the base game, choose
**Battle for Middle-earth II 1.06 (without RotWK)** on the Play screen,
then download the game files. The existing consent and pinned-file checks
apply; only the BFME2 package is requested.

For an existing BFME2 1.06 installation, select standalone BFME2 in the launcher,
then choose its folder on first run. The RotWK field is unused in this mode, even
if filled. RotWK selection requires both folders; leaving its folder blank does
not change the launcher selection. The selection and folder are remembered.
If a remembered BFME2 folder moves or becomes unusable, windowed launches reopen
folder setup with the mount errors; headless launches return those errors.
Switch back to RotWK in the launcher to use the expansion again; selecting
BFME2 never mounts an available RotWK installation over the base game.

Command-line selection uses `--game=bfme2-106` (game or launcher).
The launcher's historical `--game=bfme2` value continues to mean RotWK.
`--aio-package=bfme2-106` selects the standalone download package.
A direct headless game launch needs `BFME2_INSTALL` or a remembered folder.
The launcher picker and launcher `--game` write the same `game-selection.cfg`
read by direct game starts. Explicit game command-line choices override it;
legacy installs keep the RotWK default. Standalone folder checks and Show folder
accept a BFME2-only install, and refreshing BFME2 retains an existing downloaded
RotWK location. Unpinned language archives carry the same warning details and
confirmation signature as regular gameplay mounts.

BFME2 uses the existing verified `bfme2-106` archive policy, base INI legend,
six playable factions and BFME2 menu. The BFME2 shell-map default follows
Open-BFME-2 `GlobalData.cpp` (constructor `m_shellMapOn(1)`), while
RotWK retains its required explicit INI field. Archives, language files and checksums
are checked by the existing mount/download paths. The shared simulation is
retained: S-5911 explicitly reports that remaining BFME2-versus-RotWK logic and
subsystem-order fidelity are not fully verified. Edition detection, base-legend
loading, shell-map defaults and projectile emotion variants use main's shared
implementations. This does not change the project's source policy or claim
BFME2 retail cross-play compatibility.
RotWK mod arguments are refused in this initial standalone path. Cosmetic asset
packs still load from the enabled-pack folders and explicit `--asset-pack=`
arguments, with the same model checks and named failures as the RotWK mount.

Standalone gameplay reads custom maps from the BFME2 user-data `Maps` folder
named by its own `gi.dat`, and reports that path as `user_map_folder`. A missing
or unreadable `gi.dat` reports `user_map_error`, as in the existing RotWK mount.
The presentation-only BFME2 menu source keeps no local map search path.

Validation includes synthetic launcher selection, a complete local-service
BFME2-only download with no RotWK requests, first-run callbacks and native
install/legend tests. `godot/tests/bfme2_standalone_test.gd` is the manual
installed-data check; run it with `--game=bfme2-106 --no-update-check` and
`BFME2_INSTALL` set, with `ROTWK_INSTALL` unset.

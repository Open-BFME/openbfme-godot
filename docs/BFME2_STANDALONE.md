# Installing BFME2 without RotWK

RotWK 2.01 remains the launcher's default selection and still uses both
RotWK and BFME2 installations. To install only the base game, choose
**Battle for Middle-earth II 1.06 (without RotWK)** on the Play screen,
then download the game files. The existing consent and pinned-file checks
apply; only the BFME2 package is requested.

For an existing BFME2 1.06 installation, choose its folder on first run and
leave the optional RotWK field blank. The selection and folder are remembered.
Switch back to RotWK in the launcher to use the expansion again; selecting
BFME2 never mounts an available RotWK installation over the base game.

Command-line selection uses `--game=bfme2-106` (game or launcher).
The launcher's historical `--game=bfme2` value continues to mean RotWK.
`--aio-package=bfme2-106` selects the standalone download package.
A direct headless game launch needs `BFME2_INSTALL` or a remembered folder.

BFME2 uses the existing verified `bfme2-106` archive policy, base INI legend,
six playable factions and BFME2 menu. The BFME2 shell-map default follows
Open-BFME-2 `GlobalData.cpp` (constructor `m_shellMapOn(1)`), while
RotWK retains its required explicit INI field. Archives, language files and checksums
are checked by the existing mount/download paths. The shared simulation is
retained: S-5911 explicitly reports that BFME2-versus-RotWK logic and
subsystem-order fidelity are not fully verified. The BFME2-only
`PreLandingEmotionAffectsAllies` field is retained; its ally filter is
unported and the shared runtime still uses RotWK's relationship mask 7. This does not change the
project's source policy or claim BFME2 retail cross-play compatibility.
RotWK mod arguments are refused in this initial standalone path.

Validation includes synthetic launcher selection, a complete local-service
BFME2-only download with no RotWK requests, first-run callbacks and native
install/legend tests. `godot/tests/bfme2_standalone_test.gd` is the manual
installed-data check; run it with `--game=bfme2-106 --no-update-check` and
`BFME2_INSTALL` set, with `ROTWK_INSTALL` unset.

# Rally point banner

## Recovery

The reference is the supplied **modified** RotWK 2.01 image, not a
checksum-verified publisher binary (S-001). The BFME2 donor is
`ControlBar.cpp:ControlBar::showRallyPoint`, mapped from RW 0x71D18F to BFME2 0x71B892;
the target decompilation was checked for the changed offsets.

- **RW 0x71D18F**: a null location destroys the cached drawable and clears
  its ID. Otherwise it creates `RallyPointMarker` using `newDrawable` with
  no object, or reuses the existing drawable. It sets status bit `0x10`
  (the ZH donor names this `DRAWABLE_STATUS_NO_SAVE`), position, orientation
  from GlobalData +0x94 (`DownwindAngle`, a real, not a degrees parser),
  and the local player's day colour (+0x2A0), or night colour (+0x2A4)
  when time of day is 4.
- **RW 0x943D6F**, command context: asks the selected object's exit
  interface (**RW 0x68BB14**) for its rally point. The normal ownership
  gate is local control; a non-active local player can also see it.
- **RW 0x944EFB**, construction context: also shows the exit's rally point.
- **RW 0x71D8BE**, context switch: clears the previous marker.
- The mounted `data/ini/object/system/system.ini` defines `RallyPointMarker`
  as `DRAWABLE_ONLY`, with `W3DScriptedModelDraw`, model `RallyFlag_SKN`
  and looping idle animation `RallyFlag_SKL.RallyFlag_WAVA`. It is not an FX
  or a simulation object. No retail asset bytes are included here.

## Port

The ground command already existed: with alternate mouse enabled, raw
right-button input passes through `HudInput` and `CommandTranslator`,
emitting `MSG_SET_RALLY_POINT` for each selected local producer with an
exit. Logic stores the point and production's existing AI hand-off moves
new units there. The absent piece was the client drawable.

`InGameHud` now owns that marker's lifetime and reads the exit after each
control-bar refresh while the logic is held. `DrawableManager` allocates a
client-only slot, outside the object map and simulation/save state. Normal
`GameWorld` rendering, animation and house-colour handling display it;
there is no viewer-only substitute or additional simulation object.
Changing the rally position reuses the drawable; losing/changing the
single-object context destroys it. Missing resources are reported.

The retail-data regression `hud rally banner: ...` selects a barracks with
a left click, sets its point with a real right click, and produces a horde
through the Palantir button. It checks the message, stored point, arrival,
flag model and looping animation, position, reuse, night colour and
selection lifetime. Godot's HUD state exposes `rally_marker` for inspection.

## Verification

With the retail-data test retained and only `InGameHud.cpp` and
`DrawableManager.cpp` restored to the parent implementation, the test
passes 26/29 assertions: all three failures are absent-marker checks;
the actual right-click command and produced-horde arrival still pass.
Restoring the implementation passes all 60 assertions, including the
additional checks reached when the marker exists. No simulation pins
were changed.

The Release test executable and Godot extension build successfully.
Bounded Palantir, Men production-rally, camera and drawable regressions
pass 29 cases / 762 assertions. The camera binary-facts case reports a
skip because `RW_GAME_DAT` was unset; it is not counted as binary coverage.

Godot 4.7.2 / Vulkan (software llvmpipe) was also exercised with native
mouse input on Evendim: the animated flag appeared, moved with the second
right click, disappeared on deselection and returned on reselection.
The HUD production button queued GondorFighterHorde, whose soldiers were
observed leaving the barracks and gathering at the flag without another
movement command. Setup used the existing HUD-viewer map/barracks fixture:
the normal Solo Play / Skirmish menu did not transition. This is gameplay
render/input coverage, not a menu-to-match or clean-retail capture. The
runtime arrival check is visual; the numerical arrival assertion is in
the C++ regression above. Captures and recordings remain outside git.

## Existing limits, not hidden by this fix

- S-4320: objects whose only exit is a contain (including some keeps and
  towers) still lack the client command/exit integration. This patch does
  not manufacture rally points on them.
- S-1202: multi-selection representative-drawable semantics remain open;
  this marker follows the HUD's current single-object context.
- RW 0x6AAC52 reads player bytes +0x35A and +0x754. The port models the
  defeated state but not the additional +0x35A flag; no observer mapping
  is invented here.
- Client-only templates with ClientUpdate/ClientBehavior modules require
  a separate logic-owner module-resolution contract. They report an
  explicit error; retail `RallyPointMarker` has neither.

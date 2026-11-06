# macOS trackpad camera controls

This is an enhanced camera feature shared by BFME2 and RotWK. It does not change
simulation rules, commands, saved state or RNG.

Over the game world:

- Slide two fingers to pan the camera in both axes.
- Twist two fingers to rotate the camera heading.
- Spread fingers to zoom in; pinch together to zoom out.

Gestures over the HUD, outside the viewport, while input is disabled, or while a
modal menu is open leave the camera unchanged. Mouse-wheel zoom keeps its existing
behavior. Camera movement uses the existing zoom limits and map constraints.
Gestures update client camera state directly, without waiting for a logic frame.

Godot supplies incremental `InputEventPanGesture` and `InputEventMagnifyGesture`
events. Its macOS content view has no rotation-event implementation, so the
extension installs an AppKit local monitor for `NSEventMaskRotate`, scoped to the
HUD's focused native window. Rotation is in degrees. The monitor is removed when
the HUD leaves the tree, is rebuilt or destroyed, and on extension shutdown.
No global event monitoring or Accessibility permission is used.

Sources: [Godot gesture definitions](https://github.com/godotengine/godot/blob/master/core/input/input_event.h),
[Godot macOS delivery](https://github.com/godotengine/godot/blob/master/platform/macos/godot_content_view.mm),
and [Apple local event monitoring](https://developer.apple.com/documentation/appkit/nsevent/addlocalmonitorforevents(matching:handler:)).

Run `godot --headless --path godot --script res://tests/macos_trackpad_test.gd`
with ROTWK_INSTALL and BFME2_INSTALL set. This checks viewport-injected Godot pan/pinch
input, the rotation adapter, invalid values, disabled input, HUD exclusion and
unchanged simulation hashes. Run without `--headless` on macOS to check native
monitor installation as well.

Physical-device acceptance: in a skirmish, pan horizontally and vertically,
twist both directions, and pinch/spread. Repeat over the radar and command bar,
with the pause/options menu open, after switching focus to another application,
and after leaving and restarting the game. Verify ordinary mouse-wheel zoom still
works. Synthetic event checks cannot prove physical trackpad delivery or feel.

# Adaptive in-game HUD

The **HUD** button at the upper left, or **F10**, cycles between Classic, Modern,
and Hidden. Modern is the default on iOS. The preference is saved locally in
`user://adaptive-hud.cfg`. Hidden leaves only the restore button.

Modern provides large boxed commands with native icons, prices, queue counts and
progress indicators. Its collapsible sections are Commands, Powers, Heroes,
Builders, Map, and System. Only one section opens at a time, leaving the battlefield
clear on phones. Portrait and landscape layouts use safe-area insets on iOS.

- **Commands:** selection-sensitive construction, training, upgrades, abilities,
  and orders from the native command bar; restrictions remain enforced by the game.
- **Powers:** native powers and a purchase panel with localized names, icons,
  availability and point costs. Leaving the purchase panel closes the native store
  and submits its pending purchases, as in the original interface.
- **Heroes / Builders:** select an owned object directly.
- **Placement:** reposition, lock, rotate, build, or cancel using touch buttons.
- **System:** army selection, stop, deselect, console, HUD mode, and access to the
  existing pause/options screens. Those screens retain their current implementation.
- **Map:** a resizable terrain minimap with fog, visible unit dots, and the camera
  footprint. Tap/drag to move the camera; enable Orders to send the current selection.

For controllers, hold the existing palette trigger (RT, or Tab in keyboard
controller mode). D-pad left/right selects a tile, up/down changes sections, A
activates, B cancels commands/queues, and Y closes the panel. In Map, the D-pad moves
its crosshair, A looks, B issues an order, and Y exits. World/camera controller
bindings continue to use the existing controller bridge. The replacement suppresses
the old controller palette while active.

Native menu graphics and the old radar hit area are disabled in Modern/Hidden;
selection, simulation and orders remain live. Overlay rectangles capture only their
own input, including touches dragged outside a panel, rather than blocking a large
rectangle across the battlefield. Radar orders use the same native command path as
the classic radar; the new UI does not query hidden enemy positions.

Validation: `godot/tests/adaptive_hud_test.gd` exercises the replacement with a mock
HUD, including controller activation/cancellation, power-store cleanup, map orders,
mode restoration, and phone portrait/landscape plus desktop layout bounds. It needs
no retail data. It was run in an isolated Godot 4.6 project; production targets the
repository's Godot 4.7 build. Native adapters were syntax-checked against that
build's headers. Full retail gameplay, visual screenshots and physical iOS/controller
hardware testing remain pending.

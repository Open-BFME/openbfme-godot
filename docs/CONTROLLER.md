# Controller controls

**backslash (`\`)** toggles forced controller layout without a gamepad. During gameplay, hold **Tab** to imitate RT and open the palette; release Tab to close it. While held: arrows navigate, Enter selects, Backspace cancels queued production, X saves Favorites, and Y returns to commands. During gameplay the cursor stays at the viewport centre. Backslash again restores normal keyboard controls. The FPS counter shortcut is now **F10**. Menus retain their shared arrow-key navigation.

The first controller used owns input until disconnected. Godot's standard controller mapping supplies Xbox names (A/B/X correspond to PlayStation Cross/Circle/Square).

| Control | Menus | Gameplay |
|---|---|---|
| D-pad | Directional menu focus | Up: cycle heroes; Down: cycle builders; Left: select your units |
| Left stick | Directional focus with repeat | Analog camera pan: proportional speed, full diagonal direction |
| Right stick | Pointer movement | Rotate horizontally; zoom vertically |
| A / Cross | Activate focused button | Select, place, or issue the native contextual order (move, attack, enter, repair) |
| B / Circle | Back / Escape | Remove the object under the reticle from selection; empty ground clears selection; cancel targeting/placement |
| LB / RB | Previous / next focus | Zoom out / in |
| X / Square | Main menu: open Skirmish; skirmish setup: Start Game | Centre camera on selection |
| Y / Triangle | — | Go to the latest minimap alert |
| Left-stick click / L3 | — | Go to home base |
| Right-stick click / R3 | — | Reset camera angle |
| Start / Options | Escape | Pause/options menu |
| Hold A+B | — | Preview a formation; left stick aims and sets width, release either button to deploy |
| Hold RT | — | Open command palette |

Main-menu arrow keys and D-pad use the same reference order: Tutorials → Solo Play → Multiplayer → Options → My Heroes → Quit, wrapping at either end. Up moves through the selected group's items in reference order; Down reverses it. Enter/Space and A activate the selection. Moving the mouse returns to pointer navigation.

While holding RT, Left/Right select commands across the full command row. Up enters the four shortcuts above the left Palantir and climbs through Powers, Heroes, Builders, and Favorites; Down descends and returns to commands. A opens a shortcut or activates the selected entry. B cancels queued production on a command. Y returns to the selected unit's commands. X saves the current selection to Favorites; A on its entry recalls surviving, selectable units you own. Favorites last for the current match.

The palette uses native circular RadialBorder/RadialOver/RadialPush artwork and circular hit areas. Its command images preserve the same opaque RGB backgrounds as native recruitment buttons. All commands appear together in a single row, with smaller circles that shrink to fit the available width beside the Palantir. The bottom command row uses native mapped icons, live availability, costs and queue counts. Native HUD handlers perform activation, production cancellation and spell targeting. Heroes and Builders list your selectable units. Powers lists your assigned spellbook slots and a Buy powers entry. Its purchase list uses the native spell store; B returns, and releasing RT closes the store and applies purchases. Unavailable commands remain visible and use the native availability checks.

Held input releases on context changes, focus loss and disconnect. The palette closes when RT is released or gameplay ends. Controller state does not affect saved games.

## Lua adaptation and interfaces

`godot/dev/00_controller_config.lua` through `04_controller.lua` adapt the local BFME2Lua repository's `controller.lua`, `lib/keybinds.lua`, and `lib/controller/{config,controls,palantir}.lua`. These modules use `package.preload` because the companion VM has no filesystem module loader. Numeric filename prefixes define reload order. The game loads them at startup; **Ctrl+Shift+F5** or `bfme.console.reload()` reloads them during play.

`controller.action(button, inMatch, palette)` resolves the menu/game/palantir context. `controller.ui.navigate(action,count)` owns command selection, command focus, shortcut focus and submenu selection. Edit `lib.keybinds` for bindings and arc coordinates. Lua config controls radial left-stick deadzone, analog pan speed (`cameraPanSpeed`), pointer speed and stick menu repeat; the host reads it at startup and when RT opens.

`controller_input.gd` handles devices and axes; `controller_ui.gd` renders the palette and adapts Lua actions to the live HUD. Added native `InGameHud` interfaces:

- `activate_command(slot,in_palantir,cancel=false)`: reevaluates availability and uses the normal command/message handlers.
- `get_command_icon(name)`: resolves mapped retail artwork as an atlas texture.
- `get_controller_objects(role)`: lists selectable local heroes, builders or units.
- `select_controller_group(ids)`: validates ownership/liveness and synchronizes client and logic selection through the normal message stream.
- `get_command_buttons()` also exposes `image`, `label`, and `timer`.

This implements the command palette and core controller play flow. Reference features such as formation previews, touchpad gestures, modifier combinations and specialized controller editors for every lobby gadget are not included. Physical-controller testing remains necessary; automated Lua, input, UI and live-game checks cover the host interfaces.

Controller mode matches the reference visibility policy: hide native portrait commands, the builder-list shortcut, the assigned powers bar and floating building-command bubbles (including their hit areas). The minimap, portrait, resources and hero entries remain. The three minimap buttons (Options, Objectives and Player Magic) are hidden and cannot receive pointer clicks in controller mode. Native visibility is restored when keyboard-controller mode is disabled or the pad disconnects.

Controller command focus and mouse hover use the native InGameHelpBox provider, delay, command composition and APT tooltip movie. Hero/builder entries use native unit help; assigned powers use native spell command help. No custom tooltip panel is drawn.

Like the reference Palantir controller, focusing an enabled builder construction command readies its native placement preview. Targeted portrait commands also arm on focus. Trigger release preserves placement; A/Enter while the palette is open confirms the current world target. Production and purchases never execute merely from focus.

Hold comma/period (`<` / `>`, with or without Shift) during gameplay to rotate the camera left/right. The reloadable Lua game and palantir bindings map `Comma`/`Period` to `rotateLeft`/`rotateRight`. Releasing the key, opening a menu/console, or losing focus stops rotation.

Original controller artwork is loaded from the ignored local `godot/controller_art/` folder, copied from BFME2Lua `dev/cursors`. It contains retail-extracted Xbox/PC art and must not be committed. Local exports include it. Selected command circles use the original 30 fps good/evil halo and A glyph; active controller mode replaces the pointer with the original contextual reticle strips. During controller gameplay the pointer stays at the viewport centre, including while targeting from the palette. The left stick pans; the right stick rotates and zooms. Controller camera bounds let the centre reach the map border; leaving controller mode restores normal camera margins. A evaluates the object under the reticle: applicable actions such as attack, garrison, capture and repair take precedence; otherwise it selects that object, or orders movement over ground. Its release always uses the same button as its press.

While a construction preview is active, hold A to anchor it and aim the left stick to rotate the building. Release A to confirm, or press B to cancel. Camera movement stays locked during the hold, then waits 300 ms and for the left stick to return to its deadzone before resuming. The cursor stays on the anchor.

The console’s **Controller** tab sets **Selection assistance radius** (0–200 screen pixels at 720p, scaled with viewport height; default 48). With no direct object under the reticle, A selects the nearest eligible friendly, unselected unit in that radius. Direct hits, placement, ability targeting and GUI controls keep their normal behavior. Set 0 to disable. The setting is saved in `user://controller_options.cfg`; its live Lua option is `controller.config.selectionAssistRadius`. Hidden palantir controls also remove their parent backgrounds (`CommandBackground` and `PalantirButtons`).

Controller targeting previews the same direct or assisted target that A uses. A selectable unit or building displays the green A reticle and its native health bar before selection; moving outside the assistance radius clears that hover. Attack and garrison actions retain their contextual reticles.

Double-tap A within 300 ms while issuing orders with units selected to attack-move to the reticle. The second tap sends the native attack-move order and replaces the move star with the Xbox gold axe ground marker. Selection, construction and menu taps retain their normal behavior.

Hold **LT** or **Shift** during building placement to snap positions to a world-aligned map grid and rotations to 45° steps. A terrain-following grid appears around the preview while held. Set **Placement grid spacing** in the console’s Controller options (10–200 map units; default 50). The saved Lua option is `controller.config.placementGridSpacing`. Release the modifier to return to free placement.

Hold A for at least 250 ms to grow a gold selection circle around the cursor. Release to select friendly selectable units within its map radius (buildings excluded). The radius grows up to 600 map units; camera movement pauses during the hold. B cancels. Short taps keep contextual selection/orders and double-tap attack-move; construction holds still rotate buildings.

With selected mobile units, hold both mouse buttons on the terrain to preview a formation. Drag to choose its facing and width; release either button to deploy. Green ground rings mark each unit or battalion destination and arrows show its final facing. Escape or focus loss cancels. Formation placement preserves the selection and suppresses selection drags and camera drag scrolling.

A+B formation aiming pauses both camera sticks. Releasing the formation leaves a short delay and waits for the left stick to return to neutral before camera panning resumes. Holding A alone still grows the selection circle. B alone deselects on release so either A+B press order works. Opening the palette, leaving gameplay, or disconnecting cancels the preview.

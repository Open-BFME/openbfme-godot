# In-game player messages

Invalid building placement and the other existing `InGameUI::message` callers
now feed a six-entry on-screen log. Messages resolve through the mounted game
text; `InGameUI.ini` supplies MessageColor1/2, MessagePosition, MessageFont,
MessagePointSize, MessageBold and MessageDelayMS. Language.MessageFont overrides
font settings, as in the recovered initialization code. Negative MessagePosition.X
anchors the right edge. Oldest entries draw first, with a one-pixel black shadow.

Source: Open-BFME-2 `Code/GameEngine/Source/GameClient/InGameUIMessages.cpp`
(addMessageText, RVA 0x0029BA58), `InGameUIPostDraw.cpp` (postDraw,
RVA 0x002A0161), `InGameUIUpdate.cpp` and `InGameUICtor.cpp`. Six-entry capacity,
color selection, coordinate rounding and cumulative age-based alpha subtraction
follow those recovered bodies. Fading runs at the existing 30 Hz client cadence.
The port's five logic frames per second are used for the recovered integer
`MessageDelayMS / logic_fps / 1000` timeout expression; it is deliberately not
rewritten as an assumed duration in seconds. This is BFME2 donor behavior;
independent RotWK binary verification of these display routines is outstanding.

Tests: native `player message log:*` covers settings, capacity, colors and fade;
`godot/tests/player_message_log_test.gd` exercises invalid placement through the
real HUD, localized output and successive alternating colors. Retail game files
are required for the integration test.

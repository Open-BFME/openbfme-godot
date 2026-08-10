Builder sidebar frame art - drop-in slot
=======================================

This directory is the in-repo slot for the ornate vertical frame that wraps the
builder's right-edge build column (the round building icons shown while a
builder/porter is selected). Drop a PNG here and the game picks it up; nothing
else has to change. The contract lives in
game/src/retail_slice/retail_side_command_frame.gd.

FILE NAMES
----------
  sidebar_frame_<faction>.png   per-faction frame
  sidebar_frame_default.png     shared fallback for factions with no own frame

Faction keys (lower case, spaces and dashes folded to underscores):
  men  elves  dwarves  isengard  mordor  wild  angmar

SIZE AND FORMAT
---------------
  * PNG with a real alpha channel. Transparency is required: everything
    outside the frame body must be fully transparent (alpha 0) - not white,
    not black, and not a checkerboard baked into RGB. Generators often return
    an opaque image with a painted-on checkerboard; that will render as a grey
    slab over the battlefield.
  * Native size 147 x 1074 px, or any integer multiple (294 x 2148,
    441 x 3222). The aspect 147:1074 is the contract - the game stretches the
    image to that aspect, so a different aspect will visibly distort.
  * The art is drawn full-bleed into that rectangle. The game right-anchors it
    to the screen edge and scales it to 96% of the screen height, so at 1080p
    it renders about 142 x 1037 px. The right edge of the image lands exactly
    on the right edge of the screen - the reference art's body runs to 147 px
    and the generated default insets 3 px, either is fine.

WHERE THE ICONS LAND
--------------------
All numbers below were measured off the retail reference crop with a pixel
grid, and are the same numbers the game lays the sockets out from.

Fractions of the image WIDTH (147 px native):
  * build icons are OVALS, wider than tall. They are centred at x = 0.585
    (86 px), are 0.735 of the width across (108 px) and 0.571 of the width
    tall (84 px), so they cover x = 0.218 .. 0.952 (32 px .. 140 px).
  * the frame body sits behind and to the right of them, roughly
    x = 0.646 .. 1.00 (95 px .. 147 px). Only a sliver of it shows past the
    icons - that is correct, retail looks the same.
  * everything left of x = 0.218 (32 px) is outside the widget; art there is
    drawn but nothing is placed on it.

Fractions of the image HEIGHT (1074 px native):
  * the icon column runs y = 0.1331 .. 0.8538 (143 px .. 917 px). Nine icons
    fill it exactly at natural size, centres 86.25 px apart starting at
    y = 185; longer build sets shrink to fit the same band.
  * put the scroll/volute terminals in the margins outside that band. In the
    reference they are centred at y = 152 and y = 972.

The icons are drawn OVER the frame, so art under the icon ovals is hidden.
Do not rely on detail there.

WHERE ELSE THE GAME LOOKS
-------------------------
Highest priority first; a per-faction file in ANY root beats a default file in
a higher-priority root.
  1. user://ui/sidebar/                     - drop-in for a shipped build; no
                                              repo change and no reimport
  2. <mounted content pack>/assets/ui/hud/sidebar/
  3. this directory (res://data/base/assets/ui/hud/sidebar/)
With no file anywhere the game paints a repository-generated frame instead.

Note for this directory only: Godot normally wants an .import sidecar for a
res:// image. The loader falls back to reading the PNG straight off disk when
the sidecar is missing, so a plain drop works when running from the repo; an
EXPORTED build needs the file present at export time. The user:// slot has no
such caveat.

Retail note: BFME2/RotWK do not ship a per-faction frame. Retail composes one
shared frame per socket from 46x46 tiles in the apt_libInGameImagesMain atlas
(see the rect table in retail_side_command_frame.gd). Faction-specific frames
are an OpenBFME addition.

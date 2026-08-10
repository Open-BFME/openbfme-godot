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
  * PNG with a real alpha channel. Transparency is required: the icon column
    and everything outside the frame must be fully transparent (alpha 0), not
    white and not a checkerboard baked into RGB.
  * Native size 147 x 1074 px, or any integer multiple (294 x 2148,
    441 x 3222). The aspect 147:1074 is the contract - the game stretches the
    image to that aspect, so a different aspect will visibly distort.
  * The art is drawn full-bleed into that rectangle. The game right-anchors it
    to the screen edge and scales it to 96% of the screen height, so at 1080p
    it renders about 142 x 1037 px.

WHERE THE ICONS LAND (leave these areas clear)
----------------------------------------------
Fractions of the image WIDTH (147 px native):
  * round build icons are centred at x = 0.408  (60 px) and are
    0.70 of the width across (103 px), i.e. they cover x = 0.058..0.758
    (8 px .. 111 px). Keep that column transparent or the icons are hidden.
  * the frame body should live to the right of that, roughly x = 0.42..1.00
    (62 px .. 147 px), running to the right edge of the image.

Fractions of the image HEIGHT (1074 px native):
  * the icon column runs from y = 0.055 to y = 0.945 (59 px .. 1015 px).
    Nine icons fill that band at natural size; longer build sets shrink to fit.
  * the top and bottom 5.5% are the end caps - put the scroll/volute terminals
    there so they sit above and below the icons.

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

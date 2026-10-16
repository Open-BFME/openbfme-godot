# Options (lanes OPTS-2 .. OPTS-5)

RotWK 2.01's Options menu, every control and every `Options.ini` key the game
reads or writes, with what each one does in OpenBFME. Then the OpenBFME
extras, which are a separate, clearly labelled screen.

Addresses are RotWK `game.dat` (caveat S-001); "decomp" is the Open-BFME-2
BFME2 1.06 donor, used only where `tools/re/rw2decomp.py` says tier A.

## The file

`Options.ini` in the user data folder is ZH's `UserPreferences` as RotWK's
`OptionPreferences` (ctor RW 0x6E56F3): `key = value` lines, read whole,
written back sorted by `strcmp` as `"%s = %s\n"`, every entry kept, also the
ones RotWK does not know. RotWK writes through `_wfopen(..., L"w")` (RW
0x7B274C), a text-mode stream, so its lines end in CR LF. OpenBFME keeps the
ending of the file it read (CR LF, or LF when every break is a bare LF) and
writes CR LF for a new file, so a file RotWK wrote round-trips byte for byte
(`test_opts2_options.cpp`, a CR LF retail-format fixture). OpenBFME's own keys start with `OpenBFME`
(`OpenBFMESoftParticles` was UI-2's `SoftParticles`; it is renamed once on
load). `--options-file=PATH` gives a run its own file (the checks).

## RotWK's Options screen (Options.apt, `AptOptions`)

Opened by RW 0x91ED91(network, allowResolution, allowAdvanced, advancedOnly):
main menu (0, 1, 1, 0) RW 0x91C37E, LAN lobby (0, 0, 1, 0) RW 0x846F58, quit
menu (0, 0, 0, 0) RW 0x921783. InitGadgets RW 0x9205C4 (decomp tier A),
Save RW 0x91FC9C, Reset RW 0x91F4D1, Cancel RW 0x91EC52.

### Basic page

| Control (gadget) | Options.ini key, getter | Effect in RotWK | OpenBFME |
|---|---|---|---|
| Music / Sound FX / Voice / Ambient / Movie sliders | `MusicVolume`, `SFXVolume`, `VoiceVolume`, `AmbientVolume`, `MovieVolume` (`%f`, RW 0x6E5FB3 / 0x6E68F6) | TheAudio vslot 0xE8 while the slider moves, with the type's MiscAudio `VolumeSample*` sound (RW 0x91F90D); again on Reset and Cancel | live with the samples (OPTS-4, S-4510): the sample starts when none is sounding and stops when the slider is let go (the voice line plays out); from the main menu the music slider plays no sample (the menu music is the sample) |
| Graphics (`Detail` combo) | `StaticGameLOD` (RW 0x6E563A getStaticGameDetail, RW 0x6E6798) | GameLODManager setStaticLODLevel RW 0x6020B4 -> apply RW 0x601C62 | applied (see the detail levels below) |
| Custom Settings (advanced page) | the nine keys below | Custom record RW 0x6019D1, then the apply | applied |
| Brightness | `Brightness` (`%d`; RW 0x6E5ECD, missing 50) | GlobalData + 0xBCC gamma (RW 0x641E8B: < 50: 1 - (50 - b) * 0.008, 0 and below 0.6; > 50: 1 + (b - 50) * 0.02), the display's gamma ramp | applied: a pass over the window draws DX8 Set_Gamma's ramp (`game_options.gd`); retail skips the ramp in a window, OpenBFME applies it in every mode |
| Show All Health Bars (`HealthBars`) | `AllHealthBars` (yes / no; RW 0x6E6179, strcmp "yes") | GlobalData + 0x9BE: health bars on infantry and cavalry too (RW 0x676346); enabled only while the detail allows it (RW 0x91EB77 / 0x91EBD1: not at VeryLow, at Custom only with DecalLOD not Off) | applied, the box's enable rule ported |
| Scroll Speed | `ScrollFactor` (read: RW 0x6E59A9, atoi, below 0 -> 1, above 100 -> 100, * 0.02, so 0 is a value it takes; missing: GlobalData + 0xAFC; Save writes the slider as `%d`, raised to 1 when it is 0, RW 0x91FE9E) | GlobalData + 0xA9C / + 0xAA0 (Horizontal / VerticalScrollSpeedFactor) replaced right after GameData.ini (RW 0x641E0C, parseGameDataDefinition RW 0x644148) | applied (before OPTS-2 the camera used GameData's 0.4 / 0.5 instead) |
| Alternate Mouse Setup | `AlternateMouseSetup` (RW 0x6E61D4) | GlobalData + 0x5C (RW 0x641E7A) | applied (PLAY-1; live since OPTS-2) |
| Resolution | `Resolution` (`%d %d`, RW 0x6E5F27) | the display mode (Save RW 0x91FCC8 .. 0x91FD84 and a confirm flow) | stop S-3401: unavailable (retail's AllowResolutionChange 0 state); from the main menu the row reads "Use OpenBFME → Display." (APT:CannotChangeResolution replaced), in a game retail's own text |
| EAX3 | `UseEAX3` | EAX 3 hardware effects | disabled (greyed; value kept): no EAX (S-1913) |
| High Audio Quality | `AudioLOD` (Low / High, RW 0x6025F1) | GameLOD.ini AudioLOD (Dolby, ambient streams, reverb) | disabled (greyed; value kept): AudioLOD not ported (S-2481) |
| Online IP, Port Number, Send Delay, Refresh NAT | `GameSpyIPAddress`, `FirewallPortOverride`, `SendDelay`, `FirewallBehavior` ... | online play | disabled from the main menu as in retail (NetworkEnabled 0); RefreshNat stop S-1913 |
| Display Foreign Language, Turn Off Messenger, Language Filter | `DisplayForeignLanguage`, `TurnOffMessengerInGame`, `LanguageFilter` | online lobby | not on the 2.01 screen's main-menu page; kept |
| Reset To Defaults / Cancel / Accept | | RW 0x91F4D1 / 0x91EC52 / 0x91FC9C | ported |

Other keys RotWK reads and keeps (RW 0x6E5600 .. 0x6E6700): `IdealStaticGameLOD`,
`FirewallPortAllocationDelta`, `FirewallNeedToRefresh`, `CampaignDifficulty`,
`UseSystemMapDir`, `SaveCameraInReplays`, `UseCameraInReplays`, `HasSeenLogoMovies`,
`HasGotOnline`, `BCU` (GlobalData + 0xAF5), `RefreshNet`, `SuppressStrategicPromo`,
`SuppressCahPromo`, `IPAddress`, `DoubleClickGuard`. RotWK 2.01 never calls the
`DoubleClickGuard` getter (RW 0x6E6115 has no caller; GlobalData + 0xB6A is
read only there), so it has no effect in 2.01.

### Advanced page (`AdvancedOption0..8`, table RW 0xDA2228)

| Row | Key | Choices | Effect (RW 0x6019D1 -> Custom record -> RW 0x601C62) | OpenBFME |
|---|---|---|---|---|
| 0 | `ModelLOD` | Low .. UltraHigh | manager + 0x1774; GlobalData + 0x24 = ModelLOD <= 0 | stop S-3402 |
| 1 | `AnimationLOD` | VeryLow .. UltraHigh | + 0x1788 | stop S-3402 |
| 2 | `EffectsLOD` | VeryLow .. UltraHigh | + 0x1778; MaxParticleCount (+ 0xAC8) and the two particle priorities (+ 0x17A0 / + 0x17A4) of that preset; UseHeatEffects = >= 2 (+ 0x25) | particle cap and priorities applied (FX player); heat effects stop S-3402 (no smudge pass, S-194) |
| 3 | `ShadowLOD` | Off .. UltraHigh | decals = 1..2, volumes = 2, shadow mapping = >= 3 (+ 0x60 / 61 / 62) | Off disables shadows; Low .. UltraHigh enable cascaded directional shadow maps (Godot replacement for retail decals/volumes) |
| 4 | `TerrainLOD` | Low .. High | normal map >= 2, distance textures and ShowProps >= 1 (+ 0x49 / 4A / 1C), tank tracks of preset Low / Medium / High | stop S-3402 |
| 5 | `WaterLOD` | Low .. UltraHigh | + 0x1784; + 0x5E = >= 2; soft water edge >= 1 (+ 0x84) | stop S-3402 |
| 6 | `TextureQualityLOD` | Low .. High | TextureReductionFactor 2 / 1 / 0 (RW 0x517540) | applied: W3D textures lose mip levels (models loaded afterwards; retail reloads at once: S-3402) |
| 7 | `ShaderLOD` | Low .. UltraHigh | [RW 0xD9B058]; ShaderMaterialReplacement >= 2 | stop S-3402 |
| 8 | `DecalLOD` | Off .. High | + 0x9A5 = > 0, + 0x9A7 = < 2; Off forces every health bar (+ 0x9BE) | health bars applied; the decal style stop S-3402 |

The six rows marked stop S-3402 are greyed on the advanced page and take no
click (the screen finds each row by the label it binds, APT:AdvancedOption<i>);
their saved values and the presets still carry them. The three usable rows act
in part:

- **VFX Detail (EffectsLOD):** the particle cap and the two particle priorities
  take effect at once in a game; the heat effects it also turns on are not
  drawn (no smudge pass).
- **Texture Quality:** the texture reduction applies to the models loaded
  after the change, so it shows from the next game (or map) loaded; retail
  reloads the textures at once.
- **Decal Detail (DecalLOD):** Off shows every health bar (and greys the
  Show All Health Bars box); the selection decals' style is not drawn.

The presets VeryLow .. UltraHigh are GameLOD.ini's `StaticGameLOD` blocks
(RW 0x60252D); the records start with the constructor's values (RW 0x601745).
At start GameLODManager::init (RW 0x602165) takes `StaticGameLOD`, else
RW 0x4465D8 applies findStaticLODLevel (RW 0x602C03): `IdealStaticGameLOD`, else
the hardware detection and benchmark (stop S-2481: OpenBFME takes UltraHigh,
the picture it drew before, and says so).

## One Options menu (lane OPTS-5, the owner's decision of 2026-10-14)

"Let's scrap the original settings options and rework the menu to be our menu for now, but put the settings for graphics and options from the
original game in places that make sense for it in the new menu." Options is the OpenBFME menu below: the main menu's SETTINGS and CUSTOM
SETTINGS (this one opens on Graphics), the LAN lobby's and the online shell's Options and the quit menu's Options in a game all open it at once.
The retail basic page, its CUSTOM SETTINGS page and the OPENBFME button are not reachable. **The switch** is
`AptOptionsScreen::kOpenBFMEMenu` in `engine/src/GameClient/GUI/AptScreens/AptSimpleScreens.h`: `false` brings back OPTS-3's screen (the retail
basic page with the OPENBFME button); the retail screen's code is the same in both, stop S-4670.

The original game's settings are rows of the menu (keys `Retail.<gadget>`), driven by the retail screen's own code: Options.apt still runs with
its settings frames hidden, InitGadgets fills its gadgets from Options.ini as before, a row's change is that gadget's change plus its retail
handler, and ACCEPT / CANCEL / RESET TO DEFAULTS are RotWK's Save (RW 0x91FC9C) / Cancel (RW 0x91EC52) / Reset (RW 0x91F4D1), besides the
OpenBFME rows' own. So the keys, formats, ranges, defaults and live behaviour are the original game's, and an Options.ini the original game wrote
round-trips byte for byte (`opts5 options retail: a retail Options.ini round-trips ...`). Live as in RotWK: the five volumes with their samples
(OPTS-4) and, new here, the brightness (the gamma at once while the slider moves, RW 0x91F90D; Reset's RW 0x91F5F3 and Cancel's RW 0x91ECB8 set it
too). RESET TO DEFAULTS also sets the detail level the game takes without a setting (IdealStaticGameLOD, else the S-2481 stand-in Ultra High):
the custom settings page's own reset. In a game the detail rows are greyed with the movie's own reason (APT:CannotChangeGraphics), as retail
disables its Graphics list there; the online rows are enabled only when the online screens open Options (RotWK's AptOnline::Options, RW
0x91D6BA, opens it with NetworkEnabled 1).

| Row | Tab | Origin | Saved in |
|---|---|---|---|
| Window mode, Resolution, Vertical sync, Frame rate limit, Show the frame rate, Render scale, Upscaling | Display | OpenBFME (OPTS-1) | `openbfme-display.cfg` |
| Widescreen view | Display | OpenBFME (UI-6) | Options.ini `OpenBFMEWidescreen` |
| Brightness | Display | retail | Options.ini `Brightness` |
| Graphics (Very Low .. Ultra High, Custom) | Graphics (left) | retail | `StaticGameLOD` |
| Model, Animation, VFX, Shadows, Terrain, Water, Texture Quality, Shader, Decal Detail | Graphics (left) | retail (custom settings page) | `ModelLOD` .. `DecalLOD` (written for Custom, removed for a level) |
| Anti-aliasing, Anisotropic, Look, Soft particles | Graphics (right) | OpenBFME | `OpenBFMEAntiAliasing`, `OpenBFMEAnisotropic`, `OpenBFMELook`, `OpenBFMESoftParticles` |
| Master volume | Audio | OpenBFME | `OpenBFMEMasterVolume` |
| Music, Sound FX, Voice, Ambient, Movie | Audio | retail | `MusicVolume`, `SFXVolume`, `VoiceVolume`, `AmbientVolume`, `MovieVolume` |
| EAX3, High Audio Quality | Audio | retail (greyed: S-1913, S-2481) | `UseEAX3`, `AudioLOD` (kept) |
| Mute in the background, Output device | Audio | OpenBFME | `OpenBFMEMuteUnfocused`, `OpenBFMEAudioDevice` |
| Scroll Speed | Camera | retail | `ScrollFactor` |
| Edge scrolling, Rotation speed, Max zoom-out, Free camera, Invert drag scrolling, Keep the mouse in the window | Camera | OpenBFME | `OpenBFME*` |
| Show All Health Bars | Interface | retail | `AllHealthBars` |
| Health bars, Show the game clock, Interface scale | Interface | OpenBFME | `OpenBFMEHealthBars`, `OpenBFMEGameClock`, `OpenBFMEUIScale` |
| Alternate mouse setup | Interface | retail key, OpenBFME row (RotWK 2.01's screen has no box for it) | `AlternateMouseSetup` |
| Online IP, Port Number, Send Delay | Online | retail (enabled from the online screens) | `GameSpyIPAddress`, `FirewallPortOverride`, `SendDelay` |
| Refresh NAT | Online | retail (refused by name: S-1913) | |
| Uber performance mode | Performance | OpenBFME (UBER-1) | `OpenBFMEUberPerformance` |
| Hot keys (read only) | Controls | retail data (CommandMap.ini) | |

Why there: the brightness is a property of the picture, so Display; the detail level and its custom settings are what the retail screen calls
Graphics, with OpenBFME's renderer settings beside them in a second column (the tab has fourteen rows; two columns keep every row on screen);
the volumes with the master volume; Scroll Speed with the camera rows (the original game's "Controls" frame held only it; our Controls tab is the
hot key list); Show All Health Bars with the other health bar choice; the network settings on a tab of their own; Uber performance mode on its own
tab because it changes how the game itself runs (OPTS-2 had it on Graphics+). Interface scale moved from Graphics+ to Interface (it scales the
HUD's decorations, not the 3D picture).

**Help lines.** Every row has one. The original game's rows take the string file's text (the labels are in
`AptOptionsScreen::retailRowTable`, `engine/src/GameClient/GUI/AptScreens/AptSimpleScreens.cpp`, with OpenBFME's reason appended where it greys
a row); OpenBFME's rows take theirs from the `HELP` table of `godot/scripts/openbfme_options_screen.gd` (the one table of the menu's own texts, for
a later translation; the hover-text lane HOVER-1 can find both there).

| Row | Help label (lotr.str) |
|---|---|
| Brightness | TOOLTIP:Options/tooltipBrightness |
| Graphics | TOOLTIP:Options/tooltipDetail |
| Model Detail, Shader Detail | APT:WarnHighGraphicDetail (the custom settings page's own notice; no tooltip of their own) |
| Animation / VFX / Shadows / Terrain / Water / Texture Quality / Decal Detail | TOOLTIP:Options/tooltipShowAnimations / tooltipParticleCap / tooltip3DShadows / tooltipTerrainLighting / tooltipSmoothWaterBorder / tooltipTextureDetail / tooltipShowUnitDecals |
| Music / Sound FX / Voice / Ambient / Movie | TOOLTIP:Options/tooltipMusic / tooltipSoundFX / tooltipVoice / tooltipAmbient / tooltipMovie |
| EAX3 / High Audio Quality | ToolTip:Settings/EAX / ToolTip:Settings/HighQualityAudio (RotWK's AptButtonTooltipMap.ini) |
| Scroll Speed | TOOLTIP:Options/tooltipScrollSpeed |
| Show All Health Bars | TOOLTIP:Options/tooltipEnableHealthBars |
| Online IP / Port Number / Send Delay / Refresh NAT | TOOLTIP:OnlineIP / APT:PortNumHelp / TOOLTIP:SendDelay / TOOLTIP:RefreshNat |

The pairing of a row with a TOOLTIP:Options/tooltip<x> text is an inference (S-4670): RotWK 2.01's movie has tooltip areas only for EAX3, High
Audio Quality and Send Delay, but its string file still carries BFME2's texts for the others.

Proof: `workspace/rebuild/proof/OPTS-5/` (each tab at 2560 x 1440 and 1280 x 800, the main menu's and the in-game entry points).

## OpenBFME's screen (not retail)

Lane OPTS-3 made it a page of Options.apt, built from the menu's own pieces
(`GameClient/GUI/AptScreens/OpenBFMEOptionsPage.h`, stop S-3403):

- **The button.** OPENBFME is a copy of Options.apt's own CUSTOM SETTINGS
  button (`Main.FrameLeft.Advanced`, duplicated as `OpenBFMEEntry`: the same
  art, font and hover animation) in the free corner above CANCEL, the mirror
  of "Version 2.1" above ACCEPT. It covers no retail content (the test checks
  every text, frame and bottom bar button of the movie) and shows on the basic
  page only; the custom settings page has nothing of OpenBFME's.
- **The page.** It takes the place of the basic page's three settings frames
  (hidden while it is up): tabs on the left (the selected one on listbox.wnd's
  selection image), rows of label and retail gadget on the right
  (`window\apt\combobox.wnd`, `checkbox.wnd`, `horzslider.wnd`; the hot keys in
  `listbox.wnd`), each on the dark box with the 1 pixel outline Options.apt draws
  under its own combos and sliders, a help line for the row under the pointer,
  and OPENBFME SETTINGS as the subtitle. Frames, fonts and colours are
  Options.apt's (Albertus MT 18 white headers, 16 pale blue labels, 14 white
  help; frame lines 0.45 0.50 0.56), read from its draw ops.
- **The bottom bar** is the movie's own: CANCEL and ACCEPT go back to the basic
  page, RESET TO DEFAULTS sets the defaults; Esc is CANCEL. While the page is
  up the engine takes those clicks before the movie (Options.apt plays its
  closing animation before it sends Save / Cancel).
- **Native resolution.** The gadget layer draws the page like the movies'
  gadgets: stage coordinates mapped to the window, fonts rasterised at the
  window's pixel size. The OPTS-1/2 screen was Godot Controls laid out on the
  1024 x 768 stage and scaled up as a whole: its retail images were shrunk
  first (the check box to 26 pixels) and its 16 pixel texts were drawn at stage
  size and magnified, which is the softness the owner saw at 2560 x 1440.

Defaults are retail behaviour, except the Max zoom-out (the owner's 2.5x, lane
ZOOM-1); nothing here reaches the simulation (viewport, environment, audio
buses, camera, HUD decorations), except the Uber performance mode, which picks
the simulation profile of the player's next games (refused, never mixed, with
another profile). Tabs:

| Tab | Setting | Key | Default |
|---|---|---|---|
| Display | window mode, resolution, vsync, frame limit (a cap given another way shows as its own "custom" entry), counter, render scale (5% steps), upscaling (OPTS-1) | `openbfme-display.cfg` | windowed, 100%, bilinear |
| | Widescreen view: Original / Wider (UI-6, stop S-3702) | `OpenBFMEWidescreen` | original |
| Graphics+ | Anti-aliasing: Off / FXAA / MSAA 2x / 4x / TAA | `OpenBFMEAntiAliasing` | off |
| | Anisotropic filtering (models): Off .. 16x | `OpenBFMEAnisotropic` | 4x (what the renderer used) |
| | Look: original / enhanced (AgX tone mapping, glow on overbright effects) | `OpenBFMELook` | original |
| | Interface scale: 100 .. 200% (health bars, frame counter, clock) | `OpenBFMEUIScale` | 100 |
| | Soft particle edges (UI-2 / FX-3) | `OpenBFMESoftParticles` | yes |
| | Uber performance mode (UBER-1, `docs/UBER.md`): the next game and LAN lobby run the Uber simulation profile, which uses every CPU core and is not compatible with the original game (its help line says so); Uber players play and replay only with Uber players | `OpenBFMEUberPerformance` | no |
| Camera | Scroll speed | `ScrollFactor` (retail) | 50 |
| | Edge scrolling: full screen only / always / off | `OpenBFMEEdgeScroll` | retail |
| | Rotation speed 50 .. 200% | `OpenBFMERotateSpeed` | 100 |
| | Max zoom-out: Original (1x), 1.5x, 2x, 2.5x, 3x retail's highest camera (ZOOM-1, stop S-3501) | `OpenBFMEMaxZoomOut` | 2.5 |
| | Free camera: up to the map's size (Ctrl+Z in a game) | `OpenBFMEFreeCamera` | no |
| | Invert drag scrolling | `OpenBFMEInvertDragScroll` | no |
| | Keep the mouse inside the window (in a game) | `OpenBFMEConfineMouse` | no |
| Interface | Health bars: selected (retail) / selected and damaged / always (all players: every unit and building, infantry and cavalry included) | `OpenBFMEHealthBars` | retail |
| | Health bars on infantry and cavalry too | `AllHealthBars` (retail) | no |
| | Alternate mouse setup | `AlternateMouseSetup` (retail) | no |
| | Game clock | `OpenBFMEGameClock` | no |
| Audio | Master volume 0 .. 100 (live while it moves, with the sound effects sample: OPTS-4) | `OpenBFMEMasterVolume` | 100 |
| | Mute in the background | `OpenBFMEMuteUnfocused` | no |
| | Output device (Godot's list: Windows, Linux, macOS) | `OpenBFMEAudioDevice` | Default |
| Controls | the CommandMap's hot keys, read only | | |

### Max zoom-out and Always health bars (lane ZOOM-1)

- **Max zoom-out.** RotWK clamps the camera's height above the ground to the
  map's maximum (setHeightAboveGround RW 0x48CBFE; GameData
  DefaultCameraMaxHeight 300, or the map's cameraMaxHeight). The option
  multiplies that limit for the player's own zoom (the wheel and the keys); a
  game still starts at retail's height, and a reset (middle click), the camera
  lock and scripted cameras keep retail's heights. Beyond 1x the far plane and
  the fog ranges move out by the extra eye distance, so the far view is drawn
  and fogged as at 1x. `--zoom-out=<f>` sets it for one run. The camera's
  update and the simulation never read it (client state; sim_audit).
- **The map's edge** is retail's: W3DShroud (RW 0x472480 / 0x472EF4 /
  0x472792, tier A with the BFME2 decomp) draws a one-texel border at
  ShroudAlpha (black) around the shroud's cells, so the ground outside the
  playable area fades to black; ported for the terrain, the water and rivers
  (ZH W3DWater's shroud passes) and the roads (inference). Stop S-567.
- **Health bars, Always (all players).** Retail shows a bar only over the
  selected or moused-over objects (RW 0x676346); Show All Health Bars
  (`AllHealthBars`) only adds infantry and cavalry to those kinds. The
  OpenBFME choice "Always (all players)" shows every unit's and building's bar
  of every player without a selection, infantry and cavalry included, but
  only over what the local player sees, as retail's icon queue (RW 0x4853EB):
  nothing over a fogged or shrouded object (status above PARTIAL_CLEAR,
  RW 0x68D8F7), a garrisoned unit (its drawable is hidden, RW 0x67055A) or an
  enemy's invisible unit.
- Measured on Evendim (8 players, 7 hard computers, 7 minutes of game time,
  both armies in view; `--zoom1=bars,zoom,perf --vsync=off`, 1x / 2.5x
  alternated twice, Godot's measured viewport GPU and render-CPU time):

  | Machine | Window | Frame 1x / 2.5x | GPU 1x / 2.5x | Render CPU 1x / 2.5x |
  |---|---|---|---|---|
  | Deck (Linux, windowed) | 1280 x 800 | 9.11 / 8.61 ms | 0.84 / 0.93 ms | 0.49 / 0.49 ms |
  | the reference PC RTX 4090 (Windows export, below-normal priority) | 1280 x 800 | 28.09 / 26.61 ms | 0.28 / 0.31 ms | 1.55 / 1.62 ms |
  | the reference PC RTX 4090 | 2560 x 1440 | 29.18 / 29.88 ms | 0.45 / 0.48 ms | 1.62 / 1.80 ms |

  The far view adds about 0.1 ms of GPU time; the frame time is the game's
  main-thread work, the same at both heights, so no LOD or culling change was
  needed. (the reference PC's frame times were taken while another job rendered a
  video on the same PC.)

### Widescreen view (lane UI-6, the owner's FB-0021)

- **What RotWK does.** Its camera keeps a 50 degree horizontal field of view at
  every aspect (W3DView::setWidth RW 0x4874A7: 50 degrees times the view's
  share of the display width; CameraClass::Set_View_Plane RW 0x533590 and
  Set_Aspect_Ratio RW 0x533630: the view plane's height is its width divided
  by the aspect; tier A, byte-matched in the BFME2 decomp). A screen wider
  than 4:3 therefore sees less from top to bottom ("Vert-"): 38.6 degrees
  vertically at 4:3, 29.4 at 16:9, 22.6 at 21:9, 14.9 at 32:9. The HUD and the
  menus are Apt movies stretched to the window as RotWK stretches them (an
  x and a y scale of their own, the texts at the smaller one); the option does
  not change them.
- **Original** (the default) is exactly that. **Wider** keeps 4:3's vertical
  view (38.6 degrees) on screens wider than 4:3 and widens the horizontal one
  ("Hor+": 63.7 degrees across at 16:9); a 4:3 or narrower screen is retail's
  in both. TacticalCamera::setWidescreenView drives the picture, the picking,
  the radar's view box and the scroll constraints (all from one view). The
  camera is client state: the simulation, replays and multiplayer never read
  it (sim_audit). `--widescreen=original|wider` sets it for one run.

Left out, with the reason, in `docs/ROADMAP.md`: shadow map resolution (uses the renderer default), tooltip delay (the HUD's tooltips are not ported),
double-click guard (RotWK 2.01 never reads it), colour-blind player colours
(player colours are part of the game setup), hotkey rebinding.

## Proof

Lane UI-6: `workspace/rebuild/proof/UI-6/widescreen-<size>-<original|wider>.png`
(1920 x 1080, 2560 x 1080, 3440 x 1440, 5120 x 1440), the radar's view box at
2560 x 1080 in both, and the menus, the lobby and the game at 21:9 and 32:9.

`workspace/rebuild/proof/OPTS-3/<size>/` at 1280 x 720, 1920 x 1080, 2560 x 1440
and 3840 x 2160: `before-*` (the OPTS-2 tree, 709942a6) and `after-*` of every
tab and of the retail basic and custom settings pages, plus a zoomed pair at
2560 x 1440. A retail screenshot cannot be taken on the build machines; the
retail pages as OpenBFME draws them are the comparison. `tests/opts1_test.gd`
and `tests/opts2_test.gd` drive the page by its gadgets and real clicks;
`test_fb7_menus.cpp` "opts3" checks the button's place, the page's gadgets and
events, and the bottom bar.

ZOOM-1: `workspace/rebuild/proof/ZOOM-1/` (the same spot at 1x .. 3x, the map's
edge and Umbar's sea before / after the shroud border, the health bars retail /
Always).

# Options (lane OPTS-2)

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
| Music / Sound FX / Voice / Ambient sliders | `MusicVolume`, `SFXVolume`, `VoiceVolume`, `AmbientVolume` (`%f`, RW 0x6E5FB3 / 0x6E68F6) | TheAudio vslot 0xE8 on Save / Cancel | applied (FB7-1) |
| Movie slider | `MovieVolume` | the movie's volume | disabled (greyed; value kept): the movies' sound is audio events, no separate volume (S-1913) |
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
| 3 | `ShadowLOD` | Off .. UltraHigh | decals = 1..2, volumes = 2, shadow mapping = >= 3 (+ 0x60 / 61 / 62) | stop S-3402 (the renderer draws no shadows yet) |
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
buses, camera, HUD decorations). Tabs:

| Tab | Setting | Key | Default |
|---|---|---|---|
| Display | window mode, resolution, vsync, frame limit (a cap given another way shows as its own "custom" entry), counter, render scale (5% steps), upscaling (OPTS-1) | `openbfme-display.cfg` | windowed, 100%, bilinear |
| Graphics+ | Anti-aliasing: Off / FXAA / MSAA 2x / 4x / TAA | `OpenBFMEAntiAliasing` | off |
| | Anisotropic filtering (models): Off .. 16x | `OpenBFMEAnisotropic` | 4x (what the renderer used) |
| | Look: original / enhanced (AgX tone mapping, glow on overbright effects) | `OpenBFMELook` | original |
| | Interface scale: 100 .. 200% (health bars, frame counter, clock) | `OpenBFMEUIScale` | 100 |
| | Soft particle edges (UI-2 / FX-3) | `OpenBFMESoftParticles` | yes |
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
| Audio | Master volume 0 .. 100 | `OpenBFMEMasterVolume` | 100 |
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
  | JonathanPC RTX 4090 (Windows export, below-normal priority) | 1280 x 800 | 28.09 / 26.61 ms | 0.28 / 0.31 ms | 1.55 / 1.62 ms |
  | JonathanPC RTX 4090 | 2560 x 1440 | 29.18 / 29.88 ms | 0.45 / 0.48 ms | 1.62 / 1.80 ms |

  The far view adds about 0.1 ms of GPU time; the frame time is the game's
  main-thread work, the same at both heights, so no LOD or culling change was
  needed. (JonathanPC's frame times were taken while another job rendered a
  video on the same PC.)

Left out, with the reason, in `docs/ROADMAP.md`: shadow map resolution (no
shadows drawn yet), tooltip delay (the HUD's tooltips are not ported),
double-click guard (RotWK 2.01 never reads it), colour-blind player colours
(player colours are part of the game setup), hotkey rebinding.

## Proof

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

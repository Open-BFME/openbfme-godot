## OpenBFME: the game. The real entry point (the project's main scene).
##
## Boots the retail shell (AptLevel0 + MainMenu through the WindowManager / Shell, the native gadget layer drawn over the movies), lets the player open Skirmish and set up the
## lobby, and on Start: resolves the lobby's random choices in the logic (retail's order), pushes LoadScreen.apt with the real players' cards, loads the map synchronously
## while driving the load screen's bar with real progress (the logic's milestones, then terrain, object assets and live objects), pops the shell and shows the live game
## (GameWorld) with Palantir.apt loaded as the HUD.
##
##   godot --path godot                        the game
##   godot --path godot -- [options]           options after `--`:
##     --res=1280x720        window size
##     --seed=N              the lobby's game seed (default: the clock, like retail's GetTickCount)
##     --auto                scripted run: click MainMenu -> Skirmish with mouse events, set the lobby (--map/--faction/--ai/--color), click Start, play --advance seconds
##     --map=<key>           (with --auto) the map cache key, default maps/map mp evendim/map mp evendim.map
##     --faction=<Name>      (with --auto) slot 0's PlayerTemplate (default FactionMen); --ai=<2..5> slot 1's AI level; --color=<index> slot 0's colour (default random)
##     --advance=<seconds>   (with --auto) game time to run after the first frame, then quit
##     --free-camera         (lane PLAY-1) lift the camera's zoom-out limit to the map's extent (not retail; Options.ini OpenBFMEFreeCamera = yes does the same)
##     --zoom-out=<f>        (lane ZOOM-1) the Max zoom-out for the run: f times retail's highest camera, one of 1 (retail), 1.5, 2, 2.5, 3 (any other
##                           value: GAME FAIL, exit 2); without it Options.ini OpenBFMEMaxZoomOut (default 2.5)
##     --uber1=<troops>      (lane UBER-1, with --auto; an 8-player --map) scripts/uber1_player.gd: a battle of that many living troops in the real client,
##                           its render frames timed (--uber1-seconds=S, default 20): average fps, 1% low, worst frame, logic frames/s; prints UBER1 lines
##     --zoom1=<a,b,...>     (lane ZOOM-1, with --auto) the scenarios of scripts/zoom1_player.gd (zoom: the same spot at 1x / 1.5x / 2x / 2.5x / 3x, the
##                           frame rate at 1x and 2.5x; bars: both armies with every health bar on); prints ZOOM1 lines; --screens writes zoom1-*.png
##     --rules-shot=<n>      (lane RULES-1, with --auto --screens) the lobby's Rules tab is opened (rules1-rules-tab.png), Legendary Heroes n x is chosen
##                           (rules1-rules-tab-legendary.png), the game starts (the loading screen names the rule) and scripts/rules1_player.gd places a
##                           legendary and a plain hero side by side, damaged alike (rules1-heroes.png); "RULES1 RESULT" ends the run
##     --gamemodes=<mode>    (lane GAMEMODES-1, with --auto --screens; mode battle or sandbox, with --teams=0,0 for the sandbox) scripts/gamemodes1_player.gd:
##                           battle: the enemy's structures fall while its builders live, the victory screen follows (gamemodes1-battle-*.png); sandbox: the lobby
##                           as Start is pressed (gamemodes1-sandbox-lobby.png), the ally loses everything, no victory screen (gamemodes1-sandbox-*.png);
##                           "GAMEMODES1 RESULT" ends the run
##     --hud5=<a,b,...>      (lane HUD-5, with --auto) the owner's first Windows session's items, scripted (scripts/hud5_player.gd: fortress, players, powers,
##                           gate, construction, levels); "HUD5 <name>: ok / FAIL", screenshots hud5-<name>.png in --screens
##     --hud6=<a,b,...>      (lane HUD-6, with --auto) scripts/hud6_player.gd: radial (the fortress's bubbles), heroes (its hero ring), help (the
##                           help box over a bubble and over an arc button), sidebar (a builder's framed side bar), barracks (lane BUGS-1: the
##                           help box over a Men barracks' unit bubble keeps its text); hud6-<name>.png in --screens
##     --hover1=<a,b,...>    (lane HOVER-1, with --auto) scripts/hover1_player.gd: ability (a unit cost line), spellbook (a power's help box), herobar
##                           (a portrait's unit help); hover1-*.png in --screens
##     --hover1-menus=DIR    (lane HOVER-1) the main menu's and Options' tool tips (GuiFX.apt's ToolTip): the pointer on a button of each navigation and on
##                           Options' sound quality box, the window saved as DIR/hover1-menu-*.png after the tool tip's delay; then quit
##     --hud7=<a,b,...>      (lane HUD-7, with --auto) scripts/hud7_player.gd: radar (the view box and a radar click), deselect (a left click on empty
##                           ground), sell (a structure's Sell button), upgrade (the fortress's upgrade bubble in progress), herocancel (a hero recruited, then
##                           cancelled by a right click); hud7-<name>.png in --screens
##     --autocast1=<a,...>   (lane AUTOCAST-1, with --auto) scripts/autocast1_player.gd: witchking (a right click on the Witch-king's Morgul Blade
##                           button turns its autocast on, the button glows, he casts it at an enemy hero by himself; off again); autocast1-<name>.png
##     --start-spot=<n>      (with --auto) slot 0 clicks the lobby's start spot n first (RW 0x845830): e.g. 0 = Player_1_Start, a fortress map's fortress
##     --spell3=<a,b,...>    (lane SPELL-3, with --auto) scripts/spell3_player.gd: spellbook (buy every power in the store, cast every slot by input);
##                           "SPELL3 <name>: ok / FAIL", spell3-<faction>-*.png in --screens
##     --play1=<a,b,...>     (lane PLAY-1, with --auto) the input-driven scenarios of scripts/play1_player.gd (orders, palantir, powers, camera, explore, dragbox, ghosts,
##                           skirmish, tour, tribute): every action is an InputEvent pushed into the viewport; prints PLAY1 lines; with --end the end of the game
##                           follows, with --quit the quit menu's Exit to the score screen and its Continue back to the menu
##     --input1=<a,b,...>    (lane INPUT-1, with --auto) the keyboard scenarios of scripts/input1_player.gd (groups, ...): every key is an InputEventKey
##                           given to Input.parse_input_event; prints INPUT1 lines
##     --classic-mouse / --alternate-mouse  (lane PLAY-1) force the left / right click to order (default: Options.ini AlternateMouseSetup, else retail's right click)
##     --vsync=on|off|mailbox|adaptive, --max-fps=<n>   frame pacing (lane SMOOTH-1, scripts/frame_pacing.gd): default vsync on, no frame cap
##     --screens=<dir>       (with --auto) write start1-lobby.png, start1-loading.png and start1-game.png there
##     --check               (with --auto) no window assumptions: print the report lines and quit with the exit code (0 = no errors)
##     --report              print the load report as JSON
##   lane END-1, the end of the game:
##     --end                 (with --auto) after --advance seconds the enemy's base is destroyed (a test hook: start option test_hooks), the end sequence runs
##                           (victory screen, 7 s, hide), the game is left to the score screen (TimeLine.apt), Continue goes back to the main menu and a second
##                           game starts and runs 3 s; exit 0 when every step happened. --end-lose destroys the local player instead (the defeat screen).
##                           With --screens=<dir> it writes end1-<faction>-victory.png / -defeat.png, end1-<faction>-score.png and end1-menu.png
##     --options-shot=FILE   (lane UI-2) open Options from the main menu, save the window to FILE after 2 s and quit (the soft particles box)
##     --resize-check[=DIR]  (lane UI-4) resize / maximise the window over the main menu (nav closed and open) and send stray input around the nav
##                           entries; exit 0 when no widget state changed (tests/ui4_resize_test.gd; DIR: windowed screenshots)
##     --ui6-walk[=DIR]      (lane UI-6, scripts/ui6_walk.gd) a tester's menu findings with real input: the main menu, the lobby's map list, the
##                           Profile nav under the pointer, the lobby kept after the main menu, the power points in the game; DIR: windowed screenshots
##                           ui6-<step>.png; exit 0 when every step held
##     --widescreen=V        (lane UI-6) the Widescreen view for this run: original (RotWK's view) or wider (4:3's vertical view, wider to the
##                           sides); without it Options.ini OpenBFMEWidescreen (default original)
##     --menu1-walk[=DIR]    (lane MENU-1, scripts/menu1_walk.gd) the front-end paths with real input (Skirmish's profile prompt, the lobby, its Profile
##                           page, Options, LAN, Replays, Create-a-Hero ...): after each step at most one screen's widgets may be visible (the top one),
##                           the titles never overprint; DIR: windowed screenshots walk1-<step>.png; exit 0 when every step held
##     --menu-colours=DIR    (lane UI-4) the owner's "don't use the green text" (2026-10-10): every front-end screen the main menu's nav entries open
##                           (the boxes some open, Skirmish's profile box and lobby, Create-a-Hero's introduction and builder), each text drawn (the canvas's,
##                           the gadgets', the credits roll's) with its colour and a heuristic ink sample of its rectangle (DIR/colours-<screen>.png);
##                           exit 0 when no text is green-dominant and nothing was left unproven (a capture, a press, a screen without text)
##     --end-capture=DIR     (lane UI-2, with --end) save every rendered frame of the first 5 s of the end screen, half size, as DIR/fNNNN.png and the elapsed
##                           milliseconds of each in DIR/times.txt (the ring animation video)
##     In a live game Esc (or the Palantir's options button) toggles the quit menu QuitMenu.apt (lane END-2): Resume, Options, Restart / Forfeit, Exit
##   lane END-2, the quit menu (with --auto):
##     --quit                Esc opens the quit menu (the game pauses), Resume closes it, Esc and Exit with its confirmation leave to the score screen (its tabs
##                           and statistics page), Continue returns to the Skirmish lobby and a second game starts; --screens writes end2-*.png
##     --quit-restart        Esc, Restart and its confirmation start the same game again
##     --perf                (lane SMOOTH-1) while playing, print GAME PERF every 5 s: render fps, frame time mean / p95 / p99 / max and the C++ parts of the frame
##     --perf-stat=<prefix>  (lane PERF-3, with --perf; Linux with perf) `perf stat` counts the main thread's user instructions and cycles in each GAME PERF window
##                           (<prefix>-<n>.txt) and GAME PERF STAT prints them per render frame one window later: the work per frame, which other load on the
##                           machine does not change (unlike the frame time)
##     --fps                 (lane SMOOTH-1) show the render frame rate and frame time in a corner (lane OPTS-1: the counter of scripts/fps_overlay.gd, for the run)
##   lane OPTS-1, the OpenBFME display options (not retail; scripts/display_settings.gd, openbfme-display.cfg in the user data folder): Options.apt shows an
##   OPENBFME button that opens scripts/openbfme_options_screen.gd (window mode, resolution, vsync, frame cap, frame-rate counter, render scale, FSR);
##   F10 toggles the counter, Alt+Enter full screen (scripts/fps_overlay.gd)
##     --display-check       the scripted check of tests/opts1_test.gd: every window mode, resolution and scaler through the screen, with the main menu's
##                           and Options.apt's widget states compared before and after; F10 and Alt+Enter as key events; exit 0 when all held
##     --display-shots=DIR   (with --display-check / --display-game) opts1-entry.png, opts1-screen.png, opts1-fps.png, opts1-game-*.png (the proof screenshots)
##     --display-game        (with --auto) after --advance seconds: Esc, the quit menu's Options, the OPENBFME screen over the live game (the HUD takes no
##                           input while it is up; a resolution change keeps the menus' 1024 x 768 stage on the whole window), Esc back to the game; exit 0 when all held
##   lane OPTS-2, the Options menu: RotWK's detail level / brightness / scroll speed take effect, and the OpenBFME screen's tabs (scripts/game_options.gd)
##     --options-file=PATH   the Options.ini of the run (default: the user data folder's); the checks use one of their own
##     --opts2-check         the scripted check of tests/opts2_test.gd (scripts/opts2_check.gd): Options.apt's pages, every tab of the OpenBFME screen, the
##                           extras applied / written with the OpenBFME prefix / cancelled, no green label text; exit 0 when all held
##     --opts2-game          (with --auto and --options-file) after --advance seconds: the game clock, the camera's scroll factor, the detail's particle limits on
##                           the live FX player, the health bars' mode and Brightness's gamma pass in the live game; exit 0 when all held
##     --opts2-size=WxH      (with --opts2-check / --opts2-game) the window's size for the screenshots (1280x720, 2560x1440)
##     --opts4-check         the scripted check of tests/opts4_test.gd (scripts/opts4_check.gd): an open drop-down on top, the sliders' tick rows inside
##                           their windows, the volume sliders live with their samples; --opts4-size=WxH the window, --opts4-shots=DIR the screenshots
##     --opts5-check         the scripted check of tests/opts5_test.gd (scripts/opts5_check.gd): Options is the one OpenBFME menu, the original game's settings
##                           among its rows (their effects, Options.ini, CANCEL / RESET), the layout rules; --opts5-size=WxH, --opts5-shots=DIR
##     --opts5-game          (with --auto) after --advance seconds: the quit menu's Options opens the menu over the game (what may not change in a game greyed)
##     --opts2-shots=DIR     (with --opts2-check) opts2-<tab>-<w>x<h>.png and the Options.apt pages (the proof screenshots; a window, not --headless)
##     --opponents=<n>       (lane PERF-1, with --auto) slots 1..n are computers of level --ai (default 1; a 4-player map takes 3)
##     --warp=<seconds>      (lane PERF-1, with --auto) run the game at --warp-scale (default 8) times speed until this much game time has passed, then at normal speed
##                           for --advance seconds (the frame rate of a late game: GAME PERF restarts when the warp ends)
##   lane QA-1, the scripted player (scripts/qa_player.gd; tools/qa/qa_matrix.py runs the matrix):
##     --qa                  (with --auto) after the start the local side is played by the scripted human through the HUD's input (builds, trains, upgrades,
##                           casts, attacks) for --qa-minutes of game time (default 20) at --qa-speed times speed (default 4), then the game is left the way
##                           a player leaves it (victory: the end screen, then the quit menu's Exit; else Esc, Exit, Yes) to the score screen, whose
##                           Continue must return to the lobby. Prints QA ISSUE / QA SUMMARY lines; exit 0 when the flow completed (issues are data)
##     --qa-idle             (with --qa) the local side does nothing (an AI-vs-AI game; --teams=0,0,1,1 puts the slots on teams)
##     --qa-cash=<n>         (with --qa) a test hook gives the local player n once at the start (so a short run can win; logged as QA TEST HOOK)
##     --qa-tag=<name>       the run's name in the screenshots (--screens: qa1-<tag>-*.png)
##     --teams=<t0,t1,...>   (with --auto) the lobby Team of each slot in order (-1 = none)
##     --qa-scene=<name>     (lane QA-2, with --qa) instead of playing, one feedback scene (scripts/qa_scene.gd: melee, archers, troll, rohirrim, charge,
##                           garrison, grond, wall) is set up by a logged test hook, ordered through the HUD's input and recorded (--screens)
##   lane MP-1, a LAN game without the shell's lobby screens (GameNetwork/LANLobby.h: join by address; the APT LAN lobby is NET-2):
##     --net-host=PORT       host a game: --map, --seed, --net-slots=kind:Faction:start:team;... (kind human / easy / medium / hard / brutal; the
##                           first human slot is the host's), --net-run-ahead=N, --net-crc=N (CRC interval)
##     --net-join=IP:PORT    join the game hosted there (--net-name=Name)
##     --net-script          the local player is the scripted test player (GameNetwork/ScriptedPlayer.h)
##     --net-record=FILE     record the game (Common/Recorder.h)
##     --net-frames=N        quit after logic frame N, printing the network status (exit 2 on a desync)
##     --net-camera=x,y,z    look there at the start instead of the local start (Godot axes; the demo shows one place in both windows)
##     --net-overlay         draw the slot, the logic frame, the state hash and the CRC checks over the game
##     --net-end             (lane END-1) at --net-frames the LAN game is left like a finished game (clear_game_data to the score screen, which must be type 3), Continue
##                           goes to the main menu and a skirmish starts: its local slot must be slot 0 (the finished game's network state is gone); exit 0 then
##     --net-capture=DIR     save the window as DIR/f<logic frame>.png once per logic frame (the two-window demo video is put together by frame number)
##   lane MP-2, the disconnect path (GameNetwork/DisconnectManager.h): a silent peer brings up DisconnectScreen.apt (names, timeout bars, Kick, Quit); a dropped
##   player's "Network:PlayerLeftGame" notice; dropped / alone / Quit leaves the game to the score screen
##     --net-disconnect-ms=N / --net-player-timeout-ms=N   replace GameData's NetworkDisconnectTime / NetworkPlayerTimeoutTime (tests, demos)
##     --net-kick            press Kick for every player the disconnect screen offers it for
##     --net-shot-disconnect=FILE   save the window once the disconnect screen has been up for 2 s (the media)
##     --net-shot-desync=FILE       save the window 1 s after the desync message box came up (the media)
##   lane MP-2, replays: every skirmish and LAN game is recorded to <user data>/Replays/<GUI:LastReplay>.replay (Common/Recorder.h); the main menu's Load Replay
##   opens SaveLoad.apt's replay page (AptSaveLoad: the files of that folder) and Load plays the chosen one back (GameWorld.prepare_replay / start_replay)
##     --replay=FILE         play FILE back at once (no menu); --replay-check: quit when it ends, exit 0 when every frame's hash matched
##     --replay-speed=N      (lane QA-2) play the replay at N times speed (the QA matrix checks every game's replay this way)
##     --replay-watch=<Player>:<Template>   (lane QA-2) while a replay plays, every 50 logic frames print each of the player's objects of that template
##                           (position, conditions, production queue) and the player's money / command points: QA-2's reproductions
##     --replay-menu-test    (with --auto) after the game, open Load Replay, screenshot it (--screens), load the first replay and play it to the end
##     --no-record           do not record the games
##   lane MP-2, the LAN lobby: the main menu's LAN opens LanLobby.apt on GameWorld's LAN lobby (GameNetwork/LANAPI.h: RotWK's lobby ports 8086 ..
##   8093, broadcast); every player's start (the shell request "LanGameStart") loads the network game with net_begin as --net-host / --net-join do
##     --lan=host|join       script the lobby through the movie's commands: open LAN from the main menu; host: Create Game, the map (--map), the own faction
##                           (--faction), --lan-ai=N medium AIs after the players, Play Game once --lan-players=N humans are in and accepted; join: Join of the first
##                           game listed, the own faction, Accept (again after every options change). --net-name=Name, --net-script, --net-frames as above
##     --lan-targets=a.b.c.d,...   send the lobby's messages there instead of broadcasting (tests on one machine: 127.0.0.1); --lan-port-base=N (8086)
##     --lan-shot=FILE       (host) save the window when the lobby is complete, before Play Game (the media)
##     --lan-shot-start=FILE (host, lane UI-1) save the window 8 s after Play Game: the start countdown's QM:STARTINGGAME box (the media)
##     --net-shot=FILE:FRAME save the window once the network game reached logic frame FRAME (the media)
##     --net-press=TEMPLATE  (lane NETLAG-1) from logic frame 20 press the HUD button that produces TEMPLATE on the first own object showing it, then print
##                           "GAME NET PRESS ... queued at logic frame F: N frames, M ms after the press" once the unit is in its producer's queue
##
##   lane ONLINE-2, online play through the relay (a test feature; docs/ONLINE.md phase O1, scripts/online_lobby.gd): the lobby service builds the START, every
##   peer plays behind the relay (GameWorld.net_online); the game then loads and runs as a LAN game does (--net-script, --net-frames, --net-shot work)
##     --online-start-file=FILE   lane MP-DROP-1 (tests without a lobby): play the START in FILE (a signed one, tools/net/online_client.py fixture)
##     --online-join=ID      join that game of the lobby (also openbfme://join/ID, the launcher's link), say ready, play when the host starts it
##     --online-host         host a game on the lobby: --map, --online-slots=STATE:faction:start:team;... (STATE player / open / closed / easy / medium /
##                           hard / brutal, faction a player template index or -1), starts once --online-players=N humans are in and ready
##     --online-lobby=URL    the lobby service (default: the OPENBFME_LOBBY environment variable); --online-name=NAME the guest name
##   lane ONLINE-3 (reconnect and pause, docs/ONLINE.md "Phase O3"; test features):
##     --online-rejoin[=ID]  the game restarted: rejoin the player's running game (the saved lobby session; the lobby's rejoin START), re-simulate it from
##                           the relay's log ("Rejoining" on screen), then play on
##     Pause key             pauses a network game for every player (OpenBFME addition, S-3901); Pause again (any player) resumes
##     --net-pause-at=FRAME / --net-resume-after-ms=MS   the same from the command line (tests); --net-shot-pause=FILE / --net-shot-rejoin=FILE save the
##                           window once the game has been paused / rejoining for 2 s / 0.5 s (the proof pictures)
##     --online-spectate=ID  watch the game ID as a spectator (the lobby's spectator START; waits for the host to start it; also mid-game): the whole map
##                           without shroud or fog (retail's ReplayObserver side), caught up from the relay's log, then followed, the host's spectator
##                           delay behind; Tab / Shift+Tab observe the next / previous player (retail's observeNextPlayer: that player's view, then
##                           every player's again) with its resources on screen; nothing the spectator does reaches the game
##     --net-observe-at=FRAME  observe the next player once the spectator reaches FRAME (tests); --net-shot-spectate=FILE / --net-shot-observe=FILE save
##                           the window 3 s after the spectator is live / observes a player (the proof pictures)
##                           (OPENBFME_LOBBY_TOKEN: a lobby session token instead of a new guest; never sent to another origin: no redirects)
##   lane ONLINE-4, the online screens (a test feature; docs/ONLINE.md section 5): the main menu's Online opens RotWK's OnlineShell.apt (guest sign-in, the
##   home page with the lobby's message of the day, Open Play = the server browser, the MpGameSetup staging room) on the lobby service (scripts/online_http.gd).
##   The lobby: --online-lobby=URL, else OPENBFME_LOBBY, else Options.ini's OpenBFMEOnlineLobby, else https://play.openbfme.com. The guest name of the last
##   sign-in is kept in Options.ini (OpenBFMEOnlineName).
##     --online-walk=host|join   script the screens through the movie's own commands: Online, the sign-in (--online-name), Open Play; host: Create Game
##                           (--online-password=PW), Play Game once --online-players=N players are in and ready; join: the first game listed (sorted by name),
##                           its password, chat, Ready. --online-shots=DIR saves every screen (online-<screen>-<W>x<H>.png)
##
##   lane CAMP-1, the campaigns (scripts/campaign_flow.gd): the main menu's campaign commands (Expansion1Campaign, BonusCampaign, ContinueCampaign) play the
##   linear campaigns mission by mission
##     --campaign=NAME       start the campaign at once (no menu): ANGMAR_CAMPAIGN, ANGMAR_BONUS_CAMPAIGN; --mission=N its N-th mission (from 0);
##                           --difficulty=E|N|H
##     --campaign-win        (test hooks) each mission's decisive moment is played by the GameWorld test hooks (Amon Sul, Fornost, the bonus mission)
##     --campaign-check=N    quit after N missions ended (exit 0 when all were won), printing the CAMPAIGN lines
##     --play1=<a,b,...>     (lane SELPERF-1, with --campaign) the play1 scenarios in the mission once it plays (--campaign-play1-wait=<s>, default 20), then quit
##     --movies=off          (lane CAMP-1H) the campaign movies (intro, mission intros, PLAY_MOVIE_IN_GAME: their narration over black, S-1710) are not played
##     --campaign-menu=CMD[:ARG] (lane CAMP-2) once the main menu is shown, run its command AptMainMenu::CMD (Expansion1Campaign:N, BonusCampaign:N,
##                           ContinueCampaign), as its button does
##     every game's start and every return to the shell print "GAME SHELL PICTURES <where>: ..." (lane CAMP-2: the backdrop and the front-end background as
##     the canvas drew them, and any developer text on screen; tests/camp2_test.gd)
##     --shell-pictures-quit=N (lane CAMP-2) quit (exit 0) once N SHELL PICTURES lines were printed
##     --intro / --no-intro  (off by default) the start-up movies (EALogoMovie, NewLineLogo, TolkienLogo, Overall_Game_Intro: RW 0x645B8D / 0x64838D,
##                           when GameData PlayIntro): by default they play in a windowed start without automation options (retail: not with a
##                           .map file to start or headless, RW 0x63C9BB / 0x63CC07); --intro plays them anyway, --no-intro / --movies=off never
##     --intro-check         (lane CAMP-2, tests) play the start-up movies, then quit (exit 0 when all played)
##     --movie-file=TITLE=PATH (lane CAMP-2, tests) the movie TITLE opens PATH instead of its file (a damaged or missing movie)
##     --movie-skip-after=MS (lane CAMP-2, tests) every movie is skipped MS milliseconds after its start, as a player's Esc
##     --dev-overlay         (lane CAMP-2) the developer overlays of the campaign presentation (the logic frame, "input disabled", the objective list);
##                           the real game draws no developer text (tests/textscan: tools/check_dev_overlays.py)
##
##   lane CAH-1, Create-a-Hero: the main menu's Create-a-Hero opens CreateAHero.apt over its map mode (scripts/create_a_hero_view.gd); the heroes are the system
##   heroes of the archives and the profile's MyHero_<id>.cah in retail's save folder (--cah-profile=DIR); the Skirmish / LAN lobby's Hero combo picks one for the slot
##     --cah                 (with --auto) build a hero in the builder through the movies' own functions and commands (class, appearance, name, powers), save it,
##                           back to the main menu, pick it in the Skirmish lobby's Hero combo (with --faction), start, recruit it at the fortress; exit 0 when the
##                           hero was saved, chosen, installed and made. --screens writes cah1-*.png. Lane CAH-2 r2: the run saves into a temporary folder
##                           of its own (--cah-real-profile: the real save folder) and removes its hero and that folder at its end
## HOOKS OF OTHER LANES (leave the names as they are):
##   _on_shell_service(kind, a, b)   every ShellServices call: kind "sound" (a = the sound name) is AUDIO-1's hook for UI sounds and music cues; "load_music" carries the faction's
##                                   LoadScreenMusic when a load starts; "background", "mouse_visible", "tooltip" are the others
##   _on_shell_request(action, arg)  the main-menu actions the shell hands to the engine (ExitGame, Lan, LoadGame ...)
##   _install_hud()                  HUD-1's install point: called once the live game exists and Palantir.apt is up; it gets the GameWorld, the AptMenuPlayer and the local player
extends Node

const DEFAULT_MAP := "maps/map mp evendim/map mp evendim.map"
const LEVEL_MAIN_MENU_FRAMES := 20
const KEEP_RENDERING := true

enum State { BOOT, MENU, LOADING, PLAYING, SCORE, FAILED }

var _res := Vector2i(0, 0)
var _seed := 0
var _auto := false
var _check := false
var _print_report := false
var _map_key := DEFAULT_MAP
var _faction := "FactionMen"
var _ai := 3
var _color := -1
var _advance := 3.0
var _screens := ""
var _perf := false           # lane SMOOTH-1: --perf / --fps
var _opponents := 1          # lane PERF-1: --opponents / --warp / --warp-scale
var _warp := 0.0
var _warp_scale := 8.0
var _perf_times: Array = []
var _perf_cpu0 := -1.0       # lane PERF-1: the main thread's CPU time at the start of the GAME PERF window
var _perf_windows := {}      # lane RENDERWAIT-1: the client window counts at the start of the GAME PERF window
var _perf_stat := ""          # lane PERF-3: --perf-stat=<prefix>
var _perf_log_path := ""      # lane MP-3: --perf-log=<file>: every rendered frame of the game, "<logic frame> <frame time us>" (tools/net/mp3_perf_table.py)
var _perf_log: FileAccess = null
var _perf_log_prev_us := 0
var _net_census := ""         # lane MP-3: --net-census=<file>: the network game's per-logic-frame census CSV, written when the game ends
var _perf_stat_pid := -1      # the perf stat of the current window
var _perf_stat_index := 0
var _perf_stat_done := {}     # the finished window's { file, frames }, read at the next window's end
var _perf_deltas: Array = []
var _perf_parts := {}
var _perf_last := 0
var _perf_prev_us := 0
const DisplaySettings := preload("res://scripts/display_settings.gd") # lane OPTS-1
const OnlineStats := preload("res://scripts/online_stats.gd") # lane ONLINECOUNT-1
var _display: RefCounted                 # lane OPTS-1: the display settings in effect (openbfme-display.cfg)
var _display_path := ""
var _display_overlay: CanvasLayer        # scripts/fps_overlay.gd: the counter and the F10 / Alt+Enter keys
var _display_screen: CanvasLayer         # scripts/openbfme_options_screen.gd
var _game_options: Node                  # lane OPTS-2: scripts/game_options.gd, the retail detail / brightness / scroll and the OpenBFME extras in effect
var _display_check := false              # --display-check
var _display_shots := ""                 # --display-shots=DIR
var _options_file := ""                  # lane OPTS-2: --options-file=PATH
var _opts2_check := false                # lane OPTS-2: --opts2-check
var _opts2_game := false                 # lane OPTS-2: --opts2-game
var _opts2_shots := ""                   # lane OPTS-2: --opts2-shots=DIR
var _opts2_size := Vector2i.ZERO         # lane OPTS-2: --opts2-size=WxH (the screenshots' window size)
var _opts4_check := false                # lane OPTS-4: --opts4-check
var _opts4_shots := ""                   # lane OPTS-4: --opts4-shots=DIR
var _opts4_size := Vector2i.ZERO         # lane OPTS-4: --opts4-size=WxH
var _opts5_check := false                # lane OPTS-5: --opts5-check
var _opts5_game := false                 # lane OPTS-5: --opts5-game
var _opts5_shots := ""                   # lane OPTS-5: --opts5-shots=DIR
var _opts5_size := Vector2i.ZERO         # lane OPTS-5: --opts5-size=WxH
var _display_game := false               # --display-game

var _fs: RefCounted
var _audio: Node
var _world: Node3D           # GameWorld
var _map_editor: CanvasLayer
var _lua_console: CanvasLayer
var _console_input_states: Array[Dictionary] = []
var _shell: Node2D           # AptMenuPlayer
var _camera: Camera3D
var _env: WorldEnvironment
var _sun: DirectionalLight3D
var _quit_q_time := -1
const DOUBLE_Q_MS := 450
var _state := State.BOOT
var _frames := 0
var _menu_revealed := false
var _report := {}
var _failed := ""
var _loading_shot_done := false
var _load_log: Array = []
var _hud_installed := false
var _hud: Node
## lane PLAY-1: the mouse setup forced on the command line ("alternate": the right click orders, "classic": the left click orders, "": the player's setting)
var _mouse_override := ""
var _move_hints: Node3D
## lane PLAY-1: the free camera (the owner's presentation option, not retail): --free-camera or Options.ini "OpenBFMEFreeCamera = yes"
var _free_camera_flag := false
## lane ZOOM-1: --zoom-out=<f> (0 = Options.ini's OpenBFMEMaxZoomOut)
var _zoom_out_flag := 0.0
var _zoom_out_error := "" # lane ZOOM-1 r2: a --zoom-out value the option does not take
## lane UI-6: --widescreen=original|wider ("" = Options.ini's OpenBFMEWidescreen)
var _widescreen_flag := ""
## lane PLAY-1: the input-driven scenarios of scripts/play1_player.gd (--play1=orders,palantir,powers,camera)
var _play1 := PackedStringArray()
var _zoom1 := PackedStringArray() # lane ZOOM-1
var _play_script := "res://scripts/play1_player.gd" # lane SPELL-3: --spell3 runs its scenarios through scripts/spell3_player.gd (PLAY-1's input helpers)
var _uber1 := 0 # lane UBER-1: --uber1=<troops>
var _hud5 := PackedStringArray() # lane HUD-5
var _rules_shot := 0 # lane RULES-1: --rules-shot=<factor>
var _gamemodes := "" # lane GAMEMODES-1: --gamemodes=battle|sandbox
var _hud6 := PackedStringArray() # lane HUD-6 (scripts/hud6_player.gd: radial, heroes, sidebar, help)
var _hover1 := PackedStringArray() # lane HOVER-1 (scripts/hover1_player.gd: ability, spellbook, herobar)
var _hover1_menus := "" # lane HOVER-1: --hover1-menus=DIR
var _hover1_menus_started := false
var _hud7 := PackedStringArray() # lane HUD-7 (scripts/hud7_player.gd: radar, deselect, sell, upgrade, herocancel)
var _autocast1 := PackedStringArray() # lane AUTOCAST-1 (scripts/autocast1_player.gd: witchking)
var _start_spot := -1            # lane HUD-5
## lane INPUT-1: the keyboard scenarios of scripts/input1_player.gd (--input1=groups,...)
var _input1 := PackedStringArray()
var _game_frames := 0
var _auto_task_started := false
# lane ONLINE-2
var _online_join := ""
var _online_host := false
var _online_lobby := ""
var _online_name := ""
var _online_slots := "player:-1:0:0;open"
var _online_players := 2
var _online: Node = null
# lane ONLINE-4
var _online_http: Node = null
var _online_walk := ""
var _online_start_file := "" # lane MP-DROP-1: --online-start-file=FILE
var _online_shots := ""
var _online_password := ""
var _online_saved_name := ""
var _capture_dir := "" ## --capture-dir=DIR (lane ONLINE-4): the window saved as DIR/c<unix ms>.jpg about 15 times a second, wall clock (a real-time
var _capture_next := 0  ## recording of a networked game: Movie Maker's --write-movie runs off the wall clock); the frames are put together afterwards
var _online_note: CanvasLayer = null ## "Online play is a test feature", shown while OnlineShell.apt is up
var _stats: Node = null              ## lane ONLINECOUNT-1: scripts/online_stats.gd (null: not asked for: off, or not made yet)
var _stats_off := ""                 ## why the count is not asked for ("" = it is); set once, when the menu first shows
var _stats_label: Label = null       ## the main menu's / the online menu's "N players online" line
const ONLINE_DEFAULT_LOBBY := "https://play.openbfme.com"
const ONLINE_SESSION_FILE := "user://online_session.txt" # lane ONLINE-3's file (scripts/online_lobby.gd SESSION_FILE)
var _online_saved_session := ""
var _online_rejoin := false        # lane ONLINE-3
var _net_pause_at := -1
var _net_resume_after_ms := -1
var _net_shot_pause := ""
var _net_shot_rejoin := ""
var _pause_label: Label = null
var _pause_since := 0
var _pause_key_down := false
var _rejoin_since := 0
var _online_spectate := false      # lane ONLINE-3: spectators
var _net_observe_at := -1
var _net_shot_spectate := ""
var _net_shot_observe := ""
var _spectate_label: Label = null
var _spectate_state := ""
var _spectate_live_ms := 0
var _observe_ms := 0
var _observe_key_down := false
# lane MP-1
var _net_host := -1
var _net_join := ""
var _net_name := "Joiner"
var _net_slots := "human:FactionMen:0:0;human:FactionMordor:1:1"
var _net_run_ahead := 1  # lane NETLAG-1: RotWK's command delay, the current frame + 1 (was 2)
var _net_crc := 100
var _net_script := false
var _net_record := ""
var _net_frames := -1
var _net_camera := ""
var _net_overlay := false
var _net_capture := ""
var _net_captured := -1
var _net_last_frame := -1
var _net_state := ""
var _net_label: Label
var _net_last_print := -1
var _net_end := false
var _net_end_started := false
var _net_disconnect_ms := -1      # lane MP-2
var _lan_mode := ""
var _lan_players := 2
var _lan_ai := 0
var _lan_targets := ""
var _lan_port_base := -1
var _lan_shot := ""
var _end_capture := "" # lane UI-2: --end-capture=DIR
var _options_shot := "" # lane UI-2: --options-shot=FILE
var _resize_check := "" # lane UI-4: --resize-check[=DIR]: the screenshots' folder
var _resize_check_on := false
var _menu_colours := "" # lane UI-4: --menu-colours=DIR
var _menu_colours_problems: PackedStringArray = [] # what the walk could not prove (a capture, a press, a screen without text)
var _menu_walk := "" # lane FB7-1: --menu-walk=DIR
var _menu1_walk := "" # lane MENU-1: --menu1-walk=DIR (scripts/menu1_walk.gd)
var _menu1_walk_on := false
var _menu1_runner: RefCounted = null
var _ui6_walk := "" # lane UI-6: --ui6-walk=DIR (scripts/ui6_walk.gd)
var _ui6_walk_on := false
var _ui6_runner: RefCounted = null
var _menu_walk_only := "" # lane CAH-2 r2: --menu-walk-only=BUTTON (one button, the probe)
var _menu_walk_drop := "" # lane CAH-2 r2: --menu-walk-drop=ACTION (TEST HOOK: the host ignores that shell request, so the walk must fail)
var _walk_failures := 0
var _walk_finished := false
var _walk_logger: Object = null
const WALK_WATCHDOG_SECONDS := 1500
var _options_shot_frames := 0
var _lan_phase := 0
var _lan_wait := 0
var _lan_shot_done := false
var _lan_start_ms := -1
var _lan_shot_start := "" # lane UI-1: (host) the window 8 s after Play Game: the countdown's QM:STARTINGGAME box (the media)
var _net_shot := ""
var _net_shot_frame := -1
# lane NETLAG-1: --net-press=TEMPLATE: the command delay through the HUD (a build button pressed, the frame and the time its unit reaches the queue)
var _net_press := ""
var _net_press_state := 0 # 0 not yet, 1 pressed (waiting for the queue), 2 done, 3 an object selected (the control bar shows its buttons next frames)
var _net_press_candidates: Array = []
var _net_press_wait := 0
var _net_press_producer := -1
var _net_press_frame := -1
var _net_press_ms := 0
var _net_press_queue := 0
var _net_player_timeout_ms := -1
var _net_kick := false
var _net_shot_disconnect := ""
var _dc_shown := false
var _dc_since := 0
var _dc_shot_done := false
var _dc_left_seen := 0
var _dc_log_seen := 0 # lane MP-DROP-1: the disconnect path's log lines already printed
var _dc_quit_handled := false
var _desync_shown := false
var _net_shot_desync := ""
var _replay_file := ""          # lane MP-2
var _replay_check := false
var _replay_speed := 1.0
var _replay_watch := ""
var _replay_watch_next := 0
var _replay_menu_test := false
var _record := true
var _replay_active := false
var _replay_last_print := -1
var _last_local_slot := -1
var _save_flow: RefCounted = null # lane SAVE-2: the saved games (scripts/save_flow.gd)
var _save_dir_arg := ""          # lane SAVE-2: --save-dir=DIR (the Save folder; default the user data folder's Save, as Create-a-Hero's)
var _save_test := ""             # lane SAVE-2: --save-test (with --auto: save in the skirmish, load it back, the main menu's Load Game; with --campaign: a mission)
# lane END-1
var _end := false
var _end_lose := false
var _end_esc := false
var _end_shown := false
var _profile_made := false
var _games_started := 0
var _end_log: Array = []
var _end_message_layer: CanvasLayer
var _end_message_label: Label
var _end_message_until := 0
var _rules_layer: CanvasLayer # lane RULES-1: the OpenBFME rules line over the loading screen
var _graph_layer: CanvasLayer
var _graph_node: Control
var _score_report := {}
var _continue_pressed := false
var _local_name := ""
var _local_index := -1 # lane UI-4: the local player's index (GameWorld.create_object's player)
var _second_game_ok := false
var _net_lobby_again := false
var _continuing := false
var _campaign_flow: Node        # lane CAMP-1: scripts/campaign_flow.gd
var _campaign_complete := false
var _cli_campaign := ""
var _cli_mission := 0
var _cli_difficulty := 1
var _campaign_win := false
var _campaign_check := -1
var _campaign_movies := true    # lane CAMP-1H: --movies=off
var _campaign_menu := ""        # lane CAMP-2: --campaign-menu=CMD[:ARG]
var _game_kind := ""            # lane CAMP-2: how the game being entered started (the SHELL PICTURES lines)
var _shell_pictures_pending := 0 # lane CAMP-2: reports still waiting for their frames (--campaign-check quits after them)
var _shell_pictures_seen := 0
var _shell_pictures_start_done := false
var _shell_pictures_quit := 0    # lane CAMP-2: --shell-pictures-quit=N
var _movie_files := {}          # lane CAMP-2: --movie-file=TITLE=PATH (tests: the file a movie title opens)
var _movie_skip_after := -1     # lane CAMP-2: --movie-skip-after=MS (tests: every movie is skipped MS after its start, as by Esc)
var _intro := "off"            # lane CAMP-2: --intro (on) / --no-intro (off)
var _intro_check := false       # lane CAMP-2: --intro-check (tests): the start-up movies, then quit
## lane MENU-1: the options a player's own start may carry (the launcher's, the display options); any other option makes a scripted run
const PLAYER_ARG_PREFIXES := ["--fps", "--dev-overlay", "--intro", "--no-intro", "--alternate-mouse", "--classic-mouse", "--free-camera", "--res=",
	"--vsync=", "--max-fps=", "--movies=", "--zoom-out=", "--widescreen=", "--online-lobby=", "--no-update-check", "--game=", "--bfme1-install="] # --zoom-out: ZOOM-1, --widescreen: UI-6, player options (PLAYING.md)
var _scripted_skirmish_ini := ""


func _exit_tree() -> void:
	if not _scripted_skirmish_ini.is_empty():
		DirAccess.remove_absolute(_scripted_skirmish_ini) # lane MENU-1: the scripted run's own Skirmish.ini


func _scripted_run() -> bool:
	for arg in OS.get_cmdline_user_args():
		var player := false
		for p in PLAYER_ARG_PREFIXES:
			player = player or String(arg).begins_with(p)
		if not player:
			return true
	return false


const INTRO_QUIET_ARGS := ["--fps", "--dev-overlay", "--intro", "--alternate-mouse", "--free-camera"] # options that are not automation
var _dev_overlay := false       # lane CAMP-2: --dev-overlay (developer text on screen; never in the real game)
var _control_bar_hidden := false # lane CAMP-2: HideControlBar / ShowControlBar (RW 0x804576 / 0x8046A1) of the script's letterbox and HIDE_UI / SHOW_UI
var _qa := false                # lane QA-1
var _qa_idle := false
var _qa_minutes := 20.0
var _qa_speed := 4.0
var _qa_scene := ""
var _qa_tag := "qa"
var _qa_cash := 0
var _teams := PackedStringArray()
var _quit_flow := ""            # lane END-2: --quit / --quit-restart (the scripted quit menu)  # lane END-2: Continue after a LAN game opens the LAN lobby again
var _cah: Node = null           # lane CAH-1: the Create-a-Hero builder's 3D view while CreateAHero.apt is up
var _cah_profile := ""          # lane CAH-1: --cah-profile=DIR (the folder of the profile's MyHero*.cah; default <user data>/Save)
var _cah_demo := false          # lane CAH-1: --cah (the scripted builder -> lobby -> game run)
var _cah_builder_only := false  # lane CAH-1: --cah-builder-only (quit after the builder part)
var _cah_real_profile := false   # lane CAH-2 r2: --cah-real-profile (the --cah run writes the player's real save folder; default: a temporary one)
var _cah_temp_root := ""        # lane CAH-2 r2: the --cah run's temporary user data folder (removed at its end)
var _rotwk_install := ""        # lane CAH-2: the mounted RotWK install folder (its gi.dat names the user data folder)
var _cah_hero_id := ""          # lane CAH-1: the unique id of the hero the --cah run saved


var _startup_shortcut: Node

func _ready() -> void:
	if OS.has_feature("ios"):
		var keyboard := preload("res://scripts/release/ios_keyboard.gd").new()
		keyboard.shell = func() -> Node: return _shell
		add_child(keyboard)
	print("STARTUP game ready at %d ms" % Time.get_ticks_msec())
	_startup_shortcut = preload("res://scripts/startup_shortcut.gd").new()
	add_child(_startup_shortcut)
	_display_setup() # lane OPTS-1: the player's display settings first; the command line below wins for the run
	print("FRAME PACING ", preload("res://scripts/frame_pacing.gd").apply_from_args(OS.get_cmdline_user_args())) # lane SMOOTH-1
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--res="):
			var p := arg.substr(6).split("x")
			_res = Vector2i(int(p[0]), int(p[1]))
		elif arg == "--cah":
			_cah_demo = true
		elif arg == "--cah-builder-only":
			_cah_demo = true
			_cah_builder_only = true
		elif arg.begins_with("--cah-profile="):
			_cah_profile = arg.substr(14)
		elif arg == "--cah-real-profile":
			_cah_real_profile = true
		elif arg.begins_with("--seed="):
			_seed = int(arg.substr(7))
		elif arg == "--auto":
			_auto = true
		elif arg.begins_with("--perf-stat="):
			_perf_stat = arg.substr(12)
		elif arg.begins_with("--perf-log="):
			_perf_log_path = arg.substr(11)
		elif arg.begins_with("--net-census="):
			_net_census = arg.substr(13)
		elif arg == "--perf":
			_perf = true
		elif arg == "--fps":
			_display_overlay.session_fps = true
		elif arg == "--display-check":
			_display_check = true
		elif arg == "--display-game":
			_display_game = true
		elif arg.begins_with("--display-shots="):
			_display_shots = arg.substr(16)
		elif arg.begins_with("--options-file="):
			_options_file = arg.substr(15)
		elif arg == "--opts2-check":
			_opts2_check = true
		elif arg == "--opts2-game":
			_opts2_game = true
		elif arg.begins_with("--opts2-shots="):
			_opts2_shots = arg.substr(14)
		elif arg == "--opts4-check":
			_opts4_check = true
		elif arg.begins_with("--opts4-shots="):
			_opts4_shots = arg.substr(14)
		elif arg.begins_with("--opts4-size="):
			_opts4_size = Vector2i(arg.substr(13).get_slice("x", 0).to_int(), arg.substr(13).get_slice("x", 1).to_int())
		elif arg == "--opts5-check":
			_opts5_check = true
		elif arg == "--opts5-game":
			_opts5_game = true
		elif arg.begins_with("--opts5-shots="):
			_opts5_shots = arg.substr(14)
		elif arg.begins_with("--opts5-size="):
			_opts5_size = Vector2i(arg.substr(13).get_slice("x", 0).to_int(), arg.substr(13).get_slice("x", 1).to_int())
		elif arg.begins_with("--opts2-size="):
			_opts2_size = Vector2i(arg.substr(13).get_slice("x", 0).to_int(), arg.substr(13).get_slice("x", 1).to_int())
		elif arg == "--check":
			_check = true
		elif arg == "--alternate-mouse":
			_mouse_override = "alternate"
		elif arg == "--classic-mouse":
			_mouse_override = "classic"
		elif arg == "--free-camera":
			_free_camera_flag = true
		elif arg.begins_with("--zoom-out="):
			# lane ZOOM-1 r2: one of the option's values (OpenBFMEMaxZoomOut's choices), anything else is refused below (no silent 1x)
			var z := arg.substr(11)
			if Array(["1", "1.5", "2", "2.5", "3"]).has(z):
				_zoom_out_flag = z.to_float()
			else:
				_zoom_out_error = "--zoom-out=%s: not a Max zoom-out value (1, 1.5, 2, 2.5 or 3)" % z
		elif arg.begins_with("--widescreen="):
			# lane UI-6: the Widescreen view for a run (the option's values only; anything else is refused below)
			_widescreen_flag = arg.substr(13)
			if not Array(["original", "wider"]).has(_widescreen_flag):
				_zoom_out_error = "--widescreen=%s: not a Widescreen view value (original or wider)" % _widescreen_flag
		elif arg.begins_with("--play1="):
			_play1 = arg.substr(8).split(",", false)
		elif arg.begins_with("--zoom1="):
			_zoom1 = arg.substr(8).split(",", false)
		elif arg.begins_with("--uber1="):
			_uber1 = arg.substr(8).to_int()
		elif arg.begins_with("--rules-shot="):
			_rules_shot = int(arg.substr(13))
		elif arg.begins_with("--gamemodes="):
			_gamemodes = arg.substr(12)
		elif arg.begins_with("--spell3="):
			_play1 = arg.substr(9).split(",", false)
			_play_script = "res://scripts/spell3_player.gd"
		elif arg.begins_with("--hud5="):
			_hud5 = arg.substr(7).split(",", false)
		elif arg.begins_with("--hud6="):
			_hud6 = arg.substr(7).split(",", false)
		elif arg.begins_with("--hover1="):
			_hover1 = arg.substr(9).split(",", false)
		elif arg.begins_with("--hover1-menus="):
			_hover1_menus = arg.substr(15)
		elif arg.begins_with("--hud7="):
			_hud7 = arg.substr(7).split(",", false)
		elif arg.begins_with("--autocast1="):
			_autocast1 = arg.substr(12).split(",", false)
		elif arg.begins_with("--start-spot="):
			_start_spot = int(arg.substr(13))
		elif arg.begins_with("--input1="):
			_input1 = arg.substr(9).split(",", false)
		elif arg == "--report":
			_print_report = true
		elif arg == "--end":
			_end = true
		elif arg == "--end-esc":
			_end = true
			_end_esc = true
		elif arg == "--qa":
			_qa = true
		elif arg == "--qa-idle":
			_qa = true
			_qa_idle = true
		elif arg.begins_with("--qa-minutes="):
			_qa_minutes = float(arg.substr(13))
		elif arg.begins_with("--qa-scene="):
			_qa_scene = arg.substr(11)
		elif arg.begins_with("--qa-speed="):
			_qa_speed = float(arg.substr(11))
		elif arg.begins_with("--qa-cash="):
			_qa_cash = int(arg.substr(10))
		elif arg.begins_with("--qa-tag="):
			_qa_tag = arg.substr(9)
		elif arg.begins_with("--teams="):
			_teams = arg.substr(8).split(",")
		elif arg.begins_with("--save-dir="):
			_save_dir_arg = arg.substr(11)
		elif arg == "--save-test" or arg.begins_with("--save-test="):
			_save_test = arg.substr(12) if arg.begins_with("--save-test=") else "-"
		elif arg == "--quit":
			_quit_flow = "exit"
		elif arg == "--quit-restart":
			_quit_flow = "restart"
		elif arg == "--end-lose":
			_end = true
			_end_lose = true
		elif arg.begins_with("--map="):
			_map_key = arg.substr(6)
		elif arg.begins_with("--faction="):
			_faction = arg.substr(10)
		elif arg.begins_with("--ai="):
			_ai = int(arg.substr(5))
		elif arg.begins_with("--opponents="):
			_opponents = clampi(int(arg.substr(12)), 1, 7)
		elif arg.begins_with("--warp="):
			_warp = float(arg.substr(7))
		elif arg.begins_with("--warp-scale="):
			_warp_scale = float(arg.substr(13))
		elif arg.begins_with("--color="):
			_color = int(arg.substr(8))
		elif arg.begins_with("--advance="):
			_advance = float(arg.substr(10))
		elif arg.begins_with("--screens="):
			_screens = arg.substr(10)
		elif arg.begins_with("--online-join="):
			_online_join = arg.substr(14).trim_prefix("openbfme://join/").trim_suffix("/")
		elif arg.begins_with("openbfme://join/"):
			_online_join = arg.substr(16).trim_suffix("/") # the launcher hands the link on as it came
		elif arg.begins_with("--online-start-file="):
			_online_start_file = arg.substr(20) # lane MP-DROP-1
		elif arg == "--online-host":
			_online_host = true
		elif arg == "--online-rejoin" or arg.begins_with("--online-rejoin="):
			_online_rejoin = true # lane ONLINE-3
			if arg.begins_with("--online-rejoin="):
				_online_join = arg.substr(16)
		elif arg.begins_with("--online-spectate="):
			_online_spectate = true # lane ONLINE-3
			_online_join = arg.substr(18)
		elif arg.begins_with("--net-observe-at="):
			_net_observe_at = int(arg.substr(17))
		elif arg.begins_with("--net-shot-spectate="):
			_net_shot_spectate = arg.substr(20)
		elif arg.begins_with("--net-shot-observe="):
			_net_shot_observe = arg.substr(19)
		elif arg.begins_with("--net-pause-at="):
			_net_pause_at = int(arg.substr(15))
		elif arg.begins_with("--net-resume-after-ms="):
			_net_resume_after_ms = int(arg.substr(22))
		elif arg.begins_with("--net-shot-pause="):
			_net_shot_pause = arg.substr(17)
		elif arg.begins_with("--net-shot-rejoin="):
			_net_shot_rejoin = arg.substr(18)
		elif arg.begins_with("--online-lobby="):
			_online_lobby = arg.substr(15)
		elif arg.begins_with("--online-name="):
			_online_name = arg.substr(14)
		elif arg.begins_with("--online-slots="):
			_online_slots = arg.substr(15)
		elif arg.begins_with("--online-players="):
			_online_players = int(arg.substr(17))
		elif arg.begins_with("--capture-dir="):
			_capture_dir = arg.substr(14)
			DirAccess.make_dir_recursive_absolute(_capture_dir)
		elif arg.begins_with("--online-walk="):
			_online_walk = arg.substr(14)
		elif arg.begins_with("--online-shots="):
			_online_shots = arg.substr(15)
		elif arg.begins_with("--online-password="):
			_online_password = arg.substr(18)
		elif arg.begins_with("--net-host="):
			_net_host = int(arg.substr(11))
		elif arg.begins_with("--net-join="):
			_net_join = arg.substr(11)
		elif arg.begins_with("--net-name="):
			_net_name = arg.substr(11)
		elif arg.begins_with("--net-slots="):
			_net_slots = arg.substr(12)
		elif arg.begins_with("--net-run-ahead="):
			_net_run_ahead = int(arg.substr(16))
		elif arg.begins_with("--net-crc="):
			_net_crc = int(arg.substr(10))
		elif arg == "--net-end":
			_net_end = true
		elif arg == "--net-script":
			_net_script = true
		elif arg.begins_with("--net-record="):
			_net_record = arg.substr(13)
		elif arg.begins_with("--net-frames="):
			_net_frames = int(arg.substr(13))
		elif arg.begins_with("--net-camera="):
			_net_camera = arg.substr(13)
		elif arg == "--net-overlay":
			_net_overlay = true
		elif arg.begins_with("--net-capture="):
			_net_capture = arg.substr(14)
		elif arg.begins_with("--net-disconnect-ms="):
			_net_disconnect_ms = int(arg.substr(20))
		elif arg.begins_with("--net-player-timeout-ms="):
			_net_player_timeout_ms = int(arg.substr(24))
		elif arg == "--net-kick":
			_net_kick = true
		elif arg.begins_with("--net-shot-disconnect="):
			_net_shot_disconnect = arg.substr(22)
		elif arg.begins_with("--net-shot-desync="):
			_net_shot_desync = arg.substr(18)
		elif arg.begins_with("--replay="):
			_replay_file = arg.substr(9)
		elif arg == "--replay-check":
			_replay_check = true
		elif arg.begins_with("--replay-speed="):
			_replay_speed = float(arg.substr(15))
		elif arg.begins_with("--replay-watch="):
			_replay_watch = arg.substr(15)
		elif arg == "--replay-menu-test":
			_replay_menu_test = true
		elif arg == "--no-record":
			_record = false
		elif arg.begins_with("--campaign="):
			_cli_campaign = arg.substr(11)
		elif arg.begins_with("--mission="):
			_cli_mission = int(arg.substr(10))
		elif arg.begins_with("--difficulty="):
			_cli_difficulty = preload("res://scripts/campaign_flow.gd").difficulty_from_command(arg.substr(13))
		elif arg == "--campaign-win":
			_campaign_win = true
		elif arg.begins_with("--campaign-check="):
			_campaign_check = int(arg.substr(17))
		elif arg == "--movies=off":
			_campaign_movies = false
		elif arg == "--dev-overlay":
			_dev_overlay = true
		elif arg.begins_with("--movie-file="):
			var spec := arg.substr(13)
			_movie_files[spec.get_slice("=", 0)] = spec.substr(spec.find("=") + 1)
		elif arg.begins_with("--movie-skip-after="):
			_movie_skip_after = int(arg.substr(19))
		elif arg == "--intro":
			_intro = "on"
		elif arg == "--no-intro":
			_intro = "off"
		elif arg == "--intro-check":
			_intro = "on"
			_intro_check = true
		elif arg.begins_with("--campaign-menu="):
			_campaign_menu = arg.substr(16)
		elif arg.begins_with("--shell-pictures-quit="):
			_shell_pictures_quit = int(arg.substr(22))
		elif arg.begins_with("--lan="):
			_lan_mode = arg.substr(6)
		elif arg.begins_with("--lan-ai="):
			_lan_ai = int(arg.substr(9))
		elif arg.begins_with("--lan-players="):
			_lan_players = int(arg.substr(14))
		elif arg.begins_with("--lan-targets="):
			_lan_targets = arg.substr(14)
		elif arg.begins_with("--lan-port-base="):
			_lan_port_base = int(arg.substr(16))
		elif arg.begins_with("--menu-walk="):
			_menu_walk = arg.substr(12)
		elif arg == "--menu1-walk" or arg.begins_with("--menu1-walk="):
			_menu1_walk = arg.substr(13) if arg.length() > 13 else ""
			_menu1_walk_on = true
		elif arg == "--ui6-walk" or arg.begins_with("--ui6-walk="):
			_ui6_walk = arg.substr(11) if arg.length() > 11 else ""
			_ui6_walk_on = true
		elif arg.begins_with("--menu-colours="):
			_menu_colours = arg.substr(15)
		elif arg == "--resize-check" or arg.begins_with("--resize-check="):
			_resize_check = arg.substr(15) if arg.length() > 15 else ""
			_resize_check_on = true
		elif arg.begins_with("--menu-walk-only="):
			_menu_walk_only = arg.substr(17)
		elif arg.begins_with("--menu-walk-drop="):
			_menu_walk_drop = arg.substr(17)
		elif arg.begins_with("--options-shot="):
			_options_shot = arg.substr(15)
		elif arg.begins_with("--end-capture="):
			_end_capture = arg.substr(14)
		elif arg.begins_with("--lan-shot="):
			_lan_shot = arg.substr(11)
		elif arg.begins_with("--lan-shot-start="):
			_lan_shot_start = arg.substr(17)
		elif arg.begins_with("--net-press="):
			_net_press = arg.substr(12)
		elif arg.begins_with("--net-shot="):
			var spec := arg.substr(11)
			_net_shot = spec.substr(0, spec.rfind(":"))
			_net_shot_frame = int(spec.substr(spec.rfind(":") + 1))
	if not _zoom_out_error.is_empty():
		printerr("GAME FAIL: ", _zoom_out_error)
		get_tree().quit(2)
		return
	# lane QA-1 (review r1): the scripted player is a single-player run; a network flag would start both tasks (the scripted skirmish and the LAN session)
	if _qa and (_net_host > 0 or not _net_join.is_empty() or _net_script or _net_end or not _net_record.is_empty() or _net_frames >= 0 or not _lan_mode.is_empty()
			or not _replay_file.is_empty() or _replay_menu_test):
		printerr("GAME FAIL: --qa / --qa-idle cannot be combined with the --net-*, --lan* or --replay* options (a LAN game or a replay is not a scripted skirmish)")
		get_tree().quit(2)
		return
	if _res != Vector2i(0, 0):
		get_window().size = _res
	RenderingServer.set_default_clear_color(Color(0, 0, 0))
	_build_stage()
	await get_tree().process_frame
	_boot()


## lane OPTS-1: the display settings (scripts/display_settings.gd) read from the user data folder and applied before anything is drawn; the counter and
## its keys (scripts/fps_overlay.gd) go under the scene root after this node, so they see a key before the shell and the HUD. --display-check works on a
## file of its own (removed at its end) and starts from the defaults, so a test never reads or changes the player's settings.
func _display_setup() -> void:
	_display = DisplaySettings.new()
	_display_path = DisplaySettings.default_path()
	if OS.get_cmdline_user_args().has("--display-check") or OS.get_cmdline_user_args().has("--display-game"):
		_display_path = OS.get_user_data_dir().path_join("opts1-check-" + DisplaySettings.FILE_NAME)
		DirAccess.remove_absolute(_display_path)
	if not _display.load_file(_display_path):
		for e in _display.errors:
			printerr("GAME DISPLAY settings: ", e)
	var r: Dictionary = _display.apply(get_tree().root, null)
	print("GAME DISPLAY settings %s: %s%s" % [_display_path.get_file(), ", ".join(r.applied), "" if r.problems.is_empty() else " PROBLEMS " + str(r.problems)])
	_display_overlay = load("res://scripts/fps_overlay.gd").new()
	_display_overlay.name = "OpenBFMEDisplay"
	_display_overlay.settings = _display
	_display_overlay.settings_path = _display_path
	get_tree().root.add_child.call_deferred(_display_overlay)


## lane OPTS-2: the settings of Options.ini the device applies (scripts/game_options.gd): RotWK's detail level, brightness, scroll speed and the OpenBFME
## extras
func _game_options_setup() -> void:
	_game_options = load("res://scripts/game_options.gd").new()
	_game_options.name = "OpenBFMEGameOptions"
	_game_options.shell = _shell
	_game_options.game = self
	add_child(_game_options)
	var values: Dictionary = _game_options.current()
	var done: PackedStringArray = _game_options.apply_global(values)
	var detail: Dictionary = _shell.get_detail_state()
	print("GAME OPTIONS %s; detail %s (texture reduction %d, %d particles)%s" % [", ".join(done), detail.get("level_name", "?"), detail.get("texture_reduction", -1),
		detail.get("max_particle_count", -1), "" if _game_options.problems.is_empty() else " PROBLEMS " + str(_game_options.problems)])
	if not str(detail.get("stop", "")).is_empty():
		print("GAME OPTIONS stop: ", detail.stop)


## lane OPTS-1: the OPENBFME button over Options.apt and the screen it opens (scripts/openbfme_options_screen.gd)
func _display_screen_setup() -> void:
	_display_screen = load("res://scripts/openbfme_options_screen.gd").new()
	_display_screen.name = "OpenBFMEOptions"
	_display_screen.shell = _shell
	_display_screen.hud_getter = func() -> Node: return _hud
	_display_screen.settings = _display
	_display_screen.settings_path = _display_path
	_display_screen.overlay = _display_overlay
	_display_screen.game_options = _game_options # lane OPTS-2: the other tabs
	_display_screen.audio_getter = func() -> Node: return _audio # lane OPTS-4
	add_child(_display_screen)


## lane OPTS-2: --opts2-check (scripts/opts2_check.gd)
func _run_opts2_check() -> void:
	var check: Object = load("res://scripts/opts2_check.gd").new()
	var failed: int = await check.run(self, _opts2_shots)
	get_tree().quit(0 if failed == 0 else 1)


## lane OPTS-4: --opts4-check (scripts/opts4_check.gd)
func _run_opts4_check() -> void:
	var check: Object = load("res://scripts/opts4_check.gd").new()
	var failed: int = await check.run(self, _opts4_shots)
	get_tree().quit(0 if failed == 0 else 1)


## lane OPTS-5: --opts5-check (scripts/opts5_check.gd)
func _run_opts5_check() -> void:
	var check: Object = load("res://scripts/opts5_check.gd").new()
	var failed: int = await check.run(self, _opts5_shots)
	get_tree().quit(0 if failed == 0 else 1)


## lane OPTS-1: --display-check (scripts/opts1_check.gd)
func _run_display_check() -> void:
	var check: Object = load("res://scripts/opts1_check.gd").new()
	var failed: int = await check.run(self, _display_shots)
	DirAccess.remove_absolute(_display_path)
	get_tree().quit(0 if failed == 0 else 1)


func _build_stage() -> void:
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0, 0, 0) # the base game shows black outside the map (owner, 2026-10-06)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.6, 0.6, 0.6)
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	_env = WorldEnvironment.new()
	_env.environment = env
	add_child(_env)
	_camera = Camera3D.new()
	_camera.fov = 45
	_camera.near = 2.0
	_camera.far = 60000.0
	_camera.current = false
	add_child(_camera)


func _fail(message: String) -> void:
	get_tree().call_group("ios_startup", "finished")
	_cah_cleanup() # lane CAH-2 r2: a failed --cah run leaves no hero behind either
	_failed = message
	_state = State.FAILED
	printerr("GAME FAIL: ", message)
	if _check or _auto or DisplayServer.get_name() == "headless":
		get_tree().quit(1)
	else:
		Release.fatal(message) # lane RELEASE-1: a message box naming the session log, then quit


func _boot() -> void:
	if not ClassDB.class_exists("AptMenuPlayer") or not ClassDB.class_exists("GameWorld"):
		_fail("the openbfme extension is not loaded (or too old); " + Release.extension_hint())
		return
	# lane ONEZIP-1: started directly from the combined package: the launcher beside the game says which installed version plays (a few
	# seconds at most) and looks for an update in the background
	if await Release.update_handover(_scripted_run()):
		get_tree().quit(0)
		return
	_fs = ClassDB.instantiate("RetailFileSystem")
	# lane RELEASE-1: the environment, the remembered folders, or the first-run screen (scripts/release/release.gd)
	get_tree().call_group("ios_startup", "stage", "Checking game files…")
	await get_tree().process_frame
	var mount_started := Time.get_ticks_msec()
	var mount: Dictionary = await Release.mount_retail(_fs)
	Release.set_active_mods(mount.get("mods", [])) # lane MODS-1
	for mod in mount.get("mods", []):
		print("GAME mod: %s (%d archive(s), %d loose file(s), %d program file(s) not mounted)" % [mod.path, mod.big_files, mod.loose_files, mod.skipped_programs])
	print("STARTUP data mount took %d ms" % (Time.get_ticks_msec() - mount_started))
	print("STARTUP archive verification: %d hashed, %d cached" % [mount.get("archives_hashed", 0), mount.get("md5_cache_hits", 0)])
	get_tree().call_group("ios_startup", "stage", "Loading game…")
	_rotwk_install = str(mount.get("rotwk_install", "")) # lane CAH-2: the Create-a-Hero save folder's name is in the install's gi.dat
	if mount.get("quit", false):
		get_tree().quit(0)
		return
	if not mount.ok:
		_fail("mount failed:\n" + "\n".join(mount.errors))
		return
	if _fs.is_bfme1():
		DisplayServer.window_set_title("OpenBFME — BFME1")
		if _options_file.is_empty(): _options_file = "user://bfme1-Options.ini"
	_world = ClassDB.instantiate("GameWorld")
	_world.name = "GameWorld"
	add_child(_world)
	_lua_console = preload("res://scripts/lua_console.lua").new()
	_lua_console.execute = func(code: String) -> Dictionary: return _world.execute_lua(code)
	_lua_console.toggled.connect(_lua_console_toggled)
	_lua_console.restart_requested.connect(func(): call_deferred("_console_restart"))
	_lua_console.map_editor_requested.connect(func(): call_deferred("_open_map_editor"))
	add_child(_lua_console)
	var controller := preload("res://scripts/controller_input.gd").new()
	controller.name = "ControllerInput"
	var controller_loaded: Dictionary = _world.execute_lua("bfme.console.reload()")
	if not controller_loaded.ok:
		printerr("CONTROLLER Lua scripts: ", controller_loaded.output)
	controller.resolve_action = func(button: String, playing: bool) -> String:
		var result: Dictionary = _world.execute_lua("return controller.action(%s, %s, %s)" % [JSON.stringify(button), "true" if playing else "false", "true" if controller.palette_open else "false"])
		if not result.ok:
			printerr("CONTROLLER Lua: ", result.output)
			return ""
		return str(result.output).strip_edges()
	controller.gameplay = func() -> bool: return _state == State.PLAYING and not _quit_open and (_lua_console == null or not _lua_console.is_open()) and (not is_instance_valid(_map_editor) or not _map_editor.active)
	controller.input_generated.connect(func(event: InputEvent) -> void: Input.parse_input_event(event))
	controller.command_action.connect(_controller_menu_action)
	add_child(controller)
	var controller_ui := preload("res://scripts/controller_ui.gd").new()
	controller_ui.name = "ControllerUI"
	controller_ui.bridge = controller
	controller_ui.object_info = func(id: int) -> Dictionary: return _world.get_object(id)
	controller_ui.execute = func(code: String) -> Dictionary: return _world.execute_lua(code)
	controller_ui.hud = func() -> Node: return _hud
	add_child(controller_ui)
	var setup: Dictionary = _world.setup(_fs)
	print("GAME object world: ok=%s, %d templates, %d playable factions, %.1f s" % [setup.ok, setup.get("templates", 0), setup.get("factions", 0), setup.get("seconds", 0.0)])
	if not setup.ok:
		_fail("object world: " + "\n".join(setup.errors))
		return
	if not _boot_audio():
		return
	get_tree().call_group("ios_startup", "finished")
	await _play_intro_movies() # lane CAMP-2
	_shell = ClassDB.instantiate("AptMenuPlayer")
	_shell.name = "Shell"
	add_child(_shell)
	var map_search := preload("res://scripts/skirmish_map_search.gd").new()
	map_search.shell = _shell
	map_search.fs = _fs
	map_search.world = _world
	add_child(map_search)
	_save_flow = load("res://scripts/save_flow.gd").new(self) # lane SAVE-2
	# lane FB7-1: the Options screen's inputs the device knows (AptSimpleScreens.h): the AudioSettings default volumes and the display modes (Godot lists
	# no adapter modes: the screen's size and the window's; S-1913); the addresses come from the native LAN transport
	var window_size := DisplayServer.window_get_size()
	var screen_size := DisplayServer.screen_get_size()
	var modes: Array = [window_size]
	if screen_size != window_size:
		modes.append(screen_size)
	var shell_config := {"seed": _seed, "default_volumes": _audio.get_default_volumes(), "display_modes": modes, "current_resolution": window_size}
	if _fs.is_bfme1():
		shell_config["skirmish_ini"] = "user://bfme1-Skirmish.ini"
	if not _options_file.is_empty():
		shell_config["options_file"] = _options_file # lane OPTS-2
	if _scripted_run():
		# lane MENU-1: a scripted run never reads or writes the player's Skirmish.ini (the profiles): a fresh file of its own, so the first visit of
		# Skirmish always asks for a profile as the scripts expect
		var ini := OS.get_temp_dir().path_join("openbfme-skirmish-%d-%d.ini" % [OS.get_process_id(), Time.get_ticks_usec()])
		DirAccess.remove_absolute(ini)
		shell_config["skirmish_ini"] = ini
		_scripted_skirmish_ini = ini
	var boot: Dictionary = _shell.boot_shell(_fs, _world, shell_config)
	if not boot.ok:
		_fail("shell: " + "\n".join(boot.errors))
		return
	print("GAME shell booted in %.0f ms: %d strings, lobby seed %d" % [boot.load_ms, boot.strings, boot.seed])
	# lane CAH-1: the Create-a-Hero builder's and the lobby's heroes: the system heroes of the archives and the profile's MyHero*.cah (RW 0x61EEB6: <user data>\\Save\\)
	var cah: Dictionary = {"ok":true} if _fs.is_bfme1() else _shell.set_create_a_hero(_world, _cah_profile_dir())
	print("GAME Create-a-Hero heroes: %d in %s %s" % [cah.get("heroes", 0), _cah_profile_dir(), "" if cah.ok else str(cah.errors)])
	_shell.shell_request.connect(_on_shell_request)
	_shell.shell_service.connect(_on_shell_service)
	_shell.shell_screen.connect(_on_shell_screen)
	_game_options_setup() # lane OPTS-2: Options.ini's settings in effect (before any game loads its textures)
	_display_screen_setup() # lane OPTS-1: the OPENBFME button over Options.apt and its screen
	if not _shell.shell_push("MainMenu.apt"):
		_fail("the shell refused MainMenu.apt: " + str(_shell.get_shell_report().errors))
		return
	_audio.play_shell_music(false) # no shell map yet (View3D is a stop): the low-LOD shell music, as menu_viewer
	_state = State.MENU
	set_process(true)
	_campaign_flow = load("res://scripts/campaign_flow.gd").new() # lane CAMP-1
	_campaign_flow.name = "CampaignFlow"
	_campaign_flow.game = self
	_campaign_flow.world = _world
	_campaign_flow.hooks = _campaign_win
	_campaign_flow.auto_win = _campaign_win
	_campaign_flow.movies = _campaign_movies # lane CAMP-1H
	add_child(_campaign_flow)
	var quick_skirmish: bool = _startup_shortcut.skirmish_requested and not _scripted_run()
	_startup_shortcut.queue_free()
	if _resume_console_restart():
		return
	if quick_skirmish:
		_run_startup_skirmish()
		return
	if not _cli_campaign.is_empty():
		_campaign_flow.mission = _cli_mission
		_campaign_flow.campaign = _cli_campaign
		_campaign_flow.difficulty = _cli_difficulty
		_campaign_flow.begin_mission()
		if not _play1.is_empty():
			_run_campaign_play1() # lane SELPERF-1
		elif not _save_test.is_empty():
			_run_campaign_save_test() # lane SAVE-2
		return
	if _display_check:
		_run_display_check() # lane OPTS-1
	elif _opts2_check:
		_run_opts2_check() # lane OPTS-2
	elif _opts4_check:
		_run_opts4_check() # lane OPTS-4
	elif _opts5_check:
		_run_opts5_check() # lane OPTS-5
	elif _auto and not _auto_task_started:
		_auto_task_started = true
		_run_auto()
	elif not _menu_walk.is_empty():
		_run_menu_walk()
	elif _menu1_walk_on:
		_menu1_runner = load("res://scripts/menu1_walk.gd").new() # lane MENU-1
		_menu1_runner.run(self, _menu1_walk)
	elif _ui6_walk_on:
		_ui6_runner = load("res://scripts/ui6_walk.gd").new() # lane UI-6
		_ui6_runner.run(self, _ui6_walk)
	elif _resize_check_on:
		_run_resize_check()
	elif not _menu_colours.is_empty():
		_run_menu_colours()
	if not _online_start_file.is_empty():
		# lane MP-DROP-1 (tests without a lobby service): a START the test signed (tools/net/online_client.py fixture), played as the staging room's
		_on_online_started(FileAccess.get_file_as_string(_online_start_file))
	elif not _online_walk.is_empty():
		_online_walk_run() # lane ONLINE-4
	elif not _online_join.is_empty() or _online_host or _online_rejoin or _online_spectate:
		_online_open() # lane ONLINE-2
	elif _net_host > 0 or not _net_join.is_empty():
		_net_open()
	elif not _replay_file.is_empty():
		_begin_replay(_replay_file)


func _process(delta: float) -> void:
	if _world != null:
		_world.update_lua()
	_frames += 1
	if not _capture_dir.is_empty():
		_capture_tick() # lane ONLINE-4
	Release.set_in_game(_state == State.PLAYING) # lane RELEASE-1: the version corner in the menus only
	_campaign_check_tick() # lane CAMP-1
	if _state == State.MENU:
		# Wait for the deferred APT load as well as the first frames: intro playback can
		# advance the game clock before MainMenu.apt exists. A failed reveal must retry.
		# (menus-apt.md 1.2, S-139: who calls ShowMainMenu is not read)
		if not _menu_revealed and _frames >= LEVEL_MAIN_MENU_FRAMES and _shell.shell_top_level() >= 0 and _shell.shell_stack().size() > 0 and _shell.shell_stack()[-1] == "MainMenu.apt" and _shell.has_level(_shell.shell_top_level()):
			var shown: Dictionary = _shell.shell_invoke(_shell.shell_top_level(), "ShowMainMenu", PackedStringArray())
			if not shown.ok:
				printerr("GAME ShowMainMenu: ", shown.error)
			_menu_revealed = shown.ok
			if _menu_revealed and not _shell_pictures_start_done:
				_shell_pictures_start_done = true
				_report_shell_pictures("in the shell at start") # lane CAMP-2: what a return to the shell must show again
		if _cah != null:
			_cah.tick(delta)
		var message: Dictionary = _shell.take_new_game()
		if not message.is_empty():
			_begin_game(message)
		elif _net_state == "lobby":
			_net_poll_lobby()
		elif not _lan_mode.is_empty() and _menu_revealed:
			_lan_script_tick()
		elif not _campaign_menu.is_empty() and _menu_revealed and _frames >= LEVEL_MAIN_MENU_FRAMES + 30 and _shell_pictures_seen > 0:
			# lane CAMP-2: the main menu's campaign button, by its command (AptMainMenu's fscommand table)
			var cmd := _campaign_menu
			_campaign_menu = ""
			print("GAME campaign menu: AptMainMenu::%s (%s)" % [cmd.get_slice(":", 0), cmd.get_slice(":", 1)])
			if not _shell.shell_fscommand("AptMainMenu::" + cmd.get_slice(":", 0), cmd.get_slice(":", 1)):
				_fail("the main menu refused AptMainMenu::" + cmd)
		elif not _options_shot.is_empty() and _menu_revealed:
			_options_shot_tick()
		elif not _hover1_menus.is_empty() and _menu_revealed and not _hover1_menus_started:
			_hover1_menus_started = true
			_hover1_menus_run()
		if _online_http != null:
			_online_keep_name() # lane ONLINE-4
		if _online_note != null:
			_online_note.visible = _shell.shell_stack().has("OnlineShell.apt")
		_online_count_tick()
	elif _state == State.PLAYING:
		if _online_note != null:
			_online_note.visible = false
		if _stats != null:
			_stats.set_mode("") # lane ONLINECOUNT-1: never in a game
		_game_frames += 1
		if not _net_state.is_empty():
			_net_tick()
		_update_listener()
		_end_game_tick()
		if _campaign_flow != null and _campaign_flow.active:
			_campaign_flow.tick() # lane CAMP-1
		if _replay_active:
			_replay_tick()
		if _perf:
			_perf_frame(delta)
		if not _perf_log_path.is_empty():
			_perf_log_frame()
	elif _state == State.SCORE:
		_score_tick()


# lane AUDIO-5: the 3D sounds (effects, deaths, weapons) are heard from retail's microphone (RW recalculateMicrophone 0x45235B): between the tactical
# camera's eye and the point it looks at, facing the camera's heading. get_camera gives both in Godot axes (x, z, -y); the audio core takes SAGE space.
func _update_listener() -> void:
	if _audio == null or _hud == null or not _hud.has_method("get_camera"):
		return
	var cam: Dictionary = _hud.get_camera()
	if not cam.has("eye") or not cam.has("target"):
		return
	var e: Vector3 = cam.eye
	var t: Vector3 = cam.target
	_audio.update_microphone(Vector3(e.x, -e.z, e.y), Vector3(t.x, -t.z, t.y), true)
	# lane BUILD-5: the MiscAudio sounds the input asked for (BadBuildingPlacementSound of a refused building site, RW 0x83EAC5)
	if _hud.has_method("take_misc_sounds"):
		for field in _hud.take_misc_sounds():
			_audio.play_misc(str(field))


# ---- the shell's hooks -----------------------------------------------------------------------------------------------------------------------

func _on_shell_service(kind: String, a: String, b: String) -> void:
	# AUDIO-1: kind "sound" is a UI sound or music cue name (a); "load_music" is the faction's LoadScreenMusic when a load starts
	if kind == "sound":
		_audio.play_shell_sound(a)
	elif kind == "load_music":
		_audio.play_music(a)
	elif kind == "option":
		_apply_option(a, b)
	elif kind == "gamma":
		# lane OPTS-5: the Options screen's brightness while its slider moves, its Reset and Cancel (TheDisplay setGamma, RW 0x91F90D / 0x91F5F3 / 0x91ECB8)
		if _game_options != null:
			_game_options.set_gamma(a.to_float())
	elif kind == "tooltip" or kind == "mouse_visible" or kind == "background":
		pass


# lane FB7-1: an Options.ini entry the Options screen saved takes effect. Lane OPTS-4: the five volumes (RW 0x91F90D / 0x91EC52 / 0x91FC9C: TheAudio
# vslot 0xE8(type, value * 0.01)) are set by the shell's device itself, in the installed audio manager, while a slider moves (AptMenuPlayer's
# services; without a manager the shell's report says so). Lane OPTS-2: the detail level (StaticGameLOD, applied by GameLODManager), the brightness
# (the display gamma), the scroll speed, the health bars and the mouse setup (scripts/game_options.gd)
const OPTION_VOLUME_KEYS := ["SFXVolume", "VoiceVolume", "MusicVolume", "AmbientVolume", "MovieVolume"]
func _apply_option(key: String, value: String) -> void:
	if OPTION_VOLUME_KEYS.has(key):
		pass
	elif _game_options == null:
		return
	elif key == "StaticGameLOD" or key == "Brightness" or key == "ScrollFactor" or key == "AllHealthBars" or key == "AlternateMouseSetup":
		var done: PackedStringArray = _game_options.apply_global(_game_options.current())
		print("GAME OPTIONS %s = %s: %s" % [key, value, ", ".join(done)])


## the log line of a shell request: its argument with the credentials masked (Sol r1: OnlineGameStart's is the START with its relay ticket)
static func shell_request_line(action: String, argument: String) -> String:
	return "GAME shell request: %s (%s)" % [action, AptMenuPlayer.online_redact(argument)]


func _on_shell_request(action: String, argument: String) -> void:
	print(shell_request_line(action, argument))
	if not _menu_walk_drop.is_empty() and action == _menu_walk_drop:
		print("GAME TEST HOOK: --menu-walk-drop drops the request ", action)
		return
	if action == "ExitGame":
		get_tree().quit(0)
	elif action == "ScoreScreenContinue":
		_score_continue()
	elif action == "LoadReplay":
		_open_replay_menu()
	elif action == "LoadGame":
		_save_flow.open_menu(2, 0xB, false) # lane SAVE-2: the main menu's Load Game (RW 0x91C360: mode 2, Campaign | Skirmish | War of the Ring, S-4003)
	elif action == "SaveGameFile" or action == "LoadGameFile" or action == "DeleteGameFile":
		_save_flow.request(action, argument) # lane SAVE-2: SaveLoad.apt's saved-game pages
	elif action == "LoadReplayFile":
		_begin_replay(argument)
	elif action == "Lan":
		_open_lan_lobby()
	elif action == "LanGameStart":
		_lan_game_start()
	elif action == "Online":
		_open_online_shell() # lane ONLINE-4
	elif action == "OnlineGameStart":
		_on_online_started(argument) # lane ONLINE-4: the staging room's START, played as ONLINE-2's
	elif _fs.is_bfme1() and action in ["GoodCampaign", "EvilCampaign", "ContinueCampaign", "LoadCampaign"]:
		_shell.shell_screen_unavailable(action)
	elif action == "GoodCampaign":
		_campaign_flow.start("GOOD_CAMPAIGN", _campaign_flow.difficulty_from_command(argument))
	elif action == "EvilCampaign":
		_campaign_flow.start("EVIL_CAMPAIGN", _campaign_flow.difficulty_from_command(argument))
	elif action == "Expansion1Campaign": # lane CAMP-1: RW 0x91BE64 (ANGMAR_CAMPAIGN; the demo campaign only with [0xDE4324] + 0x60)
		_campaign_flow.start("ANGMAR_CAMPAIGN", _campaign_flow.difficulty_from_command(argument))
	elif action == "BonusCampaign": # RW 0x91BEFA
		_campaign_flow.start("ANGMAR_BONUS_CAMPAIGN", _campaign_flow.difficulty_from_command(argument))
	elif action == "ContinueCampaign":
		if not _campaign_flow.continue_saved():
			print("GAME stop: ", _shell.shell_screen_unavailable("ContinueCampaignNone")) # lane CAH-2: no progress must not leave the menu disabled

	elif action == "Credits":
		# lane UI-4: AptMainMenu::Credits (RW 0x91B5E9): the shell's music gives way (RW 0x35BD3F's twin) to the misc audio's CreditsMusic
		_audio.stop_music(false)
		_audio.play_misc("CreditsMusic")
	elif action == "CreditsExit":
		# CreditsExit (RW 0x91B6FD): the music stopped (TheAudio vslot 0x8C (2, 1, 0)) and the shell's music back (RW 0x35C2B9's twin)
		_audio.stop_music(false)
		_audio.play_shell_music(false)
	elif action == "CreateAHero":
		_cah_begin()
	elif action == "CreateAHeroExit":
		_cah_end()
	elif action == "ToggleQuitMenu":
		_toggle_quit_menu()
	elif action == "PalantirObjectives":
		_palantir_objectives()
	elif action == "TributeReturnToGame":
		_close_tribute()
	elif action.begins_with("QuitMenu"):
		_quit_menu_action(action, argument)
	else:
		# lane CAH-2 (S-1914): a main-menu request this host opens no screen for must not leave the menu disabled: the player gets a message box
		# ("... is not available in this build yet") whose Ok makes the menu usable again, and the stop line is printed
		var line: String = _shell.shell_screen_unavailable(action)
		if not line.is_empty():
			print("GAME stop: ", line)


# ---- lane CAH-1: the Create-a-Hero builder (CreateAHero.apt over the map mode, scripts/create_a_hero_view.gd) ---------------------------------------------

func _cah_profile_dir() -> String:
	# lane CAH-2: retail's folder: <application data>\<gi.dat UserDataLeafName>\Save\ (RW 0x644148 / 0x6DD398), e.g. %APPDATA%\My The Lord of the Rings,
	# The Rise of the Witch-king Files\Save\ on Windows; --cah-profile=DIR overrides it (tests)
	if not _cah_profile.is_empty():
		return _cah_profile
	if _cah_demo and not _cah_real_profile:
		# lane CAH-2 r2: the scripted --cah run never writes into the player's real profile unless --cah-real-profile says so: a fresh temporary
		# folder of its own (removed by _cah_cleanup)
		_cah_profile = OS.get_temp_dir().path_join("openbfme-cah-%d-%d" % [OS.get_process_id(), Time.get_ticks_usec()]).path_join("Save")
		_cah_temp_root = _cah_profile.get_base_dir()
		DirAccess.make_dir_recursive_absolute(_cah_profile)
		print("CAH isolated save folder ", _cah_profile, " (--cah-real-profile writes the real one)")
		return _cah_profile
	var r: Dictionary = _shell.create_a_hero_save_folder(_rotwk_install)
	if not r.ok:
		printerr("GAME Create-a-Hero: no save folder: ", r.error, " (the builder cannot save heroes)")
		return ""
	return r.folder


func _cah_begin() -> void:
	if _cah != null:
		return
	_cah = load("res://scripts/create_a_hero_view.gd").new()
	_cah.name = "CreateAHeroView"
	add_child(_cah)
	var size: Vector2i = get_viewport().get_visible_rect().size
	if not _cah.begin(_world, _shell, size):
		_fail("the Create-a-Hero map mode: " + "\n".join(_cah.errors))
		return
	# lane CAH-2: the map mode is a game start (RW 0x91A018 -> RW 0x7111E5(7)): the front-end background and the shell's backdrop go as at any game's start
	# (RW 0x622C88(0), RW 0x601C62: showShellMap(0)), so the builder's 3D view is seen. INFERENCE (S-1405): the mode-7 start's own calls were not read
	_shell.shell_hide_background()
	_shell.shell_show_shell_map(false)
	_apply_lighting(_cah.report)


func _cah_end() -> void:
	if _cah == null:
		return
	_cah.end()
	_cah.queue_free()
	_cah = null
	# lane CAH-2: back in the shell (RW 0x7792BC: showShellMap(1)); the front-end background as the menus' openers show it (SetBackground "fadein")
	_shell.shell_show_shell_map(true)
	_shell.shell_fscommand("SetBackground", "fadein")


func _on_shell_screen(filename: String) -> void:
	print("GAME screen: ", filename)
	if _state == State.MENU and filename != "MainMenu.apt":
		_audio.play_submenu_music()


# The retail audio manager (AUDIO-1): UI sounds, shell and load-screen music; the logic's and drawables' sounds go through AudioApi once it is booted.
func _boot_audio() -> bool:
	if not ClassDB.class_exists("GameAudio"):
		_fail("the openbfme extension has no GameAudio class (too old); " + Release.extension_hint())
		return false
	_audio = ClassDB.instantiate("GameAudio")
	_audio.name = "GameAudio"
	add_child(_audio)
	var booted: Dictionary = _audio.boot(_fs, {"seed": _seed})
	if not booted.ok:
		_fail("audio boot failed:\n" + "\n".join(booted.errors))
		return false
	print("GAME audio booted: %d events" % booted.events)
	return true


# ---- starting a game -------------------------------------------------------------------------------------------------------------------------

func _begin_game(message: Dictionary) -> void:
	print("GAME new game: map %s seed %d cash %d" % [message.map, message.seed, message.starting_cash])
	_game_kind = "lan" if _net_state == "loading" else ("network" if not _net_state.is_empty() else ("restart" if not _restart_stack.is_empty() else "skirmish"))
	_last_new_game = message.duplicate(true) # lane END-2: the quit menu's Restart starts it again (RW 0x9220DE)
	if _restart_stack.is_empty():
		_start_stack = _shell.shell_stack() # lane END-2: the screens the score screen's Continue shows again (TheShell + 0x9C, RW 0x925798)
	_restart_stack = PackedStringArray()
	_sync_simulation_profile() # lane UBER-1 (a LAN game keeps its lobby's profile: prepare_new_game names any mismatch)
	var prep: Dictionary = _world.prepare_new_game(message)
	_last_local_slot = int(prep.get("local_slot", -1))
	if not prep.ok:
		_fail("prepare_new_game: " + "\n".join(prep.errors))
		return
	var resolved: Dictionary = prep.resolved
	for i in range(resolved.slots.size()):
		var s: Dictionary = resolved.slots[i]
		if s.state >= 2:
			print("GAME   slot %d: state %d faction %d colour %d start %d team %d (was faction %d colour %d start %d)" % [i, s.state, s.player_template, s.color, s.start_pos, s.team, -1, -1, -1])
	_state = State.LOADING
	_shell_leave_for_game()
	var cards: Dictionary = _shell.set_load_screen_from_game(resolved)
	if cards.has("error"):
		_fail("load screen: " + cards.error)
		return
	_show_rules_summary(str(cards.get("rules_summary", "")))
	var local_card: int = cards.get("local_card", -1)
	if local_card >= 0 and local_card < cards.cards.size():
		var music: String = cards.cards[local_card].get("load_music", "")
		if not music.is_empty():
			_audio.play_music(music) # the local player's faction LoadScreenMusic
	# lane MENU-1: a skirmish / LAN game (modes 2 / 1) from the lobby (GameLogic + 0x114 = 3) loads behind the players' screen LoadScreen.apt
	# (GameLogic::getLoadScreen RW 0x626A87 -> AptLoadScreen RW 0x81D356)
	var choice: Dictionary = _shell.choose_load_screen({"game_mode": 1 if _game_kind == "lan" else 2, "screen_kind": 3})
	if choice.kind != "players":
		_fail("load screen: %s instead of the players' screen" % str(choice))
		return
	_shell_load_screen_push()
	# a few frames so the loading screen is on screen before the synchronous load starts
	for i in 4:
		await get_tree().process_frame
	_shell.auto_process = false
	var started := Time.get_ticks_msec()
	_load_log.clear()
	var opts := {"progress": Callable(self, "_on_load_progress"), "test_hooks": _end or not _zoom1.is_empty() or _rules_shot > 0 or not _gamemodes.is_empty() or _uber1 > 0 or _play_script.ends_with("spell3_player.gd")} # lane ZOOM-1: the bars scenario hurts some units (logged)
	if _record and _net_state.is_empty():
		opts["record"] = _last_replay_path() # lane MP-2: every skirmish is recorded
	var rep: Dictionary = _world.start_new_game(opts)
	_report = rep
	print("GAME load: %d ms, ok=%s" % [Time.get_ticks_msec() - started, rep.ok])
	if rep.has("timings_ms"): # lane PERF-1: where the load went (ms per stage)
		print("GAME load timings: ", JSON.stringify(rep.timings_ms))
	if not rep.ok:
		for e in rep.errors.slice(0, 20):
			print("  load error: ", e)
		_fail("start_new_game failed")
		return
	_enter_game(rep)


## lane CAMP-2: every way into a game (a skirmish, a LAN game, Restart, a replay, a campaign mission, the campaign's next mission and Continue) takes the
## shell's pictures away, as a game's start does in retail: the front-end background (Skirmish's start RW 0x9286D7: RW 0x622C88(0)) and the shell backdrop
## (RW 0x601C62: showShellMap(0), lane FB7-1). The owner's campaign showed the backdrop (ShellMapLowLOD) over the whole mission (CAMP-2 bug 1)
func _shell_leave_for_game() -> void:
	_shell.shell_hide_background()
	_shell.shell_show_shell_map(false)


## lane CAMP-2: what is on screen behind the movies, from the scene: the shell backdrop the canvas drew, the front-end background's visible commands in the
## render list, and the developer text (_dev_text_on_screen); printed 45 frames after a game's start (in game) or a return to the shell
func _report_shell_pictures(where: String) -> void:
	_shell_pictures_pending += 1
	for i in 45:
		await get_tree().process_frame
	var b: Dictionary = _shell.get_backdrop_state()
	# the front-end background (Background.apt) is a View3D clip whose hide is the 3D model's own animation: its clip stays in the render list, so its
	# state is the window manager's mode (0 hidden, 1 front end, 2 in game; RW 0x622C88 / 0x6230B6) with the clip count for the record
	print("GAME SHELL PICTURES %s: backdrop '%s', background mode %d (%d clip commands), control bar drawn %s, developer text %s" % [where,
		b.backdrop_image, int(b.background_mode), int(b.background_commands), str(control_bar_drawn()) if _state == State.PLAYING else "-",
		str(_dev_text_on_screen())])
	_shell_pictures_pending -= 1
	_shell_pictures_seen += 1
	if _shell_pictures_quit > 0 and _shell_pictures_seen >= _shell_pictures_quit:
		print("GAME SHELL PICTURES quit after %d reports" % _shell_pictures_seen)
		get_tree().quit(0)


## lane CAMP-2: the visible Labels with text that are not the game's own text (the nodes that show game text carry the meta "game_text"): developer
## overlays, which must never show in the real game (--fps / --net-overlay / --dev-overlay put them there on purpose)
func _dev_text_on_screen() -> Array:
	var out: Array = []
	var stack: Array = [get_tree().root]
	while not stack.is_empty():
		var n: Node = stack.pop_back()
		if (n is Label or n is RichTextLabel) and n.is_visible_in_tree() and not String(n.text).strip_edges().is_empty() and not n.has_meta("game_text"):
			out.append("%s: %s" % [n.get_path(), String(n.text).strip_edges().left(60)])
		stack.append_array(n.get_children())
	return out


## lane CAMP-2: every way back to the shell (the score screen's Continue, the campaign's return to the main menu) shows the backdrop again (RW 0x7792BC:
## showShellMap(1)); the front-end background comes back with the main menu's own FadeInBackground (SetBackground fadein, RW 0x815507)
func _shell_back_from_game() -> void:
	_shell.shell_show_shell_map(true)
	_report_shell_pictures("back in the shell")


## lane CAMP-2: HideControlBar(true) (RW 0x804576) / ShowControlBar(false) (RW 0x8046A1) as the map scripts call them: CAMERA_LETTERBOX_BEGIN / END
## (ScriptActions::executeAction RW 0x7CAFA5 cases 118 / 119 -> doLetterBoxMode RW 0x7BC8B7, which also turns the display's letterbox on / off) and
## HIDE_UI / SHOW_UI (cases 347 / 348). BFME2 decomp ControlBarVisibility.cpp (tier A for RW 0x804576): the palantir, the banner UI and the in-game UI part
## are told to hide. Here the HUD movie's level (Palantir.apt, with the radar and the native components it carries) is hidden, and the HUD's own drawing
## with it. INFERENCE: the hide animation (HideControlBar's immediate=true has none; ShowControlBar(false) animates in retail) is not played
func _set_control_bar_hidden(hidden: bool) -> void:
	if hidden == _control_bar_hidden:
		return
	_control_bar_hidden = hidden
	print("GAME control bar %s" % ("hidden" if hidden else "shown"))
	var stack: PackedStringArray = _shell.shell_stack()
	if stack.size() > 0 and stack[0] == "Palantir.apt":
		# the HUD movie is the shell's only game screen (_enter_game); a menu pushed over it (the quit menu) stays as it is
		_shell.set_level_visible(_palantir_level(), not hidden)
	if _hud != null:
		_hud.visible = not hidden
	# the tests read it back from the render list a few frames later
	for i in 3:
		await get_tree().process_frame
	if _control_bar_hidden == hidden and _state == State.PLAYING:
		print("GAME control bar drawn after the %s: %s" % ["hide" if hidden else "show", str(control_bar_drawn())])


## the Apt level of the HUD movie (the bottom of the shell's stack in a game)
func _palantir_level() -> int:
	return int(_shell.shell_levels()[0]) if _shell.shell_levels().size() > 0 else -1


## lane CAMP-2: whether the HUD movie is drawn (the tests read it from the render list, not from the flag)
func control_bar_drawn() -> bool:
	var level := _palantir_level()
	return level >= 0 and _shell.level_drawn(level)


## lane CAMP-2: the start-up movies, before the shell (GameClient's start-up callbacks): RW 0x645B8D plays EALogoMovie (display slot 0x10C with 0, 8),
## RW 0x64838D NewLineLogo and TolkienLogo (0, 8) then Overall_Game_Intro (1, 0x30) and writes the preference HasSeenLogoMovies = yes; both only when
## GlobalData + 0xAF2 (GameData PlayIntro) is set. Each can be skipped (scripts/movie_player.gd). NOT PORTED (stop S-2341): the two arguments of the
## display's movie slot (ZH playLogoMovie: the minimum movie and copyright times; RotWK's meaning not read) and the preference write
func _play_intro_movies() -> void:
	var play := _intro == "on"
	if _intro == "auto":
		play = DisplayServer.get_name() != "headless"
		for arg in OS.get_cmdline_user_args():
			if not INTRO_QUIET_ARGS.has(arg):
				play = false # an automated start (retail: a .map file to start with)
	if _intro == "off" or not _campaign_movies:
		play = false
	var gate: Dictionary = _world.get_play_intro()
	if not gate.get("ok", false):
		printerr("GAME intro: GameData PlayIntro: ", gate.get("error", "?"))
	print("GAME intro movies: %s (GameData PlayIntro %s%s)" % ["play" if play and gate.get("play_intro", true) else "not played", str(gate.get("play_intro", true)),
		"" if gate.get("from_ini", false) else ", the default"])
	if not play or not gate.get("play_intro", true):
		return
	var player: Node = load("res://scripts/movie_player.gd").new()
	player.skip_after_ms = _movie_skip_after
	player.path_overrides = _movie_files
	player.name = "IntroMovies"
	add_child(player)
	for title in ["EALogoMovie", "NewLineLogo", "TolkienLogo", "Overall_Game_Intro"]:
		await player.play(_world, _audio, title)
		if not player.last.is_empty():
			print("GAME intro movie: ", JSON.stringify(player.last))
	print("GAME STOP [S-2341] the start-up movies' display arguments (0, 8 / 1, 0x30) and the preference HasSeenLogoMovies are not ported")
	print("GAME intro movies done: %d errors %s" % [player.errors.size(), str(player.errors)])
	player.queue_free()
	if _intro_check:
		get_tree().quit(0 if player.errors.is_empty() else 1)


## lane RULES-1: an OpenBFME rule in force (Legendary Heroes) is written over the loading screen, which has no field for it; gone when the game starts.
func _show_rules_summary(text: String) -> void:
	_hide_rules_summary()
	if text.is_empty():
		return
	print("GAME rules: ", text)
	_rules_layer = CanvasLayer.new()
	_rules_layer.layer = 91
	add_child(_rules_layer)
	var label := Label.new()
	label.set_meta("game_text", true)
	label.text = text
	label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	label.anchor_right = 1.0
	label.anchor_top = 0.13 # below the movie's title, above its map panel
	label.add_theme_font_size_override("font_size", 24)
	label.add_theme_color_override("font_color", Color(1.0, 0.84, 0.35))
	label.add_theme_color_override("font_outline_color", Color.BLACK)
	label.add_theme_constant_override("outline_size", 6)
	_rules_layer.add_child(label)


func _hide_rules_summary() -> void:
	if _rules_layer != null:
		_rules_layer.queue_free()
		_rules_layer = null


func _shell_load_screen_push() -> void:
	if not _shell.shell_push("LoadScreen.apt"):
		_fail("the shell refused LoadScreen.apt: " + str(_shell.get_shell_report().errors))


## Called by the load with the retail percentage (RotWK updateLoadProgress): the load screen's bar and a frame, synchronously, like retail's loading loop.
func _on_load_progress(percent: int) -> void:
	_load_log.append(percent)
	_shell.set_load_progress(percent)
	_shell.tick(0.033)
	_shell.render(true)
	RenderingServer.force_draw(false, 0.0)
	if _menu1_runner != null and percent >= 40:
		_menu1_runner.loading_check(percent) # lane MENU-1: the loading screen's layering and picture
	if not _loading_shot_done and percent >= 40 and not _screens.is_empty():
		_loading_shot_done = true
		_save_screenshot("loading")


func _enter_game(rep: Dictionary) -> void:
	_hide_rules_summary() # lane RULES-1
	_shell.hide_still_load_screen() # lane MENU-1: a campaign mission's picture loading screen
	# retail: the shell is popped, the HUD movie (Palantir) is the only movie left
	while _shell.shell_stack().size() > 0:
		_shell.shell_pop()
		_shell.tick(0.033)
	if not _shell.shell_push("Palantir.apt"):
		_fail("the shell refused Palantir.apt: " + str(_shell.get_shell_report().errors))
		return
	_shell.auto_process = true
	_control_bar_hidden = false # lane CAMP-2: a new HUD movie is shown
	_apply_lighting(rep)
	_camera.current = true
	# AUDIO-2 (review r1 fix 2): the live game's audio side before the game advances: sound owners, the player filter, ambient / group / move sounds, TheEva's
	# side and the in-game music scripts (which replace the load screen music)
	_audio.stop_music(true)
	var at: Dictionary = _world.attach_audio()
	print("GAME audio attached: ", at)
	if not at.get("ok", false):
		_fail("attach_audio failed: " + str(at.get("error", "")))
		return
	var ar: Dictionary = _world.get_audio_report()
	if not str(ar.get("music_error", "")).is_empty():
		printerr("GAME in-game music: ", ar.music_error)
	_world.set_auto_advance(true)
	_state = State.PLAYING
	_games_started += 1
	_report_shell_pictures("in game (%s)" % _game_kind) # lane CAMP-2
	print("GAME playing: frame %d, %d objects, hash %d" % [_world.get_frame(), _world.get_object_count(), _world.get_state_hash()])
	if _print_report:
		print("GAME REPORT ", JSON.stringify(rep))
		for o in rep.start.starting_objects:
			print("GAME starting object: ", JSON.stringify(_world.get_object(o.id)))
	_install_hud(rep)
	# lane END-1: GuiFX.apt (the end screen's movie) is loaded with the game, so ShowEndGame finds it
	var gfx: Dictionary = _shell.end_game_screen({"kind": "preload"})
	print("GAME GuiFX for the end screen: level %d %s" % [gfx.get("level", -1), str(gfx.get("error", ""))])
	if not _net_state.is_empty():
		_net_begin()


## HUD-1's install point: the live game exists and Palantir.apt is pushed (the factory table makes it an AptPalantir). The HUD node takes the GameWorld, the camera, the
## shell-mode AptMenuPlayer and the local player: it feeds the camera and the mouse / keyboard to the HUD (selection, orders, the Palantir, the radar, the cursors) and draws
## the native components of the movie.
func _install_hud(rep: Dictionary) -> void:
	if is_instance_valid(_map_editor):
		_map_editor.free()
		_map_editor = null
	_hud_installed = false
	var hud: Node = ClassDB.instantiate("InGameHudNode")
	add_child(hud)
	var local: String = str(rep.get("start", {}).get("local_player", ""))
	_local_name = local
	_local_index = int(rep.get("start", {}).get("local_player_index", -1)) # lane UI-4
	var start: Dictionary = rep.get("start", {})
	if not start.has("camera_start"):
		_fail("the start report has no camera start (the retail camera has nothing to look at)")
		return
	# CAM-1: the HUD node owns the camera from here (TacticalCamera: retail limits, zoom, scrolling, rotation); it looks at the player's start waypoint
	var camera_start: Vector3 = start.camera_start
	if not _net_state.is_empty():
		print("GAME NET camera start ", camera_start)
	if not _net_camera.is_empty():
		var c := _net_camera.split(",")
		camera_start = Vector3(float(c[0]), float(c[1]), float(c[2]))
	var alternate := _alternate_mouse_setup()
	print("GAME mouse setup: %s orders" % ("the right click" if alternate else "the left click"))
	var free_camera := _free_camera_setting()
	if free_camera:
		print("GAME free camera: on (the zoom-out limit is the map's extent; not retail)")
	# lane HUD-5: Options.ini AllHealthBars (RW 0x6E6179: "yes" -> true, absent -> false) gates the infantry / cavalry health bars (GlobalData + 0x9BE)
	var all_bars = _shell.get_option("AllHealthBars") if _shell != null and _shell.has_method("get_option") else null
	var zoom_out := _zoom_out_setting()
	print("GAME max zoom-out: %sx%s" % [str(zoom_out), " (retail)" if zoom_out <= 1.0 else " (OpenBFME option, not retail)"])
	var wider := _widescreen_setting()
	print("GAME widescreen view: %s" % ("wider (OpenBFME option, not retail)" if wider else "original"))
	var r: Dictionary = hud.attach(_fs, _world, _camera, _shell, local, {"alternate_mouse": alternate, "camera_start": camera_start, "free_camera": free_camera,
		"all_health_bars": all_bars != null and str(all_bars) == "yes", "zoom_out_factor": zoom_out, "widescreen_wider": wider})
	print("GAME HUD installed: ok=%s errors=%s" % [r.ok, r.errors])
	if not r.ok:
		hud.queue_free()
		return
	_hud = hud
	_hud_installed = true
	if not has_node("AdaptiveHUD"):
		var adaptive := preload("res://scripts/adaptive_hud.gd").new()
		adaptive.name = "AdaptiveHUD"
		adaptive.hud = func() -> Node: return _hud
		adaptive.gameplay = func() -> bool: return _state == State.PLAYING and not _quit_open and (_lua_console == null or not _lua_console.is_open()) and (not is_instance_valid(_map_editor) or not _map_editor.active)
		adaptive.execute = func(code: String) -> Dictionary: return _world.execute_lua(code)
		adaptive.open_menu = _toggle_quit_menu
		adaptive.open_console = func() -> void:
			if _lua_console != null: _lua_console.toggle()
		adaptive.bridge = get_node_or_null("ControllerInput")
		add_child(adaptive)
	if _game_options != null:
		_game_options.game_started(_world, hud) # lane OPTS-2: the detail's particle limits, the camera and interface options
	_shell.set_tribute_world(_world) # lane PLAY-1: PlayerTribute.apt's rows and Send (the Palantir's flag)
	# lane SPELL-2: the spell book is the retail movies the HUD drives (the Palantir's InGameSpellBook, SpellStore.apt from its store button); lane SPELL-4:
	# the HUD draws the aiming marker of a power waiting for its ground click itself (the button's RadiusCursorTemplate, GameClient/RadiusCursor.h)
	# lane PLAY-1: the move hint on the ground where a move order was given (ZH W3DInGameUI::drawMoveHints, GameData MoveHintName)
	_move_hints = load("res://scripts/move_hints.gd").new()
	add_child(_move_hints)
	_game_nodes.append(_move_hints)
	# lane PLAY-1: the building placement ghost (RotWK placeBuildAvailable RW 0x69C5E6, the placement update RW 0x6A2AE5)
	# lane BUILD-5 r2: the placement ghost is the HUD's own (InGameHudNode: GodotPlacementGhost, drawn after the displayed camera)
	var mh: Dictionary = _move_hints.setup(hud, _fs, _world)
	print("GAME move hints: ", JSON.stringify(mh))
	if not mh.ok:
		printerr("GAME move hints: ", mh.errors)


## lane PLAY-1: which button orders. TARGET FACTS (RotWK game.dat, caveat S-001): GlobalData's constructor sets m_useAlternateMouse (+ 0x5C) to 1 (RW 0x642A4B:
## the right click orders, the left click selects); at start RW 0x641E72 replaces it with OptionPreferences' answer (RW 0x6E61D4): the Options.ini key
## "AlternateMouseSetup" absent -> GlobalData's value, present -> true unless the value is exactly "yes" (strcmp with RW 0xBD3D80 "yes", setne). The Options
## screen shows the inverse (RW 0x920A80: neg / sbb / inc): its "alternate mouse setup" box is the left-click setup.
func _alternate_mouse_setup() -> bool:
	if _mouse_override != "":
		return _mouse_override == "alternate"
	# the scripted QA player (scripts/qa_player.gd, qa_scene.gd) is written for the left-click setup (it orders with left clicks and drops the selection with
	# a right click): its runs keep that setup unless --alternate-mouse asks for the retail default
	if _qa:
		return false
	var v = _shell.get_option("AlternateMouseSetup") if _shell != null and _shell.has_method("get_option") else null
	if v == null:
		return true
	return str(v) != "yes"


## lane UBER-1: the simulation profile of this player's games: Options.ini OpenBFMEUberPerformance = yes is the Uber performance mode (docs/UBER.md).
## The world's profile decides the profile identity (the LAN lobby, the replays) and must match every game it starts; set before a lobby opens and
## before each game. False (and a printed reason) when the world refuses the change (inside a LAN game).
func _sync_simulation_profile() -> bool:
	var v = _shell.get_option("OpenBFMEUberPerformance") if _shell != null and _shell.has_method("get_option") else null
	var name := "uber" if v != null and str(v).to_lower() == "yes" else "faithful"
	if _world == null or not _world.has_method("set_simulation_profile"):
		return true
	if _world.simulation_profile() == name:
		return true
	var r: Dictionary = _world.set_simulation_profile(name)
	print("GAME simulation profile: %s (%s)" % [name, "ok" if r.ok else str(r.get("error", ""))])
	return r.ok


## lane PLAY-1: the free camera lifts retail's zoom-out limit (TacticalCamera::setFreeCamera; camera only, the simulation never sees it)
func _free_camera_setting() -> bool:
	if _free_camera_flag:
		return true
	var v = _shell.get_option("OpenBFMEFreeCamera") if _shell != null and _shell.has_method("get_option") else null
	return v != null and str(v).to_lower() == "yes"


## lane UI-6: the Widescreen view (TacticalCamera::setWidescreenView; camera only): --widescreen, else Options.ini OpenBFMEWidescreen
func _widescreen_setting() -> bool:
	if not _widescreen_flag.is_empty():
		return _widescreen_flag == "wider"
	if _game_options != null:
		return _game_options._widescreen_wider(_game_options.current())
	var opts: Dictionary = _shell.get_options() if _shell != null and _shell.has_method("get_options") else {}
	return str(opts.get("OpenBFMEWidescreen", "original")) == "wider"


## lane ZOOM-1: the Max zoom-out (TacticalCamera::setZoomOutFactor; camera only): --zoom-out, else Options.ini OpenBFMEMaxZoomOut (scripts/game_options.gd)
func _zoom_out_setting() -> float:
	if _zoom_out_flag > 0.0:
		return _zoom_out_flag
	if _game_options != null:
		return _game_options._zoom_out_factor(_game_options.current())
	# no options node: the key's value through the same check (a value the key does not take is reported, its default used, as for every extra)
	var opts: Dictionary = _shell.get_options() if _shell != null and _shell.has_method("get_options") else {}
	var problems := []
	var v: String = load("res://scripts/game_options.gd").value_of(opts, "OpenBFMEMaxZoomOut", problems)
	for p in problems:
		printerr("GAME OPTIONS problem: ", p)
	return v.to_float()


func _apply_lighting(rep: Dictionary) -> void:
	if not rep.has("lighting"):
		return
	var lit: Dictionary = rep.lighting
	_env.environment.ambient_light_color = lit.ambient
	_env.environment.ambient_light_energy = 1.0
	if _sun != null:
		_sun.queue_free()
	_sun = DirectionalLight3D.new()
	_sun.name = "ObjectSun"
	_sun.light_color = lit.diffuse
	var shadow_lod := int(_shell.get_detail_state().get("shadow_lod", 3)) if _shell != null else 3
	_sun.shadow_enabled = shadow_lod > 0
	_sun.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS
	_sun.directional_shadow_max_distance = 3000.0
	_sun.directional_shadow_blend_splits = true
	_sun.shadow_bias = 0.03
	_sun.shadow_normal_bias = 1.0
	RenderingServer.global_shader_parameter_set("w3d_shadow_strength", 0.65 if _sun.shadow_enabled else 0.0)
	var dir: Vector3 = lit.direction
	if dir.length() > 0.0001:
		_sun.transform = Transform3D(Basis.looking_at(dir.normalized(), Vector3.UP if absf(dir.normalized().y) < 0.99 else Vector3.RIGHT), Vector3.ZERO)
	add_child(_sun)


# ---- the scripted run (--auto) --------------------------------------------------------------------------------------------------------------

func _mouse_event(stage_position: Vector2, pressed: bool, with_button: bool) -> void:
	var window_position: Vector2 = _shell.stage_to_window(stage_position)
	if with_button:
		var button := InputEventMouseButton.new()
		button.button_index = MOUSE_BUTTON_LEFT
		button.pressed = pressed
		button.position = window_position
		button.global_position = window_position
		get_viewport().push_input(button)
	else:
		var motion := InputEventMouseMotion.new()
		motion.position = window_position
		motion.global_position = window_position
		get_viewport().push_input(motion)


func _step(frames: int) -> void:
	# at least `frames` render frames AND frames / 60 seconds (SMOOTH-1: at an uncapped frame rate a two-frame click lasted less than one tick of the shell's clock)
	var until := Time.get_ticks_msec() + int(frames * 1000.0 / 60.0)
	for i in frames:
		await get_tree().process_frame
	while Time.get_ticks_msec() < until:
		await get_tree().process_frame


## BFME1: the path (under `menu`) of the button clip whose `buttonName` is `label` ("" when none has it); the movie's labels name the
## buttons, its instance names do not (docs/BFME1.md)
func _bfme1_button(level: int, menu: String, label: String) -> String:
	var prefix := "_level%d.%s." % [level, menu]
	for b in _shell.list_buttons(level):
		var path := String(b.path)
		if not path.begins_with(prefix):
			continue
		var clip := menu + "." + path.trim_prefix(prefix).get_slice(".", 0)
		var name: Dictionary = _shell.get_member(level, clip, "buttonName")
		if name.get("found", false) and name.value == label:
			return clip
	return ""


## Waits (at most 10 s) until the button's position is the same for 10 frames in a row: an animated menu has finished moving it.
func _wait_button_settled(level: int, path: String) -> void:
	var last := Vector2(INF, INF)
	var still := 0
	for i in 300:
		var b: Dictionary = _shell.find_button(level, path)
		var p := Vector2(b.x, b.y) if b.found else Vector2(INF, INF)
		still = still + 1 if b.found and p == last else 0
		if still >= 10:
			return
		last = p
		await _step(1)


func _click_button(level: int, path: String) -> bool:
	var button: Dictionary = _shell.find_button(level, path)
	if not button.found:
		printerr("GAME no button at or under '%s' on _level%d" % [path, level])
		return false
	var p := Vector2(button.x, button.y)
	_mouse_event(p, false, false)
	await _step(2)
	_mouse_event(p, true, true)
	await _step(2)
	_mouse_event(p, false, true)
	await _step(2)
	print("GAME clicked ", button.path)
	return true


## lane HUD-5: the viewport's picture, or null (logged) when the device cannot capture (the headless display server has no texture to read)
func _capture_image(what: String) -> Image:
	var tex := get_viewport().get_texture()
	var image: Image = tex.get_image() if tex != null else null
	if image == null or image.is_empty():
		print("GAME screenshot %s skipped: the display device cannot capture (headless)" % what)
		return null
	return image


func _save_screenshot(state: String) -> void:
	if _screens.is_empty():
		return
	DirAccess.make_dir_recursive_absolute(_screens)
	var path := "%s/start1-%s.png" % [_screens, state]
	var image := _capture_image(path)
	if image == null:
		return
	var err := image.save_png(path)
	print("GAME screenshot %s -> %s (%dx%d)" % [path, error_string(err), image.get_width(), image.get_height()])


## lane SELPERF-1: --campaign=NAME --play1=<a,b,...>: the scenarios of scripts/play1_player.gd in the campaign mission once it plays (after
## --campaign-play1-wait=<s> seconds of its opening, default 20), then quit with their result (the owner's "in the campaign mission, when I select ...")
func _run_campaign_play1() -> void:
	var waited := 0
	while (_state != State.PLAYING or _hud == null) and _state != State.FAILED and waited < 900:
		await _step(30)
		waited += 1
	if _state != State.PLAYING or _hud == null:
		_fail("campaign: the mission did not start playing")
		return
	var wait := 20.0
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--campaign-play1-wait="):
			wait = float(a.substr(22))
	var t0 := Time.get_ticks_msec()
	while Time.get_ticks_msec() - t0 < int(wait * 1000.0):
		await get_tree().process_frame
	print("GAME campaign play1: %s, logic frame %d, %d objects" % [_cli_campaign, _world.get_frame(), _world.get_object_count()])
	var script: GDScript = load("res://scripts/play1_player.gd")
	if script == null or not script.can_instantiate():
		_fail("scripts/play1_player.gd does not load")
		return
	var player = script.new()
	await player.run(self, _play1)
	get_tree().quit(0 if player.fail_count == 0 else 1)


func _controller_menu_action(action: String) -> void:
	if action != "skirmish" or _state != State.MENU or _shell == null or _quit_open:
		return
	if _lua_console != null and _lua_console.is_open():
		return
	var stack = _shell.shell_stack()
	if stack.is_empty():
		return
	match stack[-1]:
		"MainMenu.apt": _shell.shell_fscommand("AptMainMenu::Skirmish", "")
		"Skirmish.apt": _shell.shell_fscommand("AptSkirmish::StartGame", "")

func _run_startup_skirmish() -> void:
	await _wait_main_ready()
	var level: int = _shell.shell_top_level()
	if not await _click_button(level, "SoloPlayNav"):
		return
	await _step(90)
	if not await _click_button(level, "SoloPlayNav.Skirmish"):
		return
	await _step(90)
	# Press Start on the existing setup. Normal validation leaves the lobby open
	# if the player still needs to choose a profile or fix the setup.
	_shell.shell_fscommand("AptSkirmish::StartGame", "")


func _run_auto() -> void:
	# main menu: wait for ShowMainMenu, open the solo nav and click Skirmish (real mouse events)
	await _step(LEVEL_MAIN_MENU_FRAMES + 70)
	if _cah_demo:
		await _run_cah()
		return
	if not await _auto_lobby_and_start():
		return
	await _step(30)
	_save_screenshot("game")
	if not _save_test.is_empty():
		var saved_ok: bool = await _save_flow.run_test() # lane SAVE-2
		print("GAME SAVE RESULT: ", "ok" if saved_ok else "FAILED")
		get_tree().quit(0 if saved_ok else 1)
		return
	if _qa:
		await _run_qa()
		return
	if not _gamemodes.is_empty():
		var g1: GDScript = load("res://scripts/gamemodes1_player.gd")
		if g1 == null or not g1.can_instantiate():
			_fail("scripts/gamemodes1_player.gd does not load")
			return
		var pg = g1.new()
		await pg.run(self, _gamemodes)
		get_tree().quit(0 if pg.fail_count == 0 else 1)
		return
	if _rules_shot > 0:
		var r1: GDScript = load("res://scripts/rules1_player.gd")
		if r1 == null or not r1.can_instantiate():
			_fail("scripts/rules1_player.gd does not load")
			return
		var pr = r1.new()
		await pr.run(self, "GondorBoromir")
		get_tree().quit(0 if pr.fail_count == 0 else 1)
		return
	if not _hud5.is_empty():
		var h5: GDScript = load("res://scripts/hud5_player.gd")
		if h5 == null or not h5.can_instantiate():
			_fail("scripts/hud5_player.gd does not load")
			return
		var p5 = h5.new()
		await p5.run(self, _hud5)
		get_tree().quit(0 if p5.fail_count == 0 else 1)
		return
	if not _hud6.is_empty():
		var h6: GDScript = load("res://scripts/hud6_player.gd")
		if h6 == null or not h6.can_instantiate():
			_fail("scripts/hud6_player.gd does not load")
			return
		var p6 = h6.new()
		await p6.run(self, _hud6)
		get_tree().quit(0 if p6.fail_count == 0 else 1)
		return
	if not _hover1.is_empty():
		var hv: GDScript = load("res://scripts/hover1_player.gd")
		if hv == null or not hv.can_instantiate():
			_fail("scripts/hover1_player.gd does not load")
			return
		var p1v = hv.new()
		await p1v.run(self, _hover1)
		get_tree().quit(0 if p1v.fail_count == 0 else 1)
		return
	if not _hud7.is_empty():
		var h7: GDScript = load("res://scripts/hud7_player.gd")
		if h7 == null or not h7.can_instantiate():
			_fail("scripts/hud7_player.gd does not load")
			return
		var p7 = h7.new()
		await p7.run(self, _hud7)
		get_tree().quit(0 if p7.fail_count == 0 else 1)
		return
	if not _autocast1.is_empty():
		var ac: GDScript = load("res://scripts/autocast1_player.gd")
		if ac == null or not ac.can_instantiate():
			_fail("scripts/autocast1_player.gd does not load")
			return
		var pac = ac.new()
		await pac.run(self, _autocast1)
		get_tree().quit(0 if pac.fail_count == 0 else 1)
		return
	if not _input1.is_empty():
		var iscript: GDScript = load("res://scripts/input1_player.gd")
		if iscript == null or not iscript.can_instantiate():
			_fail("scripts/input1_player.gd does not load")
			return
		var iplayer = iscript.new()
		await iplayer.run(self, _input1)
		get_tree().quit(0 if iplayer.fail_count == 0 else 1)
		return
	if not _zoom1.is_empty():
		var zscript: GDScript = load("res://scripts/zoom1_player.gd")
		if zscript == null or not zscript.can_instantiate():
			_fail("scripts/zoom1_player.gd does not load")
			return
		var zplayer = zscript.new()
		await zplayer.run(self, _zoom1)
		get_tree().quit(0 if zplayer.fail_count == 0 else 1)
		return
	if _uber1 > 0:
		var uscript: GDScript = load("res://scripts/uber1_player.gd")
		if uscript == null or not uscript.can_instantiate():
			_fail("scripts/uber1_player.gd does not load")
			return
		var uplayer = uscript.new()
		await uplayer.run(self, _uber1)
		get_tree().quit(0 if uplayer.fail_count == 0 else 1)
		return
	if not _play1.is_empty():
		var script: GDScript = load(_play_script)
		if script == null or not script.can_instantiate():
			_fail("%s does not load" % _play_script)
			return
		var player = script.new()
		await player.run(self, _play1)
		if _end:
			await _run_end()
			return
		if _quit_flow == "exit":
			# lane PLAY-1: the player leaves as one does: Esc, the quit menu's Exit and its confirmation, the score screen, Continue back to the menu
			await _run_quit()
			return
		get_tree().quit(0 if player.fail_count == 0 else 1)
		return
	if _warp > 0.0:
		# lane PERF-1: fast forward (the logic frames due run as fast as the worker allows), then measure the late game at normal speed
		var w0 := Time.get_ticks_msec()
		_world.set_time_scale(_warp_scale)
		while _world.get_frame() < int(_warp * 5.0):
			await _step(30)
		_world.set_time_scale(1.0)
		print("GAME warp: logic frame %d after %.1f s (x%.1f)" % [_world.get_frame(), (Time.get_ticks_msec() - w0) / 1000.0, _warp_scale])
		_perf_times.clear()
		_perf_deltas.clear()
		_perf_parts.clear()
		_perf_prev_us = 0
		_perf_cpu0 = -1.0
	var t0 := Time.get_ticks_msec()
	while Time.get_ticks_msec() - t0 < int(_advance * 1000.0):
		await get_tree().process_frame
	if not _quit_flow.is_empty():
		await _run_quit()
		return
	if _opts2_game:
		var check2: Object = load("res://scripts/opts2_check.gd").new()
		var failed2: int = await check2.run_game(self, _opts2_shots) # lane OPTS-2
		get_tree().quit(0 if failed2 == 0 else 1)
		return
	if _opts5_game:
		var check5: Object = load("res://scripts/opts5_check.gd").new()
		var failed5: int = await check5.run_game(self, _opts5_shots) # lane OPTS-5
		get_tree().quit(0 if failed5 == 0 else 1)
		return
	if _display_game:
		var check: Object = load("res://scripts/opts1_check.gd").new()
		var failed: int = await check.run_game(self, _display_shots) # lane OPTS-1
		DirAccess.remove_absolute(_display_path)
		get_tree().quit(0 if failed == 0 else 1)
		return
	if _end:
		await _run_end()
		return
	if _replay_menu_test:
		await _run_replay_menu_test()
		return
	_auto_report()


## --cah (lane CAH-1): the builder, then the lobby and the game with the new hero
func _cah_shot(name: String) -> void:
	if _screens.is_empty():
		return
	DirAccess.make_dir_recursive_absolute(_screens)
	get_viewport().get_texture().get_image().save_png(_screens.path_join("cah1-%s.png" % name))


func _cah_invoke(function: String, args: Array = []) -> void:
	var r: Dictionary = _shell.shell_invoke(_shell.shell_top_level(), function, PackedStringArray(args))
	if not r.get("ok", false):
		printerr("CAH invoke %s: %s" % [function, r.get("error", "")])


func _cah_command(command: String, argument: String = "") -> void:
	if not _shell.shell_fscommand(command, argument):
		printerr("CAH command not registered: ", command)


func _run_cah() -> void:
	print("CAH the main menu's Create-a-Hero")
	_cah_command("AptMainMenu::CreateAHero")
	await _step(150)
	_cah_shot("1-promo")
	# the promo page's Continue (CahNewFeatures: SetExtern SuppressCAHPromo, ShowScreen Manager)
	_cah_invoke("ShowScreen", ["Manager"])
	await _step(150)
	_cah_shot("2-manager")
	print("CAH view ", JSON.stringify(_shell.get_create_a_hero_view()))
	# Create new hero: the class page, the Wizard / the Men's class pairs
	_cah_invoke("SetCreateNewHero", ["true"])
	_cah_invoke("ShowScreen", ["Class"])
	await _step(120)
	for pair in [["1", "0"], ["2", "1"], ["0", "1"]]:
		_cah_invoke("SetClassAndType", pair)
		await _step(75)
	_cah_shot("3-class")
	_cah_invoke("ShowScreen", ["Appearance"])
	await _step(120)
	for slot in [0, 1, 2, 3, 4]:
		_cah_command("AptCreateAHero::Appearance::NextAppearance", str(slot))
		await _step(35)
	_cah_command("AptCreateAHero::Appearance::AutoChangeBttn", "AttribRecommend")
	await _step(20)
	_cah_command("AptCreateAHero::Appearance::DecreaseAttribute", "4")
	_cah_command("AptCreateAHero::Appearance::IncreaseAttribute", "0")
	await _step(30)
	if not _shell.create_a_hero_type_name("Hurin Ironhand"):
		_fail("the builder's name entry is not up")
		return
	await _step(30)
	_cah_shot("4-appearance")
	# lane CAH-2 r2: a colour picked with the mouse on the shown colour tab's palette (GadgetColorPicker.swf's Click button: the drag writes <tab>Cursor,
	# the ColorPicker component reads the palette texel, SetColor -> Appearance::On<tab>): the hero's record must change
	var before: PackedByteArray = _shell.get_create_a_hero_view().get("record", PackedByteArray())
	var picked := false
	for b in _shell.list_buttons(_shell.shell_top_level()):
		if b.hittable and String(b.path).contains("ColorPicker") and String(b.path).ends_with(".Click"):
			var at := Vector2(b.x + 60.0, b.y + 4.0)
			_mouse_event(at, false, false)
			await _step(2)
			_mouse_event(at, true, true)
			await _step(6)
			_mouse_event(at, false, true)
			await _step(20)
			picked = true
			print("CAH colour picked on ", b.path)
			break
	var after: PackedByteArray = _shell.get_create_a_hero_view().get("record", PackedByteArray())
	print("CAH colour pick changed the hero: ", str(picked and before != after))
	if not picked or before == after:
		_fail("the colour picker did not change the hero (picked %s)" % str(picked))
		return
	_cah_shot("4b-colour")
	_cah_command("AptCreateAHero::Appearance::OnComplete")
	_cah_invoke("ShowScreen", ["Powers"])
	await _step(120)
	for k in 10:
		var v: Dictionary = _shell.get_create_a_hero_view()
		var cell: String = v.get("available_power", "")
		if cell.is_empty():
			_cah_command("AptCreateAHero::OnNoPowerSelect")
		else:
			_cah_command("AptCreateAHero::OnPowerSelect", cell)
		await _step(25)
	_cah_shot("5-powers")
	var view: Dictionary = _shell.get_create_a_hero_view()
	print("CAH powers chosen: ", view.get("powers_chosen", 0))
	_cah_command("AptCreateAHero::OnPowerSelectionComplete")
	_cah_invoke("ShowScreen", ["Manager"])
	await _step(150)
	_cah_shot("6-saved")
	for h in _shell.get_create_a_hero_heroes():
		if h.name == "Hurin Ironhand" and not h.system:
			_cah_hero_id = h.unique_id
	if _cah_hero_id.is_empty():
		_fail("the built hero was not saved: " + JSON.stringify(_shell.get_create_a_hero_view()))
		return
	print("CAH saved hero ", _cah_hero_id, " in ", _cah_profile_dir())
	if not _cah_check_saved_file():
		return
	if _cah_builder_only:
		var rep: Dictionary = _shell.get_shell_report()
		for e in rep.get("errors", []):
			print("CAH shell error: ", e)
		print("CAH shell notes: ", JSON.stringify(rep.get("notes", {})))
		print("CAH OK (builder only)")
		_cah_cleanup()
		get_tree().quit(0)
		return
	# back to the main menu (Class::Exit), then the Skirmish lobby with the hero
	_cah_command("AptCreateAHero::Class::Exit")
	await _step(120)
	_menu_revealed = false
	await _step(LEVEL_MAIN_MENU_FRAMES + 70)
	if not await _auto_lobby_and_start():
		return
	await _step(60)
	_save_screenshot("game")
	await _cah_recruit()


## lane CAH-2 r2: the --cah run removes what it wrote: the hero file it saved (also in the real profile with --cah-real-profile) and its temporary folder
func _cah_cleanup() -> void:
	if not _cah_demo:
		return
	if not _cah_hero_id.is_empty():
		var path := _cah_profile_dir().path_join("MyHero_%s.cah" % _cah_hero_id)
		if FileAccess.file_exists(path):
			print("CAH cleanup: removed %s: %s" % [path, error_string(DirAccess.remove_absolute(path))])
		_cah_hero_id = ""
	if not _cah_temp_root.is_empty():
		for sub in [_cah_temp_root.path_join("Save"), _cah_temp_root]:
			if DirAccess.dir_exists_absolute(sub):
				for f in DirAccess.get_files_at(sub):
					DirAccess.remove_absolute(sub.path_join(f))
				DirAccess.remove_absolute(sub)
		print("CAH cleanup: removed the temporary folder ", _cah_temp_root, " (exists: %s)" % str(DirAccess.dir_exists_absolute(_cah_temp_root)))
		_cah_temp_root = ""


## lane CAH-2: the saved file is MyHero_<id>.cah in the save folder (RW 0x61A3AD) and holds exactly the record the save path writes (CreateAHeroHero::save,
## RW 0x80B4F5); the folder read again from disk (RW 0x61EEB6) lists the hero with the same record
func _cah_check_saved_file() -> bool:
	var path := _cah_profile_dir().path_join("MyHero_%s.cah" % _cah_hero_id)
	var bytes := FileAccess.get_file_as_bytes(path)
	if bytes.is_empty():
		_fail("the saved hero's file %s cannot be read: %s" % [path, error_string(FileAccess.get_open_error())])
		return false
	var record := PackedByteArray()
	for h in _shell.get_create_a_hero_heroes():
		if h.unique_id == _cah_hero_id:
			record = h.record
	var again: Dictionary = _shell.set_create_a_hero(_world, _cah_profile_dir())
	var reloaded := PackedByteArray()
	for h in _shell.get_create_a_hero_heroes():
		if h.unique_id == _cah_hero_id and not h.system:
			reloaded = h.record
	print("CAH file %s: %d bytes, the save path's record %d bytes, equal %s; reloaded (%d heroes) equal %s" % [path, bytes.size(), record.size(), str(bytes == record), again.get("heroes", 0), str(reloaded == bytes)])
	if bytes != record or reloaded != bytes:
		_fail("the saved hero's file differs from the record the save path writes, or from the reloaded hero")
		return false
	return true


func _cah_recruit() -> void:
	var local_index := -1
	var fortress := -1
	var players: Array = _world.get_player_objects(_local_name)
	for o in players:
		if o.get("commandcenter", false):
			fortress = int(o.id)
	var st: Dictionary = _world.get_create_a_hero(0)
	for i in 8:
		var c: Dictionary = _world.get_create_a_hero(i)
		if not c.is_empty():
			local_index = i
			st = c
	print("CAH installed ", JSON.stringify(st))
	if st.is_empty() or st.get("unique_id", "") != _cah_hero_id or not st.get("can_build", false):
		_fail("the lobby's hero was not installed by the game start: " + JSON.stringify(st))
		return
	var heroes: Array = _world.get_heroes(local_index, fortress)
	var index := -1
	for h in heroes:
		if h.template == "CreateAHero":
			index = int(h.index)
	if index < 0 or fortress < 0:
		_fail("no CreateAHero in the fortress's hero list: " + JSON.stringify(heroes))
		return
	_world.give_money(local_index, 10000)
	_world.queue_hero(local_index, fortress, index)
	var fo: Dictionary = _world.get_object(fortress)
	if _hud != null:
		_hud.camera_look_at(Vector2(fo.x, fo.y))
		_hud.camera_set_height(260.0)
	_world.set_time_scale(4.0) # the recruitment's build time, fast forward (the video)
	var made := -1
	for i in 120:
		await _step(30)
		for o in _world.get_player_objects(_local_name):
			if o.template == "CreateAHero":
				made = int(o.id)
		if made > 0:
			break
	_world.set_time_scale(1.0)
	if made < 0:
		_fail("the Create-a-Hero was not made")
		return
	var ho: Dictionary = _world.get_object(made)
	print("CAH hero made: ", JSON.stringify(ho))
	for i in 16: # follow it out of the gate
		ho = _world.get_object(made)
		if _hud != null:
			_hud.camera_look_at(Vector2(ho.x, ho.y))
			_hud.camera_set_height(110.0)
		await _step(30)
	if _hud != null:
		# select it with a click: the Palantir shows the Create-a-Hero's portrait and its rank-1 command set
		ho = _world.get_object(made)
		var px: Vector2 = _hud.world_to_pixel(Vector2(ho.x, ho.y))
		_hud.inject_mouse_move(px)
		await _step(4)
		_hud.inject_mouse_button(MOUSE_BUTTON_LEFT, true, px)
		await _step(2)
		_hud.inject_mouse_button(MOUSE_BUTTON_LEFT, false, px)
		print("CAH selection: ", JSON.stringify(_hud.get_selection()))
	_cah_shot("7-in-game")
	print("CAH OK")
	_cah_cleanup()
	get_tree().quit(0)


## --replay-menu-test (lane MP-2): the game just played was recorded; leave it, open Load Replay from the main menu, then play the newest replay back to its end
func _run_replay_menu_test() -> void:
	var recorded: Dictionary = _world.recording_status()
	print("GAME REPLAY recording: ", JSON.stringify(recorded))
	_exit_to_score_screen()
	await _step(30)
	_score_continue()
	await _step(LEVEL_MAIN_MENU_FRAMES + 70)
	_on_shell_request("LoadReplay", "")
	await _step(90)
	if not _screens.is_empty():
		DirAccess.make_dir_recursive_absolute(_screens)
		var shot := _capture_image("mp2-replay-menu.png")
		if shot != null:
			shot.save_png(_screens.path_join("mp2-replay-menu.png"))
	var files: Array = _world.list_replays(_replay_dir())
	if files.is_empty():
		_fail("no replay was recorded in " + _replay_dir())
		return
	print("GAME REPLAY menu lists %d files; loading %s" % [files.size(), files[0].path])
	_on_shell_request("LoadReplayFile", files[0].path)


## The scripted way from the main menu through the Skirmish lobby to a running game (real mouse events on the retail movies); false (and _fail) on a failure.
func _auto_lobby_and_start() -> bool:
	var stack: PackedStringArray = _shell.shell_stack()
	if stack.size() == 0 or stack[-1] != "Skirmish.apt":
		var menu_level: int = _shell.shell_top_level()
		# BFME1's MainMenu.apt: the menu bar's Solo Play opens SoloPlayMenu, whose Skirmish starts the lobby (docs/BFME1.md). Its instance names
		# do not follow the buttons: the clip named "Skirmish" is the Campaign button (its init sets buttonName "$Campaign"), "Tutorial" is
		# Skirmish ... so the buttons are found by the label the movie gives them, as a player finds them
		var nav := _bfme1_button(menu_level, "MainMenu", "$SoloPlay") if _fs.is_bfme1() else "SoloPlayNav"
		var item := "SoloPlayNav.Skirmish"
		if not await _click_button(menu_level, nav):
			_fail("no %s button" % nav)
			return false
		await _step(90)
		if _fs.is_bfme1():
			item = _bfme1_button(menu_level, "SoloPlayMenu", "$Skirmish")
		await _wait_button_settled(menu_level, item) # BFME1's sub-menu slides in: a click before it stops lands on a neighbour
		if not await _click_button(menu_level, item):
			_fail("no Skirmish item")
			return false
		# the menu's close animation runs before its GameCode('Skirmish') (BFME1's is longer than RotWK's): up to 10 s
		for wait in 300:
			if _shell.shell_stack().size() >= 2 and _shell.shell_stack()[-1] == "Skirmish.apt":
				break
			await _step(1)
		if _shell.shell_stack()[-1] != "Skirmish.apt":
			print("GAME shell report: ", JSON.stringify(_shell.get_shell_report()))
		await _step(90)
	else:
		print("GAME the score screen's Continue left the Skirmish lobby up: ", str(stack))
		await _step(30)
	if _shell.shell_stack().size() < 2 or _shell.shell_stack()[-1] != "Skirmish.apt":
		_fail("Skirmish.apt is not on the shell stack: " + str(_shell.shell_stack()))
		return false
	# the lobby: the profile popup, the map, the slots
	var slots: Array = [{"slot": 0, "faction": _faction, "color": _color}]
	if not _cah_hero_id.is_empty():
		slots[0]["hero"] = _cah_hero_id # lane CAH-1: the Hero combo
	for i in range(1, _opponents + 1):
		slots.append({"slot": i, "state": _ai})
	for i in range(mini(_teams.size(), slots.size())): # lane QA-1: --teams
		slots[i]["team"] = int(_teams[i])
	var spec := {
		"map": _map_key,
		"slots": slots,
	}
	if not _profile_made:
		_profile_made = true
		spec["profile"] = "Gimli" # the first visit asks for a profile (the popup); the shell session keeps it for the second game (lane END-1)
	var applied: Dictionary = {}
	for attempt in 60:
		applied = _shell.lobby_apply(spec)
		if applied.ok:
			break
		await _step(10) # the lobby movie is still settling (the add-profile popup opens on its first updates)
	if not applied.ok:
		_fail("lobby: " + str(applied.errors))
		return false
	await _step(90)
	if _start_spot >= 0:
		# lane HUD-5: slot 0 takes the start spot by a click (RW 0x845830), as the owner does in the lobby
		var spot: Dictionary = _shell.lobby_gadget_rect("spot/%d" % _start_spot)
		if not spot.found:
			_fail("lobby: no start spot %d" % _start_spot)
			return false
		var c := Vector2(spot.x + spot.w * 0.5, spot.y + spot.h * 0.5)
		_mouse_event(c, false, false)
		await _step(2)
		_mouse_event(c, true, true)
		await _step(2)
		_mouse_event(c, false, true)
		await _step(30)
		print("GAME lobby: slot 0 took start spot %d" % _start_spot)
	if _cah_demo:
		await _step(150) # lane CAH-1: the Hero combo with the new hero, on screen for the video
	if _rules_shot > 0:
		# lane RULES-1: the Rules tab as the player opens it, then the Legendary Heroes factor chosen through its combo
		if not await _click_button(_shell.shell_top_level(), "OpenPlay.MpMapAndSettings.TabButtons.RulesTab"):
			_fail("no Rules tab button")
			return false
		await _step(60)
		_save_named("rules1-rules-tab")
		var chosen: Dictionary = _shell.lobby_apply({"legendary_heroes": _rules_shot})
		if not chosen.ok:
			_fail("lobby: " + str(chosen.errors))
			return false
		await _step(30)
		_save_named("rules1-rules-tab-legendary")
	_save_screenshot("lobby")
	var skirmish_level: int = _shell.shell_top_level()
	# the Start button: RotWK's lobby (lobby.StartGame), the BFME2-style lobby (lobby.bottomNav.StartGame), BFME1's Skirmish.apt (bottomNav.StartGame)
	var start := ""
	for candidate in ["lobby.StartGame", "lobby.bottomNav.StartGame", "bottomNav.StartGame"]:
		if _shell.find_button(skirmish_level, candidate).found:
			start = candidate
			break
	if start.is_empty() or not await _click_button(skirmish_level, start):
		_fail("no StartGame button")
		return false
	if _gamemodes == "sandbox":
		# lane GAMEMODES-1: the one-team lobby as Start is pressed (RotWK's start check RW 0x8431B8 takes it as a sandbox; the skirmish owner shows no line for it)
		await _step(4)
		_save_named("gamemodes1-sandbox-lobby")
	# the game starts from _process (take_new_game); wait for it, play, report
	var waited := 0
	while _state != State.PLAYING and _state != State.FAILED and waited < 900:
		await get_tree().process_frame
		waited += 1
	if _state != State.PLAYING:
		var rep: Dictionary = _shell.get_shell_report()
		print("GAME shell report: ", JSON.stringify(rep))
		_fail("the game did not start (state %d)" % _state)
		return false
	return true


func _auto_report() -> void:
	var gs: Dictionary = _world.get_stats()
	print("GAME ran %.1f s: logic frame %d, %d objects, hash %d" % [_advance, gs.frame, gs.objects, _world.get_state_hash()])
	var shell_report: Dictionary = _shell.get_shell_report()
	print("GAME shell notes: ", JSON.stringify(shell_report.get("notes", {})))
	for e in shell_report.get("errors", []):
		print("  shell error: ", e)
	var errors: Array = _report.get("errors", [])
	print("GAME load errors: %d, progress %s" % [errors.size(), str(_load_log)])
	for e in errors.slice(0, 30):
		print("  load error: ", e)
	# the movies' own script errors (the lobby's and the HUD's callbacks that are other lanes') are reported above and are not the start's failures
	print("GAME shell script errors reported: %d" % shell_report.get("errors", []).size())
	print("GAME shell calls without a function (S-380, retail skips them): %d" % shell_report.get("calls_without_function", []).size())
	for c in shell_report.get("calls_without_function", []):
		print("  call without a function: ", c)
	# lane BUILD-5 r2: the placement ghost's and the cursors' problems are reported, never hidden
	if _hud != null and _hud.has_method("get_placement_ghost_state"):
		var gs2: Dictionary = _hud.get_placement_ghost_state()
		print("GAME placement ghost errors: %d" % gs2.get("errors", []).size())
		for e in gs2.get("errors", []):
			print("  placement ghost error: ", e)
		print("GAME placement ghost notes (hide misses): ", JSON.stringify(gs2.get("notes", [])))
		var hr: Dictionary = _hud.get_report()
		print("GAME cursor errors: %d" % hr.get("cursor_errors", []).size())
		for e in hr.get("cursor_errors", []):
			print("  cursor error: ", e)
	if _check or _auto:
		get_tree().quit(1 if errors.size() > 0 else 0)


# ---- lane END-1: the end of the game ------------------------------------------------------------------------------------------------------------------

## Every frame of a live game: the end sequence's presentation requests (GameWorld.take_end_game_requests: the end screen on GuiFX.apt, the defeat messages,
## the sound fade of MPorSkirmishFadeToScoreScreen; Eva is played by the engine itself).
func _end_game_tick() -> void:
	if _world == null:
		return
	for r in _world.take_end_game_requests():
		_end_log.append(r)
		print("GAME END request: ", JSON.stringify(r))
		if r.kind == "show_end_game" or r.kind == "hide_end_game":
			var res: Dictionary = _shell.end_game_screen(r)
			print("GAME END %s: %s" % [r.kind, JSON.stringify(res)])
			if not res.get("ok", false):
				printerr("GAME END the end screen failed: ", res.get("error", ""))
		elif r.kind == "message":
			_show_end_message(r)
		elif r.kind == "transition":
			_fade_tactical_sound()
		elif r.kind == "clear_game_data":
			# lane PLAY-1: the end-game timer ran out (RW 0x602FFE -> RW 0x603533: MSG_CLEAR_GAME_DATA, 25 logic frames after the local side's VICTORY /
			# DEFEAT): the game is left to the score screen as retail leaves it (no surrender: the game is already decided for this player)
			print("GAME END the end-game timer ran out: leaving to the score screen")
			call_deferred("_exit_to_score_screen")
	if _end_message_label != null and _end_message_until > 0 and Time.get_ticks_msec() > _end_message_until:
		_end_message_label.text = ""
		_end_message_until = 0


func _input(event: InputEvent) -> void:
	if is_instance_valid(_map_editor) and _map_editor.active and (_lua_console == null or not _lua_console.is_open()) and _map_editor.handle_input(event):
		get_viewport().set_input_as_handled()
		return
	if _lua_console != null:
		if _lua_console.is_open():
			_quit_q_time = -1
			return
	if event is InputEventKey and event.pressed and not event.echo:
		var can_quit := _state in [State.MENU, State.SCORE]
		var focus := get_viewport().gui_get_focus_owner()
		can_quit = can_quit and not (focus is LineEdit or focus is TextEdit)
		if can_quit and _shell != null:
			can_quit = not _shell.get_report().get("gadget_keyboard_focus", false)
		var plain_q: bool = event.keycode == KEY_Q and not (event.ctrl_pressed or event.alt_pressed or event.meta_pressed or event.shift_pressed)
		if can_quit and plain_q:
			var now := Time.get_ticks_msec()
			if _quit_q_time >= 0 and now - _quit_q_time <= DOUBLE_Q_MS:
				get_viewport().set_input_as_handled()
				get_tree().quit()
				return
			_quit_q_time = now
		else:
			_quit_q_time = -1
	# lane END-2: Esc toggles the quit menu (the options meta command, RW 0x9DB877 -> ToggleQuitMenu RW 0x921C9D), as the Palantir's options button does
	if _state == State.PLAYING and event is InputEventKey and event.pressed and not event.echo and event.keycode == KEY_ESCAPE:
		get_viewport().set_input_as_handled()
		# review r1: Esc in the Options screen opened from the quit menu closes Options only (the quit menu and the pause stay)
		var stack: PackedStringArray = _shell.shell_stack()
		# lane OPTS-3: Esc on Options' OpenBFME page is the page's CANCEL (back to Options' basic page), as AptMenuPlayer does in the menus
		if _display_screen != null and _display_screen.is_open():
			_display_screen.close(false)
			return
		if _quit_open and not stack.is_empty() and stack[-1] == "Options.apt":
			_shell.shell_pop()
			return
		# lane PLAY-1: Escape over the powers screen closes it with its purchases (AptSpellStore: OnBttnClose and Escape, RW 0x8232ED), not the quit menu
		if _hud != null and _hud.get_spellbook_state().get("store", {}).get("open", false):
			_hud.close_spell_store()
			print("GAME powers screen closed by Escape")
			return
		# lane PLAY-1: Escape over the tribute screen closes it (its key handler RW 0x914E91 -> RW 0x914C51), not the quit menu
		if not stack.is_empty() and stack[-1] == "PlayerTribute.apt":
			_close_tribute()
			return
		_toggle_quit_menu()


func _lua_console_toggled(open: bool) -> void:
	if open:
		for target in [_shell, _hud]:
			if is_instance_valid(target):
				_console_input_states.append({"node": target, "enabled": target.is_processing_input()})
				target.set_process_input(false)
		if is_instance_valid(_hud):
			_hud.notification(Node.NOTIFICATION_WM_WINDOW_FOCUS_OUT)
	else:
		for state in _console_input_states:
			if is_instance_valid(state.node):
				state.node.set_process_input(state.enabled)
		_console_input_states.clear()


## lane PLAY-1: the Palantir's flag (AptPalantir::OnBttnObjectives RW 0x6D40C9). In a skirmish or a multiplayer game (RW 0x625456: mode 2 or a network game)
## RW 0x914EF0 pushes PlayerTribute.apt over the game when none is up and the game is not ending (the end-game timer, RW 0x914F37); otherwise RW 0x8E8843 opens
## the objectives screen, which is not ported (S-1922)
func _palantir_objectives() -> void:
	var ctx: Dictionary = _world.get_quit_menu_context()
	var mode: int = ctx.get("mode", 2)
	if mode != 2 and mode != 1 and mode != 5:
		if _campaign_flow != null and is_instance_valid(_campaign_flow._cine):
			_campaign_flow._cine.toggle_objectives()
		return
	if _shell.shell_stack().has("PlayerTribute.apt") or _quit_open or _state != State.PLAYING:
		return
	# lane HUD-5: the Status page's rows (name, army, team, status) from the live game, read when the movie loads the page (RW 0x9151C3 / 0x915BF6)
	var ps: Dictionary = _shell.set_player_status(_world.get_player_status_state())
	if not ps.get("ok", false):
		printerr("GAME players screen: ", ps.get("error", "?"))
	else:
		print("GAME players screen rows: ", JSON.stringify(ps.rows))
	if not _shell.shell_push("PlayerTribute.apt"):
		printerr("GAME PlayerTribute.apt: ", str(_shell.get_shell_report().errors))
		return
	print("GAME tribute screen open (PlayerTribute.apt, RW 0x914EF0)")


## PlayerTribute.apt closes (its <path>_ReturnToGame, Escape): RW 0x914C51 marks it closing and the shell pops it (RW 0x62215B)
func _close_tribute() -> void:
	var stack: PackedStringArray = _shell.shell_stack()
	if stack.is_empty() or stack[-1] != "PlayerTribute.apt":
		return
	_shell.shell_pop()
	print("GAME tribute screen closed")


## The in-game UI message (InGameUI vslot 0x3C / 0x48: "GUI:YouHaveBeenDefeated", "GUI:PlayerHasBeenDefeated" with the name). The Palantir's message area is
## not drawn by the HUD yet: shown in a presentation label at the top of the screen for 5 s.
func _show_end_message(r: Dictionary) -> void:
	var t: Dictionary = _shell.fetch_text(r.text)
	var text: String = t.text
	for token in ["%ls", "%s", "%hs"]:
		text = text.replace(token, r.name)
	print("GAME END message: ", text)
	if _end_message_layer == null:
		_end_message_layer = CanvasLayer.new()
		_end_message_layer.layer = 90
		add_child(_end_message_layer)
		_end_message_label = Label.new()
		_end_message_label.set_meta("game_text", true) # lane CAMP-2: the game's text (_dev_text_on_screen)
		_end_message_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		_end_message_label.anchor_right = 1.0
		_end_message_label.offset_top = 90
		_end_message_label.add_theme_font_size_override("font_size", 22)
		_end_message_label.add_theme_color_override("font_color", Color(1.0, 0.92, 0.6))
		_end_message_label.add_theme_color_override("font_outline_color", Color.BLACK)
		_end_message_label.add_theme_constant_override("outline_size", 6)
		_end_message_layer.add_child(_end_message_label)
	_end_message_label.text = text
	_end_message_until = Time.get_ticks_msec() + 5000


var _saved_volumes := {}

## WindowTransitions.ini MPorSkirmishFadeToScoreScreen: SOUNDFADE of the TACTICAL view from frame 0 to 30, LeaveSilent (the transition handler's 30 frames are
## taken as one second of real time: S-1060)
func _fade_tactical_sound() -> void:
	if _audio == null:
		return
	for slider in ["sound", "ambient", "voice"]:
		if not _saved_volumes.has(slider):
			_saved_volumes[slider] = _audio.get_volume(slider)
	var tw := create_tween()
	tw.tween_method(func(f: float):
		for slider in _saved_volumes.keys():
			_audio.set_volume(slider, _saved_volumes[slider] * (1.0 - f)), 0.0, 1.0, 1.0)


func _restore_sound_volumes() -> void:
	for slider in _saved_volumes.keys():
		_audio.set_volume(slider, _saved_volumes[slider])
	_saved_volumes.clear()


var _game_nodes: Array = []

# ---- lane END-2: the quit menu ---------------------------------------------------------------------------------------------------------------------

var _quit_open := false
var _quit_paused := false
var _quit_log: Array = []
var _last_new_game := {}
var _start_stack := PackedStringArray()
var _restart_stack := PackedStringArray()

## ToggleQuitMenu (RW 0x921C9D): an open menu closes with code 0 (back to the game), else QuitMenu.apt opens over the game; a game that is not a network game
## pauses while it is open (RW 0x921C4C: RW 0x625AF1(1, 0, 1))
func _toggle_quit_menu() -> void:
	if _state != State.PLAYING:
		return
	if _quit_open:
		_close_quit_menu()
		return
	var ctx: Dictionary = _world.get_quit_menu_context()
	var r: Dictionary = _shell.quit_menu({"kind": "open", "context": ctx})
	print("GAME QUIT open: ", JSON.stringify(r), " context ", JSON.stringify(ctx))
	_quit_log.append("open")
	if not r.get("ok", false):
		printerr("GAME QUIT the quit menu did not open: ", r.get("error", ""))
		return
	_quit_open = true
	var multiplayer: bool = ctx.get("mode", 2) == 1 or ctx.get("mode", 2) == 5
	if not multiplayer:
		_world.set_paused(true)
		_quit_paused = true


## the menu's close (RW 0x9216B1) and dtor (RW 0x921904): without an Exit a paused game goes on
func _close_quit_menu() -> void:
	if not _quit_open:
		return
	var r: Dictionary = _shell.quit_menu({"kind": "close"})
	print("GAME QUIT close: ", JSON.stringify(r))
	_quit_open = false
	if _quit_paused:
		_world.set_paused(false)
		_quit_paused = false


func _quit_menu_action(action: String, argument := "") -> void:
	print("GAME QUIT action: ", action)
	_quit_log.append(action)
	if action == "QuitMenuReturn":
		_close_quit_menu()
	elif action == "QuitMenuForfeit":
		# RW 0x921841: close, then MSG_SELF_DESTRUCT(false) unless the local alliance has won (TheVictoryConditions vslot 0x48)
		var ctx: Dictionary = _world.get_quit_menu_context()
		_close_quit_menu()
		if not ctx.get("allied_victory", false):
			print("GAME QUIT surrender: ", JSON.stringify(_world.self_destruct(false)))
	elif action == "QuitMenuExit":
		call_deferred("_quit_menu_exit")
	elif action == "QuitMenuRestart":
		call_deferred("_restart_game")
	elif action == "QuitMenuSave" or action == "QuitMenuLoad":
		# lane SAVE-2: RW 0x92179F / 0x9217F0 -> AptSaveLoad::OpenScreen(3 / 2, the game's type, 1): SaveLoad.apt over the game, above the quit menu
		_save_flow.open_menu(3 if action == "QuitMenuSave" else 2, int(argument) if argument.is_valid_int() else 2, true)
	elif action == "QuitMenuOptions":
		# RW 0x91ED91: the options screen over the game (the shell's Options.apt; its Back pops it)
		if not _shell.shell_push("Options.apt"):
			printerr("GAME QUIT Options.apt: ", str(_shell.get_shell_report().errors))


## Exit (RW 0x921904 -> RW 0x625E36(1)): a multiplayer game that is not a sandbox (TheGameInfo vslot 0x50, isSandbox RW 0x800C8F) sends MSG_SELF_DESTRUCT(true), then
## MSG_CLEAR_GAME_DATA leaves the game to the score screen (RW 0x779E28: clearGameData(true) for a skirmish or a multiplayer game)
func _quit_menu_exit() -> void:
	var ctx: Dictionary = _world.get_quit_menu_context()
	_close_quit_menu()
	var multiplayer: bool = ctx.get("mode", 2) == 1 or ctx.get("mode", 2) == 5
	if multiplayer and not ctx.get("sandbox", false):
		# the network session sends it with the leave (NetGameSession::finish); this peer's logic is cleared before it would run it (retail runs it in the
		# frame that also clears the game: the score screen is built before the victory rules look again)
		print("GAME QUIT exit self-destruct: ", JSON.stringify(_world.self_destruct(true)))
	_exit_to_score_screen()


## Restart (RW 0x9220DE): the game is cleared without the score screen and the same new-game message starts again (a skirmish keeps its seed: RW 0x922222 reseeds
## only outside mode 2)
const CONSOLE_RESTART_FILE := "user://console_restart.cfg"

func _open_map_editor() -> void:
	if _state != State.PLAYING or not _net_state.is_empty() or _game_kind not in ["skirmish", "restart"]:
		_lua_console._append("Map editor is available in local skirmishes only.\n")
		return
	if _lua_console.is_open():
		_lua_console.toggle()
	if not is_instance_valid(_map_editor):
		_map_editor = preload("res://scripts/map_editor.gd").new()
		_map_editor.world = _world
		_map_editor.hud = _hud
		_map_editor.camera = _camera
		_map_editor.fs = _fs
		add_child(_map_editor)
	_map_editor.set_active(true)

func _console_restart() -> void:
	var config := ConfigFile.new()
	# A local skirmish starts over with its resolved map, seed and player slots.
	# Network sessions, campaigns and replays return to the menu.
	if _state == State.PLAYING and _game_kind in ["skirmish", "restart"] and _net_state.is_empty():
		config.set_value("restart", "skirmish", _last_new_game.duplicate(true))
	else:
		config.set_value("restart", "skirmish", {})
	var err := config.save(CONSOLE_RESTART_FILE)
	if err != OK:
		push_error("Could not prepare game restart: " + error_string(err))
		return
	if OS.has_feature("mobile") or OS.has_feature("web"):
		# These platforms cannot relaunch their application process.
		err = get_tree().reload_current_scene()
		if err != OK:
			DirAccess.remove_absolute(CONSOLE_RESTART_FILE)
			push_error("Could not restart game: " + error_string(err))
	else:
		OS.set_restart_on_exit(true, OS.get_cmdline_args())
		get_tree().quit()

func _resume_console_restart() -> bool:
	if not FileAccess.file_exists(CONSOLE_RESTART_FILE):
		return false
	var config := ConfigFile.new()
	var err := config.load(CONSOLE_RESTART_FILE)
	# Consume before starting: a failed load must never cause a restart loop.
	DirAccess.remove_absolute(CONSOLE_RESTART_FILE)
	if err != OK:
		return false
	var message: Dictionary = config.get_value("restart", "skirmish", {})
	if message.is_empty():
		return false
	_begin_game(message)
	return true


func _restart_game() -> void:
	if _last_new_game.is_empty():
		return
	_close_quit_menu()
	_restart_stack = _start_stack
	_tear_down_game("restart")
	_begin_game(_last_new_game)


## the running game cleared without the score screen (Restart, lane SAVE-2: a save loaded over a game): the HUD and the game's nodes go, the shell is emptied
func _tear_down_game(why: String) -> void:
	if is_instance_valid(_map_editor):
		_map_editor.free()
		_map_editor = null
	_world.set_auto_advance(false)
	_net_state = ""
	for n in _game_nodes:
		if is_instance_valid(n):
			remove_child(n)
			n.free()
	_game_nodes.clear()
	if _game_options != null:
		_game_options.game_ended() # lane OPTS-2
	if _hud != null:
		remove_child(_hud)
		_hud.free()
		_hud = null
	_hud_installed = false
	_shell.set_tribute_world(null)
	while _shell.shell_stack().size() > 0:
		_shell.shell_pop()
		_shell.tick(0.033)
	var r: Dictionary = _world.clear_game_data(false)
	print("GAME QUIT %s clear_game_data: %s" % [why, JSON.stringify(r)])
	_camera.current = false
	_restore_sound_volumes()
	_replay_active = false

## GameLogic::clearGameData(true) (RW 0x7792BC) and the score screen (RW 0x927898 pushes TimeLine.apt): the game is torn down (the worker stopped, objects,
## drawables, audio and the network session released), the HUD goes, the shell shows the score screen with the statistics the logic kept.
func _exit_to_score_screen() -> void:
	if _state != State.PLAYING:
		return
	_state = State.SCORE
	if _quit_open:
		_close_quit_menu()
	_world.set_paused(false)
	_world.set_auto_advance(false)
	# leaving during the seven-second display: the end screen movie goes first (HideEndGame), or GuiFX would stay over the score screen
	if _world.get_end_game_state().get("showing", false):
		var hid: Dictionary = _shell.end_game_screen({"kind": "hide_end_game"})
		print("GAME END hide_end_game on leaving: ", JSON.stringify(hid))
	_net_lobby_again = not _net_state.is_empty() # lane END-2: Continue opens the LAN lobby again
	_net_state = "" # a LAN game's state ends with the game (the next skirmish is not a network game)
	if _dc_shown: # lane MP-2: the disconnect screen goes with the game
		_shell.disconnect_screen({"visible": false})
	_dc_shown = false
	_dc_shot_done = false
	_dc_left_seen = 0
	_dc_log_seen = 0
	_dc_quit_handled = false
	_desync_shown = false
	# the HUD and the HUD movie go first (they read the game), then the game data
	for n in _game_nodes:
		if is_instance_valid(n):
			remove_child(n)
			n.free()
	_game_nodes.clear()
	if _game_options != null:
		_game_options.game_ended() # lane OPTS-2
	if _hud != null:
		remove_child(_hud)
		_hud.free()
		_hud = null
	_hud_installed = false
	_shell.set_tribute_world(null)
	while _shell.shell_stack().size() > 0:
		_shell.shell_pop()
		_shell.tick(0.033)
	var r: Dictionary = _world.clear_game_data(true)
	_score_report = r
	print("GAME END clear_game_data: ", JSON.stringify(r))
	_camera.current = false
	if _end_message_label != null:
		_end_message_label.text = ""
	_restore_sound_volumes()
	if not r.get("ok", false):
		_fail("clear_game_data: " + str(r.get("error", "")))
		return
	if not _shell.set_score_screen_from_world(_world):
		_fail("the score screen has no data")
		return
	if not _shell.shell_push("TimeLine.apt"):
		_fail("the shell refused TimeLine.apt: " + str(_shell.get_shell_report().errors))
		return
	_audio.stop_music(false)
	_audio.play_misc("ScoreScreenMusic") # MiscAudio ScoreScreenMusic
	_install_graph_overlay()


## AptTimeLine::RenderGraph is a device-drawn component (S-1063): the lines of the graph the movie's _graphMode picks, drawn over the movie's graph area.
class GraphOverlay extends Control:
	var shell: Node2D
	var graph := {}
	var rect := Rect2()
	func _draw() -> void:
		if graph.is_empty() or rect.size.x <= 0.0:
			return
		var lines: Array = graph.get("lines", [])
		for pass_i in 2:
			for l in lines:
				var c := Color.hex(0xFF000000 | int(l.color))
				var pts: PackedVector2Array = l.points
				if pts.size() < 2:
					continue
				var out := PackedVector2Array()
				var stride := maxi(1, pts.size() / 600)
				for i in range(0, pts.size(), stride):
					var p: Vector2 = pts[i]
					out.append(rect.position + Vector2(p.x * rect.size.x, (1.0 - clampf(p.y, 0.0, 1.0)) * rect.size.y))
				var last: Vector2 = pts[pts.size() - 1]
				out.append(rect.position + Vector2(last.x * rect.size.x, (1.0 - clampf(last.y, 0.0, 1.0)) * rect.size.y))
				# RW 0x924DD1 twice: width 3 with alpha, then width 1 opaque
				if pass_i == 0:
					draw_polyline(out, Color(c, 0.35), 3.0, true)
				else:
					draw_polyline(out, c, 1.0, true)
				if pass_i == 1:
					var marks: PackedVector2Array = l.get("marks", PackedVector2Array())
					for m in marks:
						draw_rect(Rect2(rect.position + Vector2(m.x * rect.size.x, (1.0 - clampf(m.y, 0.0, 1.0)) * rect.size.y) - Vector2(4, 4), Vector2(8, 8)), c, false, 2.0)


func _install_graph_overlay() -> void:
	if _graph_layer == null:
		_graph_layer = CanvasLayer.new()
		_graph_layer.layer = 80
		add_child(_graph_layer)
	_graph_node = GraphOverlay.new()
	_graph_node.shell = _shell
	_graph_node.set_anchors_preset(Control.PRESET_FULL_RECT)
	_graph_node.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_graph_layer.add_child(_graph_node)


var _graph_path := ""
var _graph_area := ""

## the movie instance the graph is drawn in: the first of the retail instance names that exists (TimeLine.apt names GridArea / GridRender)
func _find_graph_instance(level: int) -> void:
	for p in ["Main.Axis.GridArea.GridRender", "Main.Axis.GridArea"]:
		var info: Dictionary = _shell.instance_info(level, p)
		if info.get("found", false):
			_graph_path = p
			return


func _score_tick() -> void:
	if _graph_node == null:
		return
	var level: int = _shell.shell_top_level()
	if _graph_path.is_empty():
		_find_graph_instance(level)
	var g: Dictionary = _shell.get_timeline_graph("", _graph_path)
	if not g.get("ok", false):
		return
	_graph_node.graph = g
	var info: Dictionary = _shell.instance_info(level, _graph_path) if not _graph_path.is_empty() else {}
	# the graph is the timeline page's: on the statistics page (the Axis clip hidden) nothing is drawn
	var shown := true
	for p in ["Main", "Main.Axis", "Main.Axis.GridArea", _graph_path]:
		var vi: Dictionary = _shell.instance_info(level, p)
		if not vi.get("found", false) or not vi.get("visible", true):
			shown = false
	_graph_node.visible = shown
	if info.get("found", false) and info.has("bounds"):
		var bounds: Rect2 = info.bounds
		var a: Vector2 = _shell.stage_to_window(bounds.position)
		var b: Vector2 = _shell.stage_to_window(bounds.end)
		_graph_node.rect = Rect2(a, b - a)
	_graph_node.queue_redraw()


## AptTimeLine::OnButtonContinue (RW 0x925699): for a skirmish or LAN game (types 2 / 3) the shell music starts again with a fade (TheAudio 0xDE42FC vslot 0x138(2)
## + 0x9C, the event RW 0x6DA95E / setShouldFade RW 0x6DA774, vslot 0x64; lane QA2-FIX: END-2 read this as TheShell) and TheGameEngine + 0x310 is set (RW 0x62215B),
## on which the engine's next update (RW 0x624EE7) pops the shell's top screen (RW 0x75DB34: Shell::pop, the score screen): retail's lobby is the screen the
## shell kept under the game. INFERENCE (stop S-1771): the port pops the shell when the game starts (_enter_game), so it pushes again the screens it held
## then: MainMenu and Skirmish for a skirmish, each loaded before the next covers it (a screen covered before its movie loaded stays drawn: QA-2 #5); a LAN
## game here is started without the shell's LAN screens (MP-1), so its peers open the LAN lobby again (GameNetwork/LANLobby: the host hosts, the joiner joins)
func _score_continue() -> void:
	if _state != State.SCORE or _continuing:
		return # a second Continue the shell's ticks below deliver is the same press
	_continuing = true
	_continue_pressed = true
	if _graph_node != null:
		_graph_node.queue_free()
		_graph_node = null
	while _shell.shell_stack().size() > 0:
		_shell.shell_pop()
		_shell.tick(0.033)
	_audio.stop_music(false)
	var screens: PackedStringArray = _start_stack if _start_stack.size() > 0 else PackedStringArray(["MainMenu.apt"])
	var levels: Array[int] = []
	for screen in screens:
		_shell.shell_push(screen) # over a screen that is shown the push waits for its closing transition (Shell::push, pending)
		var ticks := 0
		# lane QA2-FIX (QA-2 #5): the screen's movie must be in its level before the next push shuts the screen down: the window manager loads a pushed
		# movie on its next update, and hideAptWindow on a window that is not loaded yet does nothing (WindowManager_hideAptWindow), so MainMenu's buttons
		# showed through the lobby after every game
		while (_shell.shell_stack().size() == 0 or _shell.shell_stack()[-1] != screen or not _shell.has_level(_shell.shell_top_level())) and ticks < 300:
			_shell.tick(0.033)
			ticks += 1
		if _shell.shell_stack().size() == 0 or _shell.shell_stack()[-1] != screen or not _shell.has_level(_shell.shell_top_level()):
			_fail("the shell refused %s: %s" % [screen, str(_shell.get_shell_report().errors)])
			return
		levels.append(_shell.shell_top_level())
	var covered_hidden := true
	for i in levels.size() - 1:
		covered_hidden = covered_hidden and not bool(_shell.instance_info(levels[i], "").get("visible", true))
	print("GAME END Continue covered screens hidden: %s (levels %s)" % [covered_hidden, str(levels)])
	print("GAME STOP [S-1771] Continue pushes the shell's start screens again (retail pops the score screen off the stack it kept through the game, RW 0x75DB34)")
	_audio.play_shell_music(false)
	_shell_back_from_game() # lane FB7-1: back in the shell (RW 0x7792BC: showShellMap(1))
	_menu_revealed = false
	_frames = 0
	_state = State.MENU
	_continuing = false
	print("GAME END back to the shell: ", str(_shell.shell_stack()))
	if _net_lobby_again:
		_net_lobby_again = false
		_net_open() # the LAN lobby again


## lane FB7-1: --menu-walk=DIR: the main-menu paths a player takes, with real mouse events: every screen and the main menu after each return
## saved as DIR/walk-<step>.png; prints MENUWALK lines (the stack, every button of the top screen and whether the mouse hits it); exit 0 when every
## return reached a main menu whose nav buttons are hittable
## lane CAH-2 r2: the walk's verdict. Every failure goes through _walk_fail (one counter, whatever function found it); a GDScript runtime error during
## the walk (counted by scripts/walk_error_logger.gd) fails it too, and a walk that never reaches its end (a coroutine stopped by an error) fails by
## the watchdog. Exit 0 only for 0 failures and 0 script errors.
func _walk_fail(message: String) -> void:
	_walk_failures += 1
	print("MENUWALK FAIL ", message)


func _walk_start() -> void:
	_walk_failures = 0
	_walk_logger = load("res://scripts/walk_error_logger.gd").new()
	OS.add_logger(_walk_logger)
	var watchdog := get_tree().create_timer(WALK_WATCHDOG_SECONDS)
	watchdog.timeout.connect(func() -> void:
		_walk_fail("watchdog: the walk did not finish in %d s (a step stopped by an error?)" % WALK_WATCHDOG_SECONDS)
		_walk_finish())


func _walk_finish() -> void:
	if _walk_finished:
		return
	_walk_finished = true
	var errors: int = _walk_logger.script_errors() if _walk_logger != null else 0
	if errors > 0:
		_walk_failures += errors
		print("MENUWALK FAIL %d script error(s) during the walk, the first: %s" % [errors, _walk_logger.first_error()])
	print("MENUWALK %s (%d failures)" % ["PASS" if _walk_failures == 0 else "FAIL", _walk_failures])
	get_tree().quit(0 if _walk_failures == 0 else 1)


func _run_menu_walk() -> void:
	DirAccess.make_dir_recursive_absolute(_menu_walk)
	_walk_start()
	await _step(LEVEL_MAIN_MENU_FRAMES + 90)
	var failures := 0
	await _walk_shot("main-first")
	failures += await _walk_main_ok("first")
	if not _menu_walk_only.is_empty():
		# lane CAH-2 r2: one button only (the probe): "SoloPlayNav.LoadGame" opens SoloPlayNav first; a name without a '.' is a button of its own
		var dot := _menu_walk_only.find(".")
		await _walk_press(_menu_walk_only.substr(0, dot) if dot > 0 else "", _menu_walk_only)
		_walk_finish()
		return
	failures += await _walk_options_accept() # lane WINCRASH-1 (before Create-a-Hero, whose shell request leaves the navs disabled)
	var paths := [
		["options", ["OptionsNav", "OptionsNav.Settings"], "Options.apt"],
		["skirmish", ["SoloPlayNav", "SoloPlayNav.Skirmish"], "Skirmish.apt"],
		["lan", ["MultiPlayNav", "MultiPlayNav.LocalNetwork"], "LanLobby.apt"],
		["replay", ["MultiPlayNav", "MultiPlayNav.Replay"], "SaveLoad.apt"],
	]
	for p in paths:
		var name: String = p[0]
		var level: int = _shell.shell_top_level()
		for b in p[1]:
			if not await _click_button(level, b):
				_walk_fail("%s: cannot press %s" % [name, b])
			await _step(60)
		await _step(90)
		var stack: PackedStringArray = _shell.shell_stack()
		print("MENUWALK %s: stack %s" % [name, str(stack)])
		if stack.size() == 0 or stack[-1] != p[2]:
			_walk_fail("%s: %s is not on top" % [name, p[2]])
			failures += 1
			continue
		await _walk_shot(name)
		if name == "skirmish":
			failures += await _walk_lobby_input()
		for b in _shell.list_buttons(_shell.shell_top_level()):
			print("MENUWALK   %s button %s (%.0f, %.0f) hittable=%s" % [name, b.path, b.x, b.y, b.hittable])
		if not await _walk_back(name):
			_shell.shell_pop()
			await _step(120)
			continue
		await _step(120)
		await _walk_shot("main-after-" + name)
		failures += await _walk_main_ok(name)
	failures += await _walk_every_nav_button()
	_walk_finish()


## lane CAH-2 (S-1914): every button of every main-menu nav, pressed with the mouse: the press must end in a screen on top (left again by its back
## button), or the "not available in this build yet" box (closed by its Ok), and either way in a main menu whose nav buttons are hittable; a press
## that leaves the menu disabled (a soft-lock) fails the walk. Buttons that start a game or quit are listed and not pressed (WALK_NOT_PRESSED).
const WALK_NOT_PRESSED := ["Quit", "Exit", "Campaign", "Witch", "Bonus", "Continue", "Evil", "Good", "Angmar", "Tutorial"]
func _walk_every_nav_button() -> int:
	var failures := 0
	for nav in ["SoloPlayNav", "MultiPlayNav", "OptionsNav"]:
		var subs := await _walk_nav_subs(nav)
		print("MENUWALK nav %s: %s" % [nav, str(subs)])
		if subs.is_empty():
			_walk_fail("nav %s: no buttons under it" % nav)
			failures += 1
		for sub in subs:
			var skip := false
			for word in WALK_NOT_PRESSED:
				skip = skip or String(sub).containsn(word)
			if skip:
				print("MENUWALK not pressed (starts a game or quits) %s" % sub)
				continue
			failures += await _walk_press(nav, sub)
	failures += await _walk_press("", "MyHeroes") # My Heroes is a button of its own, no nav
	return failures


# the buttons a nav shows once open: their clip paths below the nav (its own OpenButton left out)
func _walk_nav_subs(nav: String) -> PackedStringArray:
	var level: int = _shell.shell_top_level()
	await _walk_open_nav(level, nav)
	var out := PackedStringArray()
	for b in _shell.list_buttons(level):
		var path := String(b.path)
		var at := path.find(nav + ".")
		if at < 0 or not b.hittable:
			continue
		var sub := path.substr(at)
		if sub.ends_with(".bttn"):
			sub = sub.substr(0, sub.length() - 5)
		if sub.containsn("OpenButton") or out.has(sub):
			continue
		out.push_back(sub)
	_mouse_event(Vector2(5, 5), false, false)
	await _step(10)
	return out


func _walk_open_nav(level: int, nav: String) -> void:
	await _click_button(level, nav)
	await _step(60)


func _walk_press(nav: String, sub: String) -> int:
	var name := sub.replace(".", "-")
	var level: int = _shell.shell_top_level()
	var target: Dictionary = _shell.find_button(level, sub)
	var hittable := false
	for x in _shell.list_buttons(level):
		hittable = hittable or (target.found and x.path == target.path and x.hittable)
	if not hittable and not nav.is_empty():
		await _walk_open_nav(level, nav)
	if not await _click_button(level, sub):
		_walk_fail("%s: cannot press it" % name)
		return 1
	await _step(150)
	var stack: PackedStringArray = _shell.shell_stack()
	print("MENUWALK %s: stack %s" % [name, str(stack)])
	if stack.size() > 0 and stack[-1] != "MainMenu.apt":
		await _walk_shot(name)
		if not await _walk_back(name):
			_shell.shell_pop()
			await _step(120)
			return 1 + await _walk_main_ok(name) # _walk_back counted its failure
		await _step(120)
		return await _walk_main_ok(name)
	var ok := _walk_find_box_ok()
	if not ok.is_empty():
		print("MENUWALK %s: the unavailable-screen box (%s)" % [name, ok.path])
		await _walk_shot(name + "-box")
		_mouse_event(Vector2(ok.x, ok.y), false, false)
		await _step(2)
		_mouse_event(Vector2(ok.x, ok.y), true, true)
		await _step(2)
		_mouse_event(Vector2(ok.x, ok.y), false, true)
		await _step(120)
		if not _walk_find_box_ok().is_empty():
			_walk_fail("%s: the box's Ok did not close it" % name)
			return 1
	else:
		# a page of the main menu itself (Credits: the _CreditsMovie clip and its Exit button, ExitCreditsButton -> ShowMainMenu)
		for b in _shell.list_buttons(_shell.shell_top_level()):
			var path := String(b.path)
			if b.hittable and path.containsn("Exit") and not path.containsn("Quit"):
				print("MENUWALK %s: a page of the main menu, left by %s" % [name, path])
				await _walk_shot(name)
				_mouse_event(Vector2(b.x, b.y), false, false)
				await _step(2)
				_mouse_event(Vector2(b.x, b.y), true, true)
				await _step(2)
				_mouse_event(Vector2(b.x, b.y), false, true)
				await _step(150)
				break
	var bad := await _walk_main_ok(name)
	if bad > 0:
		_walk_fail("%s: soft-lock: the main menu is not usable after the press" % name)
	return bad


# the hittable Ok button of a message box on any level ({} when none is up)
func _walk_find_box_ok() -> Dictionary:
	for level in 16:
		for b in _shell.list_buttons(level):
			var path := String(b.path)
			if b.hittable and path.contains("MessageBox") and path.containsn("Ok"):
				return b
	return {}


## lane WINCRASH-1: Options left the ways that save, as the owner did on 2026-10-09 (e204772c crashed in AptOptions::Save): visit 0 clicks Accept
## (the close animation plays, then GameCode('Save') reads every control); visit 1 clicks Advanced first (Main's advanced page removes the basic
## page's clips) and leaves with Done (CloseOptions, then Save reads the basic page's sliders: the owner's crash path)
func _walk_options_accept() -> int:
	var failures := 0
	for visit in 2:
		await _wait_main_ready()
		var level: int = _shell.shell_top_level()
		for b in ["OptionsNav", "OptionsNav.Settings"]:
			if not await _click_button(level, b):
				_walk_fail("options-save: cannot press %s" % b) # lane CAH-2 r3: counted by the walk's verdict
				failures += 1
			await _step(60)
		await _step(150)
		var stack: PackedStringArray = _shell.shell_stack()
		if stack.size() == 0 or stack[-1] != "Options.apt":
			_walk_fail("options-save: Options.apt is not on top: " + str(stack))
			return failures + 1
		await _walk_shot("options-save-%d" % visit)
		var leave := "Accept"
		if visit == 1 and _display_screen != null and _display_screen.is_open():
			leave = "Cancel" # lane OPTS-5: Options is the OpenBFME menu (no retail Advanced page): the second visit leaves with CANCEL
		elif visit == 1:
			if not await _click_named_button("Advanced"):
				_walk_fail("options-save: no Advanced button")
				return failures + 1
			await _step(120)
			await _walk_shot("options-advanced")
			leave = "Done"
		if not await _click_named_button(leave):
			_walk_fail("options-save: no %s button" % leave)
			return failures + 1
		await _step(240)
		if leave == "Done":
			# lane UI-4: since PLAY-2 the advanced page's Done saves and goes back to the basic page when Options was opened on it (RW 0x9205A3 closes
			# the screen only with AdvancedOnly): the basic page's Accept then closes it (the owner's crash path: Save after the advanced page)
			var st: PackedStringArray = _shell.shell_stack()
			if st.size() > 0 and st[-1] == "Options.apt":
				if not await _click_named_button("Accept"):
					_walk_fail("options-save: the basic page after Done has no Accept button")
					return failures + 1
				leave = "Done, Accept"
				await _step(240)
		await _wait_main_ready()
		print("MENUWALK options-save %d (%s): stack %s" % [visit, leave, str(_shell.shell_stack())])
		failures += await _walk_main_ok("options-save-%d" % visit)
	return failures


# the main menu back and settled: its nav buttons hittable (its intro / return animation played out; a loaded machine renders slower and the
# menu's timelines run on real time), at most 600 frames
func _wait_main_ready() -> void:
	for attempt in 40:
		var stack: PackedStringArray = _shell.shell_stack()
		if stack.size() > 0 and stack[-1] == "MainMenu.apt":
			var level: int = _shell.shell_top_level()
			var ready := 0
			for nav in ["SoloPlayNav", "MultiPlayNav", "OptionsNav", "MyHeores", "QuitMainMenu"]:
				var b: Dictionary = _shell.find_button(level, nav)
				for x in _shell.list_buttons(level) if b.found else []:
					if x.path == b.path and x.hittable:
						ready += 1
			if ready == 5:
				return
		await _step(15)


# clicks the hittable button of the top screen whose path ends with ".<name>" or holds ".<name>." (Options.apt: _level3.Buttons.Accept.bttn)
func _click_named_button(name: String) -> bool:
	var level: int = _shell.shell_top_level()
	for button in _shell.list_buttons(level):
		var p := String(button.path)
		if button.hittable and (p.contains("." + name + ".") or p.ends_with("." + name)):
			return await _click_button(level, p.substr(p.find(".") + 1).trim_suffix(".bttn"))
	return false


## lane FB7-1 r3: the lobby with real mouse events: the profile, a click on start spot 0 (RW 0x845830: the player takes it, the spot shows "1"), then
## slot 0's Army list opened and the pointer on its third row (the highlight follows the pointer, RW 0x727081); one screenshot each
func _walk_lobby_input() -> int:
	var failures := 0
	for attempt in 30:
		if _shell.lobby_apply({"profile": "Gimli"}).ok:
			break
		await _step(10)
	await _step(60)
	var spot: Dictionary = _shell.lobby_gadget_rect("spot/0")
	if not spot.found:
		_walk_fail("lobby: no start spot 0")
		return 1
	var c := Vector2(spot.x + spot.w * 0.5, spot.y + spot.h * 0.5)
	_mouse_event(c, false, false)
	await _step(2)
	_mouse_event(c, true, true)
	await _step(2)
	_mouse_event(c, false, true)
	await _step(30)
	await _walk_shot("skirmish-spot")
	var army: Dictionary = _shell.lobby_gadget_rect("0/PlayerTemplate")
	if not army.found:
		_walk_fail("lobby: no Army combo for slot 0")
		return failures + 1
	var a := Vector2(army.x + army.w * 0.5, army.y + army.h * 0.5)
	_mouse_event(a, false, false)
	await _step(2)
	_mouse_event(a, true, true)
	await _step(2)
	_mouse_event(a, false, true)
	await _step(20)
	var list: Dictionary = _shell.lobby_gadget_rect("list/0/PlayerTemplate")
	if not list.found or list.h <= 0:
		_walk_fail("lobby: the Army list did not open")
		return failures + 1
	# the third row: rows are the list's height over its visible rows; the pointer a little below the row's top
	var row_h := float(list.h) / 8.0
	_mouse_event(Vector2(list.x + list.w * 0.4, list.y + row_h * 2.5), false, false)
	await _step(20)
	await _walk_shot("dropdown-hover")
	# close the list again (a click on the combo)
	_mouse_event(a, true, true)
	await _step(2)
	_mouse_event(a, false, true)
	await _step(20)
	return failures


func _walk_shot(name: String) -> void:
	if DisplayServer.get_name() == "headless":
		return # no frame to grab (the probe runs headless)
	await RenderingServer.frame_post_draw
	var path := "%s/walk-%s.png" % [_menu_walk, name]
	var image := _capture_image(path)
	if image == null:
		return
	print("MENUWALK screenshot %s -> %s" % [path, error_string(image.save_png(path))])


# the back button of a screen: the first of these names the screen has
func _walk_back(name: String) -> bool:
	var level: int = _shell.shell_top_level()
	# Skirmish's first visit asks for a profile (ProfilePopup): its Cancel first
	for b in _shell.list_buttons(level):
		if b.hittable and String(b.path).ends_with("ProfilePopup.Main.Cancel.bttn"):
			await _click_button(level, "ProfilePopup.Main.Cancel")
			await _step(60)
			var st: PackedStringArray = _shell.shell_stack()
			if st.size() > 0 and st[-1] == "MainMenu.apt":
				print("MENUWALK %s: the profile popup's Cancel went back to the main menu" % name)
				return true
			level = _shell.shell_top_level()
	for b in ["Buttons.Cancel", "Cancel", "cancel", "Back", "back", "BackButton", "MainMenu", "mainMenu", "lobby.MainMenu", "lobby.Back", "Exit", "BackBttn", "Buttons.Back", "Buttons.MainMenu", "MainButtons.MainMenu", "MainButtons.Cancel"]:
		var found: Dictionary = _shell.find_button(level, b)
		if found.found:
			return await _click_button(level, b)
	# lane CAH-2: a screen whose back button has another name (Create-a-Hero's promo page and Manager): the hittable buttons named like a way back,
	# pressed until the main menu is on top (at most three pages)
	for attempt in 3:
		var pressed := false
		for b in _shell.list_buttons(level):
			var path := String(b.path)
			if b.hittable and (path.containsn("MainMenu") or path.containsn("Back") or path.containsn("Exit") or path.containsn("Continue")):
				print("MENUWALK %s: back through %s" % [name, path])
				_mouse_event(Vector2(b.x, b.y), false, false)
				await _step(2)
				_mouse_event(Vector2(b.x, b.y), true, true)
				await _step(2)
				_mouse_event(Vector2(b.x, b.y), false, true)
				await _step(120)
				pressed = true
				break
		var st: PackedStringArray = _shell.shell_stack()
		if st.size() > 0 and st[-1] == "MainMenu.apt":
			return true
		if not pressed:
			break
		level = _shell.shell_top_level()
	for b in _shell.list_buttons(level):
		print("MENUWALK   %s button %s (%.0f, %.0f) hittable=%s" % [name, b.path, b.x, b.y, b.hittable])
	_walk_fail("%s: no back button" % name)
	return false


# the main menu is on top, its nav buttons are hittable, hovering one highlights it (prints the hit path)
func _walk_main_ok(when: String) -> int:
	var stack: PackedStringArray = _shell.shell_stack()
	if stack.size() == 0 or stack[-1] != "MainMenu.apt":
		_walk_fail("main menu (%s): stack %s" % [when, str(stack)])
		return 1
	var level: int = _shell.shell_top_level()
	var bad := 0
	print("MENUWALK main (%s) MainMenuShown=%s" % [when, str(_shell.get_member(level, "", "MainMenuShown"))])
	if OS.has_environment("FB7_OPS"):
		for line in _shell.describe_ops():
			if line.contains("SoloPlayNav") or line.contains("MyHeroes"):
				print("MENUWALK op (%s) %s" % [when, line])
	for clip in ["SoloPlayNav", "MultiPlayNav", "OptionsNav", "MyHeroes", "QuitMainMenu", "SoloPlayNav.OpenButton", "SoloPlayNav.openbutton", "OptionsNav.OpenButton"]:
		var info: Dictionary = _shell.instance_info(level, clip)
		print("MENUWALK main (%s) %s frame %s/%s playing %s" % [when, clip, str(info.get("frame")), str(info.get("total_frames")), str(info.get("playing"))])
	for nav in ["SoloPlayNav", "MultiPlayNav", "OptionsNav", "MyHeroes", "QuitMainMenu"]:
		var b: Dictionary = _shell.find_button(level, nav)
		if not b.found:
			_walk_fail("main menu (%s): no %s" % [when, nav])
			bad += 1
			continue
		var hits := false
		for x in _shell.list_buttons(level):
			if x.path == b.path:
				hits = x.hittable
		print("MENUWALK main (%s) %s hittable=%s" % [when, b.path, hits])
		if not hits:
			_walk_fail("main menu (%s): %s is not hittable" % [when, b.path])
			bad += 1
	var solo: Dictionary = _shell.find_button(level, "SoloPlayNav")
	if solo.found: # lane CAH-2 r2: a missing nav is a failure counted above, not a script error here
		_mouse_event(Vector2(solo.x, solo.y), false, false)
		await _step(20)
		await _walk_shot("hover-" + when)
	_mouse_event(Vector2(5, 5), false, false)
	await _step(10)
	return bad


## lane UI-2: --options-shot: the main menu's Options screen (with the OpenBFME Soft particles box) saved to FILE, then quit
## --hover1-menus (lane HOVER-1): the pointer rests on a button of each main menu navigation and on Options' sound quality box; the window is saved once
## the tool tip's delay (Mouse.ini TooltipDelayTime) has passed
func _hover1_menus_run() -> void:
	await _step(60)
	DirAccess.make_dir_recursive_absolute(_hover1_menus)
	var level: int = _shell.shell_top_level()
	for pair in [["SoloPlayNav", "SoloPlayNav.Skirmish", "skirmish"], ["MultiPlayNav", "MultiPlayNav.Online", "online"], ["OptionsNav", "OptionsNav.Settings", "settings"]]:
		await _click_button(level, pair[0])
		await _step(90)
		var b: Dictionary = _shell.find_button(level, pair[1])
		if not b.found:
			print("HOVER1 menus: no button %s" % pair[1])
			continue
		_mouse_event(Vector2(b.x, b.y), false, false)
		await _step(90) # TooltipDelayTime 800 ms, then GuiFX's ToolTip opens
		var image := _capture_image(pair[2])
		if image != null:
			var path := "%s/hover1-menu-%s.png" % [_hover1_menus, pair[2]]
			print("HOVER1 menus %s -> %s (%s)" % [pair[1], path, error_string(image.save_png(path))])
		_mouse_event(Vector2(4, 4), false, false)
		await _step(30)
		await _click_button(level, pair[0]) # the navigation closes
		await _step(60)
	get_tree().quit(0)


func _options_shot_tick() -> void:
	_options_shot_frames += 1
	if _options_shot_frames == 30:
		if not _shell.shell_push("Options.apt"):
			_fail("--options-shot: Options.apt: " + str(_shell.get_shell_report().errors))
	elif _options_shot_frames == 150:
		var image := _capture_image(_options_shot)
		if image == null:
			get_tree().quit(0)
			return
		print("GAME options shot %s -> %s" % [_options_shot, error_string(image.save_png(_options_shot))])
		get_tree().quit(0)


## lane UI-2: --end-capture: the end screen's first five seconds, every rendered frame
func _capture_end_frames() -> void:
	DirAccess.make_dir_recursive_absolute(_end_capture)
	var start := Time.get_ticks_msec()
	var times := PackedStringArray()
	var frames: Array[Image] = []
	while Time.get_ticks_msec() - start < 5000:
		await RenderingServer.frame_post_draw
		var image := _capture_image("end capture frame")
		if image == null:
			break
		image.resize(image.get_width() / 2, image.get_height() / 2, Image.INTERPOLATE_BILINEAR) # half size in memory, written afterwards
		frames.append(image)
		times.append(str(Time.get_ticks_msec() - start))
	var n := frames.size()
	for i in n:
		frames[i].save_png("%s/f%04d.png" % [_end_capture, i])
	var f := FileAccess.open(_end_capture + "/times.txt", FileAccess.WRITE)
	f.store_string("\n".join(times))
	print("GAME END capture: %d frames in %s" % [n, _end_capture])


func _save_named(name: String) -> void:
	if _screens.is_empty():
		return
	DirAccess.make_dir_recursive_absolute(_screens)
	var path := "%s/%s.png" % [_screens, name]
	var image := _capture_image(path)
	if image == null:
		return
	print("GAME screenshot %s -> %s" % [path, error_string(image.save_png(path))])


## --end: the scripted end of a game (see the header)
func _run_end() -> void:
	var faction := _faction.trim_prefix("Faction").to_lower()
	var eco: Dictionary = _world.get_economy()
	var enemy := ""
	for p in eco.players:
		if p.playable and p.name != _local_name:
			enemy = p.name
	if enemy.is_empty():
		_fail("no enemy player")
		return
	var loser := _local_name if _end_lose else enemy
	var winner := enemy if _end_lose else _local_name
	print("GAME END: %s destroys %s" % [winner, loser])
	# everything of the loser but its fortress goes (a test hook), then the winner's fortress lands the last hit on it: the last structure falls
	var objs: Array = _world.get_player_objects(loser)
	var fortress := -1
	for o in objs:
		if o.commandcenter:
			fortress = o.id
	var source := -1
	for o in _world.get_player_objects(winner):
		if o.structure:
			source = o.id
			break
	var keep := ""
	for o in objs:
		if o.id == fortress:
			keep = o.template
	if fortress >= 0 and _hud != null and _hud.has_method("camera_look_at"):
		for o in objs:
			if o.id == fortress:
				_hud.camera_look_at(Vector2(o.x, o.y))
	var k: Dictionary = _world.debug_kill_player_objects(loser, keep)
	print("GAME END killed: ", k)
	await _wait_seconds(3.0)
	# whatever the loser started in the meantime (a structure being built) goes too, so the fortress is the last structure that counts
	k = _world.debug_kill_player_objects(loser, keep)
	print("GAME END killed: ", k)
	await _wait_seconds(0.5)
	if fortress >= 0:
		var hit: Dictionary = _world.debug_damage_object(fortress, 1.0e7, source)
		print("GAME END last hit: ", hit)
	# the end screen: shown by the script (victory) or at once (the local defeat)
	var waited := 0.0
	while waited < 20.0 and not _world.get_end_game_state().get("shown", false):
		await _wait_seconds(0.25)
		waited += 0.25
	var st: Dictionary = _world.get_end_game_state()
	print("GAME END state: ", JSON.stringify(st))
	if not st.get("shown", false):
		print("GAME END victory report: ", JSON.stringify(_world.get_victory_report()))
		print("GAME END loser objects: ", JSON.stringify(_world.get_player_objects(loser)))
		_fail("the end screen was not shown")
		return
	_end_shown = true
	if not _end_capture.is_empty():
		await _capture_end_frames()
	await _wait_seconds(2.5)
	_save_named("end1-%s-%s" % [faction, "defeat" if _end_lose else "victory"])
	if _end_esc:
		# Esc during the seven-second display: the end screen movie must go with the game (HideEndGame before the game is cleared)
		print("GAME END Esc while the end screen shows: ", JSON.stringify(_world.get_end_game_state().get("showing", false)))
		# lane END-2: Esc opens the quit menu; its Exit and the confirmation leave the game (the end screen goes with it)
		await _press_escape()
		await _wait_seconds(3.0)
		if not await _click_quit("ExitMission"):
			_fail("no ExitMission button in the quit menu")
			return
		await _wait_seconds(1.5)
		if not await _click_quit("Yes"):
			_fail("no confirmation of the Exit")
			return
		var w := 0.0
		while _state != State.SCORE and w < 10.0:
			await _wait_seconds(0.25)
			w += 0.25
		await _wait_seconds(1.0)
		_save_named("end1-%s-esc-score" % faction)
		print("GAME END after Esc: end screen still showing: %s" % str(_world.get_end_game_state().get("showing", false)))
		await _finish_end_flow(faction)
		return
	# lane PLAY-1: the end-game timer (RW 0x602FFE, 25 logic frames after VICTORY / DEFEAT) leaves the game to the score screen by itself (RW 0x603533),
	# while the end screen still shows (updateEndGame hides it after 7 s of real time; leaving hides it first)
	waited = 0.0
	while waited < 20.0 and _state != State.SCORE:
		await _wait_seconds(0.25)
		waited += 0.25
	print("GAME END left by the end-game timer: %s after %.2f s" % [str(_state == State.SCORE), waited])
	if _state != State.SCORE:
		print("GAME END state: ", JSON.stringify(_world.get_end_game_state()))
		_fail("the end-game timer did not leave the game")
		return
	await _finish_end_flow(faction)


## the score screen onwards: the movie's buttons, Continue, the main menu and a second game
func _finish_end_flow(faction: String, prefix := "end1") -> void:
	if _state != State.SCORE:
		return
	await _wait_seconds(4.0)
	_save_named("%s-%s-score" % [prefix, faction])
	var sr: Dictionary = _shell.get_shell_report()
	print("GAME END score screen notes: ", JSON.stringify(sr.get("notes", {})))
	print("GAME END score screen errors: ", JSON.stringify(sr.get("errors", [])))
	var lvl: int = _shell.shell_top_level()
	print("GAME END graph: ", JSON.stringify(_shell.get_timeline_graph("", _graph_path).get("axis", {})), " at ", _graph_path)
	# Skip ends the reveal (then Continue takes its place); the other tabs of the timeline are the movie's own buttons
	if _shell.find_button(lvl, "Main.MainButtons.Skip").found:
		await _click_button(lvl, "Main.MainButtons.Skip")
		await _wait_seconds(2.0)
	for tab in ["Units", "Structures", "Resources", "FinalScore"]:
		if _shell.find_button(lvl, "Main.TabButtons." + tab).found:
			# the tab bar shows the other tabs once the reveal is over (the movie hides the current one); a slow machine needs a moment more
			for attempt in 4:
				var w := 0.0
				while w < 6.0 and not _shell.find_button(lvl, "Main.TabButtons." + tab).get("hit", "").ends_with("TabButtons.%s.Button" % tab) and _shell.get_member(lvl, _graph_path, "_graphMode").get("value", "") != tab:
					await _wait_seconds(0.25)
					w += 0.25
				await _click_button(lvl, "Main.TabButtons." + tab)
				await _wait_seconds(2.0)
				if _shell.get_member(lvl, _graph_path, "_graphMode").get("value", "") == tab:
					break
			print("GAME END tab %s: graph mode %s (member %s)" % [tab, _shell.get_timeline_graph("", _graph_path).get("mode", ""), JSON.stringify(_shell.get_member(lvl, _graph_path, "_graphMode"))])
			_save_named("%s-%s-score-%s" % [prefix, faction, tab.to_lower()])
	# More opens the statistics page; its Continue leaves (AptTimeLine::OnButtonContinue)
	if _shell.find_button(lvl, "Main.MainButtons.More").found:
		await _click_button(lvl, "Main.MainButtons.More")
		await _wait_seconds(2.5)
		# lane END-2: the statistics page lists the rows of RW 0x9CDEC1 in the StatsList gadget
		var stats: Dictionary = _shell.get_timeline_stats()
		var listed: Array = stats.get("listed", [])
		print("GAME END stats: shown %s, fills %d, columns %d, %d listed rows" % [str(stats.get("shown", [])), stats.get("fills", 0), stats.get("columns", 0), listed.size()])
		print("GAME END stats list: size %s, column widths %s, row heights %s, length %s, display height %s" % [str(stats.get("size")), str(stats.get("column_widths")),
			str(stats.get("row_heights", []).slice(0, 8)), str(stats.get("list_length")), str(stats.get("display_height"))])
		for row in listed.slice(0, 12):
			var cells := []
			for c in row:
				cells.append("%s#%08x" % [c.text, c.color])
			print("GAME END stats row: ", " | ".join(cells))
		_save_named("%s-%s-score-stats" % [prefix, faction])
	var clicked := false
	for attempt in 6:
		if await _click_button(lvl, "Main.MainButtons.Continue"):
			clicked = true
		await _wait_seconds(1.5)
		if _state != State.SCORE:
			break
	if not clicked:
		_fail("no Continue button on the score screen")
		return
	var w := 0.0
	while _state != State.MENU and w < 10.0:
		await _wait_seconds(0.25)
		w += 0.25
	if _state != State.MENU:
		_fail("Continue did not return to the menu")
		return
	await _step(LEVEL_MAIN_MENU_FRAMES + 70)
	print("GAME END Continue showed: ", str(_shell.shell_stack()))
	_save_named("%s-menu" % prefix)
	# the second game: the same scripted path through the lobby
	if not await _auto_lobby_and_start():
		return
	await _wait_seconds(3.0)
	var gs: Dictionary = _world.get_stats()
	_second_game_ok = gs.frame > 5
	print("GAME END second game: logic frame %d, %d objects, games started %d" % [gs.frame, gs.objects, _games_started])
	print("GAME END RESULT: end screen %s, score screen %s, continue %s, second game %s" % [str(_end_shown), str(not _score_report.is_empty()),
		str(_continue_pressed), str(_second_game_ok)])
	get_tree().quit(0 if (_second_game_ok and _continue_pressed and _games_started == 2) else 1)


func _wait_seconds(sec: float) -> void:
	var until := Time.get_ticks_msec() + int(sec * 1000.0)
	while Time.get_ticks_msec() < until:
		await get_tree().process_frame


# ---- lane SMOOTH-1: the render frame rate of the live game ------------------------------------------------------------------------------------------------

## lane MP-3: one line per rendered frame of the game (the logic frame shown, the frame's wall time in microseconds): the whole game's frame times for the
## average fps and the 1% / 0.1% lows (tools/net/mp3_perf_table.py)
func _perf_log_frame() -> void:
	var now := Time.get_ticks_usec()
	if _perf_log == null:
		_perf_log = FileAccess.open(_perf_log_path, FileAccess.WRITE)
		if _perf_log == null:
			printerr("GAME PERF LOG: cannot write ", _perf_log_path)
			_perf_log_path = ""
			return
		_perf_log_prev_us = now
		return
	_perf_log.store_line("%d %d" % [_world.get_presented_frame(), now - _perf_log_prev_us]) # lane PATHPERF-1: get_frame waits for the logic worker
	_perf_log_prev_us = now


func _write_measurements() -> void:
	if _perf_log != null:
		_perf_log.close()
		_perf_log = null
		_perf_log_path = ""
	if not _net_census.is_empty():
		print("GAME NET census: ", _world.net_write_census(_net_census))
		_net_census = ""


func _perf_frame(delta: float) -> void:
	var now := Time.get_ticks_usec()
	if _perf_prev_us == 0:
		_perf_prev_us = now
		_perf_last = now
		return
	_perf_times.append((now - _perf_prev_us) / 1000.0)
	_perf_deltas.append(delta * 1000.0) # the (smoothed) delta the frame's animation and interpolation advanced by
	_perf_prev_us = now
	var t: Dictionary = _world.get_frame_timings() if _world.has_method("get_frame_timings") else {}
	if _perf and _perf_times.back() > 50.0: # lane PERF-1 r2: a long frame and what the C++ side of it spent
		print("GAME HITCH %.1f ms at logic frame %d: %s" % [_perf_times.back(), _world.get_presented_frame(), JSON.stringify(t)])
	for k in ["logic_ms", "sync_ms", "streak_ms", "dyn_pose_ms", "dyn_upload_ms", "main_cpu_ms"]:
		if not _perf_parts.has(k):
			_perf_parts[k] = 0.0
		_perf_parts[k] += t.get(k, 0.0)
	if _hud != null and _hud.has_method("get_frame_timings"):
		var h: Dictionary = _hud.get_frame_timings()
		_perf_parts["hud_update_ms"] = _perf_parts.get("hud_update_ms", 0.0) + h.get("hud_update_ms", 0.0)
	if _perf_cpu0 < 0.0:
		_perf_cpu0 = _main_thread_cpu_ms()
	if _perf and not _perf_stat.is_empty() and _perf_stat_pid < 0:
		_perf_stat_index += 1
		_perf_stat_pid = OS.create_process("perf", ["stat", "-x", ",", "-e", "instructions:u,cycles:u", "-t", str(OS.get_process_id()), "-o", "%s-%d.txt" % [_perf_stat, _perf_stat_index]])
	if now - _perf_last < 5000000:
		return
	if _perf_stat_pid >= 0:
		# lane PERF-3: this window's counts are complete once perf exits; they are printed at the next window's end
		OS.execute("kill", ["-INT", str(_perf_stat_pid)])
		if not _perf_stat_done.is_empty():
			_print_perf_stat(_perf_stat_done)
		_perf_stat_done = {"file": "%s-%d.txt" % [_perf_stat, _perf_stat_index], "frames": _perf_times.size(), "index": _perf_stat_index}
		_perf_stat_pid = -1
	var n := _perf_times.size()
	var cpu1 := _main_thread_cpu_ms()
	var cpu_per_frame := (cpu1 - _perf_cpu0) / maxf(n, 1) if _perf_cpu0 >= 0.0 else -1.0
	_perf_cpu0 = cpu1
	if _perf:
		var v := _perf_times.duplicate()
		v.sort()
		var sum := 0.0
		for x in v:
			sum += x
		var parts := ""
		for k in _perf_parts.keys():
			parts += " %s=%.2f" % [k, _perf_parts[k] / maxf(n, 1)]
		var dv := _perf_deltas.duplicate()
		dv.sort()
		var low_n := maxi(1, n / 100) # lane PERF-1: the 1% low (the frame rate of the slowest 1% of the frames)
		var low_sum := 0.0
		for i in range(n - low_n, n):
			low_sum += v[i]
		print("GAME PERF main thread CPU %.2f ms per frame" % cpu_per_frame)
		print("GAME PERF frames=%d fps=%.1f low1%%=%.1f frame_ms mean %.2f p50 %.2f p95 %.2f p99 %.2f max %.2f (engine delta p5 %.2f p50 %.2f p95 %.2f) objects=%d | mean%s" % [n,
			1000.0 * n / maxf(sum, 0.001), 1000.0 * low_n / maxf(low_sum, 0.001), sum / maxf(n, 1), v[int(0.5 * (n - 1))], v[int(0.95 * (n - 1))], v[int(0.99 * (n - 1))], v[n - 1],
			dv[int(0.05 * (n - 1))], dv[int(0.5 * (n - 1))], dv[int(0.95 * (n - 1))], t.get("presented_objects", 0), parts]) # lane PATHPERF-1: get_object_count waits for the worker
		# lane RENDERWAIT-1: the window's waits on the logic worker, by site (the wait log is reset for the next window)
		if _world.has_method("get_logic_wait_sites"):
			var sites: Array = _world.get_logic_wait_sites()
			var waits := 0
			var wait_ms := 0.0
			for w in sites:
				waits += int(w.count)
				wait_ms += float(w.total_ms)
			print("GAME PERF WAITS %d waits on the logic worker, %.1f ms in the window" % [waits, wait_ms])
			for w in sites:
				print("GAME PERF WAIT %s: %d waits, total %.1f ms, worst %.1f ms" % [w.site, w.count, w.total_ms, w.worst_ms])
		# lane RENDERWAIT-1 (stop S-4280): the render frames that held the logic for the HUD's, audio's and effects' live work, those that found the worker
		# inside a frame (that work waited for a later render frame), and what the windows cost the worker
		if _world.has_method("get_client_window_stats"):
			var cw: Dictionary = _world.get_client_window_stats()
			if not _perf_windows.is_empty():
				print("GAME PERF WINDOWS held %d, missed %d (S-4280: the live client work ran in %d of %d render frames), worker parks %d (%d timed out), worker held %.1f ms, longest window %.1f ms" % [
					cw.opened - _perf_windows.opened, cw.missed - _perf_windows.missed, cw.opened - _perf_windows.opened, n, cw.parks - _perf_windows.parks,
					cw.park_timeouts - _perf_windows.park_timeouts, cw.worker_held_ms - _perf_windows.worker_held_ms, cw.get("max_window_ms", 0.0)])
			_perf_windows = cw
		if _world.has_method("reset_logic_wait_sites"):
			_world.reset_logic_wait_sites() # the wait log and the longest window start again for the next GAME PERF window
	_perf_times.clear()
	_perf_deltas.clear()
	_perf_parts.clear()
	_perf_last = now
func _print_perf_stat(w: Dictionary) -> void:
	var f := FileAccess.open(w.file, FileAccess.READ)
	if f == null:
		print("GAME PERF STAT window %d: no perf output" % w.index)
		return
	var parts := PackedStringArray()
	for line in f.get_as_text().split("\n"):
		var c := line.split(",")
		if c.size() > 2 and c[0].is_valid_int():
			parts.append("%s/frame=%.2fM" % [c[2].split(":")[0], float(c[0]) / maxf(w.frames, 1) / 1.0e6])
	print("GAME PERF STAT window %d frames=%d main %s" % [w.index, w.frames, " ".join(parts)])


# ---- lane ONLINE-2: an online game (the lobby service's START, the relay) ------------------------------------------------------------------------------

func _online_open() -> void:
	_online = load("res://scripts/online_lobby.gd").new()
	_online.lobby_url = _online_lobby if not _online_lobby.is_empty() else OS.get_environment("OPENBFME_LOBBY")
	_online.player_name = _online_name if not _online_name.is_empty() else (_net_name if _net_name != "Joiner" else "Player")
	_online.token = OS.get_environment("OPENBFME_LOBBY_TOKEN")
	_online.profile = _world.net_profile_digest()
	_online.join_id = _online_join
	_online.rejoin = _online_rejoin # lane ONLINE-3
	_online.spectate = _online_spectate
	if _online_host:
		var slots: Array = []
		for spec in _online_slots.split(";"):
			var f := spec.split(":")
			var slot := {"state": f[0]}
			if f.size() >= 4:
				slot.merge({"faction": int(f[1]), "color": slots.size(), "startPos": int(f[2]), "team": int(f[3])})
			slots.append(slot)
		_online.host_setup = {"name": "%s's game" % _online.player_name, "players": _online_players,
			"setup": {"map": _map_key, "startingCash": 1500, "slots": slots}}
	_online.started.connect(_on_online_started)
	_online.failed.connect(func(m: String): _fail("online: " + m))
	add_child(_online)
	print("GAME ONLINE lobby %s, profile %s, %s" % [_online.lobby_url, _online.profile, "rejoin" if _online_rejoin else ("spectate " + _online_join) if _online_spectate else ("join " + _online_join) if not _online_join.is_empty() else "host"])
	_online.begin()


# ---- lane ONLINE-4: the online screens (OnlineShell.apt on the lobby service) -----------------------------------------------------------------------------

## The lobby the online screens use: --online-lobby, OPENBFME_LOBBY, Options.ini's OpenBFMEOnlineLobby, then the release default
func _online_lobby_url() -> String:
	if not _online_lobby.is_empty():
		return _online_lobby
	var env := OS.get_environment("OPENBFME_LOBBY")
	if not env.is_empty():
		return env
	var opts: Dictionary = _shell.get_options()
	var configured: String = str(opts.get("OpenBFMEOnlineLobby", ""))
	return configured if not configured.is_empty() else ONLINE_DEFAULT_LOBBY


## The main menu's Online (RW 0x91D7A3 pushes OnlineShell.apt): the shell's lobby client, its HTTP transport, then the screens
func _open_online_shell() -> void:
	_sync_simulation_profile() # the lobby compares the profile identity at the join
	var url := _online_lobby_url()
	var name := _online_name if not _online_name.is_empty() else str(_shell.get_options().get("OpenBFMEOnlineName", ""))
	_online_saved_name = name
	# lane ONLINE-4: ONLINE-3's kept lobby session (a restarted game signs in as the same player: the screens offer its running game's Rejoin)
	var session := ""
	if FileAccess.file_exists(ONLINE_SESSION_FILE):
		session = FileAccess.get_file_as_string(ONLINE_SESSION_FILE).strip_edges()
	_online_saved_session = session
	var r: Dictionary = _shell.set_online(_world, {"lobby_url": url, "remembered_name": name, "session_token": session})
	if not r.ok:
		_fail("online screens: " + str(r.get("error", "")))
		return
	if _online_http != null:
		_online_http.queue_free()
	_online_http = load("res://scripts/online_http.gd").new()
	_online_http.name = "OnlineHttp"
	_online_http.shell = _shell
	_online_http.lobby_url = url
	add_child(_online_http)
	print("GAME ONLINE screens: lobby %s (online play is a test feature)" % url)
	_online_test_note()
	if not _shell.shell_push("OnlineShell.apt"):
		_fail("the shell refused OnlineShell.apt: " + str(_shell.get_shell_report().errors))


## The whole Online menu is a test feature (docs/PLAYING.md): a note in the top left corner says so while its screens are up (the retail screens keep
## that corner free; the version corner is top right)
func _online_test_note() -> void:
	if _online_note != null:
		return
	_online_note = CanvasLayer.new()
	_online_note.layer = 100
	_online_note.name = "OnlineTestNote"
	var label := Label.new()
	label.text = "Online play is a test feature"
	label.add_theme_font_size_override("font_size", 13)
	label.add_theme_color_override("font_color", Color(1, 0.9, 0.6, 0.9))
	label.add_theme_color_override("font_shadow_color", Color(0, 0, 0, 0.9))
	label.add_theme_constant_override("shadow_offset_x", 1)
	label.add_theme_constant_override("shadow_offset_y", 1)
	label.offset_left = 8
	label.offset_top = 30 # lane ONLINE-5: under the fps counter's line (scripts/fps_overlay.gd draws at y 4, 16 px): the two never overlap
	label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	label.set_meta("game_text", true) # the game's own text (not a developer overlay)
	_online_note.add_child(label)
	add_child(_online_note)


## lane ONLINECOUNT-1: "N players online" (scripts/online_stats.gd). The main menu shows it in its own line, the online menu's pages under the test-feature note;
## the count is asked for only while one of those is on top, never in a game, and not at all when the player turned it off
func _online_count_tick() -> void:
	var stack: PackedStringArray = _shell.shell_stack()
	var mode := ""
	if _menu_revealed and stack.size() > 0:
		# the staging room (the player is in a game of the lobby) shows no count and asks nothing: its own polls keep the player counted
		mode = ("" if _shell.online_status().get("in_game", false) else "online") if stack.has("OnlineShell.apt") else ("menu" if stack[-1] == "MainMenu.apt" else "")
	if _stats == null:
		if mode.is_empty() or not _stats_off.is_empty():
			return
		var named := not _online_lobby.is_empty() or not OS.get_environment("OPENBFME_LOBBY").is_empty()
		var dev := not OS.has_feature("template") or DisplayServer.get_name() == "headless" or _scripted_run()
		var made: Dictionary = OnlineStats.start(self, _online_lobby_url(), func() -> String: return str(_shell.online_status().get("session_token", "")),
			OS.get_cmdline_user_args(), OS.get_environment(OnlineStats.OFF_ENV), _shell.get_options(), OnlineStats.read_launcher_state(), dev and not named)
		if made.why != "":
			_stats_off = made.why
			print("GAME ONLINE count: not asked for: %s" % _stats_off)
			return
		print("GAME ONLINE count: asked for from the lobby while the menus show; ", OnlineStats.STOP_LINE)
		_stats = made.node
		_stats.visible_check = func() -> bool: return DisplayServer.get_name() == "headless" or (DisplayServer.window_get_mode() != DisplayServer.WINDOW_MODE_MINIMIZED and get_window().has_focus())
		_stats.changed.connect(_online_count_show)
	_stats.set_mode(mode)
	if _stats_label != null:
		_stats_label.position = Vector2(8, 48) if mode == "online" else Vector2(8, 30)


func _online_count_show(text: String) -> void:
	if _stats_label == null:
		var layer := CanvasLayer.new()
		layer.layer = 100
		layer.name = "OnlineCountLayer"
		_stats_label = Label.new()
		_stats_label.name = "OnlineCount"
		_stats_label.add_theme_font_size_override("font_size", 13)
		_stats_label.add_theme_color_override("font_color", Color(1, 0.9, 0.6, 0.9))
		_stats_label.add_theme_color_override("font_shadow_color", Color(0, 0, 0, 0.9))
		_stats_label.add_theme_constant_override("shadow_offset_x", 1)
		_stats_label.add_theme_constant_override("shadow_offset_y", 1)
		_stats_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
		_stats_label.tooltip_text = "Players in OpenBFME's online lobbies and games"
		_stats_label.set_meta("game_text", true)
		layer.add_child(_stats_label)
		add_child(layer)
	_stats_label.text = text
	_stats_label.visible = not text.is_empty()
	print("GAME ONLINE count shown: %s" % (text if not text.is_empty() else "(nothing)"))


## The guest name of the last sign-in goes to Options.ini (OpenBFMEOnlineName); a scripted run never writes the player's file
func _online_keep_name() -> void:
	var st: Dictionary = _shell.online_status()
	var session: String = str(st.get("session_token", ""))
	if not session.is_empty() and session != _online_saved_session:
		# lane ONLINE-4: the lobby session goes where ONLINE-3's rejoin reads it (also in a scripted run: the rejoin test restarts the game)
		_online_saved_session = session
		var f := FileAccess.open(ONLINE_SESSION_FILE, FileAccess.WRITE)
		if f != null:
			f.store_string(session + "\n")
			print("GAME ONLINE lobby session kept for a rejoin")
	var name: String = str(st.get("remembered_name", ""))
	if name.is_empty() or name == _online_saved_name:
		return
	_online_saved_name = name
	if _scripted_run():
		print("GAME ONLINE remembered name %s (a scripted run: Options.ini not written)" % name)
		return
	if _shell.set_option("OpenBFMEOnlineName", name):
		var saved: Dictionary = _shell.save_options()
		print("GAME ONLINE remembered name %s: %s" % [name, "saved" if saved.ok else str(saved.error)])


func _online_shot(screen: String) -> void:
	if _online_shots.is_empty():
		return
	DirAccess.make_dir_recursive_absolute(_online_shots)
	var size := get_viewport().get_visible_rect().size
	var path := _online_shots.path_join("online-%s-%dx%d.png" % [screen, int(size.x), int(size.y)])
	print("GAME ONLINE shot %s -> %s" % [screen, error_string(get_viewport().get_texture().get_image().save_png(path))])


## waits until `cond` (a Callable on online_status) holds, at most `sec` seconds; false on a timeout (reported)
func _online_wait(what: String, cond: Callable, sec: float) -> bool:
	var until := Time.get_ticks_msec() + int(sec * 1000.0)
	while Time.get_ticks_msec() < until:
		var st: Dictionary = _shell.online_status()
		if st.get("open", false) and cond.call(st):
			return true
		await get_tree().process_frame
	printerr("GAME ONLINE WALK timeout waiting for %s: %s" % [what, AptMenuPlayer.online_redact(str(_shell.online_status()))])
	return false


## a click on a button of the shell movie: the pointer to its centre, then the button down and up on later frames (as a player's)
func _online_click(path: String) -> void:
	_online_apply({"point": path})
	await get_tree().process_frame
	await get_tree().process_frame
	_online_apply({"mouse": true})
	await get_tree().process_frame
	await get_tree().process_frame
	_online_apply({"mouse": false})
	await get_tree().process_frame


## lane ONLINE-5: a click on start spot `n` of the MAP tab's map window (the pointer to its centre, then the button down and up, as a player's)
func _online_spot(n: int) -> void:
	_online_apply({"start_spot": n})
	await get_tree().process_frame
	await get_tree().process_frame
	_online_apply({"mouse": true})
	await get_tree().process_frame
	await get_tree().process_frame
	_online_apply({"mouse": false})
	await get_tree().process_frame


func _online_apply(spec: Dictionary) -> void:
	var r: Dictionary = _shell.online_apply(spec)
	print("GAME ONLINE WALK %s -> %s" % [JSON.stringify(spec), "ok" if r.ok else str(r.errors)])


## --online-walk=host|join: the online screens driven through the movies' own commands (the proof screenshots and the headless end-to-end test)
func _online_walk_run() -> void:
	while not _menu_revealed:
		await get_tree().process_frame
	await _wait_seconds(1.0)
	await _wait_seconds(2.0) # lane ONLINECOUNT-1: the count's answer
	_online_shot("main-menu")
	print("GAME ONLINE WALK: the main menu's Online")
	_shell.shell_fscommand("AptMainMenu::OnlineButtonPressed", "")
	if not await _online_wait("the sign-in page", func(st): return st.get("page", "") == "OnlineLogin", 30.0):
		return _online_walk_end(false)
	await _wait_seconds(2.0)
	_online_shot("login")
	_online_apply({"nickname": _online_name if not _online_name.is_empty() else ("Host" if _online_walk == "host" else "Joiner")})
	_online_apply({"command": "AptOnline::Login::Login"})
	if not await _online_wait("the home page", func(st): return st.get("signed_in", false) and st.get("page", "") == "OnlineHome", 30.0):
		return _online_walk_end(false)
	await _online_wait("the message of the day", func(st): return not st.get("motd", []).is_empty() and st.motd[0] != "...", 10.0)
	await _wait_seconds(2.0)
	_online_shot("home")
	print("GAME ONLINE WALK motd: ", _shell.online_status().get("motd", []))
	if _online_walk == "rejoin":
		return _online_walk_rejoin()
	await _online_click("Toggles.ButtonNav.OpenPlay") # the shell's nav button
	if not await _online_wait("Open Play", func(st): return st.get("page", "") == "OnlineOpenPlay" and int(st.get("state", -1)) == 2, 30.0):
		return _online_walk_end(false)
	await _wait_seconds(2.0)
	if _online_walk == "host":
		_online_shot("openplay-empty")
		await _online_click("*Controls.CreateGame") # the page's Create Game (the movie opens its pop-up: AptOnline::OnOpenCreateDialog)
		await _wait_seconds(1.5)
		_online_apply({"game_name": "%s's test game" % str(_shell.online_status().get("name", "Host")), "game_password": _online_password})
		await _wait_seconds(0.5)
		_online_shot("create")
		await _online_click("*CreateGamePopup.Accept") # AptOnline::CustomMatch::CreateGame
		if not await _online_wait("the staging room", func(st): return st.get("in_game", false) and int(st.get("state", -1)) == 6, 30.0):
			return _online_walk_end(false)
		await _wait_seconds(2.0)
		_online_shot("staging-host")
		var players := _online_players
		# lane ONLINE-5: once the other players are in, Play Game before anyone is ready: refused by name, and every player reads why in the chat
		if await _online_wait("%d players in the room" % players, func(st):
				var joined := 0
				for line in st.get("chat", []):
					joined += 1 if str(line).ends_with(" joined") else 0
				return joined >= players - 1, 180.0):
			_online_apply({"chat_tab": true})
			await _wait_seconds(1.0)
			await _online_click("*LowerButtons_Host.PlayGame")
			if await _online_wait("the start refused while a player is not ready", func(st): return not (st.get("start_refusals", []) as Array).is_empty(), 15.0):
				print("GAME ONLINE WALK start refused: ", _shell.online_status().get("start_refusals", []))
				await _wait_seconds(1.5)
				_online_shot("staging-start-refused")
			# the host's start position: the MAP tab's map, a click on its first spot
			await _online_click("*TabButtons.MapTab")
			if await _online_wait("the map's start spots", func(st): return int(st.get("start_spots", 0)) > 0, 15.0):
				await _online_spot(0)
				await _online_wait("the host's start position", func(st): return int((st.get("start_pos", []) as Array)[0]) == 0, 15.0)
				await _online_wait("every player's start position", func(st):
						var n := 0
						for p in st.get("start_pos", []):
							n += 1 if int(p) >= 0 else 0
						return n >= mini(players, int(st.get("start_spots", 0))), 30.0)
				await _wait_seconds(1.0)
				_online_shot("staging-map-host")
				print("GAME ONLINE WALK start positions: ", _shell.online_status().get("start_pos", []))
			# the RULES tab (the host's rule rows), for the comparison with retail's
			await _online_click("*TabButtons.RulesTab")
			await _wait_seconds(1.5)
			_online_shot("staging-rules-host")
		if not await _online_wait("%d players ready" % players, func(st):
				var ready: Array = st.get("ready", [])
				var n := 0
				for i in range(ready.size()):
					n += 1 if ready[i] or i == int(st.get("slot", -1)) else 0
				return n >= players, 180.0):
			return _online_walk_end(false)
		_online_apply({"chat_tab": true})
		await _wait_seconds(2.0)
		_online_shot("staging-ready")
		_online_apply({"command": "AptOnline::CustomMatch::PlayGame"})
		await _wait_seconds(0.3)
		_online_shot("connecting")
		# no relay: the lobby refuses the START and the player is told (docs/ONLINE.md section 5 step 5); with one the game starts
		if not await _online_wait("the START or its refusal", func(st): return not st.get("boxes", []).is_empty() or _net_state != "", 30.0):
			return _online_walk_end(false)
		if _net_state == "":
			await _wait_seconds(2.0)
			_online_shot("no-relay")
			print("GAME ONLINE WALK boxes: ", _shell.online_status().get("boxes", []))
			return _online_walk_end(str(_shell.online_status().get("boxes", [])).contains("no_relay"))
		print("GAME ONLINE WALK: the START arrived: the game loads")
		return
	# join
	if not await _online_wait("an open game in the list", func(st):
			for r in st.get("games", []):
				if not str(r).ends_with("|watch") and not str(r).ends_with("|rejoin"):
					return true
			return false, 60.0):
		return _online_walk_end(false)
	_online_apply({"command": "GameSorter::OnSortName"})
	await _wait_seconds(1.5)
	_online_shot("browser")
	print("GAME ONLINE WALK games: ", _shell.online_status().get("games", []))
	# the first open game (a live lobby also lists running games: their WATCH / REJOIN rows are not joined here)
	var join_row := 0
	var listed: Array = _shell.online_status().get("games", [])
	for i in range(listed.size()):
		if not str(listed[i]).ends_with("|watch") and not str(listed[i]).ends_with("|rejoin"):
			join_row = i
			break
	_online_apply({"select_row": join_row})
	_online_apply({"command": "AptOnline::CustomMatch::JoinGame"})
	if not _online_password.is_empty():
		if not await _online_wait("the password prompt", func(st): return int(st.get("state", -1)) == 10, 15.0):
			return _online_walk_end(false)
		await _wait_seconds(1.0)
		_online_apply({"game_password": _online_password})
		_online_shot("password")
		_online_apply({"command": "AptOnline::CustomMatch::AcceptJoinGame"})
	if not await _online_wait("the staging room", func(st): return st.get("in_game", false) and int(st.get("state", -1)) == 12, 30.0):
		return _online_walk_end(false)
	await _wait_seconds(2.0)
	_online_apply({"chat": "Hello from the joiner"}) # the chat entry's line, sent as AptMpChat::Send does (through the lobby)
	# lane ONLINE-5: ready through the row's own Ready box (the movie's button, as a player's click). Another player's join or change unreadies
	# everyone (the lobby), so the box is pressed again whenever it is unchecked, until the START comes
	var own_ready := func(st): return (st.get("ready", []) as Array)[int(st.get("slot", 0))]
	await _wait_seconds(1.0)
	# the joiner's start position: the MAP tab's map, a click on the spot of its own slot number (when the map has that many)
	var own_slot := int(_shell.online_status().get("slot", 0))
	await _online_click("*TabButtons.MapTab")
	if await _online_wait("the map's start spots", func(st): return int(st.get("start_spots", 0)) > 0, 15.0):
		if own_slot < int(_shell.online_status().get("start_spots", 0)):
			await _online_spot(own_slot)
			await _online_wait("the joiner's start position", func(st): return int((st.get("start_pos", []) as Array)[own_slot]) == own_slot, 15.0)
		await _wait_seconds(1.0)
		_online_shot("staging-map-join")
	await _online_click("*TabButtons.ChatTab")
	await _wait_seconds(1.0)
	_online_shot("staging-join-unready")
	await _online_click("*~%d.Ready" % int(_shell.online_status().get("slot", 0)))
	if not await _online_wait("ready", own_ready, 15.0):
		return _online_walk_end(false)
	await _wait_seconds(2.0)
	_online_shot("staging-join")
	# the joiner waits for the START (or the host's refusal: then nothing comes; the walk ends after a while)
	var until := Time.get_ticks_msec() + 90000
	while Time.get_ticks_msec() < until:
		if _net_state != "":
			print("GAME ONLINE WALK: the START arrived: the game loads")
			return
		var st: Dictionary = _shell.online_status()
		if st.get("in_game", false) and not own_ready.call(st):
			print("GAME ONLINE WALK: unreadied by a change in the room: the Ready box again")
			await _online_click("*~%d.Ready" % int(st.get("slot", 0)))
			await _online_wait("ready again", own_ready, 15.0)
		await _wait_seconds(0.5)
	_online_walk_end(true)


## --online-walk=rejoin (lane ONLINE-4): the game restarted after a crash; signed in with the kept session (above), the running game is offered: the
## offer's box (No), the REJOIN row of Open Play, Join Game, then the catch-up (--net-shot-rejoin) and the game
func _online_walk_rejoin() -> void:
	if not await _online_wait("the rejoin offer", func(st): return str(st.get("boxes", [])).contains("rejoin-offer"), 30.0):
		return _online_walk_end(false)
	await _wait_seconds(2.0)
	_online_shot("rejoin-offer")
	_online_apply({"box": "No"}) # the player looks at the server browser first
	await _wait_seconds(1.0)
	await _online_click("Toggles.ButtonNav.OpenPlay")
	if not await _online_wait("the REJOIN row", func(st):
			for r in st.get("games", []):
				if str(r).ends_with("|rejoin"):
					return true
			return false, 30.0):
		return _online_walk_end(false)
	await _wait_seconds(2.0)
	var rows: Array = _shell.online_status().get("games", [])
	var row := 0
	for i in range(rows.size()):
		if str(rows[i]).ends_with("|rejoin"):
			row = i
	_online_apply({"select_row": row})
	await _wait_seconds(0.5)
	_online_shot("rejoin-row")
	await _online_click("*Controls.JoinGame") # the page's own Join Game: the rejoin START
	if not await _online_wait("the rejoin START", func(_st): return _net_state != "", 30.0):
		return _online_walk_end(false)
	print("GAME ONLINE WALK: the rejoin START arrived: catching up")


## --capture-dir (lane ONLINE-4): one picture of the window every 66 ms of wall-clock time, named by its unix time in ms
func _capture_tick() -> void:
	var now := Time.get_ticks_msec()
	if now < _capture_next:
		return
	_capture_next = now + 66
	var name := "c%013d.jpg" % int(Time.get_unix_time_from_system() * 1000.0)
	get_viewport().get_texture().get_image().save_jpg(_capture_dir.path_join(name), 0.85)


func _online_walk_end(ok: bool) -> void:
	print("GAME ONLINE WALK RESULT: %s" % ("ok" if ok else "FAILED"))
	get_tree().quit(0 if ok else 1)


func _on_online_started(start_json: String) -> void:
	var r: Dictionary = _world.net_online(start_json)
	print("GAME ONLINE start: ", AptMenuPlayer.online_redact(str(r)))
	if not r.ok:
		var why := AptMenuPlayer.online_redact(str(r.get("error", "")))
		# Sol r1: from the online screens a START that cannot be played is a lobby error the player is told about (back at the game list); the
		# command-line path (ONLINE-2's --online-*) has no screen to go back to
		if _online == null and _shell.online_start_failed(why):
			printerr("GAME ONLINE error: the START was not played: ", why)
			return
		_fail("online: " + why)
		return
	_net_state = "lobby" # net_poll reports the START as started: the LAN path loads and plays it


# ---- lane MP-1: a LAN game ---------------------------------------------------------------------------------------------------------------------

func _net_open() -> void:
	var r: Dictionary
	if _net_host > 0:
		var slots: Array = []
		var i := 0
		for spec in _net_slots.split(";"):
			var f := spec.split(":")
			var states := {"human": 6, "easy": 2, "medium": 3, "hard": 4, "brutal": 5}
			slots.append({"state": states[f[0]], "name": ("Human%d" % i) if f[0] == "human" else ("Computer%d" % i), "faction": f[1], "color": i,
				"start_pos": int(f[2]), "team": int(f[3]), "player_template": -1})
			i += 1
		while slots.size() < 8:
			slots.append({"state": 1, "name": "", "faction": "", "color": -1, "start_pos": -1, "team": -1, "player_template": -1}) # SLOT_CLOSED
		var message := {"mode": "skirmish", "map": _map_key, "map_crc": 0, "map_size": 0, "seed": _seed if _seed != 0 else Time.get_ticks_msec(), "starting_cash": 1500, "slots": slots}
		r = _world.net_host(_net_host, message, _net_run_ahead, _net_crc)
	else:
		r = _world.net_join(_net_join, _net_name)
	print("GAME NET open: ", r)
	if not r.ok:
		_fail("network: " + str(r.get("error", "")))
		return
	_net_state = "lobby"


func _net_poll_lobby() -> void:
	var p: Dictionary = _world.net_poll()
	if p.state == "error":
		_fail("network lobby: " + str(p.error))
	elif p.state == "started":
		_net_state = "loading"
		print("GAME NET lobby started: local slot %d, run-ahead %d, CRC interval %d" % [p.local_slot, p.run_ahead, p.crc_interval])
		_begin_game(p.message)


func _net_begin() -> void:
	var opts := {"script": _net_script, "desync_dir": OS.get_user_data_dir(), "census": not _net_census.is_empty()}
	if not _net_record.is_empty():
		opts["record"] = _net_record
	elif _record:
		opts["record"] = _last_replay_path() # lane MP-2: every LAN game is recorded
	if _net_disconnect_ms >= 0:
		opts["disconnect_ms"] = _net_disconnect_ms
	if _net_player_timeout_ms >= 0:
		opts["player_timeout_ms"] = _net_player_timeout_ms
	var r: Dictionary = _world.net_begin(opts)
	print("GAME NET begin: ", r)
	if not r.ok:
		_fail("network: " + str(r.get("error", "")))
		return
	_net_state = "playing"
	if _net_overlay:
		var layer := CanvasLayer.new()
		layer.layer = 100
		add_child(layer)
		_net_label = Label.new()
		_net_label.position = Vector2(12, 8)
		_net_label.add_theme_font_size_override("font_size", 17)
		_net_label.add_theme_color_override("font_color", Color(1, 1, 0.4))
		_net_label.add_theme_color_override("font_outline_color", Color(0, 0, 0))
		_net_label.add_theme_constant_override("outline_size", 6)
		layer.add_child(_net_label)


# lane NETLAG-1 (--net-press=TEMPLATE): from logic frame 20 the local player's first own object whose command set shows a button producing TEMPLATE is
# selected and the button pressed through the HUD (InGameHudNode.press_command_button: ControlBar::pressButton, the HUD's command path into the lockstep);
# then the producer's queue is read every render frame until the unit is in it: "GAME NET PRESS" names the logic frames and the milliseconds
func _net_press_tick(st: Dictionary) -> void:
	if _net_press.is_empty() or _net_press_state == 2 or _hud == null or _state != State.PLAYING:
		return
	if not _world.is_logic_held():
		return # RENDERWAIT-1: the HUD's live reads and the press only in the client window (a render frame never waits for the logic)
	if _net_press_state == 0:
		if int(st.frame) < 20:
			return
		_net_press_candidates = _world.get_player_objects(_local_name)
		_net_press_state = 3
		_net_press_wait = -1
	if _net_press_state == 3:
		# one own object at a time: select it, give the control bar a few frames to show its command set, then press (the HUD's own timing)
		if _net_press_wait < 0:
			if _net_press_candidates.is_empty():
				print("GAME NET PRESS %s: no own object shows the button at logic frame %d" % [_net_press, int(st.frame)])
				_net_press_state = 2
				return
			_net_press_producer = int(_net_press_candidates[0].id)
			# as a player's click: the selection message goes through the lockstep before the press (the logic queues on the selected producer)
			_net_press_wait = 3 if _hud.select_object_with_message(_net_press_producer) else 0
			return
		_net_press_wait -= 1
		if _net_press_wait > 0:
			return
		var o: Dictionary = _net_press_candidates.pop_front()
		_net_press_wait = -1
		var before: Dictionary = _world.get_production(_local_index, int(o.id))
		if before.get("ok", false) and _hud.press_command_button(_net_press):
			_net_press_state = 1
			_net_press_queue = before.get("queue", []).size()
			_net_press_frame = int(st.frame)
			_net_press_ms = Time.get_ticks_msec()
			print("GAME NET PRESS %s pressed on %s %d at logic frame %d (queue %d)" % [_net_press, o.template, _net_press_producer, _net_press_frame, _net_press_queue])
		return
	var now: Dictionary = _world.get_production(_local_index, _net_press_producer)
	var queue: Array = now.get("queue", [])
	if queue.size() > _net_press_queue:
		_net_press_state = 2
		# the exact entry: the queue's new last entry (its template, count and cost), and the logic frame it was first seen on
		var entry: Dictionary = queue[queue.size() - 1]
		print("GAME NET PRESS %s queued at logic frame %d: %d frames, %d ms after the press (run-ahead %d)" % [_net_press, int(st.frame),
			int(st.frame) - _net_press_frame, Time.get_ticks_msec() - _net_press_ms, int(st.get("run_ahead", -1))])
		print("GAME NET PRESS ENTRY template %s quantity_total %d quantity_produced %d queue_before %d queue_after %d frame_pressed %d frame_seen %d" % [
			str(entry.get("template", "")), int(entry.get("quantity_total", -1)), int(entry.get("quantity_produced", -1)), _net_press_queue, queue.size(),
			_net_press_frame, int(st.frame)])


func _net_tick() -> void:
	var st: Dictionary = _world.net_status()
	if not st.get("active", false):
		return
	if st.has("spectate"):
		_net_spectate_tick(st) # lane ONLINE-3
		return
	if _net_label:
		var waiting := "" if st.waiting_slots.is_empty() else "  waiting for %s" % str(st.waiting_slots)
		# INFRA-2: an online host (--online-host) is the host too (the overlay said JOIN)
		var hosting := _net_host > 0 or _lan_mode == "host" or _online_host
		_net_label.text = "%s  frame %d  hash %08X  CRC checks %d  desyncs %d%s" % ["HOST" if hosting else "JOIN", st.frame, st.hash & 0xFFFFFFFF,
			st.crc_checks_passed, st.desyncs.size(), waiting]
		if st.has("relay"):
			# lane ONLINE-2: an online game's relay link (no address on screen)
			_net_label.text += "  ONLINE via relay: %s, round trip %d ms" % ["bound" if st.relay.bound else "not bound", int(st.relay.round_trip_ms)]
	if st.frame / 50 != _net_last_print:
		_net_last_print = st.frame / 50
		# lane RENDERWAIT-1: the hash is the last one taken where the logic was held (of frame hash_frame)
		print("GAME NET frame %d hash %08X (frame %d) crc_checks %d desyncs %d stalled %d" % [st.frame, st.hash & 0xFFFFFFFF, st.get("hash_frame", st.frame), st.crc_checks_passed,
			st.desyncs.size(), st.stalled_frames])
	if not _net_capture.is_empty():
		# the window as drawn after the frame's own _process (its overlay shows the frame): saved on the next _process
		if _net_captured >= 0:
			get_viewport().get_texture().get_image().save_png("%s/f%05d.png" % [_net_capture, _net_captured])
			_net_captured = -1
		if st.frame != _net_last_frame:
			_net_last_frame = st.frame
			_net_captured = st.frame
	if not _net_shot.is_empty() and st.frame >= _net_shot_frame:
		print("GAME NET shot %s at frame %d -> %s" % [_net_shot, st.frame, error_string(get_viewport().get_texture().get_image().save_png(_net_shot))])
		_net_shot = ""
	for d in st.desyncs:
		printerr(d)
	if not st.desyncs.is_empty() and not _desync_shown:
		_desync_shown = true
		_show_desync_box(st.get("desync_dumps", []))
	_net_disconnect_tick(st.get("disconnect", {}))
	_net_pause_tick(st) # lane ONLINE-3
	_net_press_tick(st) # lane NETLAG-1
	if _state != State.PLAYING:
		return
	if _net_frames > 0 and st.frame >= _net_frames:
		print("GAME NET status: ", JSON.stringify(st))
		_write_measurements()
		if _net_end:
			if not _net_end_started and _games_started == 1:
				_net_end_started = true
				_net_end_flow(st.desyncs.size())
			return
		_world.net_finish()
		get_tree().quit(2 if st.desyncs.size() > 0 else 0)


## lane ONLINE-3: a spectator: its state and the observed player on screen, Tab / Shift+Tab, the proof pictures, --net-frames
func _net_spectate_tick(st: Dictionary) -> void:
	var sp: Dictionary = st.spectate
	if _spectate_label == null:
		var layer := CanvasLayer.new()
		layer.layer = 101
		add_child(layer)
		_spectate_label = Label.new()
		_spectate_label.anchor_left = 0.0
		_spectate_label.anchor_right = 1.0
		_spectate_label.offset_top = 96
		_spectate_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		_spectate_label.add_theme_font_size_override("font_size", 22)
		_spectate_label.add_theme_color_override("font_color", Color(1, 0.92, 0.6))
		_spectate_label.add_theme_color_override("font_outline_color", Color(0, 0, 0))
		_spectate_label.add_theme_constant_override("outline_size", 8)
		layer.add_child(_spectate_label)
	var state := str(sp.state)
	if state != _spectate_state:
		print("GAME NET spectate: %s at frame %d (spectator %d, delay %d s, served %d, %d CRCs compared, %d ms catching up)" % [state, int(sp.frame), int(sp.index),
			int(sp.delay), int(sp.served), int(sp.crcs_compared), int(sp.catch_up_ms)])
		_spectate_state = state
		if state == "live":
			_spectate_live_ms = Time.get_ticks_msec()
	if state == "failed":
		_fail("spectate: " + str(sp.failure))
		return
	if st.frame / 50 != _net_last_print:
		_net_last_print = st.frame / 50
		print("GAME NET frame %d hash %08X crcs_compared %d crc_mismatches %d" % [st.frame, st.hash & 0xFFFFFFFF, int(sp.crcs_compared), int(sp.crc_mismatches)])
	# Tab / Shift+Tab: the next / previous player's view (retail's observeNextPlayer); --net-observe-at the same once
	var down := Input.is_key_pressed(KEY_TAB)
	var cycle := 0
	if down and not _observe_key_down:
		cycle = -1 if Input.is_key_pressed(KEY_SHIFT) else 1
	_observe_key_down = down
	if _net_observe_at >= 0 and int(st.frame) >= _net_observe_at:
		_net_observe_at = -1
		cycle = 1
	var obs: Dictionary = sp.observed
	if cycle != 0:
		obs = _world.net_observe_next(cycle > 0).observed
		print("GAME NET observe: %s" % ("every player" if obs.all else "%s (%s)" % [obs.name, obs.faction]))
		_observe_ms = Time.get_ticks_msec() if not obs.all else 0
	var text := "Spectating" + (" (%d s behind)" % int(sp.delay) if int(sp.delay) > 0 else "")
	match state:
		"binding":
			text += ": connecting to the relay"
		"catching_up":
			text += ": catching up, frame %d of %d" % [int(sp.frame), int(sp.served)]
		"ended":
			text += ": the game has ended"
	if obs.all:
		text += "  -  every player  -  Tab: observe a player"
	else:
		text += "  -  observing %s: %d resources, command points %d / %d, power %d, rank %d  -  Tab: next" % [obs.display_name if not str(obs.display_name).is_empty()
			else obs.name, int(obs.money), int(obs.cp_used), int(obs.cp_limit), int(obs.power_points), int(obs.rank)]
	_spectate_label.text = text
	if state == "live" and not _net_shot_spectate.is_empty() and Time.get_ticks_msec() - _spectate_live_ms > 3000:
		print("GAME NET spectate screenshot %s at frame %d: %s" % [_net_shot_spectate, st.frame, error_string(get_viewport().get_texture().get_image().save_png(_net_shot_spectate))])
		_net_shot_spectate = ""
	if not _net_shot.is_empty() and st.frame >= _net_shot_frame:
		print("GAME NET shot %s at frame %d -> %s" % [_net_shot, st.frame, error_string(get_viewport().get_texture().get_image().save_png(_net_shot))])
		_net_shot = ""
	if _observe_ms > 0 and not _net_shot_observe.is_empty() and Time.get_ticks_msec() - _observe_ms > 3000:
		print("GAME NET observe screenshot %s at frame %d: %s" % [_net_shot_observe, st.frame, error_string(get_viewport().get_texture().get_image().save_png(_net_shot_observe))])
		_net_shot_observe = ""
	if _state == State.PLAYING and ((_net_frames > 0 and st.frame >= _net_frames) or (state == "ended" and _net_frames > 0)):
		print("GAME NET status: ", JSON.stringify(st))
		_world.net_finish()
		get_tree().quit(2 if int(sp.crc_mismatches) > 0 else 0)


## lane ONLINE-3: the multiplayer pause (the Pause key, --net-pause-at / --net-resume-after-ms) and the rejoin's catch-up, with their lines on screen
func _net_pause_tick(st: Dictionary) -> void:
	if _pause_label == null:
		var layer := CanvasLayer.new()
		layer.layer = 101
		add_child(layer)
		_pause_label = Label.new()
		_pause_label.anchor_left = 0.0
		_pause_label.anchor_right = 1.0
		_pause_label.offset_top = 120
		_pause_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		_pause_label.add_theme_font_size_override("font_size", 26)
		_pause_label.add_theme_color_override("font_color", Color(1, 0.92, 0.6))
		_pause_label.add_theme_color_override("font_outline_color", Color(0, 0, 0))
		_pause_label.add_theme_constant_override("outline_size", 8)
		layer.add_child(_pause_label)
	var rejoin: Dictionary = st.get("rejoin", {})
	var pause: Dictionary = st.get("pause", {})
	var text := ""
	if rejoin.get("state", "") == "catching_up":
		text = "Rejoining the game: catching up, frame %d" % int(rejoin.frame)
		if _rejoin_since == 0:
			_rejoin_since = Time.get_ticks_msec()
			print("GAME NET rejoin: catching up from the relay's log")
		if not _net_shot_rejoin.is_empty() and Time.get_ticks_msec() - _rejoin_since > 500:
			print("GAME NET rejoin screenshot %s: %s" % [_net_shot_rejoin, error_string(get_viewport().get_texture().get_image().save_png(_net_shot_rejoin))])
			_net_shot_rejoin = ""
	elif rejoin.get("state", "") == "failed" and _rejoin_since >= 0:
		_rejoin_since = -1
		_fail("rejoin: " + str(rejoin.failure))
		return
	elif rejoin.get("state", "") == "live" and _rejoin_since > 0:
		print("GAME NET rejoin: live at frame %d after %d ms (%d CRCs compared)" % [int(rejoin.live_frame), int(rejoin.catch_up_ms), int(rejoin.crcs_compared)])
		_rejoin_since = -1
	if pause.get("paused", false):
		var who := str(pause.get("by_name", ""))
		text = "Game paused by %s  -  press Pause to resume" % (who if not who.is_empty() else "a player")
		if _pause_since == 0:
			_pause_since = Time.get_ticks_msec()
			print("GAME NET paused by slot %d before frame %d" % [int(pause.by_slot), int(pause.frame)])
		if not _net_shot_pause.is_empty() and Time.get_ticks_msec() - _pause_since > 2000:
			print("GAME NET pause screenshot %s: %s" % [_net_shot_pause, error_string(get_viewport().get_texture().get_image().save_png(_net_shot_pause))])
			_net_shot_pause = ""
		if _net_resume_after_ms >= 0 and Time.get_ticks_msec() - _pause_since >= _net_resume_after_ms:
			_net_resume_after_ms = -1
			_world.net_resume()
	elif _pause_since != 0:
		print("GAME NET resumed after %d ms" % (Time.get_ticks_msec() - _pause_since))
		_pause_since = 0
	_pause_label.text = text
	if _net_pause_at >= 0 and int(st.frame) >= _net_pause_at:
		_net_pause_at = -1
		_world.net_pause()
	var down := Input.is_key_pressed(KEY_PAUSE)
	if down and not _pause_key_down:
		if pause.get("paused", false):
			_world.net_resume()
		else:
			_world.net_pause()
	_pause_key_down = down


## lane MP-2: the disconnect screen (DisconnectScreen.apt through the shell's window manager), its buttons, the left-game notices and the local player's quit
func _net_disconnect_tick(dc: Dictionary) -> void:
	if dc.is_empty():
		return
	# lane MP-DROP-1: what the disconnect path decided (silence, the screen, votes, the drop), one line each, as the headless peers' reports carry it
	var dc_log: Array = dc.get("log", [])
	while _dc_log_seen < dc_log.size():
		print("GAME NET disconnect log: ", dc_log[_dc_log_seen])
		_dc_log_seen += 1
	var visible: bool = dc.get("visible", false)
	if visible or _dc_shown:
		var res: Dictionary = _shell.disconnect_screen(dc)
		if visible and not _dc_shown:
			_dc_since = Time.get_ticks_msec()
			print("GAME NET disconnect screen: ", JSON.stringify(res))
		elif not visible:
			print("GAME NET disconnect screen off")
		_dc_shown = visible
	if visible and _net_kick:
		var row := 0
		for r in dc.rows:
			if r.used and r.kick:
				_world.net_disconnect_kick(row)
			row += 1
	if visible and not _net_shot_disconnect.is_empty() and not _dc_shot_done and Time.get_ticks_msec() - _dc_since > 2000:
		_dc_shot_done = true
		get_viewport().get_texture().get_image().save_png(_net_shot_disconnect)
		print("GAME NET disconnect screenshot: ", _net_shot_disconnect)
		var rep: Dictionary = _shell.get_shell_report()
		print("GAME NET disconnect screen state: ", JSON.stringify(_shell.disconnect_screen(dc)), " notes ", JSON.stringify(rep.get("note_lines", [])), " calls without function ", JSON.stringify(rep.get("calls_without_function", [])))
	for a in _shell.take_disconnect_actions():
		print("GAME NET disconnect button: ", JSON.stringify(a))
		if a.kind == "kick":
			_world.net_disconnect_kick(a.row)
		elif a.kind == "quit":
			_world.net_disconnect_quit()
	var left: Array = dc.get("left", [])
	while _dc_left_seen < left.size():
		_show_end_message({"text": "Network:PlayerLeftGame", "name": left[_dc_left_seen]}) # RW 0x8D57D8
		_dc_left_seen += 1
	if dc.get("quit_requested", false) and not _dc_quit_handled:
		_dc_quit_handled = true
		print("GAME NET leaving the game: ", dc.get("quit_reason", ""))
		if _dc_shown:
			_shell.disconnect_screen({"visible": false})
			_dc_shown = false
		_exit_to_score_screen()


## lane MP-2: replays. The folder (retail: Replays\ of the user data, RW 0xC2EEF0) and the file every game is recorded to (retail: GUI:LastReplay)
func _replay_dir() -> String:
	var dir := OS.get_user_data_dir().path_join("Replays")
	DirAccess.make_dir_recursive_absolute(dir)
	return dir


func _last_replay_name() -> String:
	var t: Dictionary = _shell.fetch_text("GUI:LastReplay")
	var name: String = t.get("text", "")
	return name if t.get("found", false) and not name.is_empty() else "Last Replay"


func _last_replay_path() -> String:
	return _replay_dir().path_join(_last_replay_name() + ".replay")


## The main menu's Load Replay (RW 0x91C36C -> RW 0x816655(mode 2, Replay)): SaveLoad.apt's replay page with the folder's files
func _open_replay_menu() -> void:
	var rows: Array = []
	for f in _world.list_replays(_replay_dir()):
		var dt: Dictionary = Time.get_datetime_dict_from_unix_time(int(f.get("modified", 0)) + int(Time.get_time_zone_from_system().get("bias", 0)) * 60) # local time
		var map_key: String = str(f.get("map", ""))
		rows.append({"path": f.path, "name": str(f.file).get_basename(), "map": map_key.get_file().get_basename(),
			"date": "%02d/%02d/%04d" % [dt.month, dt.day, dt.year], "time": "%02d:%02d" % [dt.hour, dt.minute], "compatible": f.get("profile_ok", false)})
	_shell.set_save_load({"mode": 2, "flags": 4, "last_replay": _last_replay_name(), "replays": rows})
	print("GAME replay menu: %d replays in %s" % [rows.size(), _replay_dir()])
	if not _shell.shell_push("SaveLoad.apt"):
		printerr("GAME the shell refused SaveLoad.apt: ", _shell.get_shell_report().errors)


## A replay played back: prepared like a new game (the recorded lobby message), the load screen, then the game with the recording as its frame driver
func _begin_replay(path: String) -> void:
	print("GAME replay: ", path)
	if _state == State.SCORE:
		_score_continue()
		await _step(LEVEL_MAIN_MENU_FRAMES)
	_sync_simulation_profile() # lane UBER-1 (review): the identity a replay is checked against is the player's current option's
	var prep: Dictionary = _world.prepare_replay(path)
	if not prep.ok:
		_fail("prepare_replay: " + "\n".join(prep.errors))
		return
	_last_local_slot = int(prep.get("local_slot", -1))
	_state = State.LOADING
	_game_kind = "replay"
	_shell_leave_for_game() # lane CAMP-2
	var cards: Dictionary = _shell.set_load_screen_from_game(prep.resolved)
	if cards.has("error"):
		_fail("load screen: " + cards.error)
		return
	# lane MENU-1: a replay is game mode 3: GameLogic::getLoadScreen (RW 0x626A87) shows the picture screen with a random film still
	# (StillImage_Film01 .. 21, RW 0x626362), not the players' LoadScreen.apt
	var choice: Dictionary = _shell.choose_load_screen({"game_mode": 3})
	if not String(choice.get("error", "")).is_empty():
		_fail("load screen: " + String(choice.error))
		return
	var shown: Dictionary = _shell.show_still_load_screen(String(choice.picture))
	if not shown.ok:
		_fail("load screen: " + str(shown.error))
		return
	for i in 4:
		await get_tree().process_frame
	_shell.auto_process = false
	var rep: Dictionary = _world.start_replay({"progress": Callable(self, "_on_load_progress")})
	_report = rep
	if not rep.ok:
		for e in rep.errors.slice(0, 20):
			print("  replay load error: ", e)
		_fail("start_replay failed")
		return
	print("GAME replay loaded: %d frames, %d commands" % [rep.final_frame, rep.commands])
	_replay_active = true
	_enter_game(rep)
	if _replay_speed != 1.0:
		_world.set_time_scale(_replay_speed)


func _replay_tick() -> void:
	var st: Dictionary = _world.replay_status()
	if not st.get("active", false):
		return
	if not _replay_watch.is_empty() and int(st.frame) >= _replay_watch_next and _world.is_logic_held(): # RENDERWAIT-1 r2: its live reads never wait
		_replay_watch_next = int(st.frame) + 50
		_print_replay_watch(int(st.frame))
	if st.frame / 100 != _replay_last_print:
		_replay_last_print = st.frame / 100
		print("GAME REPLAY frame %d of %d, hashes compared %d, mismatches %d" % [st.frame, st.final_frame, st.hashes_compared, st.mismatches])
	if st.finished:
		_replay_active = false
		print("GAME REPLAY finished: ", JSON.stringify(st))
		if _replay_check or _replay_menu_test:
			print("GAME REPLAY RESULT: %s" % ("ok" if st.mismatches == 0 and st.hashes_compared > 0 else "FAILED"))
			get_tree().quit(0 if st.mismatches == 0 and st.hashes_compared > 0 else 1)


## lane QA-2: --replay-watch: the watched player's objects of one template, their production and the player's money / command points
func _print_replay_watch(frame: int) -> void:
	var parts := _replay_watch.split(":")
	if parts.size() != 2:
		return
	var index := -1
	var eco_line := ""
	for p in _world.get_economy().get("players", []):
		if p.name == parts[0]:
			index = int(p.index)
			eco_line = "money %d, command points %s / %s (available %s)" % [int(p.money), str(p.cp_used), str(p.cp_limit), str(p.cp_available)]
	for o in _world.get_player_objects(parts[0]):
		if o.template != parts[1]:
			continue
		var full: Dictionary = _world.get_object(int(o.id))
		var prod: Dictionary = _world.get_production(index, int(o.id))
		var queue := []
		for q in prod.get("queue", []):
			queue.append("%s %.1f%%" % [q.get("template", "?"), float(q.get("percent", 0.0))])
		print("QA REPLAY WATCH frame %d: %s %d at (%.0f, %.0f) conditions %s queue %s | %s" % [frame, o.template, int(o.id), o.x, o.y,
			str(full.get("conditions", [])), str(queue), eco_line])


## lane MP-2: RW 0x6290C7's message box after the first CRC mismatch of a game (type 4, GUI:DesyncTitle / GUI:DesyncText); the dump's path follows the text.
## The box is drawn by the presentation layer (the retail message box movie is not ported: S-1124); the game goes on.
func _show_desync_box(dumps: Array) -> void:
	var title: String = _shell.fetch_text("GUI:DesyncTitle").text
	var text: String = _shell.fetch_text("GUI:DesyncText").text
	if not dumps.is_empty():
		text += "\n\n" + str(dumps[0])
	print("GAME NET desync message: %s / %s" % [title, text])
	var layer := CanvasLayer.new()
	layer.layer = 95
	add_child(layer)
	var panel := PanelContainer.new()
	panel.anchor_left = 0.25
	panel.anchor_right = 0.75
	panel.anchor_top = 0.3
	panel.offset_bottom = 0
	layer.add_child(panel)
	var box := VBoxContainer.new()
	panel.add_child(box)
	var t := Label.new()
	t.set_meta("game_text", true) # lane CAMP-2
	t.text = title
	t.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	t.add_theme_font_size_override("font_size", 24)
	box.add_child(t)
	var body := Label.new()
	body.set_meta("game_text", true)
	body.text = text
	body.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	box.add_child(body)
	var ok := Button.new()
	ok.text = _shell.fetch_text("GUI:Ok").text
	ok.pressed.connect(func(): layer.queue_free())
	box.add_child(ok)
	_game_nodes.append(layer)
	if not _net_shot_desync.is_empty():
		await get_tree().create_timer(1.0).timeout
		get_viewport().get_texture().get_image().save_png(_net_shot_desync)
		print("GAME NET desync screenshot: ", _net_shot_desync)


# lane PERF-1: the main thread's CPU time in ms (Linux: /proc/self/task/<pid>/schedstat, nanoseconds on the CPU; -1 elsewhere): a frame cost that other
# processes' load does not inflate, readable with any build of the extension
func _main_thread_cpu_ms() -> float:
	var f := FileAccess.open("/proc/self/task/%d/schedstat" % OS.get_process_id(), FileAccess.READ)
	if f == null:
		return -1.0
	var parts := f.get_line().split(" ") # /proc files report size 0: read the line, not the "whole" file
	return float(parts[0]) / 1.0e6 if parts.size() > 0 else -1.0


## --net-end (lanes END-1 / END-2): at --net-frames the joiner surrenders through the quit menu (Esc, Forfeit, its confirmation: MSG_SELF_DESTRUCT(false) on
## the lockstep), both peers see the end (the joiner's defeat, the host's victory), leave by the end-game timer (lane PLAY-1) to the score screen (type 3), and its
## Continue opens the LAN lobby again: the host hosts, the joiner joins, and a second LAN game starts and runs 20 frames
func _net_end_flow(desyncs: int) -> void:
	var ok := desyncs == 0
	var surrender := not _net_join.is_empty()
	if surrender:
		await _press_escape()
		await _wait_seconds(4.0)
		var st: Dictionary = _shell.quit_menu({"kind": "state"})
		print("GAME NET END quit menu: ", JSON.stringify(st))
		_save_named("end2-lan-quitmenu")
		ok = ok and st.get("popup_type", "") == "Forfeit" and str(st.get("disabled", [])) == str(["Save", "Load"])
		ok = ok and await _click_quit("Restart")
		await _wait_seconds(2.0)
		_save_named("end2-lan-surrender-confirm")
		ok = ok and await _click_quit("Yes")
	var waited := 0.0
	while waited < 40.0 and not _world.get_end_game_state().get("shown", false):
		await _wait_seconds(0.25)
		waited += 0.25
	var es: Dictionary = _world.get_end_game_state()
	print("GAME NET END end screen: shown %s, victory %s, surrendered %s" % [str(es.get("shown", false)), str(es.get("victory_screen", false)), str(surrender)])
	ok = ok and es.get("shown", false) and es.get("victory_screen", false) == (not surrender)
	await _wait_seconds(2.0)
	_save_named("end2-lan-%s" % ("defeat" if surrender else "victory"))
	# lane PLAY-1: the end-game timer (RW 0x602FFE, 25 logic frames after the local side's VICTORY / DEFEAT) leaves the game to the score screen
	# (MSG_CLEAR_GAME_DATA, RW 0x603533): no quit menu is needed
	waited = 0.0
	while _state != State.SCORE and waited < 30.0:
		await _wait_seconds(0.25)
		waited += 0.25
	print("GAME END left by the end-game timer: %s after %.2f s" % [str(_state == State.SCORE), waited])
	var sd: Dictionary = _score_report.get("score_screen", {})
	var entries: Array = sd.get("entries", [])
	var local_result: int = entries[0].get("result", -1) if entries.size() > 0 else -1
	print("GAME NET END score screen type %d, entries %d, local result %d, net state '%s'" % [sd.get("type", -1), entries.size(), local_result, _net_state])
	ok = ok and sd.get("type", -1) == 3 and _state == State.SCORE and local_result == (1 if surrender else 0)
	await _wait_seconds(2.0)
	_score_continue() # the LAN lobby again
	waited = 0.0
	while waited < 90.0 and not (_state == State.PLAYING and _world.net_status().get("frame", 0) >= 20):
		await _wait_seconds(0.25)
		waited += 0.25
	var ns: Dictionary = _world.net_status()
	print("GAME NET END next game: local slot %d, net active %s, logic frame %d, games started %d, desyncs %d" % [_last_local_slot, str(ns.get("active", false)),
		ns.get("frame", 0), _games_started, ns.get("desyncs", []).size()])
	ok = ok and _games_started == 2 and ns.get("active", false) and ns.get("frame", 0) >= 20 and ns.get("desyncs", []).size() == 0
	print("GAME NET END RESULT: %s" % ("ok" if ok else "FAILED"))
	_world.net_finish()
	get_tree().quit(0 if ok else 1)


# ---- lane MP-2: the LAN lobby ------------------------------------------------------------------------------------------------------------------

## The main menu's LAN: GameWorld's LAN lobby (bound once, kept for the session) handed to LanLobby.apt
func _open_lan_lobby() -> void:
	_sync_simulation_profile() # lane UBER-1: the lobby announces the identity of the player's profile
	var st: Dictionary = _world.lan_status()
	if not st.get("open", false):
		var opts := {"name": _net_name, "run_ahead": _net_run_ahead, "crc_interval": _net_crc}
		if not _lan_targets.is_empty():
			opts["broadcast"] = false
			opts["targets"] = Array(_lan_targets.split(","))
		if _lan_port_base > 0:
			opts["port_base"] = _lan_port_base
		var r: Dictionary = _world.lan_open(opts)
		print("GAME LAN lobby: ", r)
		if not r.ok:
			_fail("LAN lobby: " + str(r.get("error", "")))
			return
	_shell.set_lan(_world)
	if not _shell.shell_push("LanLobby.apt"):
		_fail("the shell refused LanLobby.apt: " + str(_shell.get_shell_report().errors))


## Every player of the LAN game has the start: the game socket and the start go to the network game, which loads like a --net-host / --net-join one
func _lan_game_start() -> void:
	var p: Dictionary = _world.lan_begin()
	if not p.ok:
		_fail("LAN start: " + str(p.get("error", "")))
		return
	_net_state = "loading"
	print("GAME LAN game started: local slot %d, run-ahead %d, CRC interval %d" % [p.local_slot, p.run_ahead, p.crc_interval])
	_begin_game(p.message)


## --lan=host|join: the lobby driven through the movie's own commands and gadget messages
func _lan_script_tick() -> void:
	_lan_wait += 1
	var st: Dictionary = _world.lan_status()
	var stack: PackedStringArray = _shell.shell_stack()
	var top: String = stack[stack.size() - 1] if stack.size() > 0 else ""
	if _lan_phase == 0:
		if _lan_wait > 30:
			print("GAME LAN script: the main menu's LAN")
			_shell.shell_fscommand("AptMainMenu::LAN", "")
			_lan_phase = 1
			_lan_wait = 0
	elif _lan_phase == 1:
		if top == "LanLobby.apt" and _lan_wait > 60:
			if _lan_mode == "host":
				print("GAME LAN script: Create Game")
				_shell.shell_fscommand("AptLanLobby::OnCreateGameBttn", "")
				_lan_phase = 2
				_lan_wait = 0
			elif int(st.get("games", 0)) > 0:
				print("GAME LAN script: Join of the first game")
				var j: Dictionary = _shell.lobby_apply({"join_row": 0})
				if not j.ok:
					printerr("GAME LAN join: ", j.errors)
				_lan_phase = 2
				_lan_wait = 0
	elif _lan_phase == 2:
		if st.get("in_game", false) and _lan_wait > 30:
			var slots: Array = []
			if not _faction.is_empty():
				slots.append({"slot": int(st.slot), "faction": _faction})
			var spec := {"slots": slots}
			if _lan_mode == "host":
				spec["map"] = _map_key
				for i in range(_lan_ai):
					slots.append({"slot": _lan_players + i, "state": 3})
			var a: Dictionary = _shell.lobby_apply(spec)
			print("GAME LAN script: the setup ", JSON.stringify(spec), " -> ", a.ok, " ", a.errors)
			_lan_phase = 3
			_lan_wait = 0
		elif _lan_wait > 600:
			_fail("LAN script: no game after 10 s: " + str(st))
	elif _lan_phase == 3:
		if _lan_mode == "join":
			if st.get("in_game", false) and not st.get("accepted_me", false) and _lan_wait > 60:
				print("GAME LAN script: Accept")
				_shell.shell_fscommand("MpGameSetup::OnReadyPress", str(int(st.slot)))
				_lan_wait = 0
		elif int(st.get("humans", 0)) >= _lan_players and int(st.get("accepted", 0)) >= _lan_players - 1:
			if _lan_wait > 90:
				if not _lan_shot.is_empty() and not _lan_shot_done:
					_lan_shot_done = true
					var image := get_viewport().get_texture().get_image()
					print("GAME LAN lobby shot %s -> %s" % [_lan_shot, error_string(image.save_png(_lan_shot))])
					# lane UI-2: what the lobby's native slots are (map picture, View3D ...) and the shell's notes, for the media runs
					var lr: Dictionary = _shell.get_report()
					print("GAME LAN lobby placeholders ", JSON.stringify(lr.get("placeholders", [])))
					print("GAME LAN lobby shell report ", JSON.stringify(_shell.get_shell_report()))
				print("GAME LAN script: Play Game (%d players)" % int(st.humans))
				_shell.shell_fscommand("AptLanLobby::OnStartGameBttn", "")
				_lan_phase = 4
				_lan_wait = 0
				_lan_start_ms = Time.get_ticks_msec()
		else:
			_lan_wait = 0
	elif _lan_phase == 4:
		if not _lan_shot_start.is_empty() and _lan_start_ms >= 0 and Time.get_ticks_msec() - _lan_start_ms >= 8000:
			_lan_start_ms = -1
			var image := get_viewport().get_texture().get_image()
			print("GAME LAN start shot %s -> %s" % [_lan_shot_start, error_string(image.save_png(_lan_shot_start))])

# ---- lane END-2: the scripted quit menu --------------------------------------------------------------------------------------------------------------

## the button of the quit menu movie whose path ends with `name` (QuitMenu.apt's clips: Resume, Options, Restart, ExitMission ... and the popup's Yes)
func _quit_button(name: String) -> Dictionary:
	var r: Dictionary = _shell.quit_menu({"kind": "state"})
	if not r.get("open", false):
		return {}
	for b in _shell.list_buttons(r.level):
		var path: String = b.path
		if b.hittable and (path.contains("." + name + ".") or path.ends_with("." + name)):
			return b
	return {}


func _click_quit(name: String) -> bool:
	for attempt in 40:
		var b := _quit_button(name)
		if not b.is_empty():
			var p := Vector2(b.x, b.y)
			_mouse_event(p, false, false)
			await _step(2)
			_mouse_event(p, true, true)
			await _step(2)
			_mouse_event(p, false, true)
			await _step(2)
			print("GAME QUIT clicked ", b.path)
			return true
		await _wait_seconds(0.1)
	var r: Dictionary = _shell.quit_menu({"kind": "state"})
	print("GAME QUIT buttons: ", JSON.stringify(_shell.list_buttons(r.get("level", -1))))
	return false


func _press_escape() -> void:
	var ev := InputEventKey.new()
	ev.keycode = KEY_ESCAPE
	ev.pressed = true
	Input.parse_input_event(ev)
	await _step(2)
	var up := InputEventKey.new()
	up.keycode = KEY_ESCAPE
	up.pressed = false
	Input.parse_input_event(up)
	await _step(2)


## --quit: Esc opens the quit menu over the skirmish (paused), Resume closes it, Esc again, Exit and its confirmation leave the game to the score screen, whose
## statistics page and tabs are used, then Continue returns to the Skirmish lobby and a second game starts. --quit-restart: Restart and its confirmation
## start the same game again.
func _run_quit() -> void:
	var faction := _faction.trim_prefix("Faction").to_lower()
	var frame0: int = _world.get_stats().frame
	await _press_escape()
	await _wait_seconds(4.0)
	var st: Dictionary = _shell.quit_menu({"kind": "state"})
	var paused: bool = _world.is_paused()
	await _wait_seconds(0.5)
	var frame_paused: int = _world.get_stats().frame
	print("GAME QUIT opened: %s, paused %s, frame %d -> %d" % [JSON.stringify(st), str(paused), frame0, frame_paused])
	_save_named("end2-quitmenu")
	var ok: bool = st.get("open", false) and paused and st.get("initialized", 0) >= 1
	if _quit_flow == "restart":
		var seed: int = _last_new_game.get("seed", -1)
		ok = ok and await _click_quit("Restart")
		await _wait_seconds(2.0)
		_save_named("end2-quitmenu-restart-confirm")
		ok = ok and await _click_quit("Yes")
		var w := 0.0
		while (_games_started < 2 or _state != State.PLAYING) and w < 60.0:
			await _wait_seconds(0.5)
			w += 0.5
		await _wait_seconds(2.0)
		var gs: Dictionary = _world.get_stats()
		print("GAME QUIT RESULT restart: requests %s, games started %d, same seed %s, frame %d" % [str(_quit_log), _games_started, str(_last_new_game.get("seed", -2) == seed), gs.frame])
		ok = ok and _games_started == 2 and _last_new_game.get("seed", -2) == seed and gs.frame > 3 and _quit_log.has("QuitMenuRestart")
		print("GAME QUIT RESULT: ", "ok" if ok else "FAILED")
		get_tree().quit(0 if ok else 1)
		return
	# Resume
	ok = ok and await _click_quit("Resume")
	await _wait_seconds(1.5)
	var after_resume: Dictionary = _shell.quit_menu({"kind": "state"})
	var f1: int = _world.get_stats().frame
	await _wait_seconds(1.0)
	var resumed: bool = not after_resume.get("open", true) and not _world.is_paused() and _world.get_stats().frame > f1
	print("GAME QUIT resumed: %s (frame %d -> %d)" % [str(resumed), f1, _world.get_stats().frame])
	ok = ok and resumed
	# Esc, Exit, the confirmation
	await _press_escape()
	await _wait_seconds(4.0)
	ok = ok and await _click_quit("ExitMission")
	await _wait_seconds(2.0)
	_save_named("end2-quitmenu-exit-confirm")
	ok = ok and await _click_quit("Yes")
	var waited := 0.0
	while _state != State.SCORE and waited < 20.0:
		await _wait_seconds(0.25)
		waited += 0.25
	print("GAME QUIT exit: state %d, requests %s" % [_state, str(_quit_log)])
	ok = ok and _state == State.SCORE and _quit_log.has("QuitMenuExit") and _quit_log.has("QuitMenuReturn")
	print("GAME QUIT RESULT exit: ", "ok" if ok else "FAILED")
	if not ok:
		get_tree().quit(1)
		return
	await _finish_end_flow(faction, "end2")


## lane SAVE-2: --campaign=NAME --save-test: a campaign mission saved and loaded back through the screens (scripts/save_flow.gd)
func _run_campaign_save_test() -> void:
	var ok: bool = await _save_flow.run_campaign_test()
	print("GAME SAVE RESULT: ", "ok" if ok else "FAILED")
	get_tree().quit(0 if ok else 1)


## lane CAMP-1: --campaign-check=N: quit once N missions ended (or the campaign is complete); exit 0 when every one was won
func _campaign_check_tick() -> void:
	if _campaign_check <= 0 or _campaign_flow == null:
		return
	var ended: Array = _campaign_flow.ended_log
	# quit once the flow went on after the N-th end: the next mission is playing, or the main menu is back
	var next_playing: bool = _campaign_flow.active and _campaign_flow.begun > ended.size() and _state == State.PLAYING
	var menu_back: bool = not _campaign_flow.active and _state == State.MENU
	if ended.size() >= _campaign_check and (next_playing or menu_back) and _shell_pictures_pending == 0: # lane CAMP-2: after the SHELL PICTURES lines
		var won := 0
		for l in ended:
			won += 1 if String(l).contains(" victory ") else 0
		print("CAMPAIGN CHECK %d missions ended, %d won: %s" % [ended.size(), won, str(ended)])
		get_tree().quit(0 if won == ended.size() and ended.size() > 0 else 1)


# ---- lane QA-1: the scripted player -------------------------------------------------------------------------------------------------------------------

## --qa: the scripted human plays (or, --qa-idle, watches an AI game), then the game is left the way a player leaves it and the score screen's Continue must
## return to the lobby. One QA SUMMARY line carries the player's findings and the flow's.
func _run_qa() -> void:
	if not _net_state.is_empty() or _net_host > 0 or not _net_join.is_empty() or not _lan_mode.is_empty() or not _replay_file.is_empty():
		_fail("QA: the scripted player does not play a network session or a replay")
		return
	if _hud == null:
		_fail("QA: the HUD was not installed")
		return
	var qa: Node = load("res://scripts/qa_scene.gd" if not _qa_scene.is_empty() else "res://scripts/qa_player.gd").new()
	qa.name = "QAPlayer"
	add_child(qa)
	qa.setup(self, _world, _hud, _local_name, {"minutes": _qa_minutes, "screens": _screens, "tag": _qa_tag, "cash": _qa_cash})
	_world.set_time_scale(_qa_speed)
	var started := Time.get_ticks_msec()
	var summary: Dictionary = {}
	if not _qa_scene.is_empty():
		summary = await qa.scene(_qa_scene)
	elif _qa_idle:
		summary = await qa.watch()
	else:
		summary = await qa.play()
	_world.set_time_scale(1.0)
	summary["real_seconds"] = (Time.get_ticks_msec() - started) / 1000.0
	var flow := {"end_screen": false, "end_hidden": false, "auto_score": false, "quit_exit": false, "score_screen": false, "continue_to_lobby": false}
	if summary.get("victory", false) or summary.get("defeat", false):
		var waited := 0.0
		while waited < 20.0 and not _world.get_end_game_state().get("shown", false):
			await _wait_seconds(0.25)
			waited += 0.25
		flow.end_screen = _world.get_end_game_state().get("shown", false)
		await _wait_seconds(2.0)
		_save_named("qa1-%s-end" % _qa_tag)
		waited = 0.0
		while waited < 12.0 and not _world.get_end_game_state().get("hidden", false):
			await _wait_seconds(0.25)
			waited += 0.25
		flow.end_hidden = _world.get_end_game_state().get("hidden", false)
		await _wait_seconds(4.0)
		flow.auto_score = _state == State.SCORE
	if _state == State.PLAYING:
		# the player leaves: Esc, the quit menu's Exit, its confirmation
		await _press_escape()
		await _wait_seconds(2.0)
		var ok: bool = await _click_quit("ExitMission")
		await _wait_seconds(1.5)
		ok = ok and await _click_quit("Yes")
		flow.quit_exit = ok
	var w := 0.0
	while _state != State.SCORE and w < 20.0:
		await _wait_seconds(0.25)
		w += 0.25
	flow.score_screen = _state == State.SCORE
	if flow.score_screen:
		await _wait_seconds(4.0)
		_save_named("qa1-%s-score" % _qa_tag)
		var lvl: int = _shell.shell_top_level()
		for attempt in 6:
			await _click_button(lvl, "Main.MainButtons.Continue")
			await _wait_seconds(1.5)
			if _state != State.SCORE:
				break
		w = 0.0
		while _state != State.MENU and w < 10.0:
			await _wait_seconds(0.25)
			w += 0.25
		await _step(LEVEL_MAIN_MENU_FRAMES + 30)
		var stack: PackedStringArray = _shell.shell_stack()
		flow.continue_to_lobby = _state == State.MENU and stack.size() > 0 and stack[-1] == "Skirmish.apt"
		flow["stack"] = str(stack)
		# lane QA-2: the lobby after the game, at once and once the shell settled (the main menu's buttons must not show through)
		_save_named("qa1-%s-lobby-after" % _qa_tag)
		await _wait_seconds(3.0)
		_save_named("qa1-%s-lobby-after-3s" % _qa_tag)
	var shell_report: Dictionary = _shell.get_shell_report()
	summary["flow"] = flow
	summary["shell_errors"] = shell_report.get("errors", [])
	summary["load_errors"] = _report.get("errors", [])
	summary["load_stops"] = _report.get("stops", [])
	# lane QA-1: the sound side (missing events, files that did not play) as the audio manager counted it over the session
	var ar: Dictionary = _audio.get_report() if _audio != null else {}
	summary["audio"] = {"unknown_events": ar.get("unknown_events", 0), "play_failures": ar.get("play_failures", 0), "played": ar.get("played", 0),
		"missing_hooks": ar.get("missing_hooks", []), "errors": ar.get("errors", []), "unknown_event_names": ar.get("unknown_event_names", {})}
	summary["map"] = _map_key
	summary["faction"] = _faction
	summary["ai"] = _ai
	summary["opponents"] = _opponents
	summary["seed"] = _seed
	print("QA SUMMARY ", JSON.stringify(summary))
	var complete: bool = flow.score_screen and flow.continue_to_lobby
	print("QA RESULT: flow %s, %d issues" % ["complete" if complete else "INCOMPLETE", summary.get("issues", []).size()])
	get_tree().quit(0 if complete else 1)



## lane UI-4: --resize-check[=DIR]: the owner's report (2026-10-10: maximising the window greyed the main menu's buttons). The main menu settled, then:
## 1. the window resized (windowed: maximised and restored; headless: the root viewport's size, 1280x720 -> 1920x1080 -> back), with the Options nav
##    closed and open: every main-menu clip's frame, every button's hittability and the screen stack must be what they were;
## 2. the stray input a window manager sends around a maximise (a button release with no press, a press outside the window released over a button,
##    a pointer move): over every nav entry of the Options and Solo Play navs, none may press anything (no request, no screen, every button still
##    hittable after the pointer left).
## DIR (windowed) receives resize-*.png. Prints UI4 RESIZE lines; exit 0 when nothing changed.
const RESIZE_CLIPS := ["SoloPlayNav", "MultiPlayNav", "OptionsNav", "MyHeroes", "QuitMainMenu", "SoloPlayNav.OpenButton", "MultiPlayNav.OpenButton",
	"OptionsNav.OpenButton", "OptionsNav.Settings", "OptionsNav.AdvancedSettings", "OptionsNav.Credits"]
var _resize_requests := 0

func _resize_state(with_frames: bool) -> Dictionary:
	var level: int = _shell.shell_top_level()
	var out := {"stack": str(_shell.shell_stack())}
	if with_frames:
		for clip in RESIZE_CLIPS:
			var info: Dictionary = _shell.instance_info(level, clip)
			out[clip] = "frame %s playing %s visible %s" % [str(info.get("frame")), str(info.get("playing")), str(info.get("visible"))] if info.found else "absent"
	for b in _shell.list_buttons(level):
		out[String(b.path)] = "hittable=%s" % str(b.hittable)
	return out


func _resize_to(size: Vector2i, maximise: bool) -> void:
	if DisplayServer.get_name() == "headless":
		get_tree().root.size = size
	elif maximise:
		DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_MAXIMIZED)
	else:
		DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_WINDOWED)
		DisplayServer.window_set_size(size)
	await _step(90)
	print("UI4 RESIZE window %s, viewport %s" % [str(DisplayServer.window_get_size()), str(get_viewport().get_visible_rect().size)])


func _resize_shot(name: String) -> void:
	if DisplayServer.get_name() == "headless" or _resize_check.is_empty():
		return
	await RenderingServer.frame_post_draw
	DirAccess.make_dir_recursive_absolute(_resize_check)
	var path := "%s/resize-%s.png" % [_resize_check, name]
	var image := _capture_image(path)
	if image != null:
		print("UI4 RESIZE screenshot %s -> %s" % [path, error_string(image.save_png(path))])


func _resize_compare(what: String, before: Dictionary, after: Dictionary) -> int:
	var bad := 0
	for key in before:
		if after.get(key, "absent") != before[key]:
			print("UI4 RESIZE FAIL %s: %s was %s, now %s" % [what, key, before[key], after.get(key, "absent")])
			bad += 1
	for key in after:
		if not before.has(key):
			print("UI4 RESIZE FAIL %s: %s appeared (%s)" % [what, key, after[key]])
			bad += 1
	print("UI4 RESIZE %s: %d checked, %d changed" % [what, before.size(), bad])
	return bad


# the main menu as the host shows it again after a probe: no sub-screen, its buttons revealed (ShowMainMenu), the pointer away
func _resize_menu_back() -> void:
	while _shell.shell_stack().size() > 1:
		_shell.shell_pop()
		await _step(60)
	_mouse_event(Vector2(5, 5), false, false)
	_shell.shell_invoke(_shell.shell_top_level(), "ShowMainMenu", PackedStringArray())
	await _step(150)


func _run_resize_check() -> void:
	_shell.shell_request.connect(func(_a: String, _b: String) -> void: _resize_requests += 1)
	await _step(LEVEL_MAIN_MENU_FRAMES + 90)
	await _wait_main_ready()
	await _step(120)
	var start: Vector2i = DisplayServer.window_get_size()
	if DisplayServer.get_name() == "headless":
		start = Vector2i(1280, 720) # the headless server has no window: the root viewport stands for it
		await _resize_to(start, false)
	var bad := 0
	for phase in ["closed", "options-open"]:
		if phase == "options-open":
			if not await _click_button(_shell.shell_top_level(), "OptionsNav"):
				print("UI4 RESIZE FAIL cannot open the Options nav")
				get_tree().quit(1)
				return
			await _step(120)
		var before := _resize_state(true)
		await _resize_shot(phase + "-before")
		await _resize_to(Vector2i(start.x * 3 / 2, start.y * 3 / 2), true)
		await _resize_shot(phase + "-maximised")
		bad += _resize_compare(phase + " maximised", before, _resize_state(true))
		await _resize_to(start, false)
		await _resize_shot(phase + "-restored")
		bad += _resize_compare(phase + " restored", before, _resize_state(true))
	await _resize_menu_back()
	var requests_before := _resize_requests
	for sub in ["OptionsNav.Settings", "OptionsNav.AdvancedSettings", "OptionsNav.Credits", "SoloPlayNav.Skirmish", "SoloPlayNav.LoadGame"]:
		for mode in ["release only", "press outside, release over it", "pointer move"]:
			var nav: String = String(sub).get_slice(".", 0)
			await _click_button(_shell.shell_top_level(), nav)
			await _step(90)
			var b: Dictionary = _shell.find_button(_shell.shell_top_level(), sub)
			if not b.found:
				print("UI4 RESIZE FAIL no %s" % sub)
				bad += 1
				continue
			var t: Vector2 = Vector2(b.x, b.y)
			var before := _resize_state(false)
			if mode == "press outside, release over it":
				_mouse_event(Vector2(-40, -40), false, false)
				_mouse_event(Vector2(-40, -40), true, true)
				await _step(3)
			_mouse_event(t, false, false)
			await _step(3)
			if mode != "pointer move":
				_mouse_event(t, false, true)
			await _step(30)
			_mouse_event(Vector2(5, 5), false, false) # the pointer leaves: hover states go back
			await _step(90)
			bad += _resize_compare("stray input (%s) over %s" % [mode, sub], before, _resize_state(false))
			await _resize_menu_back()
	if _resize_requests != requests_before:
		print("UI4 RESIZE FAIL stray input pressed something: %d shell requests" % (_resize_requests - requests_before))
		bad += 1
	print("UI4 RESIZE %s (%d changes)" % ["PASS" if bad == 0 else "FAIL", bad])
	get_tree().quit(0 if bad == 0 else 1)


# ---- lane UI-4: --menu-colours=DIR ---------------------------------------------------------------------------------------------------------------------

## a colour is green-dominant when green is its strongest channel by more than 0.05 (C0EEFF, RotWK's light blue, has blue on top; 8AC14D is green)
func _green_dominant(c: Color) -> bool:
	return c.g > c.r + 0.05 and c.g > c.b + 0.05 and c.g > 0.2


## HEURISTIC: the label's ink inside its window rectangle: the fifth of its pixels farthest from the rectangle's median colour, averaged (glyph edges and
## anything else that differs from the background mix in)
func _ink_colour(image: Image, r: Rect2i) -> Color:
	r = r.intersection(Rect2i(Vector2i.ZERO, image.get_size()))
	if r.size.x < 2 or r.size.y < 2:
		return Color(0, 0, 0, 0)
	var px: Array[Color] = []
	for y in range(r.position.y, r.end.y):
		for x in range(r.position.x, r.end.x):
			px.append(image.get_pixel(x, y))
	var lum: Array = px.map(func(c: Color) -> float: return c.get_luminance())
	var sorted_lum: Array = lum.duplicate()
	sorted_lum.sort()
	var median: float = sorted_lum[sorted_lum.size() / 2]
	var order: Array = range(px.size())
	order.sort_custom(func(a: int, b: int) -> bool: return absf(lum[a] - median) > absf(lum[b] - median))
	var n := maxi(1, px.size() / 5)
	var sum := Color(0, 0, 0, 0)
	for i in n:
		sum += px[order[i]]
	return Color(sum.r / n, sum.g / n, sum.b / n, 1.0)


## the texts of the screen: the canvas's text ops (describe_ops) and the texts the gadgets and render callbacks drew (drawn_texts), each with its colour
## and a HEURISTIC ink sample of its window rectangle in a screenshot (the fifth of the rectangle's pixels farthest from its median: glyph pixels mixed
## with whatever else differs from the background, not isolated glyphs). Problems (no capture, no text matched) go to _menu_colours_problems.
func _menu_colours_screen(name: String) -> int:
	_shell.drawn_texts(true)
	await _step(30)
	await RenderingServer.frame_post_draw
	var image := _capture_image(name)
	if image == null:
		_menu_colours_problems.append("%s: no capture" % name)
	else:
		image.save_png("%s/colours-%s.png" % [_menu_colours, name])
	var entries: Array = [] # [source, text, colour, window rect]
	var rx := RegEx.create_from_string("^text (\\S+) '(.*)' .* rgba=\\(([0-9.]+) ([0-9.]+) ([0-9.]+) ([0-9.]+)\\).* win=\\((-?[0-9]+) (-?[0-9]+) (-?[0-9]+) (-?[0-9]+)\\)")
	for line in _shell.describe_ops():
		var m := rx.search(line)
		if m != null:
			entries.append(["apt " + m.get_string(1), m.get_string(2), Color(float(m.get_string(3)), float(m.get_string(4)), float(m.get_string(5)), float(m.get_string(6))),
				Rect2i(int(m.get_string(7)), int(m.get_string(8)), int(m.get_string(9)) - int(m.get_string(7)), int(m.get_string(10)) - int(m.get_string(8)))])
	var others := 0
	for e in _shell.drawn_texts(true):
		entries.append([String(e.source), String(e.text), e.color, Rect2i(e.window)])
		others += 1
	var green := 0
	var texts := 0
	var seen := {}
	var inks := {}
	for e in entries:
		var c: Color = e[2]
		if String(e[1]).strip_edges().is_empty() or c.a < 0.05:
			continue
		texts += 1
		var ink := _ink_colour(image, e[3]) if image != null else Color(0, 0, 0, 0)
		if _green_dominant(c) or (image != null and _green_dominant(ink)):
			green += 1
			print("UI4 COLOURS GREEN %s: '%s' (%s) drawn %s, heuristic ink %s" % [name, e[1], e[0], c.to_html(false), ink.to_html(false)])
		elif not seen.has(c.to_html(false)):
			seen[c.to_html(false)] = e[1]
		if image != null:
			inks[ink.to_html(false)] = true
	if texts == 0:
		_menu_colours_problems.append("%s: no text matched" % name)
	print("UI4 COLOURS %s: %d texts (%d from gadgets / callbacks), %d green-dominant; drawn colours %s; heuristic ink samples %s" % [name, texts, others, green, seen,
		inks.keys()])
	return green


## presses the hittable button of any level whose path holds `fragment` (any case); a miss is a problem of the walk
func _menu_colours_press(what: String, fragment: String) -> bool:
	for level in 16:
		for b in _shell.list_buttons(level):
			var path := String(b.path)
			if b.hittable and path.to_lower().contains(fragment.to_lower()):
				print("UI4 COLOURS %s: pressing %s" % [what, path])
				_mouse_event(Vector2(b.x, b.y), false, false)
				await _step(2)
				_mouse_event(Vector2(b.x, b.y), true, true)
				await _step(2)
				_mouse_event(Vector2(b.x, b.y), false, true)
				await _step(150)
				return true
	_menu_colours_problems.append("%s: no button '%s'" % [what, fragment])
	return false


## back to the main menu: a pushed screen popped, a box closed by its own Cancel / Back / OK / Close
func _menu_colours_dismiss(depth: int) -> void:
	if _shell.shell_stack().size() <= depth:
		# the "not available" box (AptMessageBox, its own level): its Ok, as the menu walk presses it
		var ok := _walk_find_box_ok()
		if not ok.is_empty():
			print("UI4 COLOURS closing the box with ", ok.path)
			_mouse_event(Vector2(ok.x, ok.y), false, false)
			await _step(2)
			_mouse_event(Vector2(ok.x, ok.y), true, true)
			await _step(2)
			_mouse_event(Vector2(ok.x, ok.y), false, true)
			await _step(120)
		# a box on the top screen's level (the campaign's difficulty): its Cancel / Back / Ok
		var names: Array = []
		for level in 16:
			if ok.is_empty():
				for b in _shell.list_buttons(level):
					if b.hittable:
						names.append([level, String(b.path)])
		var done := false
		for name in ["cancel", "back", "ok", "close", "exit", "done"]:
			for e in names:
				# Cancel, cancelButton, OK, okButton ...: a path element that starts with the name, any case
				var path: String = e[1]
				if not done and path.to_lower().contains("." + name) and not path.contains("Nav."):
					print("UI4 COLOURS closing the box with ", path)
					await _click_button(e[0], path.substr(path.find(".") + 1).trim_suffix(".bttn"))
					await _step(120)
					done = true
	await _resize_menu_back()


func _run_menu_colours() -> void:
	DirAccess.make_dir_recursive_absolute(_menu_colours)
	await _step(LEVEL_MAIN_MENU_FRAMES + 90)
	await _wait_main_ready()
	await _step(120)
	var green := await _menu_colours_screen("main")
	var screens := 1
	for nav in ["SoloPlayNav", "MultiPlayNav", "OptionsNav", "MyHeroes"]:
		if not await _click_button(_shell.shell_top_level(), nav):
			_menu_colours_problems.append("cannot press %s" % nav)
			continue
		await _step(90)
		green += await _menu_colours_screen("main-" + nav)
		screens += 1
		if nav == "MyHeroes":
			# Create-a-Hero opens on its introduction ("NEW FEATURES"); Continue goes on to the builder
			if await _menu_colours_press("Create-a-Hero introduction", "continue"):
				await _step(240)
				green += await _menu_colours_screen("MyHeroes-builder")
				screens += 1
			await _resize_menu_back()
			continue
		var subs: Array = []
		for b in _shell.list_buttons(_shell.shell_top_level()):
			var p := String(b.path)
			var at := p.find("." + nav + ".")
			if b.hittable and at >= 0:
				var sub := p.substr(at + 1).trim_suffix(".bttn")
				if sub.get_slice_count(".") == 2 and not subs.has(sub):
					subs.append(sub)
		print("UI4 COLOURS %s entries %s" % [nav, subs])
		if subs.is_empty():
			_menu_colours_problems.append("%s: no entries" % nav)
		for sub in subs:
			await _click_button(_shell.shell_top_level(), nav)
			await _step(60)
			var depth: int = _shell.shell_stack().size()
			if not await _click_button(_shell.shell_top_level(), sub):
				_menu_colours_problems.append("cannot press %s" % sub)
				continue
			await _step(240)
			var stack: PackedStringArray = _shell.shell_stack()
			print("UI4 COLOURS %s -> %s" % [sub, stack])
			# a pushed screen, or a box over the main menu (the campaign's difficulty, the "not available" notes): both are front-end screens
			green += await _menu_colours_screen(sub.replace(".", "-"))
			screens += 1
			if sub.ends_with("Skirmish"):
				# the first visit asks for a profile name over the lobby (its Cancel leaves Skirmish): a profile is given as --auto gives it
				var applied: Dictionary = {}
				for attempt in 30:
					applied = _shell.lobby_apply({"profile": "Gimli"})
					if applied.ok:
						break
					await _step(10)
				await _step(120)
				if not applied.ok or not _shell.shell_stack().has("Skirmish.apt"):
					_menu_colours_problems.append("Skirmish: the profile box was not closed into the lobby (%s, %s)" % [applied.get("errors", []), _shell.shell_stack()])
				else:
					green += await _menu_colours_screen(sub.replace(".", "-") + "-lobby")
					screens += 1
			await _menu_colours_dismiss(depth)
	for p in _menu_colours_problems:
		print("UI4 COLOURS PROBLEM ", p)
	var ok := green == 0 and _menu_colours_problems.is_empty()
	print("UI4 COLOURS %s: %d screens, %d green-dominant texts, %d problems" % ["PASS" if ok else "FAIL", screens, green, _menu_colours_problems.size()])
	get_tree().quit(0 if ok else 1)

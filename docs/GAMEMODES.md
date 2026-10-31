# RotWK 2.01 game modes (lane GAMEMODES-1)

What a Skirmish or LAN game of RotWK 2.01 can be, read from the binary
(`game.dat`, RW addresses; caveat S-001), the game text (`lotr.str` of
`englishpatch201.big`), the INI (`gamedata.ini`) and the shipped maps.

## No King of the Hill, Capture the Flag, Resource Race or Hero vs Hero

The roadmap named King of the Hill, Capture the Flag, Resource Race, Hero vs
Hero and Control Points. **RotWK 2.01 has none of them.** That list matches
other SAGE games, such as C&C 3's modes, not this one:

- `game.dat` contains no string naming any of them (searched without case,
  with and without spaces; pinned by `gamemodes1 binary facts: game.dat names
  none of the modes RotWK does not have`). The only name the script condition
  `IS_GAME_MODE_ACTIVE` knows is `ringheroes` (RW 0x7E9E83, lane RULES-1).
- `lotr.str` has no text for them. Its only "game mode" texts are the online
  Quickmatch's `GUI:GameTypeHeadToHead` / `GUI:GameTypeScenario` and the War of
  the Ring's victory types (below).
- The maps in `Maps.big`, `_patch201maps.big` and BFME2's archives are versus
  maps (`map mp ...`), campaign maps (`map ang/good/evil ...`), War of the
  Ring battle maps (`map wor ...`) and cinematics. None carries a mode-specific
  rule. `ControlPoint` in the executable is a particle / colour INI field.
  `CaptureFlag` in `VictoryConditionStructureObjectFilter` is the outpost
  capture flag, an object excluded from the victory count.
- The game text still has the descriptions of four 2v2 "scenario" maps
  (`Map:MAPMPSHelmsDeep` ...: "All human players required"). That is the
  Quickmatch Scenario type. Their maps are not shipped in 2.01.

So OpenBFME has nothing to implement for those five names. Inventing them
would break PLAN rule 1.

## The modes RotWK has

| Mode | Where | How it is chosen | Rules / win condition | UI | Addresses | OpenBFME |
|---|---|---|---|---|---|---|
| **Battle** (the ordinary skirmish / LAN game) | Skirmish, LAN, online custom | any map, two or more teams (or players without a team) | VictoryConditions **rule 3**. The game kind `TheGameLogic + 0x114` is 3 (constructor RW 0x6301D3, MSG_CLEAR_GAME_DATA RW 0x779F20), so init RW 0x808A43 sets `+ 0xC = 3`. A player is defeated when no object passes `VictoryConditionStructureObjectFilter` and none passes `VictoryConditionUnitObjectFilter` (RW 0x809476). Retail's unit filter, `ANY -DOZER -NOT_AUTOACQUIRABLE -MordorWorker`, accepts no template (RW 0x763543: ANY needs a non-empty include mask), so **the last structure decides**: builders and armies keep no one alive. Nothing is judged before ftol(5 × SecondsBeforeBaseCheckActive) = 25 frames. The last alliance wins once someone has been defeated. A defeated player's objects are killed (RW 0x6ABE7C). | "GUI:PlayerHasBeenDefeated", the victory / defeat end screen (Multiplayer_Human's MULTIPLAYER_ALLIED_VICTORY / _DEFEAT), the score screen 25 frames later. No scores, timers or flags on the HUD. | RW 0x808A43, 0x808F53, 0x809404, 0x80953C, 0x808AA8, 0x808C4C | ported. **Fixed by this lane:** COMBAT-2 had read `+ 0x114` as the game mode (3 = replay) and judged every skirmish by rule 2, so a builder kept a player alive. |
| **Sandbox** | Skirmish, LAN | one team: every occupied slot on one team, or a single player | The lobby's start check RW 0x8431B8 sends "Entering sandbox mode..." (`GUI:SandboxMode`, RW 0x8436D8, kind 0: a LAN host's chat line; a skirmish shows nothing) and starts the game. In the game, `TheGameInfo` vslot 0x50 `isSandbox` (RW 0x800C8F: every other occupied slot on the local slot's team) means nobody ever wins: MULTIPLAYER_ALLIED_VICTORY is false (RW 0x808C64). The keep-score switch stays on after the single alliance (RW 0x809034). The quit menu's Exit sends no MSG_SELF_DESTRUCT (RW 0x625E5E). A player who loses everything is still defeated (rule 3). | the LAN lobby's line; no victory screen; "<player> has been Defeated" and the defeat screen for a player who lost everything | RW 0x8431B8, 0x800C8F, 0x808C4C, 0x80902C, 0x625E36 | ported by this lane (GameInfo::isSandbox, EconomyContext::sandbox, VictoryConditions, the lobby check, the quit menu) |
| **Ring heroes** (the `ringheroes` game mode) | Skirmish, LAN | the AllowRingHeroes rule (on by default) | the IS_GAME_MODE_ACTIVE "ringheroes" script condition gates the ring heroes in the map libraries | Rules tab row | RW 0x7E9E83 | lane RULES-1 |
| **War of the Ring** | Skirmish, LAN, online (retail) | the lobby's "Strategic" map type (MpGameSetup + 0x7C == 1) | the rule block's mode 1: StrategicPhaseTimer (none, 5 .. 500 s), BattleType (auto-resolve and RTS / auto-resolve / RTS), BattleChoice, AutoResolveType (dynamic / quick), **VictoryType**: Elimination ("conquering all of their territories"), Capital Assault ("conquering their capital territory"), Conquest ("all the strongholds"), World Domination 20 / 30 / 40 / All territories (tables RW 0xDB77D0, labels RW 0xDB7794, LWScenario:WOTRGameType00n / VALUE:WorldDom*). Its RTS battles run with another game kind (the dispatcher's case 0x1F, RW 0x779CB4: 1 LAN, 2 online), so they are judged by **rule 2** (structures or builders) and the Living World's veto (RW 0x441E4A). The lobby refuses one team ("GUI:NeedWOTREnemy", RW 0x8436A3). | the strategic map (StrategicHUD), not the RTS HUD | RW 0x960A10 .., 0x779C1F .., 0x809422 | planned lane WOTR-1 (roadmap M9); not a playable strategic mode. The ordinary start check ports the one-team refusal and selects rule 2 for another game kind, but **S-3610 remains**: scenario player limits and faction validation are not ported; the map's player count stands in and every faction passes. |
| Head to Head / Scenario, Tournament, Clan games | online only | Quickmatch's game type (`GUI:GameTypeHeadToHead` / `GUI:GameTypeScenario`), the Tournament tab, the ClanGame rule (hidden outside online play) | Head to Head is the battle above. Scenario needs the 2v2 scenario maps, which are not shipped. Tournament: "No Create-A-Hero, no complex options". The clan checks are in the start check (CLAN:Error*, RW 0x843725 ..). | online screens | RW 0x84370A .. | Online custom play is implemented. Quick Match / tournaments use the separate platform API, not just the relay lobby; unavailable API responses leave these features unavailable. The online Clans tab is unavailable; standalone `SkirmishGameSetup` clan checks are ported, not wired into online staging. |

## The lobby's start check (RW 0x8431B8)

RotWK's AptMpGameSetup start check. It replaced the Zero Hour `startPressed`
check the skirmish lobby used. Order:

1. The map's maximum players (RW 0x9758E3: MapCache + 0x20, above 8 the
   constant 8) and minimum (RW 0x97597F: 0 outside the War of the Ring). No
   maximum refuses silently.
2. Per slot:
   - a human without the map: `GUI:PlayerNoMap`;
   - acceptance;
   - the clan tag;
   - users, humans, teams and random teams;
   - start positions.
3. The two-humans gate, `GUI:NeedHumanPlayers`.
4. `GUI:CouldNotTransferMap`.
5. `LAN:TooManyPlayers`, then RotWK's new `LAN:TooFewPlayers`.
6. The command line's minimum players (GlobalData + 0xA44, 0).
7. In the War of the Ring: `GUI:DisabledFaction` (not ported, S-3610), then
   `GUI:NeedStartSpots`.
8. `LAN:NeedMoreTeams`.
9. Fewer than two teams: `GUI:NeedWOTREnemy` in the War of the Ring with its
   flag, else **`GUI:SandboxMode`** and the start goes on.
10. The clan errors.
11. `GUI:NotifiedStartIntent`.
12. The NAT check.

`SkirmishGameSetup::checkStart` ports it. Each line goes to the owner's vslot
0x44 with a kind:

- **The Skirmish owner (RW 0x928482)** shows only kind 1, as a message box
  with an empty title (`MessageBoxOk(L"", text)`, RW 0x84ED5D). A skirmish
  therefore shows nothing for "Entering sandbox mode...".
- **The LAN owner (RW 0x847508):**
  - kind 0 is a local chat line, so the host sees the sandbox line;
  - kind 1 is the message box and a local line;
  - kind 2 is a system line to every player.

## Determinism

- **The battle:** the rule is part of the hashed VictoryConditions state.
- **The sandbox flag:** hashed only when set, so every game that is not a
  sandbox hashes as before. It is computed from the game setup that travels to
  every peer (LAN options, new game message, replay header). For peers that
  disagree on the local slot it can differ only in the cases retail differs
  too (an observer slot).
- **Pins:** the rule is part of the VictoryConditions hash, so every
  skirmish's state hash changes from the first frame. The PERF-1 skirmish
  vector was re-measured (remote-measure on a CI host). Changing the rule also
  moved the AI-vs-AI defeat frames of two pinned games (seed 7: 4206 to 3831;
  seed 1234: 5519 to 5443; same losers): a side is now out when its last
  structure falls, even with builders left.

## Tests

`engine/tests/test_gamemodes1.cpp`:

- **Binary facts:** the rule by game kind, the GameInfo vtables' isSandbox,
  the start check's texts, and the absent modes.
- **`GameInfo::isSandbox`.**
- **The start check:** sandbox, War of the Ring, too many players, map
  transfer, clan, start intent.
- **Two retail Evendim games, each run twice with equal hashes every frame:**
  - the battle: the last structure falls, builders and a hero remain, the
    side is out and the other wins;
  - the sandbox: one team, never won, the score stays on, a player who loses
    everything is defeated.

`test_structure_victory.cpp` and `test_hud_combat_structure.cpp` keep rule
2's builder cases under a game of kind 1.

# Cheat Mode (OpenBFME)

Cheat Mode is an OpenBFME rule for custom games. It does not exist in RotWK, which compiles its cheats out. When it is on, the host can give resources, spawn units, level heroes, reveal the map, or end the game. Every player in the game can see that the game has Cheat Mode and can see everything the host does with it.

Code: `engine/src/GameLogic/CheatMode.{h,cpp}` (lane CHEAT-1).

## Where players see it

- **Before joining.** The Open Play game list and the LAN game list add "- CHEAT MODE" to the game's name.
- **Rules tab.** In skirmish, LAN and Open Play games, the lobby has a check row "Cheat Mode (OpenBFME rule)". Only the host can tick it, and every player sees the box ticked. Quick Match and tournament rooms never offer it. The lobby refuses `cheatMode` on a room that has a code.
- **Loading screen.** The rules line includes "Cheat Mode (OpenBFME rule): on".
- **In game.** Every player sees:
  - the marker "CHEAT MODE (OpenBFME): <host> is the host" in the top right, under the message lines;
  - one message line for every command, whether it ran or was refused, in RotWK's in-game message style. Examples: "Cheat Mode: Ann gave 2000 resources to Bea", "Cheat Mode: refused: Bea is not the host".
- **Player list.** On the players screen's Status page, the host appears as "<name> (Cheat Mode host)".
- **Score screen.** It shows "Cheat Mode (OpenBFME) game: not counted in any statistics".

## The host's panel

Open the in-game menu (Esc), then press **CHEAT MODE**. The panel uses the same style as the Options menu and has these tabs:

| Tab | What it does |
|---|---|
| Resources | Gives a player resources, command points or spell book power points. |
| Units | Spawns 1 to 10 of a unit or hero from that player's faction at the centre of your view. |
| Heroes | Levels up one of the player's living heroes. |
| Map | Reveals the whole map for a player, or hides it again. |
| Game | Ends the game so that the chosen player's alliance wins, or voids it (no winner). |

## Records and statistics

- **Replays and saves.** A replay records the setup's Cheat Mode in its header (replay format 8) and every command in its frames, so it plays the same game and shows the same lines. A save keeps the state in its snapshot.
- **Online lobby.** The lobby's game record (`starts/<id>.json`, the START) contains `setup.cheatMode` = the host's slot. Stats and any ranking must skip a game that has this field.
- **This installation's game record.** The record gets `"cheat_mode": true`. It stays in the file, but it is never counted in "Your stats" and never sent to `POST /v1/results`.
- **Battle honors.** A Cheat Mode skirmish earns none.

## Command API

A stream-voting plugin would drive Cheat Mode through this API. Every command is an ordinary frame command, `MSG_OPENBFME_CHEAT` (type 32001). It is sent through the lockstep command list like any player order and recorded in the replay, and every peer runs it on the same logic frame.

A command runs only when **both** of these hold:

1. the game's setup has Cheat Mode on, and
2. the message's player is the host's seat. The network stamps that player from the sending slot, so a peer cannot pretend to be the host.

In any other case the command is refused, logged and announced on every peer. A malformed command or an unknown command number is refused the same way. Either way the game goes on.

### From GDScript (the game's `GameWorld` node)

```gdscript
world.cheat_info()          # { enabled, seat_slot, seat_player, seat_name, local_is_seat, voided, executed, refused, label }
world.cheat_players()       # [ { index, name, side, defeated } ]: the players a command may name
world.cheat_units(player)   # [ { template, id, hero, label } ]: that player's faction's units and heroes, heroes first
world.cheat_heroes(player)  # [ { id, template, label, level } ]: that player's living heroes that can level up
world.cheat_command({ "command": "add_resources", "target": 3, "value": 2000 })   # -> { ok, error }
world.take_cheat_lines()    # [ { frame, text, executed, command } ]: the lines announced since the last call
```

`cheat_command` returns `ok: false` locally when the game has no Cheat Mode or when this client is not the host's seat. Every peer checks the same two conditions again.

### Commands

| `command` | `target` | `value` | Other fields | Effect |
|---|---|---|---|---|
| `add_resources` | player index | 1 .. 100000 | | `Money::deposit`, as ZH's MSG_CHEAT_ADD_CASH does |
| `add_command_points` | player index | 1 .. 1000 | | Adds to the player's command point bonus, which raises the limit |
| `add_power_points` | player index | 1 .. 100 | | Spell book purchase points |
| `spawn` | player index | 1 .. 10 | `template` (name), `x`, `y` (map coordinates) | Spawns that many in rows of 4, 15 units apart. Every point must be on the map. Structures, projectiles and Create-a-Hero templates are refused. |
| `level_up` | player index | 1 .. 10 | `object` (id) | Levels a living object of that player that can gain levels |
| `reveal_map` | player index | 1 reveal, 0 hide | | Reveals once per player. Hiding undoes only Cheat Mode's own reveal. |
| `end_game` | player index | 0 | | Every player outside the target's alliance is defeated, and the alliance wins |
| `void_game` | -1 | 0 | | Every player is defeated. The game ends with no winner. Works once. |

A value outside its range, a player that is not in the game (the neutral player, an observer or a map side), an unknown template or object, or a point off the map is refused. The refusal is announced as "Cheat Mode: <host> (not done): <why>".

### Network form

In C++, `CheatMode::makeMessage` builds the message. The arguments are, in order:

- integers: `command`, `target`, `value`;
- for `spawn` and `level_up`, one more integer: the template id (`ThingFactory::findByTemplateID`) or the object id;
- for `spawn`, then a location argument: the first unit's point.

The command numbers are `add_resources` 1, `add_command_points` 2, `add_power_points` 3, `spawn` 4, `level_up` 5, `reveal_map` 6, `end_game` 7, `void_game` 8.

### Ideas for a plugin (not built)

A Twitch-voting plugin runs in the host's game, because only the host's seat can send these commands. It would map each poll result to one `cheat_command` call. Two points to respect:

- **Don't flood.** Every peer announces every command, so the game should not get more than one command every few seconds.
- **Expect refusals.** `take_cheat_lines()` tells the plugin what actually happened, refusals included.

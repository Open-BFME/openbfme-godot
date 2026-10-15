# bfme Lua library

OpenBFME's console embeds **Lua 5.4.9** and implements a portable subset of the
[BFME2Lua / Second Age API](https://docs.secondage.dev/#templates). This is an independent implementation over OpenBFME's engine;
it does not load or inject the Second Age DLL. The retail game's Lua 4.0.1 VM continues to run its original scripts independently.

Open the console with **backtick (`)** or **Ctrl+Shift+C** during a match (Escape closes it). Backtick no longer opens the powers menu.
The BFME-style console is scaled 2×, with Command and Log tabs, a syntax-highlighted multiline editor, and Run / Clear controls. It scrolls when the window is smaller than the enlarged content.
Reload entry scripts with **Ctrl+Shift+F5**, **Reload scripts**, `console.reload()`, or `bfme.reloadScripts()`.
Place files in `godot/dev/` (development) or `user://dev/` (packaged games); see [reloadable scripts](../godot/dev/README.md).
After reloading, the bundled helper enables `setGold(10000)` during a match. Console globals persist; reloaded functions replace their prior definitions.
Enter runs the whole editor; Shift+Enter inserts a new line. Up / Down at the editor boundaries browse history.
Shift+backtick can still type a tilde.

```lua
local bfme = import("bfme") -- also available globally and through require("bfme")
bfme.players.localPlayer.money = 10000
```

Writes queue work for the next game update. To inspect the outcome:

```lua
ticket = bfme.players.localPlayer:set("money", 10000)
-- On a subsequent update / console command:
print(bfme.getMutationStatus(ticket)) -- pending, applied, a failure reason, or nil
print(bfme.players.localPlayer.money)
```

Expressions display their returned values directly; `=expression` is also accepted. Ordinary statements, functions, loops, `pairs`, `ipairs`, `#`, and modern Lua syntax work.
Globals persist across menu and match transitions. Proxies from an earlier match expire. Look players up again after starting a new match.

## Implemented API

| API | Behavior |
| --- | --- |
| `bfme.players[id]` | Engine player slot, including neutral slot 0; unknown slots return nil |
| `bfme.players.localPlayer`, `.neutral` | Current local player and neutral player aliases |
| `bfme.templates[id]` | All loaded definitions by INI template name |
| `bfme.unitTypes[id]` | Selectable, non-structure definitions |
| `bfme.buildingTypes[id]` | Structure definitions |
| `bfme.unitInstances[id]`, `bfme.buildingInstances[id]` | Instances by engine object ID |
| `bfme.unitInstances(player)`, `bfme.buildingInstances(player)` | Dense, ID-sorted instance lists, optionally filtered by player slot or proxy |
| `pairs(collection)`, `#collection` | Enumeration and count; player/instance keys are integers |
| `proxy:set(field, value)` | Validate a write and return its mutation ticket |
| `proxy:inspect()` | Detached `{label, kind, id, values}` snapshot |
| `pairs(proxy)`, `tostring(proxy)` | Scalar snapshot and readable identity |
| `bfme.getLiveDataStatus()` | Readiness, generation, frame, local player ID and catalog counts |
| `bfme.getInstanceStatus()` | Readiness, action availability and instance counts |
| `bfme.getMutationStatus(ticket)` | Ticket result; retains 1024 results, allows 128 pending mutations |
| `bfme.pause()`, `bfme.unpause()`, `bfme.isPaused()` | Match pause; changes return tickets |
| `bfme.log(text)`, `print(...)`, `bfme.getTimeMs()` | Console output and monotonic time |
| `GetFrame()` | Current logic frame |
| `ExecuteAction("PLAYER_SET_MONEY", player, amount)` | Compatibility convenience; player is an engine slot or exact engine name |
| `ExecuteAction("PLAYER_GIVE_MONEY", player, amount)` | Add/subtract from the committed balance |

Player fields:

| Fields | Access |
| --- | --- |
| `id`, `name`, `displayName`, `username`, `faction`, `isAI` | Read only |
| `money`, `gold` | Same balance; writable integer 0..2,147,483,647 through native deposit/withdraw bookkeeping |
| `commandPoints`, `commandPointsBase`, `commandPointsCap`, `commandPointsUsed` | Writable integer 0..1,000,000 |
| `commandPointsBonus` | Writable integer -1,000,000..1,000,000 |
| `commandPointsRemaining` | Read only; effective capacity minus usage |
| `powerPoints`, `powerpoint` | Writable integer 0..1,000,000 through native science-point updates |

Template fields: `id`, `name`, `displayName`, `faction`, and `side` are read only. `cost` and `refund` accept integers 0..65535;
`buildTime` / `trainTime`, `visionRange`, and `shroudClearingRange` accept finite, nonnegative numbers. Fields absent from native data return nil.
Changing a template affects subsequent engine reads; existing objects may retain cached vision data. Runtime edits persist until the engine reloads those definitions.
Display names currently expose the definition's native INI value, which may be a localization key.

Instance fields include `id`, `objectId`, `typeId`, `type` / `template`, `playerId` / `player`, `ownerPlayer`, `faction`, `displayName`,
`position = {x,y,z}`, `x`, `y`, `z`, `rotation`, `health`, `maxHealth`, and `level`. Buildings also expose `underConstruction`, `constructionPercent`, and `builderId`.
Coordinates use SAGE world axes. Position and health are read only; modifying a position copy does not move the unit.
`ownerPlayer` accepts an engine player ID or Player proxy, and `level` uses the object's native experience system. Unsupported levels return a failed ticket.
`:kill()` invokes normal death; `:destroy()` (alias `:destory()`) queues native destruction and cleanup.

```lua
for id, unit in pairs(bfme.unitInstances) do
    print(id, unit.typeId, unit.playerId, unit.health)
end
for _, building in ipairs(bfme.buildingInstances(bfme.players.localPlayer)) do
    print(building, building.position.x, building.position.y)
end
```

## Execution and compatibility limits

Reads use detached snapshots taken when a console chunk starts. Assignments enqueue mutations; they do not update reads within that chunk.
Mutations re-resolve engine IDs between simulation frames with the logic worker held. Missing objects and expired registries fail explicitly.
Proxy fields and collection entries cannot be replaced using raw table writes. Inspection and position tables are detached copies.
Read-only console use preserves saving; queuing a game mutation disables command-log saves for that match. Console execution and mutations are allowed in multiplayer, replay playback, replay recordings, and save catch-up.
These console edits are local and are not added to the synchronized network stream or replay command log. A chunk that fails after queuing writes does not roll those writes back.

This does **not** yet implement Second Age's complete API: selection and command UI, production/construction actions, diplomacy/team edits,
AI suspension, power catalogs/unlocks, durability edits, shared values/messages, JSON/dialogs, automatic dev-script callbacks,
controller/camera routing, save sync, or multiplayer script synchronization. `bfme.capabilities` describes this implementation.
`require` currently loads the built-in bfme module only; IO, OS, debug and native module loading are not exposed.
Console execution has a 250 ms Lua instruction-hook budget and a bounded output buffer; this is not a transactional script sandbox.

## Verification

`engine/build/openbfme_tests --test-case='bfme Lua*'` checks module identity, modern Lua syntax alongside the retail VM,
proxy identity, enumeration, detached copies, money tickets, validation, expired handles, queue limits, and error recovery.
`godot --headless --path godot --script res://tests/lua_console_test.gd` exercises the console and native extension together.

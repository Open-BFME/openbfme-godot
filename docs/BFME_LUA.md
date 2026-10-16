# bfme Lua library

OpenBFME's console embeds **Lua 5.4.9** with an object-based `bfme` API. Its collections and player/template fields
follow the [BFME2Lua / Second Age API](https://docs.secondage.dev/#templates); functions are grouped into libraries
and actions are methods on handles. This is an independent implementation over OpenBFME's engine;
it does not load or inject the Second Age DLL. The retail game's Lua 4.0.1 VM continues to run its original scripts independently.

Open the console with **backtick (`)** or **Ctrl+Shift+C** during a match (Escape closes it). Backtick no longer opens the powers menu.
The BFME-style console is scaled 2×, with Command and Log tabs, a syntax-highlighted multiline editor, and Run / Clear controls. It scrolls when the window is smaller than the enlarged content.
Reload entry scripts with **Ctrl+Shift+F5**, **Reload scripts**, or `bfme.console.reload()`.
Place files in `godot/dev/` (development) or `user://dev/` (packaged games); see [reloadable scripts](../godot/dev/README.md).
After reloading, the bundled helper enables `setGold(10000)` during a match. Console globals persist; reloaded functions replace their prior definitions.
Enter runs the whole editor; Shift+Enter inserts a new line. Up / Down at the editor boundaries browse history. The last 256 commands persist between sessions in `user://console_history.cfg`, including multiline scripts.
Shift+backtick can still type a tilde.

```lua
local bfme = import("bfme") -- also available globally and through require("bfme")
bfme.players.localPlayer.money = 10000
local barracks = bfme.debug.getSelectedBuilding()
if barracks then barracks:trainUnit("GondorFighterHorde", 3) end
```

## Structure

`bfme` is a read-only module object with three kinds of members:

- **Collections** (`bfme.players`, `bfme.templates`, `bfme.unitTypes`, `bfme.buildingTypes`, `bfme.unitInstances`,
  `bfme.buildingInstances`) index engine data and return **handles**.
- **Handles** (`Player`, `Template`, `UnitType`, `BuildingType`, `Unit`, `Building`) expose fields and the actions
  that belong to them, called with `:` — `building:trainUnit(...)`, `player:construct(...)`, `unit:kill()`.
- **Libraries** (`bfme.match`, `bfme.input`, `bfme.selection`, `bfme.mutations`, `bfme.debug`, `bfme.console`)
  group functions that do not belong to a single handle.

The only globals are `bfme`, `import`, `print` and the standard Lua libraries. Each library is also reachable with
`import("bfme.<name>")` or `require("bfme.<name>")`, and its functions accept both `.` and `:` calls.
`bfme.capabilities` is a detached `{luaVersion, libraries}` table.

```lua
local debug = import("bfme.debug")
print((bfme.debug).getSelectedBuilding(), debug:getSelectedUnit())
for name in pairs(bfme.selection) do print(name) end
```

`bfme`, libraries, collections and handles cannot be replaced or extended with raw writes. A field a library does
not define reads as `nil`.

## Mutation tickets

Every action and field write queues work for the next game update and returns an integer **ticket**:

```lua
ticket = bfme.players.localPlayer:setMoney(10000)
-- On a subsequent update / console command:
print(bfme.mutations.getStatus(ticket)) -- pending, applied, a failure reason, or nil
print(bfme.players.localPlayer.money)
```

Expressions display their returned values directly; `=expression` is also accepted. Ordinary statements, functions, loops, `pairs`, `ipairs`, `#`, and modern Lua syntax work.
Globals persist across menu and match transitions. Handles from an earlier match expire. Look players up again after starting a new match.

## Collections

| API | Behavior |
| --- | --- |
| `bfme.players[id]` | Engine player slot, including neutral slot 0; unknown slots return nil |
| `bfme.players.localPlayer`, `.neutral` | Current local player and neutral player aliases |
| `bfme.templates[id]` | All loaded definitions by INI template name |
| `bfme.unitTypes[id]` | Selectable, non-structure definitions |
| `bfme.buildingTypes[id]` | Structure definitions |
| `bfme.unitInstances[id]`, `bfme.buildingInstances[id]` | Instances by engine object ID |
| `pairs(collection)`, `#collection` | Enumeration and count; player/instance keys are integers |

## Handles

Every handle has:

| Method | Behavior |
| --- | --- |
| `handle:set(field, value)` | Validate a field write and return its ticket (same as `handle.field = value`) |
| `handle:inspect()` | Detached `{label, kind, id, values}` snapshot |
| `pairs(handle)`, `tostring(handle)` | Scalar snapshot and readable identity, e.g. `Building[42]` |

### Player

| Method | Behavior |
| --- | --- |
| `player:setMoney(n)` | Set the balance (0..2,147,483,647) |
| `player:addMoney(n)` | Add or subtract from the committed balance; the result must stay in range |
| `player:revealMap()` | Explore the whole map once |
| `player:setFogOfWar(enabled)` | `false` adds a persistent visibility override, `true` removes it; repeated calls do not stack |
| `player:units()`, `player:buildings()` | Dense, ID-sorted lists of the instances this player controls |
| `player:construct(building, position, speed, onComplete)` | Self-building construction; see [Construction](#construction) |

| Fields | Access |
| --- | --- |
| `id`, `name`, `displayName`, `username`, `faction`, `isAI` | Read only |
| `money`, `gold` | Same balance; writable integer 0..2,147,483,647 through native deposit/withdraw bookkeeping |
| `commandPoints`, `commandPointsBase`, `commandPointsCap`, `commandPointsUsed` | Writable integer 0..1,000,000 |
| `commandPointsBonus` | Writable integer -1,000,000..1,000,000 |
| `commandPointsRemaining` | Read only; effective capacity minus usage |
| `powerPoints`, `powerpoint` | Writable integer 0..1,000,000 through native science-point updates |

### Template, UnitType, BuildingType

`id`, `name`, `displayName`, `faction`, and `side` are read only. `cost` and `refund` accept integers 0..65535;
`buildTime` / `trainTime`, `visionRange`, and `shroudClearingRange` accept finite, nonnegative numbers. Fields absent from native data return nil.
Type fields can be edited in the menu once definitions are loaded, as well as during a match. Changing a template affects subsequent engine reads; existing objects may retain cached vision data. Runtime edits persist until the engine reloads those definitions.
Display names currently expose the definition's native INI value, which may be a localization key.

### Unit and Building

| Method | Behavior |
| --- | --- |
| `object:kill()` | Normal death |
| `object:destroy()` | Native destruction and cleanup |
| `unit:construct(building, position, speed, onComplete)` | This unit builds for its owner; see [Construction](#construction) |
| `building:trainUnit(unit, count)` | Queue recruitment; see [Training units](#training-units) |

Fields include `id`, `objectId`, `typeId`, `type` / `template` (a UnitType/BuildingType handle), `playerId` / `player`, `ownerPlayer` (a Player handle),
`faction`, `displayName`, `position = {x,y,z}`, `x`, `y`, `z`, `rotation`, `health`, `maxHealth`, and `level`. Buildings also expose `underConstruction`, `constructionPercent`, and `builderId`.
Coordinates use SAGE world axes. Position and health are read only; modifying a position copy does not move the unit.
`ownerPlayer` accepts an engine player ID or Player handle, and `level` uses the object's native experience system. Unsupported levels return a failed ticket.

```lua
for id, unit in pairs(bfme.unitInstances) do
    print(id, unit.typeId, unit.playerId, unit.health)
end
for _, building in ipairs(bfme.players.localPlayer:buildings()) do
    print(building, building.position.x, building.position.y)
end
```

## Libraries

| Library | Functions |
| --- | --- |
| `bfme.match` | `getStatus()`, `getFrame()`, `getTimeMs()`, `setGameSpeed(x)`, `pause()`, `unpause()`, `isPaused()` |
| `bfme.input` | `getCursorPosition()` |
| `bfme.selection` | `all()`, `units()`, `buildings()`, `first()`, `count()` |
| `bfme.mutations` | `getStatus(ticket)`, `getBuilding(ticket)` |
| `bfme.debug` | `getSelectedBuilding()`, `getSelectedUnit()`, `inspect(handle)` |
| `bfme.console` | `reload()`, `log(text)` |

### bfme.match

`getStatus()` returns `{ready, generation, frame, paused, localPlayerId, playerCount, templateCount, unitCount, buildingCount, selectionCount}`.
`getFrame()` is the current logic frame and `getTimeMs()` a monotonic clock.

`setGameSpeed(0..100)` sets the simulation speed multiplier: 0 stops time, 1 normal, 2 twice as fast, 100 one hundred times as fast.
Decimals are allowed. Pause is independent: `pause()` / `unpause()` return tickets, `isPaused()` reads the sampled state.

### bfme.input

`getCursorPosition()` returns `{x, y, z}` in SAGE map coordinates, with `z` being terrain height. It uses the displayed camera
and the gameplay pointer, including controller pointer movement, just like native building placement. Returns `nil` without
an active HUD/map or when the pointer is outside the viewport or the ray misses terrain. This is the terrain underneath
the pointer, not the surface of a unit/building or the minimap's represented location.
The position is sampled at the start of the chunk and returned as a detached table.

```lua
local pos = bfme.input.getCursorPosition()
if pos then bfme.players.localPlayer:construct("GondorBarracks", pos, 2) end
```

### bfme.selection and bfme.debug

Selection reads use the **local client selection**, sampled at the start of a chunk, newest first; the first entry is
the object that drives the command bar. `all()`, `units()` and `buildings()` return handle lists, `first()` the newest handle.
Hordes appear as their container unit. Selected objects outside the unit/building collections (for example, props) are skipped.
Selection is read-only.

`bfme.debug.getSelectedBuilding()` / `getSelectedUnit()` return the most recently selected Building / Unit handle, or nil.
`bfme.debug.inspect(handle)` is `handle:inspect()` with an explicit type check.

### bfme.mutations

`getStatus(ticket)` returns `pending`, `applied`, `building` / `complete` (construction), a failure reason, or nil once
the result has aged out (1024 results retained; 128 pending mutations allowed). `getBuilding(ticket)` returns the Building
a construction ticket created while it exists in the current snapshot.

### bfme.console

`reload()` rereads the entry scripts and returns how many ran. `log(text)` writes one console line; `print(...)` also writes there.

## Construction

```lua
local p = bfme.players.localPlayer
local ticket = p:construct(
    "GondorBarracks",              -- building template name or BuildingType handle
    {x = 1500, y = 2200},          -- map x/y; terrain supplies height
    2,                             -- construction speed: 1 = normal (default), 2 = twice as fast
    function(building)
        print("Construction complete:", building.id)
    end
)
-- or have a builder walk over and build it for its owner:
bfme.unitInstances[builderId]:construct("GondorBarracks", {x = 1500, y = 2200})
```

`unit:construct` uses a living builder; it walks to the site and builds through its native builder AI.
`player:construct` raises the building on its own with native construction visuals and disabled production
until complete. Both modes charge the owner the normal gold cost. Scripts can choose a building outside the builder's
command set; terrain and overlap checks still apply, and the builder path also uses its normal placement/pathfinding checks.

Speed accepts finite values from **0.01 to 100** and affects only that building's construction work, not movement or other buildings.
Add `rotation` in radians to the position table if needed. Unsupported building modules, units that cannot build,
blocked sites and insufficient gold produce explicit ticket errors.

The method queues the request and returns a ticket, not an immediate building. On a later console command, inspect
`bfme.mutations.getStatus(ticket)` (pending, building, complete, or an error) and `bfme.mutations.getBuilding(ticket)`.

The optional callback receives the completed Building handle exactly once, at a safe
point between simulation frames. It is not called if construction is cancelled or the
building is destroyed. Callback errors appear in the ticket status. A callback can enqueue
more Lua actions; those apply at the next safe point. The existing 250 ms Lua execution
budget applies to callbacks. Requests and callbacks are discarded on registry/match reset.
They and the speed override are session-only: no save hooks or save format changes are
introduced, and they are not replayed through command-log saves. Script reload retains
already registered callbacks; a new match/load invalidates them.

## Training units

```lua
local barracks = bfme.debug.getSelectedBuilding()
local ticket = barracks:trainUnit(
    "GondorFighterHorde",         -- unit template name or UnitType handle
    3                             -- number of recruitment orders (default 1)
)
```

Count must be an integer from 1 to 1000. The building's owner pays normal costs; production follows native build
times, rally points, prerequisites, upgrades, unit limits, and queue capacity.
Each order recruits the template's normal unit/battalion quantity. The building
must be alive, completed, enabled, and offer that unit in its command set.

This queues native recruitment orders at the next safe point; it does not instantly
spawn units or wait for queue space. Inspect `bfme.mutations.getStatus(ticket)` in a
later chunk: `applied` means all orders were queued, while, for example,
`queued 2 of 3: not enough gold` means two were accepted and remain queued.
Unsupported buildings, expired IDs, and unknown unit types fail explicitly.

## Console Cheats tab

The backtick console's **Cheats** tab offers local-player gold, +100k gold, +100k command points, Reveal map,
Disable / Restore fog of war, game speed (0–100×), pause and resume. Actions submit the same Lua API as the Command
tab (`player.money`, `player:revealMap()`, `player:setFogOfWar(...)`, `bfme.match.setGameSpeed(...)`, `bfme.match.pause()`);
their output appears in Log. Gold and CP additions respect their supported limits.

## Execution and compatibility limits

Reads use detached snapshots taken when a console chunk starts. Assignments and actions enqueue mutations; they do not update reads within that chunk.
Mutations re-resolve engine IDs between simulation frames with the logic worker held. Missing objects and expired registries fail explicitly.
Inspection and position tables are detached copies.
Lua console use does not change save availability. Console edits are not included in command-log saves. Console execution and mutations are allowed in multiplayer, replay playback, replay recordings, and save catch-up.
These console edits are local and are not added to the synchronized network stream or replay command log. A chunk that fails after queuing writes does not roll those writes back.

This does **not** implement Second Age's complete API: selection edits and command UI,
diplomacy/team edits, AI suspension, power catalogs/unlocks, durability edits, shared values/messages, JSON/dialogs,
automatic dev-script callbacks, controller/camera routing, save sync, or multiplayer script synchronization.
Second Age's flat helpers (`ExecuteAction`, `GetFrame`, global `setGameSpeed`) are not provided; use the handle methods and libraries above.
`require` loads only `bfme` and `bfme.<library>`; IO, OS, debug and native module loading are not exposed.
Console execution has a 250 ms Lua instruction-hook budget and a bounded output buffer; this is not a transactional script sandbox.

## Extending the API

The API lives in the bootstrap in `engine/src/GameLogic/ScriptEngine/BfmeLuaRuntime.cpp`.

- **Something one object does** belongs on its handle: add it to `methods[<collection>]` (`players`, `unitInstances`, `buildingInstances`).
- **Anything else** goes in a library: `library('name', {fn = function(...) ... end})`.

Both use the core helpers: `record(handle)` for checked lookups, `proxy(kind, id)` for handles, `templateName(...)`
for template arguments, `numeric(...)` for validation, `queue(...)` for writes and `copy(...)` for detached results.
New engine state is added to `BfmeLuaRuntime::Snapshot` and filled in `GameWorld::bfme_snapshot()`, which runs with
the logic worker held. New queued actions are applied in `GameWorld::bfme_apply()`. Add a contract test to
`engine/tests/test_bfme_lua.cpp` and document it here.

Planned: `bfme.camera` (read/move the view), selection writes (select/deselect via the native selection messages),
`bfme.powers`, `bfme.events` (callbacks on frame/selection/death), and unit orders (`unit:moveTo`, `unit:attack`).

## Verification

`engine/build/openbfme_tests --test-case='bfme Lua*'` checks module identity, library and handle structure, modern Lua syntax alongside the retail VM,
handle identity, enumeration, detached copies, money tickets, validation, expired handles, queue limits, construction, training, selection and error recovery.
`godot --headless --path godot --script res://tests/lua_console_test.gd` exercises the console and native extension together.

-- Lua skirmish AI (lane LUAAI-1): plays a computer player through the bfme API instead of the native skirmish AI.
-- Off by default: computer players keep the native AI. Set luaai.config.autoEnable = true to opt in each match
-- (not in network games or replays, where the native AI keeps playing). By hand:
--   luaai.enableAll()          every computer player
--   luaai.enable(player)       one (a Player handle or id); luaai.disable(player) / luaai.disableAll() hand it back
--                              (until the next match; set luaai.config.autoEnable = false to keep the native AI)
--   luaai.status()             one line per controlled player
-- The native AI keeps its state while switched off and resumes from it. Only the functions in docs/BFME_LUA.md are used,
-- so this file is also the worked example of a Lua AI. It sees the whole map (no shroud), like a cheating AI.
-- Local only: orders and the AI switch are refused in network games.

luaai = luaai or { players = {} }
luaai.stats = luaai.stats or { placed = 0, refused = 0 } -- constructions the engine started / refused (every match)

luaai.config = {
    autoEnable = false,      -- opt in to handing every computer player to the Lua AI at match start
    thinkMs = 1000,          -- how often each controlled player thinks
    economyStart = 2,        -- resource buildings (farms, Mallorn trees, furnaces ...) before the first production building
    economyMax = 6,          -- resource buildings in all; after the start about one per production building
    productionMax = 4,       -- unit production buildings in all (the fortress does not count)
    perProductionType = 2,   -- at most this many of each production building (every type once before any twice)
    builderCount = 2,        -- builders kept (more are trained only below this)
    heroGold = 2500,         -- heroes are trained only with this much gold
    goldReserve = 400,       -- training leaves this much for construction while the base grows
    attackSize = 6,          -- combat units (hordes count once) before an attack
    retreatFraction = 0.4,   -- an attack falls back below this share of its starting size
    defendRadius = 1100,     -- enemies this close to the base are attacked first
    buildRadius = 750,       -- the last resort: anywhere legal this close to the base centre
    buildGap = 50,           -- the walking room kept between a new building and every structure
    snapRadius = 80,         -- how far a planned slot may move to the nearest legal site
    slotSize = 160,          -- the grid's cell (at least a building's width plus buildGap)
    blockHalfWidth = 2,      -- a block is 2 * this + 1 slots across
    blockDepth = 4,          -- and this many rows deep
    farmSiteSnap = 60,       -- how far a resource building may move from the map's marker (FarmTemplate)
    farmEnemyFortress = 900, -- markers this close to an enemy fortress are skipped (the native economy builder's rule)
    farmThreatRadius = 400,  -- markers with an enemy unit or structure this close are skipped
    economySpacing = 340,    -- without usable markers: the grid behind the fortress is this wide, so resource buildings stand apart
    reserveMs = 20000,       -- a site just ordered stays reserved this long (until its foundation stands)
    blockedMs = 120000,      -- a site where construction was refused is avoided this long
    stalledMs = 20000,       -- a foundation without progress this long gets an idle builder sent to finish it
    statusEveryMs = 15000,   -- a status line in the log
}

local cfg = luaai.config
-- buildings are told apart by their KindOf (isEconomy: ECONOMY_STRUCTURE, isFactory: FS_FACTORY without it), the same for every faction

local function resolve(player)
    if type(player) == "number" then return assert(bfme.players[player], "no player " .. player) end
    return assert(player, "expected a Player")
end

function luaai.enable(player)
    local p = resolve(player)
    local ticket = p:setAIEnabled(false)
    luaai.players[p.id] = luaai.players[p.id] or { lastStatus = 0, attackSize = 0 }
    print("LUAAI player " .. p.id .. " (" .. tostring(p.faction) .. ") is now controlled by Lua")
    return ticket
end

function luaai.disable(player)
    local p = resolve(player)
    luaai.players[p.id] = nil
    print("LUAAI player " .. p.id .. " is handed back to the native AI")
    return p:setAIEnabled(true)
end

function luaai.enableAll()
    local n = 0
    for _, p in ipairs(bfme.ai.list()) do
        if not p.defeated then luaai.enable(p); n = n + 1 end
    end
    return n
end

function luaai.disableAll()
    for id in pairs(luaai.players) do
        if bfme.players[id] then luaai.disable(id) end
    end
    luaai.players = {}
end

-- ---- what the player has -------------------------------------------------------------------------------------------

local function census(p)
    local c = { builders = {}, idleBuilders = {}, army = {}, idleArmy = {}, buildings = {}, byType = {}, economy = 0, production = 0, producers = {} }
    for _, u in ipairs(p:units()) do
        if u.isBuilder then
            c.builders[#c.builders + 1] = u
            if u.isIdle then c.idleBuilders[#c.idleBuilders + 1] = u end
        elseif (u.health or 0) > 0 then
            c.army[#c.army + 1] = u
            if u.isIdle then c.idleArmy[#c.idleArmy + 1] = u end
        end
    end
    for _, b in ipairs(p:buildings()) do
        c.buildings[#c.buildings + 1] = b
        local t = b.typeId or "?"
        c.byType[t] = (c.byType[t] or 0) + 1
        if b.isEconomy then c.economy = c.economy + 1
        elseif b.isFactory then c.production = c.production + 1 end
        if not b.underConstruction then c.producers[#c.producers + 1] = b end
    end
    return c
end

local function baseCentre(p, state, c)
    if state.base then return state.base end
    local start = p:getStartPosition()
    if not start and c.buildings[1] then start = c.buildings[1].position end
    if not start and c.builders[1] then start = c.builders[1].position end
    state.base = start
    return start
end

local function enemyObjects(p)
    local buildings, units = {}, {}
    for _, other in pairs(bfme.players) do
        if other.id ~= p.id and p:isEnemy(other) and not other.defeated then
            for _, b in ipairs(other:buildings()) do buildings[#buildings + 1] = b end
            for _, u in ipairs(other:units()) do units[#units + 1] = u end
        end
    end
    return buildings, units
end

local function nearest(list, from)
    local best, bestDistance
    for _, o in ipairs(list) do
        local d = bfme.map.distance(o, from)
        if not bestDistance or d < bestDistance then best, bestDistance = o, d end
    end
    return best, bestDistance
end

-- ---- economy and base ---------------------------------------------------------------------------------------------

-- build order: economyStart resource buildings, then production and resource buildings in turn (a production building
-- whenever there are at least as many resource buildings as production buildings + 1), up to productionMax / economyMax
local function chooseStructure(c, offered, budget)
    local economyCap = math.min(cfg.economyMax, math.max(cfg.economyStart, c.production + 2))
    local wantProduction = c.economy >= cfg.economyStart and c.production < cfg.productionMax and c.production <= c.economy - 1
    local wantEconomy = not wantProduction and c.economy < economyCap
    if not wantProduction and not wantEconomy and c.production < cfg.productionMax then wantProduction = true end
    local best
    for _, s in ipairs(offered) do
        local name = s.building
        if s.affordable and (s.cost or 0) <= budget and not s.onPlot and (s.isEconomy or s.isFactory) then
            local isEconomy = s.isEconomy
            local have = c.byType[name] or 0
            local wanted = (isEconomy and wantEconomy) or (not isEconomy and wantProduction and have < cfg.perProductionType)
            if wanted and (best == nil or have < best.have) then
                best = { name = name, cost = s.cost, have = have, economy = isEconomy, radius = s.radius or 80 }
            end
        end
    end
    return best
end

-- ---- base layout -------------------------------------------------------------------------------------------------
-- Two blocks of building slots on a grid aligned with the fortress's front (toward the nearest enemy start): production
-- buildings in a block just in front of the fortress, resource buildings in a block just behind it. A block fills from
-- its middle outward in squares (1 slot, then 3x2, then 5x3 ...), so the base grows as two compact clusters rather than
-- a ring. A slot that is blocked (terrain, an existing structure) is skipped; a slot may shift up to snapRadius to the
-- nearest legal site that keeps buildGap to every structure. Buildings face the front.
local function fortressOf(state, c)
    local kept = state.fortress and bfme.buildingInstances[state.fortress]
    if kept then return kept end
    local best -- the player's largest building: the fortress / citadel
    for _, b in ipairs(c.buildings) do
        if not best or (b.radius or 0) > (best.radius or 0) then best = b end
    end
    state.fortress = best and best.id
    return best
end

local function frontOf(p, state, centre)
    if state.front then return state.front end
    local nearestEnemy, distance
    for _, s in ipairs(bfme.map.getStartPositions()) do
        local owner = s.playerId and bfme.players[s.playerId]
        if owner and owner.id ~= p.id and p:isEnemy(owner) then
            local d = bfme.map.distance(s, centre)
            if not distance or d < distance then nearestEnemy, distance = s, d end
        end
    end
    state.front = nearestEnemy and math.atan(nearestEnemy.y - centre.y, nearestEnemy.x - centre.x) or 0
    return state.front
end

-- the slots of a block: rows away from the fortress (0 = next to it), columns across it (0 = on its axis), nearest square first
local blockSlots = {}
for row = 0, cfg.blockDepth - 1 do
    for col = -cfg.blockHalfWidth, cfg.blockHalfWidth do
        blockSlots[#blockSlots + 1] = { row = row, col = col, ring = math.max(row, math.abs(col)), d = row * row + col * col }
    end
end
table.sort(blockSlots, function(a, b)
    if a.ring ~= b.ring then return a.ring < b.ring end
    if a.d ~= b.d then return a.d < b.d end
    return a.col < b.col
end)

-- resource buildings first go on the map's markers for them (FarmTemplate): the nearest free one to the fortress that is
-- not near an enemy fortress or enemies, at the marker's position and angle (the native economy builder's choice)
local function farmSite(p, state, c, builder, choice, centre)
    local sites = {}
    for _, m in ipairs(bfme.map.getFarmSites()) do
        if not m.occupied then
            m.d = bfme.map.distance(m, centre)
            sites[#sites + 1] = m
        end
    end
    if #sites == 0 then return nil end
    table.sort(sites, function(a, b) return a.d < b.d end)
    local enemyBuildings, enemyUnits = state.enemyBuildings or {}, state.enemyUnits or {}
    for _, m in ipairs(sites) do
        local safe = true
        for _, b in ipairs(enemyBuildings) do
            local d = bfme.map.distance(b, m)
            if d < cfg.farmThreatRadius or ((b.radius or 0) >= 100 and d < cfg.farmEnemyFortress) then safe = false break end
        end
        for _, u in ipairs(enemyUnits) do
            if not safe then break end
            if bfme.map.distance(u, m) < cfg.farmThreatRadius then safe = false end
        end
        if safe then
            local site = p:findBuildSite(choice.name, m, cfg.farmSiteSnap, builder, { angle = m.angle, avoid = state.avoid })
            if site then return site, m.angle end
        end
    end
    return nil
end

local function placeStructure(p, state, c, base, builder, choice)
    local fortress = fortressOf(state, c)
    if choice.economy then
        local site, angle = farmSite(p, state, c, builder, choice, fortress and fortress.position or base)
        if site then return site, angle end
    end
    local centre = fortress and fortress.position or base
    local front = frontOf(p, state, centre)
    local direction = choice.economy and front + math.pi or front -- production in front, resources behind
    local fx, fy = math.cos(direction), math.sin(direction)       -- away from the fortress
    local sx, sy = -fy, fx                                       -- across
    local slot = math.max(choice.economy and cfg.economySpacing or cfg.slotSize, 2 * choice.radius + cfg.buildGap)
    local first = (fortress and fortress.radius or 150) + cfg.buildGap + slot / 2
    for _, s in ipairs(blockSlots) do
        local along, across = first + s.row * slot, s.col * slot
        local spot = { x = centre.x + fx * along + sx * across, y = centre.y + fy * along + sy * across }
        local site = p:findBuildSite(choice.name, spot, cfg.snapRadius, builder, { clearance = cfg.buildGap, angle = front, avoid = state.avoid })
        if site then return site, front end
    end
    -- the block is full or blocked: anywhere legal near the base
    return p:findBuildSite(choice.name, centre, cfg.buildRadius, builder, { angle = front, avoid = state.avoid }), front
end

-- ---- construction bookkeeping -------------------------------------------------------------------------------------
-- Every site the AI orders is reserved (state.avoid) until its foundation stands, so builders of one think never pick
-- overlapping sites. A construction the engine refuses (the site became illegal, the builder could not start) marks
-- the site blocked for blockedMs and the next think finds another; a foundation that stops growing gets a builder.
local function trackConstruction(state, now)
    state.constructions = state.constructions or {}
    local keep = {}
    for _, job in ipairs(state.constructions) do
        local status = bfme.mutations.getStatus(job.ticket)
        if status == "pending" and now - job.at < cfg.reserveMs then
            keep[#keep + 1] = job
        elseif status == "building" or status == "complete" then
            luaai.stats.placed = luaai.stats.placed + 1
        elseif status ~= "pending" and tostring(status):find("money", 1, true) then
            luaai.stats.unaffordable = (luaai.stats.unaffordable or 0) + 1 -- the site was fine: nothing to avoid
        elseif status ~= "pending" then
            luaai.stats.refused = luaai.stats.refused + 1
            luaai.stats.lastRefusals = luaai.stats.lastRefusals or {}
            table.insert(luaai.stats.lastRefusals, 1, job.name .. ": " .. tostring(status))
            luaai.stats.lastRefusals[6] = nil
            job.blockedUntil = now + cfg.blockedMs
            state.blocked[#state.blocked + 1] = job
            print(string.format("LUAAI %s at (%.0f, %.0f) refused: %s; another site is chosen", job.name, job.x, job.y, tostring(status)))
        end
    end
    state.constructions = keep
    local blocked = {}
    for _, b in ipairs(state.blocked) do
        if now < b.blockedUntil then blocked[#blocked + 1] = b end
    end
    state.blocked = blocked
    -- what findBuildSite keeps free: the reserved sites and the blocked ones
    state.avoid = {}
    for _, job in ipairs(keep) do state.avoid[#state.avoid + 1] = { x = job.x, y = job.y, r = job.r } end
    for _, b in ipairs(blocked) do state.avoid[#state.avoid + 1] = { x = b.x, y = b.y, r = b.r } end
end

local function finishStalled(state, c, now)
    state.progress = state.progress or {}
    for _, b in ipairs(c.buildings) do
        if b.underConstruction then
            local seen = state.progress[b.id]
            if not seen or (b.constructionPercent or 0) > seen.percent then
                state.progress[b.id] = { percent = b.constructionPercent or 0, at = now }
            elseif now - seen.at >= cfg.stalledMs and #c.idleBuilders > 0 then
                local builder = table.remove(c.idleBuilders)
                builder:resumeConstruction(b)
                seen.at = now
            end
        else
            state.progress[b.id] = nil
        end
    end
end

local function build(p, state, c, base)
    local budget = p.money - (state.spent or 0) -- what this think's orders may still spend (the snapshot's gold is not lowered by queued orders)
    for _, builder in ipairs(c.idleBuilders) do
        local choice = chooseStructure(c, builder:getBuildableStructures(), budget)
        if not choice then return end
        local site, facing = placeStructure(p, state, c, base, builder, choice)
        if site then
            local ticket = builder:construct(choice.name, { x = site.x, y = site.y, rotation = facing })
            state.constructions[#state.constructions + 1] = { ticket = ticket, name = choice.name, x = site.x, y = site.y, r = choice.radius, at = bfme.match.getTimeMs() }
            state.avoid[#state.avoid + 1] = { x = site.x, y = site.y, r = choice.radius } -- the next builder of this think keeps clear
            budget = budget - (choice.cost or 0)
            state.spent = (state.spent or 0) + (choice.cost or 0)
            c.byType[choice.name] = (c.byType[choice.name] or 0) + 1 -- one builder per choice this think
            if choice.economy then c.economy = c.economy + 1 end
            state.reserve = choice.cost
        end
    end
end

-- ---- production, research, sciences -------------------------------------------------------------------------------

-- training comes first each think; until two production buildings stand, goldReserve is kept for building them
local function produce(p, state, c)
    local reserve = (#c.idleBuilders > 0 and c.production < 2) and cfg.goldReserve or 0
    for _, b in ipairs(c.producers) do
        if (b.productionCount or 0) == 0 then
            local options = {}
            for _, t in ipairs(b:getTrainableUnits()) do
                local wanted = (t.isBuilder and #c.builders < cfg.builderCount) or (t.isHero and p.money >= cfg.heroGold)
                    or (not t.isBuilder and not t.isHero)
                if t.ok and wanted and p.money - (state.spent or 0) - (t.cost or 0) >= reserve then options[#options + 1] = t end
            end
            if #options > 0 then
                local pick = options[math.random(#options)]
                b:trainUnit(pick.unit, 1)
                state.spent = (state.spent or 0) + (pick.cost or 0)
            end
        end
    end
end

local function research(p, c)
    if p.money < 1500 then return end
    for _, b in ipairs(c.producers) do
        if (b.productionCount or 0) == 0 then
            for _, u in ipairs(b:getUpgrades()) do
                if u.ok and p.money - (u.cost or 0) > 1000 then
                    b:researchUpgrade(u.upgrade)
                    return
                end
            end
        end
    end
end

local function sciences(p)
    local cheapest
    for _, s in ipairs(p:getSciences()) do
        if s.canPurchase and (cheapest == nil or s.cost < cheapest.cost) then cheapest = s end
    end
    if cheapest then p:purchaseScience(cheapest.science) end
end

-- ---- army ---------------------------------------------------------------------------------------------------------

local function command(p, state, c, base)
    local enemyBuildings, enemyUnits = enemyObjects(p)
    -- defend: an enemy near the base draws every idle unit (and the attack, when it is small)
    local intruder, distance = nearest(enemyUnits, base)
    if intruder and distance < cfg.defendRadius then
        -- the whole army once per intruder, then only the units that went idle (re-ordering every second would re-path them all)
        local group = state.defending == intruder.id and c.idleArmy or c.army
        if #group > 0 then bfme.orders.attackMoveTo(group, intruder) end
        state.defending = intruder.id
        state.attacking = false
        return
    end
    state.defending = nil
    if state.attacking then
        if #c.army < math.max(1, math.floor(state.attackSize * cfg.retreatFraction)) then
            if #c.army > 0 then bfme.orders.moveTo(c.army, base) end
            state.attacking = false
        elseif #c.idleArmy > 0 then
            local goal = nearest(enemyBuildings, c.idleArmy[1]) or nearest(enemyUnits, c.idleArmy[1])
            if goal then bfme.orders.attackMoveTo(c.idleArmy, goal) end
        end
        return
    end
    if #c.army >= cfg.attackSize then
        local goal = nearest(enemyBuildings, base) or nearest(enemyUnits, base)
        if goal then
            bfme.orders.attackMoveTo(c.army, goal)
            state.attacking = true
            state.attackSize = #c.army
        end
    end
end

-- ---- the think loop -----------------------------------------------------------------------------------------------

local function think(p, state)
    local c = census(p)
    local base = baseCentre(p, state, c)
    if not base then return end
    local fortress = fortressOf(state, c)
    -- the fortress and its parts (a citadel, expansion pads inside its footprint) are not buildings of the build order
    if fortress then
        for _, b in ipairs(c.buildings) do
            if bfme.map.distance(b, fortress) <= (fortress.radius or 0) then
                if b.isFactory then c.production = c.production - 1 elseif b.isEconomy then c.economy = c.economy - 1 end
            end
        end
    end
    local now = bfme.match.getTimeMs()
    state.blocked = state.blocked or {}
    trackConstruction(state, now)
    finishStalled(state, c, now)
    -- spending order: the base first while it has under two production buildings or the army is big enough to attack,
    -- the army first while it is too small to attack (so the base keeps growing between waves)
    state.enemyBuildings, state.enemyUnits = enemyObjects(p)
    state.spent = 0
    if c.production < 2 or #c.army >= cfg.attackSize then
        build(p, state, c, base)
        produce(p, state, c)
    else
        produce(p, state, c)
        build(p, state, c, base)
    end
    research(p, c)
    sciences(p)
    command(p, state, c, base)
    if now - (state.lastStatus or 0) >= cfg.statusEveryMs then
        state.lastStatus = now
        print(string.format("LUAAI player %d: gold %d, buildings %d (economy %d), builders %d, army %d%s", p.id, p.money, #c.buildings,
            c.economy, #c.builders, #c.army, state.attacking and ", attacking" or ""))
    end
end

function luaai.status()
    local lines = {}
    for id, state in pairs(luaai.players) do
        local p = bfme.players[id]
        if p then
            local c = census(p)
            lines[#lines + 1] = string.format("player %d: gold %d, buildings %d, army %d%s%s", id, p.money, #c.buildings, #c.army,
                state.attacking and ", attacking" or "", state.lastError and (", last error: " .. state.lastError) or "")
        end
    end
    return #lines > 0 and table.concat(lines, "\n") or "no player is controlled by Lua (luaai.enableAll())"
end

-- one think of every controlled player (the update callback below; tests call it directly to step with the simulation)
function luaai.thinkAll()
    local status = bfme.match.getStatus()
    if not status.ready then return end
    -- once per match: the computer players go to the Lua AI (a player handed back by hand stays native until the next match)
    if cfg.autoEnable and luaai.autoGeneration ~= status.generation then
        luaai.autoGeneration = status.generation
        luaai.players = {}
        if not status.networkGame and not status.replay then luaai.enableAll() end
    end
    for id, state in pairs(luaai.players) do
        -- a new match (or a loaded save) has other players behind the same ids: enable them again there
        state.generation = state.generation or status.generation
        if state.generation ~= status.generation then luaai.players[id] = nil; goto continue end
        local p = bfme.players[id]
        if not p or p.defeated then
            luaai.players[id] = nil
        else
            local ok, err = pcall(think, p, state)
            if not ok and err ~= state.lastError then
                print("LUAAI player " .. id .. " error: " .. tostring(err))
            end
            state.lastError = (not ok) and err or nil
        end
        ::continue::
    end
end

bfme.events.onUpdate("luaai", function() luaai.thinkAll() end, cfg.thinkMs)

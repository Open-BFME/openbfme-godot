
-- Concurrent native self-building construction.
-- Fortress dependencies are respected.
-- Independent structures start together; wall pieces wait only for their hubs to begin.

buildingReplay = buildingReplay or {running = false}

local event = 'building-layout-replay'

function stopBuildingReplay()
    bfme.events.remove(event)
    buildingReplay.running = false
    -- Already submitted construction continues normally.
end

local function copy(value)
    if type(value) ~= 'table' then return value end
    local out = {}
    for k, v in pairs(value) do
        out[k] = copy(v)
    end
    return out
end

function replayBuildings(name, options)
    options = options or {}

    assert(not buildingReplay.running,
        'A layout is already being built; call stopBuildingReplay() first')

    if not buildingLayouts[name] then loadBuildings(name) end
    local layout = copy(assert(buildingLayouts[name], 'Unknown layout'))

    local player = options.player
    player = type(player) == 'number'
        and bfme.players[player]
        or player
        or bfme.players.localPlayer

    assert(player, 'Start a match and choose a player')

    local speed = options.speed or 1
    local level3 = options.level3

    assert(type(speed) == 'number' and speed >= .01 and speed <= 100,
        'speed must be 0.01..100')

    local playerId = player.id
    local generation = bfme.match.getStatus().generation

    local job = {
        running = true,
        submitted = 0,
        completed = 0,
        reused = 0,
        failed = 0,
        errors = {},
        pending = {}
    }

    buildingReplay = job

    local byId = {}
    local restored = {}
    local finished = {}
    local states = {}
    local links = {}
    local phase = 'buildings'
    local wallTicket = nil
    local wallLabel = nil

    for i, b in ipairs(layout) do
        if b.id then byId[b.id] = i end

        states[i] = {
            started = false,
            done = false,
            ticket = nil,
            buildingId = nil,
            tasks = nil,
            taskIndex = 1,
            waiting = 0,
            counted = false
        }

        states[i].isWall = false
        local hubName = b.template:lower():find('wallhub', 1, true) ~= nil
        -- Explicit metadata for new recordings; template KindOf supports old layouts.
        states[i].isHub = b.wallHub == true or hubName or false

    end

    -- Linked neighborhoods contain all span pieces, not just immediate neighbors.
    -- Use only hub endpoints as construction dependencies, avoiding segment cycles.
    for i, b in ipairs(layout) do
        states[i].hubs = {}
        if not states[i].isHub then
            for _, id in ipairs(b.links or {}) do
                local hub = byId[id]
                if hub and states[hub].isHub then states[i].hubs[#states[i].hubs+1] = hub end
            end
        end
    end
    local function capacity()
        local count = 0
        for _ in pairs(job.pending) do count = count + 1 end
        return count < 64
    end

    local function failure(label, reason)
        job.failed = job.failed + 1
        local message = tostring(label) .. ': ' .. tostring(reason)
        job.errors[#job.errors + 1] = message
        print(message)
    end

    local function nearby(owner, template, x, y, castle)
        for _, b in ipairs(owner:buildings()) do
            if b.typeId == template
                and math.abs(b.x - x) < 1
                and math.abs(b.y - y) < 1 then

                if not castle or b:getLayoutInfo().castleId == castle then
                    return b
                end
            end
        end
    end

    local function setTicket(state, ticket)
        state.ticket = ticket
        if ticket then
            job.pending[ticket] = true
        end
    end

    local function clearTicket(state)
        if state.ticket then
            job.pending[state.ticket] = nil
            state.ticket = nil
        end
    end

    local function finish(i)
        local state = states[i]
        clearTicket(state)
        state.done = true
        finished[i] = true
    end

    local function completeBuilding(i, building)
        local state = states[i]
        if state.done or not job.running then return end

        restored[i] = building.id
        state.buildingId = building.id
        state.tasks = {}
        state.taskIndex = 1

        for _, u in ipairs(layout[i].upgrades or {}) do
            state.tasks[#state.tasks + 1] = {
                kind = 'upgrade',
                value = u
            }
        end

        if level3 and building.level ~= nil and building.level < 3 then
            state.tasks[#state.tasks + 1] = {
                kind = 'level'
            }
        end

        if not state.counted then
            job.completed = job.completed + 1
            state.counted = true
        end
    end

    local function startBuilding(i, owner)
        local b = layout[i]
        local state = states[i]

        if state.started or state.done then return end

        local parent = byId[b.castleId]
        local keep = byId[b.keepId]

        if keep == i then keep = nil end

        -- Dependencies must finish before this building starts.
        if parent and not finished[parent] then return end
        if keep and not finished[keep] then return end

        for _, hub in ipairs(state.hubs) do
            local h = states[hub]
            if h.done and not restored[hub] then
                failure(b.template, 'Wall hub failed'); finish(i); return
            end
            -- A queued ticket alone is not a started hub: wait for its object.
            if not restored[hub] then return end
        end
        if not capacity() then return end
        state.started = true

        if (parent and not restored[parent])
            or (keep and not restored[keep]) then
            failure(b.template, 'Parent fortress failed')
            finish(i)
            return
        end

        local castle = parent and restored[parent]

        local existing = nearby(
            owner, b.template, b.x, b.y, castle
        )

        if existing then
            if existing.underConstruction then
                restored[i] = existing.id
                state.buildingId = existing.id
                state.reusing = true
                job.reused = job.reused + 1
                state.counted = true
                return
            end

            job.reused = job.reused + 1
            state.counted = true
            completeBuilding(i, existing)
            return
        end

        local position = {
            x = b.x,
            y = b.y,
            rotation = b.rotation or 0
        }

        if b.plot then
            local plot = nearby(
                owner,
                b.plot.template,
                b.plot.x,
                b.plot.y,
                castle
            )

            if not plot then
                failure(b.template, 'Fortress expansion pad not found')
                finish(i)
                return
            end

            position.plotId = plot.id
        end

        job.submitted = job.submitted + 1

        local ok, result = pcall(function()
            return owner:construct(
                b.template,
                position,
                speed,
                function(building)
                    if job.running and not state.done then
                        completeBuilding(i, building)
                    end
                end
            )
        end)

        if ok then
            setTicket(state, result)
        else
            failure(b.template, result)
            finish(i)
        end
    end

    local function updateBuilding(i)
        local b = layout[i]
        local state = states[i]

        if not state.started or state.done then return end

        if state.ticket then
            local status = bfme.mutations.getStatus(state.ticket)

            if status == 'pending' or status == 'building' then
                local building = bfme.mutations.getBuilding(state.ticket)
                if building then restored[i] = building.id; state.buildingId = building.id end
                return
            end

            clearTicket(state)

            if status ~= 'complete' and status ~= 'applied' then
                failure(b.template, status)

                if not state.tasks then
                    finish(i)
                    return
                end
            end
        end

        if state.reusing and not state.tasks then
            local building = bfme.buildingInstances[state.buildingId]
            if not building then
                restored[i] = nil
                failure(b.template, 'Reused building destroyed'); finish(i); return
            end
            if not building.underConstruction then completeBuilding(i, building) end
        end
        -- Construction callback has not fired yet.
        if not state.tasks then return end

        if b.castle then
            local building = bfme.buildingInstances[state.buildingId]
            local info = building and building:getLayoutInfo()

            if not info or not info.castleReady then
                state.waiting = state.waiting + 1

                if state.waiting < 100
                    and info
                    and (not info.castleError
                        or info.castleError == '') then
                    return
                end

                failure(
                    b.template,
                    info and info.castleError ~= ''
                        and info.castleError
                        or 'Fortress did not unpack'
                )

                restored[i] = nil
                finish(i)
                return
            end
        end

        local task = state.tasks[state.taskIndex]

        if not task then
            finish(i)
            return
        end

        if not capacity() then return end
        state.taskIndex = state.taskIndex + 1

        local label = b.template
        if task.kind == 'upgrade' then
            label = label .. ' ' .. tostring(task.value)
        else
            label = label .. ' level 3'
        end

        local ok, result = pcall(function()
            local building = assert(
                bfme.buildingInstances[state.buildingId],
                'Building destroyed'
            )

            if task.kind == 'upgrade' then
                return building:giveUpgrade(task.value)
            end

            return building:set('level', 3)
        end)

        if ok then
            setTicket(state, result)
        else
            failure(label, result)
        end
    end

    local function processBuildings(owner)
        for i in ipairs(states) do updateBuilding(i) end
        -- Submit hubs first so dependent pieces become eligible on the next update.
        for _, hubs in ipairs({true, false}) do
            for i, state in ipairs(states) do
                if state.isHub == hubs then startBuilding(i, owner) end
            end
        end
        local allDone, blocked = true, true
        for _, state in ipairs(states) do
            if not state.done then allDone = false end
            if state.started and not state.done then blocked = false end
        end
        if not allDone and blocked then
            for i, state in ipairs(states) do
                if not state.done then failure(layout[i].template, 'Unresolved construction dependency'); finish(i) end
            end
            return true
        end
        return allDone
    end

    local function prepareLinks()
        for i, b in ipairs(layout) do
            for _, oldId in ipairs(b.links or {}) do
                local targetIndex = byId[oldId]
                local target = targetIndex and restored[targetIndex]

                if restored[i] and target then
                    links[#links + 1] = {
                        restored[i],
                        target
                    }
                end
            end
        end
    end

    bfme.events.onUpdate(event, function()
        if not job.running then return end

        if bfme.match.getStatus().generation ~= generation then
            stopBuildingReplay()
            print('Building replay stopped: match changed')
            return
        end

        local owner = bfme.players[playerId]

        if not owner then
            failure('layout', 'Player no longer exists')
            stopBuildingReplay()
            return
        end

        if phase == 'buildings' then
            if processBuildings(owner) then
                prepareLinks()
                phase = 'links'
            end
            return
        end

        if phase == 'links' then
            if wallTicket then
                local status = bfme.mutations.getStatus(wallTicket)

                if status == 'pending' or status == 'building' then
                    return
                end

                job.pending[wallTicket] = nil
                wallTicket = nil

                if status ~= 'complete' and status ~= 'applied' then
                    failure(wallLabel or 'wall link', status)
                end
            end

            local link = table.remove(links, 1)

            if link then
                local ok, result = pcall(function()
                    local source = assert(
                        bfme.buildingInstances[link[1]],
                        'Wall destroyed'
                    )

                    local target = assert(
                        bfme.buildingInstances[link[2]],
                        'Linked wall destroyed'
                    )

                    return source:linkWallPiece(target)
                end)

                wallLabel = 'wall link'

                if ok then
                    wallTicket = result
                    if result then
                        job.pending[result] = true
                    end
                else
                    failure(wallLabel, result)
                end

                return
            end

            stopBuildingReplay()

            print(
                'Building replay finished: '
                .. job.completed .. ' constructed, '
                .. job.reused .. ' reused, '
                .. job.failed .. ' errors'
            )
        end
    end, 100)

    return #layout
end

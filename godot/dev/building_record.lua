-- Plain layout data survives script reloads and match changes.
buildingLayouts = buildingLayouts or {}

function recordBuildings(name, player)
    assert(type(name) == 'string' and name ~= '', 'Provide a layout name')
    player = type(player) == 'number' and bfme.players[player] or player or bfme.players.localPlayer
    assert(player, 'Start a match and choose a player')
    local layout = {}
    for _, b in ipairs(player:buildings()) do
        local info = b:getLayoutInfo()
        -- Fortresses themselves are BASE_FOUNDATION too; retain their castle root.
        if info.castle or (not b:isKindOf('BASE_FOUNDATION') and not b:isKindOf('BASE_DEFENSE_FOUNDATION') and not b:isKindOf('EXPANSION_PAD')) then
            layout[#layout + 1] = {id=b.id, template=b.typeId, x=b.x, y=b.y, rotation=b.rotation,
                wallHub=b:isKindOf('WALL_HUB') or nil, castle=info.castle or nil, castleId=info.castleId, keepId=info.keepId, plot=info.plot, upgrades=info.upgrades, links=info.links}
        end
    end
    buildingLayouts[name] = layout
    saveBuildings(name)
    print('Recorded ' .. #layout .. ' buildings as ' .. name)
    return #layout
end

-- Export every field, including attachment IDs and upgrade names, as portable Lua.
local function serialize(value)
    if type(value) == 'string' then return string.format('%q', value) end
    if type(value) == 'boolean' then return tostring(value) end
    if type(value) == 'number' then return string.format('%.17g', value) end
    assert(type(value) == 'table', 'Unsupported layout value')
    local fields = {}
    for key, v in pairs(value) do
        fields[#fields + 1] = '[' .. serialize(key) .. ']=' .. serialize(v)
    end
    table.sort(fields)
    return '{' .. table.concat(fields, ',') .. '}'
end
function exportBuildings(name)
    return 'buildingLayouts = buildingLayouts or {}\nbuildingLayouts[' .. string.format('%q', name) .. '] = ' ..
        serialize(assert(buildingLayouts[name], 'Unknown layout'))
end

-- Named files are JSON, never executable Lua. Recording saves automatically.
function saveBuildings(name)
    return bfme.layouts.save(name, assert(buildingLayouts[name], 'Unknown layout'))
end
function loadBuildings(name)
    local data = bfme.layouts.load(name)
    assert(type(data) == 'table', 'Invalid layout')
    for _, b in ipairs(data) do
        assert(type(b) == 'table' and type(b.template) == 'string' and
            type(b.x) == 'number' and type(b.y) == 'number', 'Invalid building in saved layout')
    end
    buildingLayouts[name] = data
    return #data
end
function listBuildingLayouts()
    return bfme.layouts.list()
end

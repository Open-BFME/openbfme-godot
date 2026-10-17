-- Local base layout markers. Run planBase() from the companion Lua console.
basePlans = basePlans or {}
local function distance(a,b) return math.sqrt((a.x-b.x)^2+(a.y-b.y)^2) end
local function number(v,default,lo,hi)
    v=v or default; assert(type(v)=='number' and v==v and v>=lo and v<=hi,'Invalid planner option');return v
end
function planBase(options)
    options=options or {}
    local match=bfme.match.getStatus()
    assert(match.ready and not match.networkGame and not match.replay,'Base planning requires a local match')
    local player=type(options.player)=='number' and bfme.players[options.player] or options.player or bfme.players.localPlayer
    assert(player,'No player')
    local name=options.name or 'base-plan'
    assert(not basePlans[name] or basePlans[name].generation~=match.generation,'This plan already exists in this match; use another name')
    local center=options.center
    if not center then
        for _,b in ipairs(player:buildings()) do
            if b:isKindOf('CASTLE_KEEP') or b:isKindOf('COMMANDCENTER') then center={x=b.x,y=b.y};break end
        end
    end
    center=center or player:getStartPosition();assert(center and center.x and center.y,'No base center; supply center={x=...,y=...}')
    local builder=options.builder
    if not builder then for _,u in ipairs(player:units()) do if u:isKindOf('DOZER') then builder=u;break end end end
    assert(builder or options.allowUnreachable,'A builder is needed to check reachable sites (or set allowUnreachable=true)')
    local ring=number(options.radius,360,200,1200)
    local gap=number(options.clearance,60,20,200)
    local reserve=number(options.siteRadius,110,60,250)
    local plot=options.plot or 'GondorBuildingFoundation_Independant'
    local hub=options.hub or 'MenWallHubSmall'
    assert(bfme.templates[plot] and bfme.templates[hub],'Required plot/hub templates are not mounted')
    local types=options.production or {'GondorBarracks','GondorArcherRange','GondorStable','GondorWorkshop'}
    assert(#types>0 and #types<=12,'Choose 1..12 production buildings')
    local plotCount=number(options.plots,#types,1,32)
    assert(plotCount==math.floor(plotCount),'plots must be a whole number')
    local plan={name=name,generation=match.generation,player=player.id,center={x=center.x,y=center.y},sites={},hubs={},missing={},tickets={},errors={}}
    local avoid={{x=center.x,y=center.y,r=170}}
    local front=number(options.rotation,0,-100,100)
    -- Rectangular blocks around a central courtyard, with a forward lane.
    -- Prefer corners, then fill the sides/rows; rotate the entire layout together.
    local layers=math.max(1,math.ceil((math.sqrt(plotCount+4)-1)/2))
    local spacing=math.max(2*reserve+gap,ring/layers)
    local candidates={}
    for x=-layers,layers do for y=-layers,layers do
        if (x~=0 or y~=0) and not (x>0 and y==0) then
            candidates[#candidates+1]={x=x,y=y,layer=math.max(math.abs(x),math.abs(y)),corner=math.abs(x)==math.abs(y)}
        end
    end end
    table.sort(candidates,function(a,b)
        if a.layer~=b.layer then return a.layer<b.layer end
        if a.corner~=b.corner then return a.corner end
        if a.y~=b.y then return a.y<b.y end;return a.x<b.x
    end)
    for _,cell in ipairs(candidates) do
        if #plan.sites>=plotCount then break end
        local kind=types[#plan.sites%#types+1]
        assert(bfme.templates[kind],'Unknown production template: '..kind)
        local x,y=cell.x*spacing,cell.y*spacing
        local near={x=center.x+x*math.cos(front)-y*math.sin(front),y=center.y+x*math.sin(front)+y*math.cos(front)}
        local site=player:findBuildSite(kind,near,120,builder,{clearance=gap,angle=front,avoid=avoid})
        if site and site.x and player:canBuildAt(plot,site,nil,front) then
            plan.sites[#plan.sites+1]={template=kind,marker=plot,x=site.x,y=site.y,rotation=front}
            avoid[#avoid+1]={x=site.x,y=site.y,r=reserve+gap}
        end
    end
    for i=#plan.sites+1,plotCount do plan.missing[#plan.missing+1]=types[(i-1)%#types+1] end
    local halfWidth=200
    for _,site in ipairs(plan.sites)do halfWidth=math.max(halfWidth,math.abs(site.x-center.x)+reserve+gap,math.abs(site.y-center.y)+reserve+gap)end
    local survey=number(options.defenseRadius,math.min(1900,halfWidth+640),halfWidth+160,3000)
    local grid=player:getDefenseGrid(center,survey,80)
    assert(planBaseDefenses,'Reload scripts to load base_defense.lua')
    local defense=planBaseDefenses(grid,center,halfWidth,function(c)
        return player:canBuildAt(hub,c,builder,0)
    end)
    plan.defenses=defense
    if defense.error then plan.missing[#plan.missing+1]=defense.error end
    local hubSpacing=number(options.hubSpacing,120,80,400)
    -- Mark closures with hubs, keeping naturally blocked cells free of markers.
    for _,line in ipairs(defense.lines)do
        local marked={}
        if #line==1 then
            -- A one-cell choke still needs a pair of endpoints, not one hub.
            local point=line[1];local choices={}
            for _,axis in ipairs({{1,0},{0,1}})do
                local pair={}
                for _,sign in ipairs({-1,1})do
                    local p={x=point.x+axis[1]*sign*defense.step*.5,y=point.y+axis[2]*sign*defense.step*.5}
                    if player:canBuildAt(hub,p,builder,0)then pair[#pair+1]=p end
                end
                if #pair==2 then choices=pair;break end
            end
            for _,point in ipairs(choices)do plan.hubs[#plan.hubs+1]={template=hub,x=point.x,y=point.y,rotation=front};marked[#marked+1]=point end
        end
        for _,point in ipairs(line)do
            local nearby=false
            for _,p in ipairs(marked)do if distance(p,point)<hubSpacing then nearby=true;break end end
            if not nearby then
                plan.hubs[#plan.hubs+1]={template=hub,x=point.x,y=point.y,rotation=front}
                marked[#marked+1]=point
            end
        end
        if #marked==1 and #line>1 then
            local far=line[1]
            for _,p in ipairs(line)do if distance(marked[1],p)>distance(marked[1],far)then far=p end end
            plan.hubs[#plan.hubs+1]={template=hub,x=far.x,y=far.y,rotation=front}
        end
    end
    assert(#plan.sites>0,'No suitable production sites found; try a different center or radius')
    -- Store the intended buildings as data; only marker templates are constructed.
    bfme.layouts.save(name,plan)
    basePlans[name]=plan
    if not options.dryRun then
        for _,list in ipairs({plan.sites,plan.hubs}) do
            for _,site in ipairs(list) do
                local ok,ticket=pcall(function() return bfme.match.placePlanningMarker(site.marker or site.template,{x=site.x,y=site.y,rotation=site.rotation},player.id) end)
                if ok then plan.tickets[#plan.tickets+1]=ticket else plan.errors[#plan.errors+1]=tostring(ticket) end
            end
        end
    end
    print('Base plan '..name..': '..#plan.sites..' production plots, '..#plan.hubs..' wall hubs, '..#plan.missing..' unavailable sites. No wall spans built.')
    return plan
end

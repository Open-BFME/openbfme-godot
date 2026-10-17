extends SceneTree
func _initialize():
 var lua=ClassDB.instantiate('LuaState');lua.open_libraries()
 var code="""
local calls={};local saved={};local searches=0
local builder={isKindOf=function(_,k)return k=='DOZER'end}
local p={id=1,buildings=function()return {}end,units=function()return {builder}end,getStartPosition=function()return {x=1000,y=1000}end}
function p:findBuildSite(kind,near,radius,b,opts)
 assert(b==builder and opts.clearance>0 and opts.avoid);searches=searches+1
 return {x=near.x,y=near.y}
end
function p:getDefenseGrid(center,radius,step)
 local n=math.ceil(radius/step);local cells={}
 for x=-n,n do for y=-n,n do cells[#cells+1]={x=center.x+x*step,y=center.y+y*step,gx=x,gy=y,open=not(center.x+x*step>1300 and center.y+y*step>1000)}end end
 return {cells=cells,n=n,step=step}
end
function p:canBuildAt(kind,pos) if kind=='MenWallHubSmall' and pos.x>1300 and pos.y>1000 then return false end;return true end
function p:construct(kind,pos,speed)
 assert(kind=='GondorBuildingFoundation_Independant' or kind=='MenWallHubSmall','Only markers should be placed')
 calls[#calls+1]={kind=kind,pos=pos};return #calls
end
bfme={players={localPlayer=p},templates=setmetatable({},{__index=function()return {}end}),match={getStatus=function()return {ready=true,generation=1}end,placePlanningMarker=function(kind,pos,player)assert(player==1);return p:construct(kind,pos,100)end},layouts={save=function(n,v)saved[n]=v end}}
"""+FileAccess.get_file_as_string('res://dev/base_defense.lua')+FileAccess.get_file_as_string('res://dev/base_planner.lua')+"""
local corridor={n=7,step=80,cells={}}
for x=-7,7 do for y=-7,7 do
 corridor.cells[#corridor.cells+1]={gx=x,gy=y,x=x*80,y=y*80,open=x>=-3 and math.abs(y)<=(x==2 and 0 or 1)}
end end
local closure=planBaseDefenses(corridor,{x=0,y=0},240,function()return true end)
assert(#closure.cells==3,'Defend wider area outside protected base rather than the smallest choke')
for _,c in ipairs(closure.cells)do assert(c.gx>3,'Never cut through reserved base space')end
-- Closed survey edges must not hide threats on open ground inside them.
local enclosed={n=10,step=80,cells={}}
for x=-10,10 do for y=-10,10 do enclosed.cells[#enclosed.cells+1]={gx=x,gy=y,x=x*80,y=y*80,open=math.abs(x)<10 and math.abs(y)<10}end end
local enclosedDefense=planBaseDefenses(enclosed,{x=0,y=0},240,function()return true end)
assert(#enclosedDefense.cells>8,'Distant ground inside closed map boundaries must still count as threats')
local plan=planBase()
assert(#plan.sites==4 and #plan.hubs>0 and #plan.hubs<=128)
for _,h in ipairs(plan.hubs)do assert(not(h.x>1300 and h.y>1000),'Hubs must follow terrain boundary')end
local min,max=math.huge,0
for _,h in ipairs(plan.hubs)do local d=(h.x-1000)^2+(h.y-1000)^2;min=math.min(min,d);max=math.max(max,d)end
assert(max-min>10000,'Terrain perimeter must not be a fixed radius ring')
assert(#plan.defenses.lines>0,'Planner returns defensive closures')
for _,site in ipairs(plan.sites)do assert(math.abs(site.x-1000)==360 and math.abs(site.y-1000)==360,'Default plots occupy rectangular corners')end
assert(#calls==#plan.sites+#plan.hubs and saved['base-plan']==plan)
for i,a in ipairs(plan.sites) do for j,b in ipairs(plan.sites) do if i~=j then assert((a.x-b.x)^2+(a.y-b.y)^2>100^2)end end end
local before=#calls
local dry=planBase({name='dry',dryRun=true})
assert(#calls==before and #dry.sites==4 and saved.dry)
local twelve=planBase({name='twelve',plots=12,radius=380,dryRun=true});assert(#twelve.sites==12 and not twelve.defenses.error)
local smaller=planBase({name='smaller',plots=12,radius=280,dryRun=true});assert(#smaller.sites==12 and not smaller.defenses.error)
local six=planBase({name='six',plots=6,dryRun=true});assert(#six.sites==6)
local one=planBase({name='one',plots=1,dryRun=true});assert(#one.sites==1)
assert(not pcall(function()planBase({name='bad',plots=2.5})end))
assert(not pcall(function()planBase()end),'Duplicate planning must not place overlapping markers')
"""
 var result=lua.do_string(code)
 if result is LuaError:push_error(str(result));quit(1);return
 print('BASE PLANNER PASS');quit(0)

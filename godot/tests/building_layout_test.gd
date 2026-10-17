extends SceneTree

func _initialize() -> void:
	var lua = ClassDB.instantiate("LuaState")
	lua.open_libraries()
	var setup = """
local started = {}
local live, statuses, callbacks, calls, actions = {}, {}, {}, {}, {}
local nextId, nextTicket, generation = 100, 0, 1
local function make(id,template,x,info,kind)
 local b={id=id,typeId=template,x=x,y=20,rotation=1.2,level=1,info=info or {upgrades={},links={}}}
 function b:isKindOf(k) return kind==k end
 function b:getLayoutInfo() return self.info end
 function b:giveUpgrade(u)
  nextTicket=nextTicket+1; statuses[nextTicket]='applied'; actions[#actions+1]=u; return nextTicket
 end
 function b:set(field,value) assert(field=='level' and value==3); self.level=3; return self:giveUpgrade('level3') end
 function b:linkWallPiece(other) return self:giveUpgrade('link:'..other.typeId) end
 return b
end
local player={id=1,buildings=function() local a={}; for _,b in pairs(live) do a[#a+1]=b end; table.sort(a,function(a,b)return a.id<b.id end); return a end}
function player:construct(template,pos,speed,cb)
 nextTicket=nextTicket+1; local t=nextTicket
 calls[#calls+1]={template=template,x=pos.x,y=pos.y,rotation=pos.rotation,speed=speed,plotId=pos.plotId}
 statuses[t]='building'; callbacks[t]=function()
  nextId=nextId+1; local b=make(nextId,template,pos.x)
  live[b.id]=b; cb(b)
 end
 return t
end
local saved={}
bfme={layouts={save=function(k,v) saved[k]=v; return k end,load=function(k)return saved[k]end,list=function()return {'test'}end},players={[1]=player,localPlayer=player},buildingInstances=live,
 match={getStatus=function() return {generation=generation} end},
 mutations={getStatus=function(id) return statuses[id] end,getBuilding=function(id) return started[id] end},
 events={onUpdate=function(_,fn) tick=fn end,remove=function() tick=nil end}}
local function pump()
 for n=1,200 do
  for t,cb in pairs(callbacks) do callbacks[t]=nil; statuses[t]='complete'; cb() end
  if not tick then return end; tick()
 end
 error('Replay did not finish')
end
function checkReplay()
 live[1]=make(1,'Fortress',100,{castle=true,castleReady=true,upgrades={},links={}},'BASE_FOUNDATION')
 live[2]=make(2,'Keep',100,{castleId=1,upgrades={'Stonework','Banners'},links={}})
 live[3]=make(3,'Pad',130,{castleId=1,upgrades={},links={}},'EXPANSION_PAD')
 live[4]=make(4,'Expansion',130,{castleId=1,keepId=2,plot={template='Pad',x=130,y=20},upgrades={'Armor'},links={}})
 live[5]=make(5,'Wall',200,{upgrades={},links={6}})
 live[6]=make(6,'ArrowTower',220,{upgrades={'Fire'},links={5}})
 live[7]=make(7,'Gate',240,{upgrades={},links={5}})
 live[8]=make(8,'Postern',260,{upgrades={},links={5}})
 assert(recordBuildings('test')==7)
 local exported=exportBuildings('test'); buildingLayouts={}; assert(loadstring(exported))()
 assert(buildingLayouts.test[1].rotation==1.2)
 buildingLayouts.test=nil; assert(loadBuildings('test')==7); assert(listBuildingLayouts()[1]=='test')
 -- The new fortress will regenerate its keep/pad. Existing matching root is reused.
 live[4]=nil; for i=5,8 do live[i]=nil end
 assert(replayBuildings('test',{level3=true,speed=2})==7)
 pump()
 assert(not buildingReplay.running and buildingReplay.failed==0)
 assert(buildingReplay.reused==2 and buildingReplay.completed==5)
 local expansion; for _,c in ipairs(calls) do if c.template=='Expansion' then expansion=c end end
 assert(expansion and expansion.plotId==3 and expansion.speed==2)
 local found={}; for _,a in ipairs(actions) do found[a]=true end
 assert(found.Stonework and found.Banners and found.Armor and found.Fire and found.level3)
 assert(found['link:Wall'] and found['link:ArrowTower'])
 -- Independent buildings submit together; walls wait for a live hub, not completion.
 buildingLayouts.concurrent={
  {id=10,template='FarmA',x=500,y=20},
  {id=11,template='FarmB',x=600,y=20},
  {id=12,template='Hub',wallHub=true,x=700,y=20},
  {id=13,template='Tower',x=720,y=20,links={12}},
  {id=14,template='Wall',x=740,y=20,links={12}}
 }
 local before=#calls
 replayBuildings('concurrent'); tick()
 assert(#calls==before+3, 'Independent buildings and hub should submit together')
 local hubTicket
 for t in pairs(callbacks) do if calls[before+1].template=='Hub' then hubTicket=math.min(hubTicket or t,t) end end
 -- All mutations are still pending: no dependent wall may submit.
 tick(); assert(#calls==before+3)
 started[hubTicket]=make(900,'Hub',700); statuses[hubTicket]='building'
 tick(); assert(#calls==before+5, 'Walls must start while their hub is still building')
 stopBuildingReplay(); callbacks={}; started={}
 -- Match changes cancel scheduled work.

 replayBuildings('test'); generation=2; tick(); assert(not buildingReplay.running)
 replayBuildings('test'); stopBuildingReplay(); assert(tick==nil)
 return true
end
"""
	var chunks = [setup, FileAccess.get_file_as_string("res://dev/building_record.lua"), FileAccess.get_file_as_string("res://dev/building_replay.lua"), "return checkReplay()"]
	for chunk in chunks:
		var result = lua.do_string(chunk)
		if result is Object and result.get_class() == "LuaError":
			push_error(str(result))
			quit(1)
			return
	print("BUILDING LAYOUT TEST PASS")
	quit(0)

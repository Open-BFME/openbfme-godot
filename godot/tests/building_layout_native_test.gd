extends SceneTree
var world: Node
var failures := 0
func _initialize(): call_deferred('_run')
func check_lua(code: String) -> String:
 var result=world.execute_lua(code)
 if not result.ok:
  push_error(str(result)); failures += 1
 return str(result.output).strip_edges()
func frames(n: int):
 world.update_lua()
 for i in n: world.advance(0.2)
 world.update_lua()
func _run():
 var fs=ClassDB.instantiate('RetailFileSystem')
 if not fs.mount_retail().ok: quit(77); return
 world=ClassDB.instantiate('GameWorld'); root.add_child(world)
 if not world.setup(fs).ok: quit(1); return
 var slots=[]
 for i in 8: slots.append({'state':1,'name':'Closed','accepted':true,'color':-1,'start_pos':-1,'player_template':-1,'team':-1})
 slots[0]={'state':6,'name':'Human','accepted':true,'color':-1,'start_pos':-1,'faction':'FactionMen','player_template':-1,'team':0}
 slots[1]={'state':3,'name':'AI','accepted':true,'color':-1,'start_pos':-1,'faction':'FactionMen','player_template':-1,'team':1}
 var prep=world.prepare_new_game({'mode':'skirmish','difficulty':1,'rank_points':0,'map':'maps/map wor buckland/map wor buckland.map','map_crc':0,'map_size':0,'map_mask':0,'seed':42,'starting_cash':100000,'slots':slots})
 var loaded=world.start_new_game({}) if prep.ok else prep
 if not loaded.ok: push_error(str(loaded)); quit(1); return
 check_lua("bfme.console.reload(); bfme.players.localPlayer:setMoney(1000000)")
 frames(1)
 check_lua("local p=bfme.players.localPlayer; local site=assert(p:findBuildSite('MenFortress',{x=800,y=800},1200)); fortress=p:construct('MenFortress',site,100)")
 frames(100)
 check_lua("assert(bfme.mutations.getStatus(fortress)=='complete',bfme.mutations.getStatus(fortress))")
 # Use real keep upgrades and the native fortress foundation path.
 check_lua("local p=bfme.players.localPlayer; for _,b in ipairs(p:buildings()) do if b:isKindOf('CASTLE_KEEP') then keep=b.id end; if b:isKindOf('EXPANSION_PAD') then pad=b.id end end; assert(keep and pad, 'Missing keep/pad'); upgrade=bfme.buildingInstances[keep]:giveUpgrade('Upgrade_MenFortressNumenorStonework')")
 frames(1)
 check_lua("assert(bfme.mutations.getStatus(upgrade)=='applied'); local found=false; for _,u in ipairs(bfme.buildingInstances[keep]:getLayoutInfo().upgrades) do if u=='Upgrade_MenFortressNumenorStonework' then found=true end end; assert(found); local b=bfme.buildingInstances[pad]; expansion=bfme.players.localPlayer:construct('MenArrowTowerExpansion',{x=b.x,y=b.y,plotId=pad},100)")
 frames(100)
 check_lua("assert(bfme.mutations.getStatus(expansion)=='complete',bfme.mutations.getStatus(expansion)); local b=assert(bfme.mutations.getBuilding(expansion)); local info=b:getLayoutInfo(); assert(info.plot and info.castleId); recordBuildings('native'); local root,exp=false,false; for _,e in ipairs(buildingLayouts.native) do if e.template=='MenFortress' then root=true end; if e.template=='MenArrowTowerExpansion' then assert(e.plot); exp=true end end; assert(root and exp)")
 # Direct final wall templates must accept their intentional shared edges.
 check_lua("local p=bfme.players.localPlayer; site=assert(p:findBuildSite('MenWallSegmentSmall',{x=1800,y=1800},1200)); wall=p:construct('MenWallSegmentSmall',site,100)")
 frames(50)
 check_lua("assert(bfme.mutations.getStatus(wall)=='complete',bfme.mutations.getStatus(wall)); local b=bfme.mutations.getBuilding(wall); tower=bfme.players.localPlayer:construct('MenWallTowerSmall',{x=b.x,y=b.y+18,rotation=b.rotation},100)")
 frames(50)
 check_lua("assert(bfme.mutations.getStatus(tower)=='complete',bfme.mutations.getStatus(tower)); local a,b=bfme.mutations.getBuilding(wall),bfme.mutations.getBuilding(tower); link=a:linkWallPiece(b)")
 frames(1)
 check_lua("assert(bfme.mutations.getStatus(link)=='applied'); assert(#bfme.mutations.getBuilding(wall):getLayoutInfo().links>0)")
 check_lua("testPlayer=bfme.players.localPlayer.id; recordBuildings('roundtrip'); for _,b in ipairs(bfme.players.localPlayer:buildings()) do b:destroy() end")
 frames(2)
 check_lua("bfme.players[testPlayer]:setMoney(1000000)")
 frames(1)
 check_lua("replayBuildings('roundtrip',{player=testPlayer,speed=100})")
 for i in 200:
  frames(10)
  await create_timer(0.11).timeout
  if check_lua("return buildingReplay.running") == 'false': break
 print(check_lua("print('PLAYER',testPlayer,bfme.players[testPlayer].faction); for _,e in ipairs(buildingLayouts.roundtrip) do print('SOURCE',e.id,e.template,e.castle,e.castleId,e.keepId,e.plot and e.plot.x,e.plot and e.plot.y) end; for _,b in ipairs(bfme.players[testPlayer]:buildings()) do local i=b:getLayoutInfo(); print('LIVE',b.id,b.typeId,i.castleId,i.keepId,b.x,b.y,i.castleReady,i.castleError) end"))
 check_lua("assert(not buildingReplay.running); assert(buildingReplay.failed==0,table.concat(buildingReplay.errors,'; '))")
 print('BUILDING LAYOUT NATIVE TEST ', 'PASS' if failures==0 else 'FAIL')
 world.free(); quit(0 if failures==0 else 1)

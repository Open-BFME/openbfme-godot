extends SceneTree
var world: Node
var failures := 0
func _initialize(): call_deferred('_run')
func lua(code: String) -> String:
 var result=world.execute_lua(code)
 if not result.ok: failures+=1; push_error(str(result))
 return str(result.output).strip_edges()
func _run():
 var fs=ClassDB.instantiate('RetailFileSystem')
 if not fs.mount_retail().ok: quit(77); return
 world=ClassDB.instantiate('GameWorld');root.add_child(world)
 if not world.setup(fs).ok: quit(1);return
 if not world.load_map('map wor buckland',{}).ok: quit(1);return
 lua("site=assert(bfme.players.localPlayer:findBuildSite('GondorFarm',{x=1800,y=1800},1200)); bfme.players.localPlayer:setMoney(1000000)")
 world.update_lua()
 var xy=lua('return site.x,site.y').split('\t')
 if xy.size()!=2: quit(1);return
 var scenery=world.create_object('DaleBuilding04',0,float(xy[0]),float(xy[1]),0)
 if scenery<0: quit(1);return
 for i in 5: world.advance(0.2)
 lua("local ok=bfme.players.localPlayer:canBuildAt('GondorFarm',site); assert(not ok); bfme.match.setClearNeutralDecorations(true)")
 world.update_lua()
 lua("local ok,reason=bfme.players.localPlayer:canBuildAt('GondorFarm',site); assert(ok,reason); assert(bfme.buildingInstances[%d]); ticket=bfme.players.localPlayer:construct('GondorFarm',site,100)" % scenery)
 world.update_lua()
 lua("assert(bfme.mutations.getStatus(ticket)=='building',bfme.mutations.getStatus(ticket))")
 for i in 5: world.advance(0.2)
 lua("assert(not bfme.buildingInstances[%d], 'Dale house was not cleared')" % scenery)
 print('NEUTRAL DECORATIONS RETAIL ', 'PASS' if failures==0 else 'FAIL')
 world.free();quit(0 if failures==0 else 1)

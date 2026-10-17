extends SceneTree
func _initialize():call_deferred('_run')
func _run():
 var fs=ClassDB.instantiate('RetailFileSystem')
 if not fs.mount_retail().ok:quit(77);return
 var world=ClassDB.instantiate('GameWorld');root.add_child(world)
 if not world.setup(fs).ok or not world.load_map('map wor buckland',{'terrain':false}).ok:quit(1);return
 var survey=world.execute_lua("local g=bfme.players.localPlayer:getDefenseGrid({x=1700,y=1700},400,80); assert(g.n==5 and #g.cells==121); assert(type(g.cells[1].open)=='boolean'); local open=0; for _,c in ipairs(g.cells)do if c.open then open=open+1 end end;assert(open>0,'Retail ground survey must contain traversable cells')")
 assert(survey.ok,str(survey))
 var result=world.execute_lua("return bfme.match.placePlanningMarker('GondorBuildingFoundation_Independant',{x=1700,y=1700},1)")
 assert(result.ok,str(result))
 world.update_lua()
 for i in 20:world.advance(.1)
 var found=false
 for id in world.get_object_ids():
  var obj=world.get_object(id)
  if obj.get('ok',false) and obj.template=='GondorBuildingFoundation_Independant':found=true
 assert(found,'Planning plot must exist without construction behaviors')
 world.free();print('BASE PLANNER MARKER PASS');quit(0)

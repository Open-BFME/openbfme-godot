extends SceneTree
func _initialize():
 var world=ClassDB.instantiate('GameWorld'); root.add_child(world)
 var result=world.execute_lua("local data={{template='MenWallTowerSmall',x=12.5,y=42,rotation=1.25,castle=true,upgrades={'Stonework'},plot={template='Pad',x=1,y=2},links={5,6}}}; return bfme.layouts.save('storage-test',data)")
 if not result.ok: push_error(str(result)); quit(1); return
 world.free()
 # A fresh GameWorld/VM proves loading does not depend on session globals.
 world=ClassDB.instantiate('GameWorld'); root.add_child(world)
 result=world.execute_lua("local a=bfme.layouts.load('storage-test'); assert(#a==1 and a[1].x==12.5 and a[1].castle==true); assert(a[1].upgrades[1]=='Stonework' and a[1].links[2]==6 and a[1].plot.template=='Pad'); local found=false; for _,n in ipairs(bfme.layouts.list()) do if n=='storage-test' then found=true end end; assert(found); local ok=pcall(function() bfme.layouts.save('../escape',{}) end); assert(not ok); bfme.layouts.save('storage-test',{sites={{template='GondorBarracks',x=1,y=2}},defenses={lines={{{x=10,y=20},{x=30,y=40}}},cells={{gx=1,gy=2,x=10,y=20}},step=80}}); local p=bfme.layouts.load('storage-test'); assert(p.defenses.lines[1][2].x==30 and p.defenses.cells[1].gy==2); bfme.layouts.save('storage-test',{}); assert(#bfme.layouts.load('storage-test')==0)")
 if not result.ok: push_error(str(result)); quit(1); return
 DirAccess.remove_absolute('user://building_layouts/storage-test.json')
 world.free(); print('BUILDING LAYOUT STORAGE PASS'); quit(0)

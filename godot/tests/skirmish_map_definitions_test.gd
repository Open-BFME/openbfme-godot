extends SceneTree
func _initialize():call_deferred('_run')
func _run():
 var fs=ClassDB.instantiate('RetailFileSystem');assert(fs.mount_retail().ok)
 var definitions=load('res://scripts/skirmish_map_definitions.gd').new()
 definitions.setup(fs,'/tmp/openbfme-definition-test-'+str(Time.get_ticks_usec()))
 for name in ['map wor buckland','map wor minas tirith']:
  var entry={'key':'maps/%s/%s.map'%[name,name]}
  assert(definitions.cached(entry).is_empty())
  var result=definitions.generate(entry)
  print('DEFINITION ',name,': ',result.get('counts',{}),' fortress=',result.get('is_fortress',false))
  assert(result.get('ok',false),'Map definition generated')
  assert(result.is_fortress==('minas tirith' in name),'Fortress detected from waypoint type')
  var saved=definitions.cached(entry)
  assert(not saved.is_empty(),'Generated definition saved and reused')
  for category in result.counts:assert(int(saved.counts[category])==int(result.counts[category]))
  if 'buckland' in name:
   assert(int(result.counts.outpost)==1 and int(result.counts.signal_fire)==2,'Buckland capturables')
  else:
   assert(int(result.counts.outpost)==3 and int(result.counts.inn)==1 and int(result.counts.signal_fire)==2,'Minas Tirith capturables')
  assert(int(result.counts.other)==0,'Capture flags do not double-count buildings')
 assert(definitions.generated==2 and definitions.cache_hits==2)
 # The browser scans with its preview builder's world (read only): same result, no second template load.
 var shared=ClassDB.instantiate('MapObjectBuilder');assert(shared.setup(fs).ok)
 var fresh=load('res://scripts/skirmish_map_definitions.gd').new();fresh.setup(fs,'/tmp/openbfme-definition-test-shared-'+str(Time.get_ticks_usec()))
 var tirith=fresh.generate({'key':'maps/map wor minas tirith/map wor minas tirith.map'},shared)
 assert(tirith.ok and tirith.is_fortress and int(tirith.counts.outpost)==3 and fresh.scanner==null,'Shared-builder scan')
 print('MAP DEFINITIONS PASS');quit(0)

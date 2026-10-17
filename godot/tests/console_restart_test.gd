extends SceneTree
func _initialize(): call_deferred('_run')
func _run():
 var script=GDScript.new()
 script.source_code="extends 'res://scripts/game.gd'\nvar resumed: Dictionary = {}\nfunc _begin_game(message: Dictionary) -> void:\n resumed=message\n"
 assert(script.reload()==OK)
 var game=script.new()
 var message={'mode':'skirmish','map':'maps/test/test.map','seed':42,'starting_cash':2000,'slots':[{'name':'Player','state':6,'create_a_hero':PackedByteArray([1,2,3])}]}
 var config=ConfigFile.new()
 config.set_value('restart','skirmish',message)
 assert(config.save(game.CONSOLE_RESTART_FILE)==OK)
 assert(game._resume_console_restart())
 assert(game.resumed==message)
 assert(not FileAccess.file_exists(game.CONSOLE_RESTART_FILE))
 assert(not game._resume_console_restart())
 config.set_value('restart','skirmish',{})
 assert(config.save(game.CONSOLE_RESTART_FILE)==OK)
 assert(not game._resume_console_restart())
 assert(not FileAccess.file_exists(game.CONSOLE_RESTART_FILE))
 game.free()
 print('CONSOLE RESTART PASS');quit(0)

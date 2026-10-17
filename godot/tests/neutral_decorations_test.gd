extends SceneTree
func _initialize():
 var lua=ClassDB.instantiate('LuaState'); lua.open_libraries()
 var setup="""
local status={ready=false,generation=1}
calls={}
bfme={match={getStatus=function() return status end,setClearNeutralDecorations=function(value) calls[#calls+1]=value end},events={onUpdate=function(_,fn)tick=fn end}}
function verify()
 tick(); assert(#calls==0)
 status.ready=true; tick(); tick(); assert(#calls==1 and calls[1]==true)
 neutralDecorations.enable(false); tick(); assert(#calls==2 and calls[2]==false)
 status.generation=2; tick(); assert(#calls==3 and calls[3]==false)
 neutralDecorations.enable(true); status.networkGame=true; tick(); assert(#calls==3)
 status.networkGame=false; status.replay=true; tick(); assert(#calls==3)
 status.replay=false; tick(); assert(#calls==4 and calls[4]==true)
end
"""
 for chunk in [setup,FileAccess.get_file_as_string('res://dev/neutral_decorations.lua'),'verify()']:
  var result=lua.do_string(chunk)
  if result is Object and result.get_class()=='LuaError': push_error(str(result)); quit(1); return
 print('NEUTRAL DECORATIONS SCRIPT PASS'); quit(0)

-- OpenBFME entry point corresponding to BFME2Lua/dev/controller.lua.
-- Engine actions stay in the Godot host; bindings and UI state reload in Lua.
controller={
    config=require('lib.controller.config'),
    controls=require('lib.controller.controls'),
    ui=require('lib.controller.palantir'),
}
function controller.action(button, inMatch, palette)
    return controller.controls.action(button,inMatch and (palette and 'palantir' or 'game') or 'menu')
end

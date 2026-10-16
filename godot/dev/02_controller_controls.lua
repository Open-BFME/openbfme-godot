-- Context dispatcher adapted from BFME2Lua's controller.lua / lib/controller/controls.lua.
package.loaded['lib.controller.controls'] = nil
package.preload['lib.controller.controls'] = function()
    local config=require('lib.controller.config')
    local keys=require('lib.keybinds')
    return {action=function(button, context)
        if not config.enabled then return '' end
        return (keys.controller[context] or {})[button] or ''
    end}
end

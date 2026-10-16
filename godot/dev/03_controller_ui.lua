-- Data model for BFME2Lua's four Palantir shortcuts and paged bottom command row.
package.loaded['lib.controller.palantir'] = nil
package.preload['lib.controller.palantir'] = function()
    local keys=require('lib.keybinds')
    local M={page=1, selected=1, shortcuts=keys.palantirShortcuts, layout=keys.shortcutBar}
    function M.commands(commands, pageSize)
        pageSize=pageSize or 4
        M.page=math.max(1,math.min(M.page,math.max(1,math.ceil(#commands/pageSize))))
        local result={}
        for i=(M.page-1)*pageSize+1,math.min(#commands,M.page*pageSize) do result[#result+1]=commands[i] end
        return result
    end
    function M.stepShortcut(delta)
        M.selected=(M.selected-1+delta)%#M.shortcuts+1
        return M.shortcuts[M.selected].action
    end
    return M
end

-- Navigation state lives in Lua so console.reload() also resets controller UI state.
local ui=require('lib.controller.palantir')
ui.command=1
ui.shortcut=1
ui.focus='commands'
ui.section='commands'
function ui.navigate(action,count)
    count=math.max(0,math.floor(count or 0))
    if action=='previousCommand' or action=='nextCommand' then
        ui.focus='commands'
        if count>0 then ui.command=(ui.command-1+(action=='nextCommand' and 1 or -1))%count+1 end
    elseif action=='shortcutUp' then
        if ui.focus~='shortcuts' then ui.focus='shortcuts'; ui.shortcut=1
        else ui.shortcut=math.min(#ui.shortcuts,ui.shortcut+1) end
    elseif action=='shortcutDown' and ui.focus=='shortcuts' then
        if ui.shortcut==1 then ui.focus='commands'
        else ui.shortcut=ui.shortcut-1 end
    elseif action=='pressCommand' and ui.focus=='shortcuts' then
        ui.section=ui.shortcuts[ui.shortcut].action
        ui.focus='commands'
        ui.command=1
    elseif action=='backPalette' then
        ui.section='commands'; ui.focus='commands'; ui.command=1
    end
    ui.command=math.max(1,math.min(ui.command,math.max(1,count)))
    ui.page=math.floor((ui.command-1)/4)+1
    return ui.section,ui.focus,ui.command,ui.shortcut,ui.page
end

-- Adapted from BFME2Lua/dev/lib/controller/config.lua for OpenBFME's Godot input host.
package.loaded['lib.controller.config'] = nil
package.preload['lib.controller.config'] = function()
    return {enabled=true, centerCursor=true, deadzone=0.25, cursorSpeed=900, selectionAssistRadius=48, placementGridSpacing=50,
        cameraPanSpeed=1200, cameraRotationSpeed=1.5, cameraZoomSpeed=250, menuRepeatDelay=300, menuRepeatRate=150}
end

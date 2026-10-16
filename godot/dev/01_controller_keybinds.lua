-- BFME2Lua's context-based binding structure; actions target OpenBFME's input host.
package.loaded['lib.keybinds'] = nil
package.preload['lib.keybinds'] = function()
    return {
        controller={
            menu={A='accept', B='back', X='skirmish', Start='options', DpadUp='up', DpadDown='down',
                DpadLeft='left', DpadRight='right', LeftShoulder='previous', RightShoulder='next'},
            palantir={Comma='rotateLeft', Period='rotateRight', DpadLeft='previousCommand', DpadRight='nextCommand', DpadUp='shortcutUp',
                DpadDown='shortcutDown', A='pressCommand', B='rightPressCommand', X='saveFavorites', Y='backPalette', Start='options'},
            game={Comma='rotateLeft', Period='rotateRight', A='contextPrimary', B='deselect', X='jumpToSelection', Y='VIEW_LAST_RADAR_EVENT',
                LeftStick='VIEW_HOME_BASE', RightStick='CAMERA_RESET', Start='options',
                DpadUp='cycleHeroes', DpadDown='cycleBuilders', DpadLeft='selectAll',
                LeftShoulder='zoomOut', RightShoulder='zoomIn'},
        },
        palantirShortcuts={
            {title='Powers', action='powers', image='SBGood_Heal'}, {title='Heroes', action='heroes', image='HIAragorn'},
            {title='Builders', action='builders', image='BGFortress_Porter'}, {title='Favorites', action='favorites', image='SBGood_RallyingCall'},
        },
        shortcutBar={anchor='bottomleft',size=55,positions={
            {x=289,y=352},{x=254,y=396},{x=206,y=430},{x=146,y=441}}},
    }
end

-- Reload with Ctrl+Shift+F5 or bfme.console.reload() while the game is running.
-- Resolve the player each call so this helper also works after changing matches.
function setGold(amount)
    local player = bfme.players.localPlayer
    if not player then error("Start a match before setting gold") end
    return player:setMoney(amount)
end

-- Income Banner (an OpenBFME SDK sample): tells this player about the paydays. A presentation script: it only shows text, so it may
-- differ between players and is not part of the mod's identity.
local PAYDAY_FRAMES = 50 -- the same 10 seconds as the sim script

function onGameStart()
	ui.banner("Income Banner: every player gets 250 resources every 10 seconds")
end

function onFrame(frame)
	local me = game.localPlayer()
	if me >= 0 and frame % PAYDAY_FRAMES == 0 then
		ui.banner(string.format("Payday! +250 (you now have %d)", game.money(me)))
	end
end

-- a game loaded from a save: this script started afresh too
function onGameLoaded()
	ui.banner("Income Banner: welcome back, the paydays go on")
end

-- Income Banner (an OpenBFME SDK sample, docs/MODS.md): every 10 seconds of game time each player gets 250 resources.
-- A sim script: it changes the game, so every player runs it in lockstep and it is part of the mod's identity.
-- sdk.state is the only thing of a script that a save keeps: what it must remember goes there, and only an event changes it.
local PAYDAY_FRAMES = 50 -- the logic runs 5 frames a second (LOGICFRAMES_PER_SECOND)
local AMOUNT = 250

function onGameStart()
	sdk.state.paydays = 0
end

-- a game loaded from a save: the script started afresh, with sdk.state as it was saved
function onGameLoaded()
	print("loaded with " .. sdk.state.paydays .. " paydays so far")
end

function onFrame(frame)
	if frame % PAYDAY_FRAMES == 0 then
		for i = 0, game.playerCount() - 1 do
			game.addMoney(i, AMOUNT)
		end
		sdk.state.paydays += 1
	end
end

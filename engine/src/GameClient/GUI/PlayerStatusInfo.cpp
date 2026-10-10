// OpenBFME. GPL-3.0.
// See GameClient/GUI/PlayerStatusInfo.h.

#include "GameClient/GUI/PlayerStatusInfo.h"

#include "GameClient/GUI/GameTextSource.h"
#include "GameClient/GUI/LoadScreenInfo.h"
#include "GameClient/GUI/Skirmish/SkirmishSetup.h"

namespace
{
// isSlotLocalAlly (BFME2 decomp GameSlotApparent.cpp, RW 0x801190 tier A): the local slot itself, a slot on the local slot's team (>= 0), or every slot when the
// local slot's original template is the observer
bool isSlotLocalAlly(const SkirmishGameInfo &game, int slotIndex, int localIndex)
{
	if (slotIndex < 0 || localIndex < 0 || localIndex >= MAX_SLOTS)
	{
		return false;
	}
	if (slotIndex == localIndex)
	{
		return true;
	}
	const SkirmishGameSlot &slot = game.slots[slotIndex];
	const SkirmishGameSlot &local = game.slots[localIndex];
	if (slot.teamNumber == local.teamNumber && slot.teamNumber >= 0)
	{
		return true;
	}
	return local.origPlayerTemplate == PLAYERTEMPLATE_OBSERVER;
}

std::string text(const GameTextSource *source, const std::string &label)
{
	return loadScreenU16ToUtf8(fetchOrMissing(source, label));
}

// MultiplayerSettings::getColor(which) + 0x10 (RW 0x78385B): -1 the random definition, -2 the observer one, a colour index in range its RGB; else null
bool colorOf(const PlayerStatusInput &in, const SkirmishSetupSource &setup, int which, std::uint32_t &argb)
{
	if (which == PLAYERTEMPLATE_RANDOM)
	{
		argb = in.randomColor;
		return true;
	}
	if (which == PLAYERTEMPLATE_OBSERVER)
	{
		argb = in.observerColor;
		return true;
	}
	if (which < 0 || which >= (int)setup.colors().size())
	{
		return false;
	}
	argb = 0xFF000000u | (setup.colors()[(size_t)which].rgb & 0xFFFFFFu);
	return true;
}
} // namespace

PlayerStatusInfo makePlayerStatusInfo(const PlayerStatusInput &in, const SkirmishSetupSource &setup, const GameTextSource *source)
{
	PlayerStatusInfo info;
	info.inSkirmish = in.gameMode == 2; // RW 0x914AA1: TheGameLogic + 0x110 == 2
	if (!in.game)
	{
		return info;
	}
	const SkirmishGameInfo &game = *in.game;
	for (int i = 0; i < MAX_SLOTS; ++i) // RW 0x9151C3
	{
		const SkirmishGameSlot &slot = game.slots[i];
		if (!slot.isOccupied() || !in.slots[i].hasPlayer)
		{
			continue;
		}
		const PlayerStatusSlotState &live = in.slots[i];
		PlayerStatusRow row;
		row.slot = i;
		row.fields[0] = loadScreenU16ToUtf8(slot.name); // RW 0x915C51: GameSlot + 0x30
		// RW 0x80147D getApparentPlayerTemplateDisplayName
		const bool ally = isSlotLocalAlly(game, i, in.localSlot);
		if (in.showRandomPlayerTemplate && slot.origPlayerTemplate == PLAYERTEMPLATE_RANDOM && !ally)
		{
			row.fields[1] = text(source, "GUI:Random");
		}
		else if (slot.origPlayerTemplate == PLAYERTEMPLATE_OBSERVER)
		{
			row.fields[1] = text(source, "GUI:Observer");
		}
		else if (slot.playerTemplate < 0)
		{
			row.fields[1] = text(source, "GUI:Random");
		}
		else if (slot.playerTemplate < (int)setup.factions().size())
		{
			row.fields[1] = text(source, setup.factions()[(size_t)slot.playerTemplate].displayLabel);
		}
		// RW 0x915C91 .. 0x915CCD: "Team:%d" of team + 1, "Team:AI" for a computer without a team
		std::string team = "Team:" + std::to_string(slot.teamNumber + 1);
		if (slot.isAI() && slot.teamNumber == -1)
		{
			team = "Team:AI";
		}
		row.fields[2] = text(source, team);
		// RW 0x915D0B .. 0x915D66
		const char *status = nullptr;
		if (!slot.isAI() && in.network && !live.connected)
		{
			status = live.observer ? "GUI:PlayerObserverGone" : "GUI:PlayerGone";
		}
		else if (!live.defeated)
		{
			status = "GUI:PlayerAlive";
		}
		else
		{
			status = live.observer ? "GUI:PlayerObserver" : "GUI:PlayerDead";
		}
		row.fields[3] = text(source, status);
		// RW 0x801211: the apparent colour index, then getColor(index) + 0x10 (RW 0x915DA1 .. 0x915DBA)
		int colorIndex = slot.color;
		if (slot.origPlayerTemplate == PLAYERTEMPLATE_OBSERVER)
		{
			colorIndex = PLAYERTEMPLATE_OBSERVER;
		}
		else if (in.showRandomColor && !ally)
		{
			colorIndex = slot.origColor;
		}
		std::uint32_t argb = 0;
		if (colorOf(in, setup, colorIndex, argb))
		{
			row.color = (std::int32_t)argb;
		}
		info.rows.push_back(row);
	}
	return info;
}

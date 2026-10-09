// OpenBFME. GPL-3.0.
//
// ScriptActions, lane CAMP-1: the actions the Angmar campaign missions need that SCRIPT-1..3 did not port (RW 0x7CAFA5's cases). See ScriptActions.h.
//
// TARGET FACTS (rotwk201_game.exe, caveat S-001):
//   * GIVE_PLAYER_UPGRADE (player, upgrade) RW 0x7CB41A -> RW 0x7BBC00: every player of the mask (RW 0x758F7C); the upgrade (RW 0x66F5E5) when it is a player
//     upgrade (+ 4 == 0) is added complete (RW 0x6AEE22(upgrade, 2, 0));
//   * NAMED_RECEIVE_UPGRADE (unit, upgrade) RW 0x7CDE4F -> RW 0x7BECF6: the unit; not a HORDE: Object::giveUpgrade (RW 0x69388B); a HORDE: its contain's
//     slot 0xB4 (has it) / 0xB8 (give it) - not ported (S-1363): the horde object gets it through giveUpgrade;
//   * SET_PLAYER_COMMAND_POINTS_USED_TO_COUNTER (player, counter) RW 0x7CECA0 -> RW 0x7C376D: the first player of the mask (RW 0x6A85B6); the counter (RW
//     0x60817A, made when missing) = the command points in use (RW 0x6A7AC7: CommandPoints + 8);
//   * UNIT_CHANGE_OBJECT_STATUS (unit, status, on) RW 0x7CF52C -> RW 0x7C4360: the unit's status bit (the parser's index) set or cleared (RW 0x62684D);
//   * lane CAMP-1H: OBJECT_ALLOW_BONUSES (bool) RW 0x7BD718: every object's difficulty bonus flag (RW 0x68B907) and the script engine's + 0x1A5D5.
// NOT PORTED (S-1363): PLAYER_SET_MAX_SPELLPOINTS (RW 0x7BD15B: the rank whose cumulative Rank + 0x38 reaches the points, RW 0x782942, stored at Player +
// 0x30 by RW 0x70EB9A; that field's reader is unidentified) and SET_PLAYER_KILLS_OF_TYPE_TO_COUNTER (RW 0x7C38F6: ScoreKeeper + 0x1FC, the per-player
// kills by template map, RW 0x79E4D8, is not ported) stay unported (counted).

#include "GameLogic/ScriptEngine/ScriptActions.h"

#include "Common/CommandPoints.h"
#include "Common/Player.h"
#include "Common/Upgrade.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/ScriptEngine/ScriptConditions.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"

namespace
{
const ScriptParameter &param(const ScriptActionRec &a, size_t i)
{
	static const ScriptParameter kEmpty;
	return i < a.params.size() ? a.params[i] : kEmpty;
}
} // namespace

bool ScriptActions::executeCampaignAction(ScriptEngine &engine, const ScriptActionRec &a, const std::string &name)
{
	if (name == "OBJECT_ALLOW_BONUSES") // lane CAMP-1H: RW 0x7BD718 (ScriptActions vtable entry RW 0xC360D8)
	{
		// every object of TheGameLogic's list (RW 0x97338F, next + 0x8C) gets the flag (RW 0x68B907), then the script engine's + 0x1A5D5 for new objects
		const bool allow = param(a, 0).intValue != 0;
		for (Object *o = engine.logic().getFirstObject(); o; o = o->getNextObject())
		{
			o->setReceivingDifficultyBonus(allow);
		}
		engine.setObjectsReceiveDifficultyBonus(allow);
		return true;
	}
	if (name == "GIVE_PLAYER_UPGRADE") // RW 0x7BBC00
	{
		const UpgradeTemplate *u = Player::resolveUpgrade(param(a, 1).stringValue, false); // RW 0x66F5E5
		if (!u || u->getUpgradeType() != UPGRADE_TYPE_PLAYER)
		{
			return true;
		}
		for (Player *p : ScriptConditions::players(engine, param(a, 0).stringValue))
		{
			p->addUpgrade(u, Player::UPGRADE_STATUS_COMPLETE, false); // RW 0x6AEE22(u, 2, 0)
		}
		return true;
	}
	if (name == "NAMED_RECEIVE_UPGRADE") // RW 0x7BECF6
	{
		Object *o = engine.getUnitNamed(param(a, 0).stringValue);
		const UpgradeTemplate *u = Player::resolveUpgrade(param(a, 1).stringValue, false);
		if (!o || !u)
		{
			return true;
		}
		const int horde = ObjectTemplateInfoBuilder::kindOfIndex("HORDE");
		if (horde >= 0 && o->isKindOf((unsigned)horde))
		{
			engine.note("[S-1363] NAMED_RECEIVE_UPGRADE on a horde: the contain's slots 0xB4 / 0xB8 run as Object::giveUpgrade");
		}
		o->giveUpgrade(u); // RW 0x69388B
		return true;
	}
	if (name == "SET_PLAYER_COMMAND_POINTS_USED_TO_COUNTER") // RW 0x7C376D
	{
		if (Player *p = ScriptConditions::firstPlayer(engine, param(a, 0).stringValue))
		{
			engine.counter(param(a, 1).stringValue).value = p->commandPoints().getUsage(); // RW 0x6A7AC7
		}
		return true;
	}
	if (name == "UNIT_CHANGE_OBJECT_STATUS") // RW 0x7C4360
	{
		Object *o = engine.getUnitNamed(param(a, 0).stringValue);
		const int bit = param(a, 1).intValue;
		if (o && bit >= 0 && bit < 128)
		{
			o->setStatus((unsigned)bit, param(a, 2).intValue != 0); // RW 0x62684D
		}
		return true;
	}
	return false;
}

std::vector<std::string> ScriptActions::campaignStopLines()
{
	return {
		"[S-1363] PLAYER_SET_MAX_SPELLPOINTS (RW 0x7BD15B: Player + 0x30, its reader unidentified) and SET_PLAYER_KILLS_OF_TYPE_TO_COUNTER (RW 0x7C38F6: "
		"ScoreKeeper + 0x1FC, the kills by template) stay unported (counted); NAMED_RECEIVE_UPGRADE on a horde runs Object::giveUpgrade instead of the "
		"contain's slots 0xB4 / 0xB8 (noted)",
		"[S-1365] Team::updateState (RW 0x7A208C, lane CAMP-1H) runs the team scripts and flags except: the entered / exited flag + 0x5C (computed from the "
		"members' enter / exit frames, RW 0x68DD46), teamEventsList (RW 0x7396E8 -> the members' AI RW 0x662597), the attacker records' expiry (RW "
		"0x7A1FB1 / 0x7A2004), the team's vslot 0x14 (RW 0x7A59CB), the AIPlayer's team building (+ 0x112 / + 0x113) and the enemy scan's off-map filter",
		"[S-1366] GATE_OPEN / GATE_CLOSE (RW 0x7BD8B8 / 0x7BD826: the GateOpenAndCloseBehavior's slots 0x18 / 0x28 / 0x1C / 0x20) stay unported (counted): the "
		"gate module is not ported, a gate keeps its BLOCKING_GATE footprint (MAP ANG Amon Sul, Carn Dum, Fornost and Bonus open their gates by script)",
		"[S-1712] Object::initObject's player object modifiers (RW 0x6AD3E6 -> RW 0x6AD2B5 when Player + 0xAC + 0xB0 + 0xB4 > 0: health scale, body vslot "
		"0x6C, Object + 0x39D bits) are not ported; their writers are not located",
	};
}

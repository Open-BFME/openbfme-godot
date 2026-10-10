// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameLogic/SelfDestruct.h.

#include "GameLogic/SelfDestruct.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Team.h"
#include "Common/Upgrade.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/VictoryConditions.h"

#include <cstring>
#include <vector>

namespace
{
thread_local SelfDestruct::Stats g_stats;

// RW 0xC14034 .. 0xC14170: the upgrades transferAssetsFromThat never hands over (binary constants of RW 0x6AF7CE .. 0x6AF8DD, in its compare order)
const char *const kNotTransferred[] = { "Upgrade_RohanDualEconomyChoice", "Upgrade_IsengardDualEconomyChoice", "Upgrade_MordorDualEconomyChoice",
	"Upgrade_EvilDualEconomyChoice", "Upgrade_MenFaction", "Upgrade_ElfFaction", "Upgrade_DwarvesFaction", "Upgrade_IsengardFaction", "Upgrade_MordorFaction",
	"Upgrade_WildFaction", "Upgrade_AngmarFaction", "Upgrade_GandalfWhite", "Upgrade_Anduril", "Upgrade_ElvenGift" };

bool kindOf(const Object &o, const char *name)
{
	const int bit = ObjectTemplateInfoBuilder::kindOfIndex(name);
	return bit >= 0 && o.isKindOf((unsigned)bit);
}

bool status(const Object &o, const char *name)
{
	const int bit = ObjectTemplateInfoBuilder::objectStatusIndex(name);
	return bit >= 0 && o.testStatus((unsigned)bit);
}
} // namespace

const SelfDestruct::Stats &SelfDestruct::stats()
{
	return g_stats;
}

void SelfDestruct::transferAssetsFromThat(GameLogic &logic, Player &ally, Player &that, bool inherited)
{
	Team *team = ally.getDefaultTeam();
	if (!team)
	{
		return; // RW 0x6AF5B4: the ally's default team (+ 0x30C) is null -> nothing
	}
	// the leaver's objects, split by TEMPORARILY_DEFECTED (RotWK walks the leaver's teams and their members; here the object list, S-1122)
	std::vector<Object *> transfer, defected;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() != &that || kindOf(*o, "INERT") || kindOf(*o, "MOVE_ONLY") || kindOf(*o, "IGNORED_IN_GUI"))
		{
			continue;
		}
		(status(*o, "TEMPORARILY_DEFECTED") ? defected : transfer).push_back(o);
	}
	ScoreKeeper &score = ally.getScoreKeeper();
	const bool counting = score.counting();
	score.setCounting(false); // RW 0x79DC9E(0)
	SelfDestruct::Stats::Transfer record; // lane MP-3 (review r1): diagnostics only
	record.leaver = that.getPlayerIndex();
	record.ally = ally.getPlayerIndex();
	const int inheritedBit = ObjectTemplateInfoBuilder::objectStatusIndex("INHERITED_FROM_ALLY_TEAM");
	for (Object *o : transfer)
	{
		if (inherited && inheritedBit >= 0)
		{
			o->setStatus((unsigned)inheritedBit, true); // RW 0x62684D(0x50, 1)
		}
		Object *container = o->getContainedBy();
		if (container && kindOf(*container, "HORDE"))
		{
			container->setTeam(team); // RW 0x6AF707: the horde container joins
			if (o->getControllingPlayer() == &that)
			{
				o->setTeam(team); // INFERENCE (S-1122): the member follows its horde (ZH OpenContain::onCapture)
			}
		}
		else
		{
			o->setTeam(team); // RW 0x6AF740 (the contain hand-over vslot 0x54 and the RW 0xDE4AB8 lists: S-1122)
		}
		score.addObjectBuilt(logic, *o, 1); // RW 0x6AF769
		++g_stats.objectsTransferred;
		record.objects.push_back(o->getID());
	}
	g_stats.transferLog.push_back(std::move(record));
	score.setCounting(counting); // RW 0x6AF788
	(void)defected;                // RW 0x69ABA7 for the defected non-horde-members: not ported (S-853, S-1122)
	// the completed upgrades, lowest mask bit first (RW 0x6AF7B7 .. 0x6AF91F)
	if (TheUpgradeCenter)
	{
		UpgradeMaskType mask = that.getCompletedUpgradeMask();
		for (unsigned bit = 0; bit < UpgradeMaskType::BITS; ++bit)
		{
			if (!mask.test(bit))
			{
				continue;
			}
			const UpgradeTemplate *u = TheUpgradeCenter->findUpgradeByMaskBit((int)bit);
			if (!u)
			{
				continue;
			}
			bool skip = false;
			for (const char *name : kNotTransferred)
			{
				skip = skip || u->getUpgradeName() == name;
			}
			if (!skip)
			{
				ally.addUpgrade(u, Player::UPGRADE_STATUS_COMPLETE, false);
				++g_stats.upgradesTransferred;
			}
		}
	}
	// the cash (RW 0x6AF925 .. 0x6AF946)
	const std::uint32_t cash = that.getMoney()->countMoney();
	that.getMoney()->withdraw(cash, nullptr, true);
	ally.getMoney()->deposit(cash, nullptr, true);
	// the territory counters of the command points (RW 0x6AF94B .. 0x6AF972)
	int good = that.commandPoints().getTerritoryGood(), evil = that.commandPoints().getTerritoryEvil();
	if (that.getPlayerTemplate() && that.getPlayerTemplate()->m_evil)
	{
		++evil;
	}
	else
	{
		++good;
	}
	ally.commandPoints().setTerritoryCounters(ally.commandPoints().getTerritoryGood() + good, ally.commandPoints().getTerritoryEvil() + evil);
	++g_stats.transfers;
}

void SelfDestruct::execute(GameLogic &logic, Player &player, bool transferAssets)
{
	++g_stats.executed;
	VictoryConditions &victory = logic.victory();
	if (transferAssets)
	{
		PlayerList &players = logic.players();
		for (int i = 0; i < players.getPlayerCount(); ++i)
		{
			Player *other = players.getNthPlayer(i);
			if (i == player.getPlayerIndex() || !other)
			{
				continue;
			}
			if (player.getRelationship(other->getDefaultTeam()) != ALLIES || other->getRelationship(player.getDefaultTeam()) != ALLIES)
			{
				continue;
			}
			if (victory.hasSinglePlayerBeenDefeated(other))
			{
				continue;
			}
			transferAssetsFromThat(logic, *other, player, true);
			victory.killPlayer(&player);
			++g_stats.kills;
			return;
		}
	}
	victory.killPlayer(&player);
	++g_stats.kills;
}

void RegisterSelfDestructHandler(GameLogicDispatch &dispatch)
{
	// one executor of RW 0x77CA3D: END-2's registration (VictoryConditions::registerHandlers, the quit menu's surrender / exit), whose body runs
	// SelfDestruct::execute; registered here for the network games and replays that do not register it otherwise
	if (dispatch.handlerLane(MSG_SELF_DESTRUCT).empty())
	{
		VictoryConditions::registerHandlers(dispatch);
	}
}

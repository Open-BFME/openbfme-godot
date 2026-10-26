// OpenBFME. GPL-3.0.
// Lane AI5: the skirmish AI's upgrade purchases (GameLogic/SkirmishAI/AIUpgrade.h): AIUnitUpgrader, AIUpgradeScienceBuilder, their requests and heuristics.
// The expected values come from the binary (draw sites and bounds, the heuristic table, the FORTRESS timing, the request codes) and from the retail data the
// AI reads (its ArmyDefinition, the command sets); none is a count copied from a run.

#include "doctest.h"
#include "StartTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Upgrade.h"
#include "GameClient/ControlBarCommands.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SkirmishAI/AIBaseBuilder.h"
#include "GameLogic/SkirmishAI/AIStatCollector.h"
#include "GameLogic/SkirmishAI/AITacticalAI.h"
#include "GameLogic/SkirmishAI/AIUpgrade.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"
#include "GameLogic/SimMath.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <functional>
#include <set>
#include <string>
#include <vector>

namespace
{
int factionIndex(const PlayerTemplateStore &store, const std::string &name)
{
	for (int i = 0; i < store.getPlayerTemplateCount(); ++i)
	{
		if (store.getNthPlayerTemplate(i)->getName() == name)
		{
			return i;
		}
	}
	return -1;
}

// Evendim, slots 0 / 1 on start positions 0 / 1, both computers of Medium difficulty (SLOT_EASY_AI + 1)
void withVersusGame(starttest::Shared &s, const std::string &a, const std::string &b, std::uint32_t seed, const std::function<void(LiveGame &, GameLogic &)> &body)
{
	NewGameMessage m;
	m.game.mapName = "maps/map mp evendim/map mp evendim.map";
	m.game.seed = seed;
	m.game.startingCash = 1500;
	const std::string factions[2] = { a, b };
	for (int i = 0; i < 2; ++i)
	{
		SkirmishGameSlot &slot = m.game.slots[i];
		slot.state = (SlotState)(SLOT_EASY_AI + 1);
		slot.name = i == 0 ? u"Player0" : u"Player1";
		slot.playerTemplate = factionIndex(s.world->playerTemplates(), factions[i]);
		slot.startPos = i;
		slot.color = i;
		slot.teamNumber = i;
	}
	static std::vector<MapCacheEntry> cache;
	std::string error;
	if (cache.empty())
	{
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, &error), error);
	}
	NewGameStart start(RandomAlgorithm::ZH_CarryChain);
	REQUIRE_MESSAGE(NewGame::prepareNewGame(m, s.world->playerTemplates(), s.settings, cache, RandomAlgorithm::ZH_CarryChain, start, &error), error);
	ArchiveW3DFileSource source(*s.mount->fs);
	WW3DAssetManager assets(source);
	LiveGame game(*s.world, *s.mount->fs, assets, s.options);
	LiveGame::Options o;
	o.start = &start;
	REQUIRE_MESSAGE(game.load(o, &error), error);
	body(game, game.logic());
}

std::vector<const AISkirmishPlayer *> ais(GameLogic &logic)
{
	std::vector<const AISkirmishPlayer *> out;
	for (int i = 0; i < logic.players().getPlayerCount(); ++i)
	{
		if (const AISkirmishPlayer *ai = logic.skirmishAI().findAI(i))
		{
			out.push_back(ai);
		}
	}
	return out;
}

AISkirmishPlayer &mutableAI(const AISkirmishPlayer *ai)
{
	return const_cast<AISkirmishPlayer &>(*ai);
}

size_t callsAt(GameLogic &logic, const char *file, int line)
{
	size_t n = 0;
	for (const auto &c : logic.random().callLog())
	{
		n += (c.file == file && c.line == line) ? 1u : 0u;
	}
	return n;
}

// the button of `name` in the object's command set (slots 0 .. 32)
bool objectOffers(const Object &obj, const std::string &buttonName)
{
	const CommandSet *set = TheCommandStore ? TheCommandStore->findCommandSet(obj.getCommandSetName()) : nullptr;
	for (int i = 0; set && i < CommandSet::MAX_BUTTONS; ++i)
	{
		const CommandButton *b = set->getCommandButton(i);
		if (b && b->m_name == buttonName)
		{
			return true;
		}
	}
	return false;
}
} // namespace

TEST_CASE("ai5 unit upgrader: every army unit's upgrade buttons become requests drawn at AIUnitUpgrader.cpp 0x5D, bought with ChanceForUnitsToUpgrade at 0xB4 (RW 0x9A155C / 0x9A1696)")
{
	OPENBFME_REQUIRE_START(s);
	withVersusGame(*s, "FactionMen", "FactionMordor", 2, [&](LiveGame &game, GameLogic &logic) {
		// the counters at the moment the call log starts (the load registered the starting objects)
		unsigned long long registered = 0, decided = 0;
		for (const AISkirmishPlayer *ai : ais(logic))
		{
			registered -= ai->build.unitUpgrader.registered;
			decided -= ai->build.unitUpgrader.submitted + ai->build.unitUpgrader.declined;
		}
		logic.random().enableCallLog(true);
		for (int i = 0; i < 3000; ++i)
		{
			game.advance(0.2);
		}
		const std::vector<const AISkirmishPlayer *> list = ais(logic);
		REQUIRE(list.size() == 2);
		for (const AISkirmishPlayer *ai : list)
		{
			const AIUnitUpgraderState &u = ai->build.unitUpgrader;
			CHECK(u.listening);
			REQUIRE(ai->army != nullptr);
			registered += u.registered;
			decided += u.submitted + u.declined;
			for (const auto *v : { &u.waiting, &u.upgrading })
			{
				for (const auto &r : *v)
				{
					CHECK(r->upgrade);
					CHECK_FALSE(r->expansion);
					// the record's button is one of its object's command set, its name the button's upgrade (RW 0x9A155C)
					const Object *obj = logic.findObjectByID(r->producer);
					if (obj)
					{
						CHECK(objectOffers(*obj, r->buttonName));
					}
					const CommandButton *b = TheCommandStore->findCommandButton(r->buttonName);
					REQUIRE(b != nullptr);
					CHECK(b->m_upgradeName == r->templateName);
					const UpgradeTemplate *up = TheUpgradeCenter->findUpgrade(r->templateName);
					REQUIRE(up != nullptr);
					CHECK(r->heuristicType == up->m_skirmishAIHeuristic); // RW 0x9EDDAE(upgrade + 0x98)
					if (up->m_skirmishAIHeuristic != -1)
					{
						CHECK(r->priority == 9999.0f); // RW 0xC18414
					}
					else
					{
						// (float)GetGameLogicRandomValue((int)UnitUpgradePriorityLow, (int)UnitUpgradePriorityHigh): an integer of the range
						CHECK(r->priority == (float)(int)r->priority);
						CHECK(r->priority >= (float)(int)ai->army->unitUpgradePriorityLow);  // tolerance: a draw of the ArmyDefinition's range
						CHECK(r->priority <= (float)(int)ai->army->unitUpgradePriorityHigh); // tolerance: a draw of the ArmyDefinition's range
					}
				}
			}
		}
		// one draw per record made (0x5D) and one per decision that was not forced by a result of 2 (0xB4), with the ArmyDefinition's bounds
		CHECK(callsAt(logic, "AIUnitUpgrader.cpp", 0x5D) == registered);
		CHECK(callsAt(logic, "AIUnitUpgrader.cpp", 0xB4) == decided);
		for (const auto &c : logic.random().callLog())
		{
			if (c.file == std::string("AIUnitUpgrader.cpp") && c.line == 0x5D)
			{
				CHECK_FALSE(c.real);
				CHECK(c.lo == (int)list[0]->army->unitUpgradePriorityLow);
				CHECK(c.hi == (int)list[0]->army->unitUpgradePriorityHigh);
			}
			if (c.file == std::string("AIUnitUpgrader.cpp") && c.line == 0xB4)
			{
				CHECK(c.real);
				CHECK(c.rlo == 0.0f);
				CHECK(c.rhi == 1.0f);
			}
		}
		CHECK(registered != 0); // tolerance: Men's and Mordor's hordes carry upgrade buttons (retail CommandSets); the count depends on what the AIs trained
		logic.random().enableCallLog(false);
	});
}

TEST_CASE("ai5 upgrade builder: the structures' upgrades and the plots' expansions, the priority draws at AIUpgradeScienceBuilder.cpp 0x18C / 0x18F, the FORTRESS draws and the bought upgrades (RW 0x9A0023)")
{
	OPENBFME_REQUIRE_START(s);
	withVersusGame(*s, "FactionMen", "FactionMordor", 2, [&](LiveGame &game, GameLogic &logic) {
		logic.random().enableCallLog(true);
		for (int i = 0; i < 4500; ++i)
		{
			game.advance(0.2);
		}
		const std::vector<const AISkirmishPlayer *> list = ais(logic);
		REQUIRE(list.size() == 2);
		unsigned long long submitted = 0;
		for (const AISkirmishPlayer *ai : list)
		{
			const AIUpgradeScienceBuilderState &b = ai->build.scienceBuilder;
			CHECK(b.listening);
			submitted += b.upgradesSubmitted;
			for (const auto &r : b.upgrades)
			{
				CHECK(r->upgrade);
				CHECK(r->reserveMoney); // + 0x21 (RW 0x9A018B)
				const UpgradeTemplate *up = TheUpgradeCenter->findUpgrade(r->templateName);
				REQUIRE(up != nullptr);
				CHECK(r->heuristicType == up->m_skirmishAIHeuristic); // RW 0x99F52F
				const Object *obj = logic.findObjectByID(r->producer);
				REQUIRE(obj != nullptr); // RW 0x99FD0E drops a record whose object is gone
				// tolerance: registered when finished, RW 0x99FCC4 tests the construction percent below 0. Merge queue q23: a finished producer that was destroyed and is
				// rebuilt (GettingBuiltBehavior RW 0x857B0B: back to 0 % under construction) keeps its records, RW 0x99FD0E drops only a gone object's (measured: a
				// GondorBarracks rebuilding at frame 4,501 with its Upgrade_GondorBarracksLevel2 record)
				CHECK((obj->getConstructionPercent() < 0.0f || obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION))); // tolerance: finished, or rebuilt after its death
				CHECK_FALSE(obj->isKindOfName("WALL_HUB")); // RW 0x99FAA6
				if (r->state == 2)
				{
					// complete (RW 0x99F83D): it left the production queue, so the upgrade is the player's or the object's
					const Player *p = obj->getControllingPlayer();
					CHECK((up->getUpgradeType() == UPGRADE_TYPE_PLAYER ? p->hasUpgradeComplete(up) : obj->hasUpgrade(up)));
				}
			}
			for (const auto &r : b.expansions)
			{
				CHECK(r->expansion);
				CHECK(r->priority == -1.0f); // RW 0xBD19DC
				const CommandButton *btn = TheCommandStore->findCommandButton(r->buttonName);
				REQUIRE(btn != nullptr);
				CHECK(btn->m_command == GUI_COMMAND_FOUNDATION_CONSTRUCT);
			}
		}
		// the priority of a pick: result 0 draws (NormalLow, NormalHigh) at 0x18C, 1 (ImportantLow, ImportantHigh) at 0x18F, 2 none
		size_t normal = 0, important = 0;
		for (const auto &c : logic.random().callLog())
		{
			if (c.file == std::string("AIUpgradeScienceBuilder.cpp") && c.line == 0x18C)
			{
				++normal;
				CHECK(c.rlo == list[0]->army->upgradeSciencePriorityNormalLow);
				CHECK(c.rhi == list[0]->army->upgradeSciencePriorityNormalHigh);
			}
			if (c.file == std::string("AIUpgradeScienceBuilder.cpp") && c.line == 0x18F)
			{
				++important;
				CHECK(c.rlo == list[0]->army->upgradeSciencePriorityImportantLow);
				CHECK(c.rhi == list[0]->army->upgradeSciencePriorityImportantHigh);
			}
			if (c.file == std::string("AIUpgradeHeuristicFortress.cpp"))
			{
				CHECK(c.lo == 1);
				CHECK(((c.line == 0x4F && c.hi == 5) || (c.line == 0x58 && c.hi == 10)));
			}
			if (c.file == std::string("AIExpansionScience.cpp"))
			{
				CHECK(c.line == 0x5C);
				CHECK(c.lo == 600);
				CHECK(c.hi == 1000);
			}
		}
		CHECK(normal + important <= submitted); // tolerance: a pick with result 2 draws nothing
		CHECK(submitted != 0); // tolerance: 4500 frames reach the MidGame phase (PhaseDuration_Rush 270 / 300 s), where the builder buys one upgrade at a time
		logic.random().enableCallLog(false);
	});
}

TEST_CASE("ai5 heuristics: none answers 1, IMPORTANT and BOILINGOIL 0 (RW 0xC8EAF0 / 0xC8EAE8), FORTRESS waits (7 - phase) * 5 seconds and draws 1 in 10 or 1 in 5 (RW 0x9ED374), the summon ones are a stop")
{
	OPENBFME_REQUIRE_START(s);
	withVersusGame(*s, "FactionMen", "FactionMordor", 2, [&](LiveGame &game, GameLogic &logic) {
		game.advance(0.2);
		const std::vector<const AISkirmishPlayer *> list = ais(logic);
		REQUIRE(list.size() == 2);
		AISkirmishPlayer &ai = mutableAI(list[0]);
		Player &player = *logic.players().getNthPlayer(ai.playerIndex);
		AIBuildRequest r;
		r.heuristicType = AIUpgrade::HEURISTIC_NONE;
		CHECK(AIUpgrade::evaluate(logic, r, player) == 1);
		CHECK(r.heuristicResult == 1);
		r.heuristicType = AIUpgrade::HEURISTIC_IMPORTANT;
		CHECK(AIUpgrade::evaluate(logic, r, player) == 0);
		r.heuristicType = AIUpgrade::HEURISTIC_BOILINGOIL;
		CHECK(AIUpgrade::evaluate(logic, r, player) == 0);
		r.heuristicType = AIUpgrade::HEURISTIC_SUMMON_ANTICAVALRY;
		CHECK(AIUpgrade::evaluate(logic, r, player) == 3);
		bool stopped = false;
		for (const std::string &st : logic.report().stops)
		{
			stopped = stopped || st.rfind("[S-1876]", 0) == 0;
		}
		CHECK(stopped);

		// FORTRESS (RW 0x9ED351: 3, next frame -1)
		AIBuildRequest f;
		AIUpgrade::makeHeuristic(logic, f, AIUpgrade::HEURISTIC_FORTRESS);
		CHECK(f.heuristicNextFrame == 0xFFFFFFFFu);
		CHECK(f.heuristicFortressResult == 3);
		logic.random().enableCallLog(true);
		logic.random().clearCallLog();
		const int savedPhase = ai.build.phase;
		ai.build.phase = 0; // the Rush: no evaluation, no draw
		CHECK(AIUpgrade::evaluate(logic, f, player) == 3);
		CHECK(logic.random().callLog().empty());
		CHECK(f.heuristicNextFrame == 0xFFFFFFFFu);
		ai.build.phase = 1;
		player.getMoney()->withdraw(player.getMoney()->countMoney(), false); // money + reserve <= 2000: GetGameLogicRandomValue(1, 10) at 0x58
		const std::uint32_t frame = logic.getFrame();
		const int seconds = (7 - 1) * 5 - (player.getSkirmishDifficulty() > 1 ? 5 : 0);
		const int result = AIUpgrade::evaluate(logic, f, player);
		CHECK(f.heuristicNextFrame == frame + (std::uint32_t)(LOGICFRAMES_PER_SECOND * seconds));
		const auto &calls = logic.random().callLog();
		// one draw when the player's structure list holds a DOZER_FACTORY (the fortress), none otherwise
		bool fortress = false;
		for (ObjectID id : AITacticalAI::recordOf(logic, player.getPlayerIndex()).structures())
		{
			const Object *o = logic.findObjectByID(id);
			fortress = fortress || (o && o->isKindOfName("DOZER_FACTORY"));
		}
		REQUIRE(calls.size() == (fortress ? 1u : 0u));
		if (fortress)
		{
			CHECK(calls[0].file == "AIUpgradeHeuristicFortress.cpp");
			CHECK(calls[0].line == 0x58);
			CHECK(calls[0].lo == 1);
			CHECK(calls[0].hi == 10);
			CHECK(result == (calls[0].result == 1 ? 0 : 3));
		}
		// before the next frame it answers its last result without a draw
		logic.random().clearCallLog();
		CHECK(AIUpgrade::evaluate(logic, f, player) == result);
		CHECK(logic.random().callLog().empty());
		// past it, with more than 2000: GetGameLogicRandomValue(1, 5) at 0x4F
		player.getMoney()->deposit(2001, false);
		f.heuristicNextFrame = frame;
		f.heuristicFortressResult = 3;
		AIUpgrade::evaluate(logic, f, player);
		if (fortress)
		{
			REQUIRE(logic.random().callLog().size() == 1u);
			CHECK(logic.random().callLog()[0].line == 0x4F);
			CHECK(logic.random().callLog()[0].hi == 5);
		}
		ai.build.phase = savedPhase;
		logic.random().enableCallLog(false);
	});
}

TEST_CASE("ai5 legality: a horde's NEED_UPGRADE object upgrade is refused (9) until the player owns the technology and a forge (its RequiredObjectFilter), then legal (0), and refused again once the horde has it (RW 0x695338)")
{
	OPENBFME_REQUIRE_START(s);
	withVersusGame(*s, "FactionMen", "FactionMordor", 2, [&](LiveGame &game, GameLogic &logic) {
		// the first GondorFighterHorde the Men AI trains (Command_PurchaseUpgradeGondorForgedBlades: OBJECT_UPGRADE, NEED_UPGRADE
		// Upgrade_TechnologyGondorForgedBlades, retail CommandButton.ini)
		Object *horde = nullptr;
		for (int i = 0; i < 3000 && !horde; ++i)
		{
			game.advance(0.2);
			for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
			{
				// with its members (RW 0x694914 asks the horde's contained objects: an empty horde is affected by nothing)
				const ContainModuleInterface *c = o->getContain();
				const ContainModuleInterface::ContainedItemsList *items = c ? c->getContainedItemsList() : nullptr;
				if (o->getTemplate()->getName() == "GondorFighterHorde" && !o->isEffectivelyDead() && o->getProductionUpdate() && items && !items->empty())
				{
					horde = o;
					break;
				}
			}
		}
		REQUIRE(horde != nullptr);
		const CommandButton *blades = TheCommandStore->findCommandButton("Command_PurchaseUpgradeGondorForgedBlades");
		REQUIRE(blades != nullptr);
		Player &owner = *horde->getControllingPlayer();
		const UpgradeTemplate *tech = TheUpgradeCenter->findUpgrade("Upgrade_TechnologyGondorForgedBlades");
		const UpgradeTemplate *up = TheUpgradeCenter->findUpgrade("Upgrade_GondorForgedBlades");
		REQUIRE(tech != nullptr);
		REQUIRE(up != nullptr);
		if (!owner.hasUpgradeComplete(tech) && !horde->hasUpgrade(up))
		{
			CHECK(AIUpgrade::legality(logic, *horde, blades) == 9);
		}
		owner.addUpgrade(tech, Player::UPGRADE_STATUS_COMPLETE, true);
		// Upgrade_GondorForgedBlades has RequiredObjectFilter = NONE +GondorForge (retail Upgrade.ini): without a forge the horde is not affected
		// (RW 0x694914), so the upgrade stays refused; with one it is legal
		bool forge = false;
		for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
		{
			forge = forge || (o->getTemplate()->getName() == "GondorForge" && o->getControllingPlayer() == &owner && !o->isEffectivelyDead());
		}
		if (!forge && !horde->hasUpgrade(up))
		{
			CHECK(AIUpgrade::legality(logic, *horde, blades) == 9);
		}
		std::string err;
		Coord3D at = *horde->getPosition();
		at.x += 300.0f;
		REQUIRE_MESSAGE(game.createObject("GondorForge", owner.getPlayerIndex(), at, 0.0f, &err) != nullptr, err);
		if (!horde->hasUpgrade(up))
		{
			CHECK(AIUpgrade::legality(logic, *horde, blades) == 0);
		}
		horde->giveUpgrade(up);
		CHECK(AIUpgrade::legality(logic, *horde, blades) == 9); // an OBJECT upgrade it has (RW 0x691421)
		CHECK(AIUpgrade::legality(logic, *horde, nullptr) == 9); // no button / upgrade
	});
}

TEST_CASE("ai5: two runs of Medium Men against Medium Mordor agree on every frame hash with the upgrade builders (2000 frames)")
{
	OPENBFME_REQUIRE_START(s);
	std::vector<std::uint32_t> a, b;
	for (auto *out : { &a, &b })
	{
		withVersusGame(*s, "FactionMen", "FactionMordor", 7, [&](LiveGame &game, GameLogic &logic) {
			for (int i = 0; i < 2000; ++i)
			{
				game.advance(0.2);
				out->push_back(logic.computeStateHash());
			}
		});
	}
	REQUIRE(a.size() == b.size());
	CHECK(a == b);
}

TEST_CASE("ai5 money saver: every 2 seconds the reserve target is max(hero cost, min(difficulty, 2) * 5000) and money * (PercentToSave / (3 - d)) moves into the reserve with more than 2 economy structures (RW 0x8F0502)")
{
	OPENBFME_REQUIRE_START(s);
	withVersusGame(*s, "FactionMen", "FactionMordor", 2, [&](LiveGame &game, GameLogic &logic) {
		for (int i = 0; i < 2500; ++i)
		{
			game.advance(0.2);
		}
		const std::vector<const AISkirmishPlayer *> list = ais(logic);
		REQUIRE(list.size() == 2);
		int deposits = 0;
		for (const AISkirmishPlayer *c : list)
		{
			AISkirmishPlayer &ai = mutableAI(c);
			Player &player = *logic.players().getNthPlayer(ai.playerIndex);
			AIStatCollector &rec = *logic.skirmishAI().record(player.getPlayerIndex());
			const int d = player.getSkirmishDifficulty() < 2 ? player.getSkirmishDifficulty() : 2;
			const std::uint32_t frame = logic.getFrame();
			// not yet due: nothing changes
			ai.retryTimer = frame + 1;
			const std::uint32_t reserveBefore = rec.reserve();
			AIBaseBuilder::saveMoney(logic, ai, player);
			CHECK(ai.retryTimer == frame + 1);
			CHECK(rec.reserve() == reserveBefore);
			// due: the target, then the deposit when the reserve is below it
			ai.retryTimer = frame;
			rec.setReserve(0);
			player.getMoney()->withdraw(player.getMoney()->countMoney(), false);
			player.getMoney()->deposit(1000, false);
			const std::uint32_t heroCost = (std::uint32_t)ai.build.units.heroCost;
			const std::uint32_t cap = (std::uint32_t)(d * 5000);
			const std::uint32_t target = heroCost <= cap ? cap : heroCost;
			const bool economy = rec.economy().size() > 2u;
			const float v = SimMath::mulf32(1000.0f, SimMath::divf32(ai.army->percentToSave[ai.build.phase], (float)(3 - d)));
			const std::uint32_t amount = (1.0f < v && economy) ? SimMath::ftol2Low32((double)v) : 0u;
			AIBaseBuilder::saveMoney(logic, ai, player);
			deposits += amount != 0 ? 1 : 0;
			CHECK(rec.reserveTarget() == target);
			CHECK(rec.reserve() == amount);
			CHECK(player.getMoney()->countMoney() == 1000u - amount);
			CHECK(AIBaseBuilder::moneyPlusReserve(logic, player) == 1000); // RW 0x99E766
			CHECK(ai.retryTimer == frame + 2u * LOGICFRAMES_PER_SECOND);
			// RW 0x99E79A: an amount above the reserve is refused, one within it goes back to the money
			AIBaseBuilder::withdrawFromReserve(logic, player, amount + 1);
			CHECK(rec.reserve() == amount);
			AIBaseBuilder::withdrawFromReserve(logic, player, amount);
			CHECK(rec.reserve() == 0u);
			CHECK(player.getMoney()->countMoney() == 1000u);
		}
		CHECK(deposits != 0); // tolerance: at frame 2500 an AI with more than 2 farms saves (Evendim, seed 2); which of the two has them is the run's
	});
}

TEST_CASE("ai5 expansions: the structure an expansion request builds stands at its plot's position (RW 0x9ED8C4 with the plot's + 0x30 .. + 0x3C; Sol review r1)")
{
	OPENBFME_REQUIRE_START(s);
	// merge queue q26: the game of seed 2 no longer reaches a fortress expansion (the merged simulation plays it otherwise), so the claim is checked in the
	// first of the AI-vs-AI suite's seeds whose game builds one; which seed that is, is an engine fact, not a retail one
	bool checked = false;
	for (const std::uint32_t seed : { 2u, 7u, 1234u })
	{
		if (checked)
		{
			break;
		}
		withVersusGame(*s, "FactionMen", "FactionMordor", seed, [&](LiveGame &game, GameLogic &logic) {
			const AIBuildRequest *found = nullptr;
			const Object *built = nullptr, *plot = nullptr;
			for (int i = 0; i < 7000 && !built; ++i)
			{
				game.advance(0.2);
				for (const AISkirmishPlayer *ai : ais(logic))
				{
					for (const auto &r : ai->build.scienceBuilder.expansions)
					{
						const Object *b = r->built != INVALID_ID ? logic.findObjectByID(r->built) : nullptr;
						if (b && !built)
						{
							found = r.get();
							built = b;
							plot = logic.findObjectByID(r->producer);
						}
					}
				}
			}
			MESSAGE("seed " << seed << ": " << (built ? "an expansion built" : "no expansion built"));
			if (!built)
			{
				return;
			}
			checked = true;
			REQUIRE(plot != nullptr);
			CHECK(built->getTemplate()->getFinalOverride()->getName() == found->templateName);
			CHECK(built->getPosition()->x == plot->getPosition()->x);
			CHECK(built->getPosition()->y == plot->getPosition()->y);
			CHECK(found->offset.x == plot->getPosition()->x);
			CHECK(found->offset.y == plot->getPosition()->y);
		});
	}
	CHECK(checked); // a computer buys its fortress expansions in the MidGame of one of the seeds (Evendim)
}

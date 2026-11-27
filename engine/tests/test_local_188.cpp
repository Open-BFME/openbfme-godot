// OpenBFME unit tests. GPL-3.0.
// LOCAL-188 (fork issue 981): AIEconomyBuilder::onUnRegister (RW 0x8EE8CD, DONOR BFME2 0x4EA1A5 AIEconomyBuilder.cpp).
// Expectations come from RotWK 2.01 game.dat disassembly of RW 0x8EE8CD:
//   - Requires constructionPercent < 0.0f (finished) OR OBJECT_STATUS_UNDER_CONSTRUCTION (status bit 2).
//   - Ignores templates with KindOf LINKED_TO_FLAG (template + 0x10E bit 2, bit 50).
//   - Decrements AIEconomyState::built (+0x18) when finished && built > 0.
//   - Finds the matching farm site (inUse && owner == playerIndex && built == obj->getID()),
//     erases it from requests/pending/inProgress if !finished(), increments failures (+0x6C), clears inUse (+0x64).

#include "doctest.h"
#include "CombatTestUtil.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SkirmishAI/AIBaseBuilder.h"
#include "GameLogic/SkirmishAI/AIStatCollector.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"

#include <memory>
#include <vector>

using namespace combattest;

namespace
{
const char *const kFarmIni =
	"Object TestFarm\n"
	"  KindOf = STRUCTURE ECONOMY_STRUCTURE\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"End\n"
	"Object TestFlagFarm\n"
	"  KindOf = STRUCTURE ECONOMY_STRUCTURE LINKED_TO_FLAG\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"End\n";
}

TEST_CASE("LOCAL-188 AIEconomyBuilder::onUnRegister decrements built count and releases farm site (RW 0x8EE8CD)")
{
	CombatWorld cw(kFarmIni);
	Object *farm = cw.spawn("TestFarm", 200.0f, 200.0f, 0);
	REQUIRE(farm != nullptr);
	farm->setConstructionPercent(-1.0f); // completed structure in SAGE (RW 0x8EE8D7: 0.0f > constructionPercent)

	std::vector<std::shared_ptr<AIBuildRequest>> pool;
	auto site = std::make_shared<AIBuildRequest>();
	site->farm = true;
	site->inUse = true;
	site->owner = 0;
	site->built = farm->getID();
	site->state = 2; // complete
	site->failures = 0;
	pool.push_back(site);

	AISkirmishPlayer ai;
	ai.playerIndex = 0;
	ai.farmSites = &pool;
	ai.build.economy.built = 1;

	// RW 0x8EE8CD: completed farm removal decrements built (+0x18), increments site failures (+0x6C), clears inUse (+0x64).
	AIBaseBuilder::onEconomyObjectUnRegistered(*cw.logic, ai, *farm, AIStatCollector::LISTEN_ECONOMY);
	CHECK(ai.build.economy.built == 0);
	CHECK(site->inUse == false);
	CHECK(site->failures == 1);

	// Unfinished (UNDER_CONSTRUCTION) farm removal: does not decrement built, erases from requests/pending/inProgress, releases site.
	Object *wip = cw.spawn("TestFarm", 300.0f, 200.0f, 0);
	REQUIRE(wip != nullptr);
	wip->setConstructionPercent(50.0f);
	wip->setStatus(2u, true); // OBJECT_STATUS_UNDER_CONSTRUCTION (RW 0x8EE8E3)

	auto wipSite = std::make_shared<AIBuildRequest>();
	wipSite->farm = true;
	wipSite->inUse = true;
	wipSite->owner = 0;
	wipSite->built = wip->getID();
	wipSite->state = 1; // in progress (!finished())
	wipSite->failures = 0;
	pool.push_back(wipSite);
	ai.build.economy.built = 2;
	ai.build.economy.inFlight = 1;
	ai.build.economy.requests.push_back(wipSite);
	ai.build.inProgress.push_back(wipSite);

	AIBaseBuilder::onEconomyObjectUnRegistered(*cw.logic, ai, *wip, AIStatCollector::LISTEN_ECONOMY);
	CHECK(ai.build.economy.built == 2); // RW 0x8EE904: not finished, built unchanged
	CHECK(ai.build.economy.inFlight == 0);
	CHECK(ai.build.economy.requests.empty());
	CHECK(ai.build.inProgress.empty());
	CHECK(wipSite->inUse == false);
	CHECK(wipSite->failures == 1);
}

TEST_CASE("LOCAL-188 AIEconomyBuilder::onUnRegister ignores LINKED_TO_FLAG economy structures (RW 0x8EE8CD / 0x8EE8F4)")
{
	CombatWorld cw(kFarmIni);
	Object *flagFarm = cw.spawn("TestFlagFarm", 200.0f, 200.0f, 0);
	REQUIRE(flagFarm != nullptr);
	flagFarm->setConstructionPercent(-1.0f);

	std::vector<std::shared_ptr<AIBuildRequest>> pool;
	auto site = std::make_shared<AIBuildRequest>();
	site->farm = true;
	site->inUse = true;
	site->owner = 0;
	site->built = flagFarm->getID();
	site->state = 2;
	site->failures = 0;
	pool.push_back(site);

	AISkirmishPlayer ai;
	ai.playerIndex = 0;
	ai.farmSites = &pool;
	ai.build.economy.built = 1;

	// RW 0x8EE8F4: template + 0x10E bit 2 (LINKED_TO_FLAG) exits immediately without touching built or farmSites.
	AIBaseBuilder::onEconomyObjectUnRegistered(*cw.logic, ai, *flagFarm, AIStatCollector::LISTEN_ECONOMY);
	CHECK(ai.build.economy.built == 1);
	CHECK(site->inUse == true);
	CHECK(site->failures == 0);
}

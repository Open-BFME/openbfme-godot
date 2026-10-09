// OpenBFME unit tests. GPL-3.0.
// Lane AI-1, step 1: the skirmish AI data blocks (GameLogic/SkirmishAI/SkirmishAIData.h).
// Expected values: the field tables and name lists are re-read from game.dat (RW_GAME_DAT) at run time; the store rules and the parsers' acceptance
// are synthetic (the RW functions named in each test); the retail corpus counts were taken independently from the shipped
// data\ini\default\skirmishaidata.ini (104 AIBase, 8 AIDozerAssignment, 1 SkirmishAIData, 8 ArmyDefinition blocks) and playeraitypes.ini (17 blocks).

#include "doctest.h"

#include "PeImage.h"
#include "RetailTestMount.h"
#include "StartTestUtil.h"

#include "Common/INI.h"
#include "Common/INIException.h"
#include "Common/NameKeyGenerator.h"
#include "Common/StateHash.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

using retailtest::PeImage;

namespace
{
std::vector<std::uint8_t> bytesOf(const std::string &s)
{
	return std::vector<std::uint8_t>(s.begin(), s.end());
}

struct Loader
{
	NameKeyGenerator keys;
	SkirmishAIStore store{ keys };
	INIEnvironment env;
	Loader()
	{
		keys.init();
		store.registerBlocks(env.blocks);
	}
	void load(const std::string &text)
	{
		INI ini(env);
		ini.loadMemory("skirmishaidata.ini", bytesOf(text), INI_LOAD_OVERWRITE);
	}
};

// the generic INI parsers by their RW address (the other lanes' tables use the same map)
std::map<std::uint32_t, INIFieldParseProc> genericParsers()
{
	return { { 0x42e558, INI::parseBool }, { 0x42ed00, INI::parseReal }, { 0x42ee5e, INI::parseAsciiString }, { 0x42ec5e, INI::parseInt },
		{ 0x42eefa, INI::parsePercentToReal }, { 0x42eed6, INI::parseAsciiStringVector } };
}

// compares a port table with the RW table at `va`: same tokens in the same order, the generic parsers where RW uses one.
// The RW tables of these blocks (except PlayerAIType's) have NO terminating row: the next words are the table's own strings or code (RW 0xC1391C is
// the text "BuildCostReduction"), so the row count is given and the word after the last row is checked not to be a field name of the block.
void compareTable(const PeImage &pe, std::uint32_t va, const FieldParse *mine, const char *name, size_t expectedRows)
{
	const auto generic = genericParsers();
	for (size_t i = 0; i < expectedRows; ++i, va += 16)
	{
		const std::string token = pe.cstring(pe.u32At(va));
		INFO(name << " row " << i << " " << token);
		REQUIRE(mine[i].token != nullptr);
		CHECK(std::string(mine[i].token) == token);
		const std::uint32_t proc = pe.u32At(va + 4);
		auto g = generic.find(proc);
		if (g != generic.end())
		{
			CHECK(mine[i].parse == g->second);
		}
		else
		{
			// a custom parser in RW: the port must not use a generic one there
			for (const auto &kv : generic)
			{
				CHECK(mine[i].parse != kv.second);
			}
		}
	}
	CHECK_MESSAGE(mine[expectedRows].token == nullptr, name << ": the port has more rows than RW's " << expectedRows);
}

std::vector<std::string> rwNameList(const PeImage &pe, std::uint32_t va, size_t n)
{
	std::vector<std::string> out;
	for (size_t i = 0; i < n; ++i)
	{
		out.push_back(pe.cstring(pe.u32At(va + (std::uint32_t)(4 * i))));
	}
	return out;
}
} // namespace

TEST_CASE("skirmish ai data: the field tables and name lists are the binary's")
{
	const PeImage *pe = PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("skirmish ai data: binary tables (set RW_GAME_DAT)");
		return;
	}
	// RW 0xC13C18 has 29 rows and NO terminating row (the next word is the float 240.0f at RW 0xC13DE8): an unknown field in a SkirmishAIData block
	// makes retail read a token pointer 0x43700000 (a crash); the port reports the unknown field (stop S-411)
	compareTable(*pe, 0xC13C18, SkirmishAIStore::skirmishAIDataFields(), "SkirmishAIData", 29);
	compareTable(*pe, 0xC138EC, SkirmishAIStore::combatChainFields(), "CombatChainDefinition", 3);
	compareTable(*pe, 0xC13944, SkirmishAIStore::brutalCheatsFields(), "BrutalDifficultyCheats", 2);
	compareTable(*pe, 0xC13850, SkirmishAIStore::difficultyTuningFields(), "DifficultyTuning", 5);
	compareTable(*pe, 0xC52B40, SkirmishAIStore::armyDefinitionFields(), "ArmyDefinition", 47);
	compareTable(*pe, 0xC52538, SkirmishAIStore::armyMemberFields(), "ArmyMemberDefinition", 4);
	compareTable(*pe, 0xC52588, SkirmishAIStore::templateNameFields(), "AIEconomyAssigment", 1);
	compareTable(*pe, 0xC52450, SkirmishAIStore::aiBaseFields(), "AIBase", 5);
	compareTable(*pe, 0xC138A8, SkirmishAIStore::dozerAssignmentFields(), "AIDozerAssignment", 2);
	compareTable(*pe, 0xBFBA64, SkirmishAIStore::playerAITypeFields(), "PlayerAIType", 1);

	std::vector<std::string> kinds, targets;
	for (int i = 0; SkirmishAI::TheAIKindOfNames[i]; ++i)
	{
		kinds.push_back(SkirmishAI::TheAIKindOfNames[i]);
	}
	for (int i = 0; SkirmishAI::TheAITargetNames[i]; ++i)
	{
		targets.push_back(SkirmishAI::TheAITargetNames[i]);
	}
	CHECK((kinds == rwNameList(*pe, 0xDB55C8, 17)));
	CHECK(pe->u32At(0xDB55C8 + 17 * 4) == 0u);
	CHECK((targets == rwNameList(*pe, 0xDA1208, 5)));
	CHECK(pe->u32At(0xDA1208 + 5 * 4) == 0u);
	// the block parsers' rows {name, proc}
	CHECK(pe->cstring(pe->u32At(0xDA0C74)) == "SkirmishAIData");
	CHECK(pe->u32At(0xDA0C78) == 0x6A9456u);
	CHECK(pe->cstring(pe->u32At(0xDA0C68)) == "AIDozerAssignment");
	CHECK(pe->cstring(pe->u32At(0xDADA78)) == "AIBase");
	CHECK(pe->cstring(pe->u32At(0xDADAA8)) == "ArmyDefinition");
	CHECK(pe->cstring(pe->u32At(0xD9EC30)) == "PlayerAIType");
}

TEST_CASE("skirmish ai data: defaults are the constructors' (RW 0x6A9807, 0x82FF49, 0x82F142, 0x6A9248)")
{
	SkirmishAIData d;
	CHECK(d.combatChains[0].unit == -1);
	CHECK(d.combatChains[16].targetTypes[16] == -1);
	CHECK(d.combatChains[3].targetPriorityModifiers[5] == 0.0f);
	CHECK(d.brutalCheats.buildCostReduction == 0.0f);
	CHECK(d.brutalCheats.buildTimeReduction == 0.1f);
	CHECK(d.teamIdleCheckRadius == 50.0f);
	CHECK(d.timeBetweenEnemyChangeHI == 240.0f);
	CHECK(d.defaultTargetThreatRadius == 300.0f);
	CHECK(d.logicFramesTillRetreatChecksStart == 2400);
	CHECK(d.logicFramesTillAISelfDestructs == 900);
	for (int i = 0; i < 4; ++i)
	{
		CHECK(d.difficultyTuning[i].difficulty == i);
		CHECK(d.difficultyTuning[i].economyMaxFarms == -1);
		CHECK(d.difficultyTuning[i].economyUpgrade.denominator == 1);
	}
	ArmyDefinition a;
	CHECK(a.fortressRebuildPriority == 1950.0f);
	CHECK(a.economyBuilderMinMoney == 300);
	CHECK(a.phaseDurationMidGame == 420.0f);
	CHECK(a.chanceForUnitsToUpgrade == 30.0f);
	CHECK(a.upgradeSciencePriorityImportantHigh == 250.0f);
	AIBaseTemplate b;
	CHECK(b.allowsArbitraryRotation);
}

TEST_CASE("skirmish ai data: store rules (army first wins, dozer last wins, a repeated base map is listed again)")
{
	Loader l;
	l.load("ArmyDefinition A\n Side = Men\n DefaultUnitPriority = 7\n ArmyMemberDefinition X\n  Unit = U1\n  PercentageOfArmyPhase1 = 30\n  PercentageOfArmyPhase2 = 0\n "
		   "End\n ArmyMemberDefinition Y\n  Unit = U2\n  PercentageOfArmyPhase1 = 10\n End\nEnd\n"
		   "ArmyDefinition B\n Side = Men\n DefaultUnitPriority = 9\nEnd\n"
		   "AIDozerAssignment D1\n Side = Men\n Unit = P1\nEnd\nAIDozerAssignment D2\n Side = Men\n Unit = P2\nEnd\n"
		   "AIBase B1\n Side = Men\n Map = \"M1\"\n GameMapToUseOn = \"<ANY>\"\nEnd\nAIBase B2\n Side = Men\n Map = M2\nEnd\nAIBase B3\n Side = Elves\n Map = \"M1\"\nEnd\n");
	const ArmyDefinition *army = l.store.findArmy("Men");
	REQUIRE(army != nullptr);
	CHECK(army->defaultUnitPriority == 7.0f);
	CHECK(l.store.armiesDiscarded() == 1);
	REQUIRE(army->members.size() == 2);
	// RW 0x82FD6C: phase 1 sums 40 -> scale 2.5; phase 2 sums 0 -> unchanged
	CHECK(army->members[0].percentageOfArmy[0] == 75.0f);
	CHECK(army->members[1].percentageOfArmy[0] == 25.0f);
	CHECK(army->members[0].percentageOfArmy[1] == 0.0f);
	REQUIRE(l.store.findDozer("Men") != nullptr);
	CHECK(*l.store.findDozer("Men") == "P2");
	// AsciiString parse keeps the quotes of a quoted token (parseAsciiString reads one token)
	const std::vector<NameKeyType> *men = l.store.basesOfSide("Men");
	REQUIRE(men != nullptr);
	CHECK(men->size() == 2);
	const std::vector<NameKeyType> *elves = l.store.basesOfSide("Elves");
	REQUIRE(elves != nullptr);
	REQUIRE(elves->size() == 1);
	CHECK((*elves)[0] == (*men)[0]);
	CHECK(l.store.basesDiscarded() == 1);
	CHECK(l.store.findBase((*men)[0])->side == "Men");
}

TEST_CASE("skirmish ai data: SkirmishAIData sub blocks land on the row their key names")
{
	Loader l;
	l.load("SkirmishAIData TheSkirmishAIData\n"
		   " CombatChainDefinition C\n  Unit = CAVALRY\n  TargetTypes = STRUCTURE PIKEMAN\n  TargetPriorityModifiers = 50.0 -1.0\n End\n"
		   " BrutalDifficultyCheats B\n  BuildCostReduction = 15%\n End\n"
		   " DifficultyTuning E\n  Difficulty = EASY\n  EconomyMaxFarms = 3\n  EconomyUpgradeProbability = 1 : 1050\n End\n"
		   " DifficultyTuning X\n  EconomyMaxFarms = 9\n End\n"
		   " MakeAllSkirmishSidesAIControlled = Yes\n AnyTypeTemplateDisabledSlots = 1 2\n AnyTypeTemplateDisabledSlots = 3\n"
		   "End\n");
	const SkirmishAIData &d = l.store.data();
	CHECK(d.combatChains[3].unit == 3);
	CHECK(d.combatChains[3].targetTypes[0] == 6);
	CHECK(d.combatChains[3].targetTypes[1] == 2);
	CHECK(d.combatChains[3].targetTypes[2] == -1);
	CHECK(d.combatChains[3].targetPriorityModifiers[1] == -1.0f);
	CHECK(d.brutalCheats.buildCostReduction == doctest::Approx(0.15f));
	CHECK(d.brutalCheats.buildTimeReduction == 0.1f); // the block's local default, not the old value
	CHECK(d.difficultyTuning[0].economyMaxFarms == 3);
	CHECK(d.difficultyTuning[0].economyUpgrade.numerator == 1);
	CHECK(d.difficultyTuning[0].economyUpgrade.denominator == 1050);
	// a block without Difficulty is the ctor's HARD (2)
	CHECK(d.difficultyTuning[2].economyMaxFarms == 9);
	CHECK(d.makeAllSkirmishSidesAIControlled);
	CHECK((d.anyTypeTemplateDisabledSlots == std::vector<int>{ 1, 2, 3 }));
}

TEST_CASE("skirmish ai data: rejections are the binary's")
{
	{
		Loader l;
		CHECK_THROWS_AS(l.load("SkirmishAIData T\n CombatChainDefinition C\n  Unit = DRAGON\n End\nEnd\n"), INIException); // RW 0x8ED54E
	}
	{
		Loader l;
		CHECK_THROWS_AS(l.load("SkirmishAIData T\n DifficultyTuning E\n  EconomyUpgradeProbability = 1 : 0\n End\nEnd\n"), INIException); // RW 0x6A92A2
	}
	{
		Loader l;
		CHECK_THROWS_AS(l.load("SkirmishAIData T\n DifficultyTuning E\n  Difficulty = easy\n End\nEnd\n"), INIException); // exact compare RW 0x406585
	}
	{
		Loader l;
		CHECK_THROWS_AS(l.load("ArmyDefinition A\n Side = Men\n TacticalAITargets = DEFENSIVE NOWHERE\nEnd\n"), INIException); // RW 0x6C6A97
	}
	{
		Loader l;
		CHECK_THROWS_AS(l.load("ArmyDefinition A\n Side = Men\n Banana = 1\nEnd\n"), INIException); // unknown field (initFromINI)
	}
	{
		Loader l;
		std::string many = "SkirmishAIData T\n CombatChainDefinition C\n  Unit = HERO\n  TargetTypes =";
		for (int i = 0; i < 18; ++i)
		{
			many += " INFANTRY";
		}
		many += "\n End\nEnd\n";
		CHECK_THROWS_AS(l.load(many), INIException); // RW 0x8ED5D7: an 18th entry
	}
	{
		// S-411: RW writes the row before the table for a chain without Unit; the port throws
		Loader l;
		try
		{
			l.load("SkirmishAIData T\n CombatChainDefinition C\n  TargetTypes = HERO\n End\nEnd\n");
			FAIL("expected the S-411 rejection");
		}
		catch (const INIException &e)
		{
			CHECK(std::string(e.what()).find("[S-411]") != std::string::npos);
		}
	}
}

TEST_CASE("skirmish ai data: PlayerAIType keeps definition order and overwrites a repeated name (RW 0x615871)")
{
	Loader l;
	l.load("PlayerAIType A\n LibraryMap = \"Libraries\\X\\X.map\"\nEnd\nPlayerAIType B\n LibraryMap = \"Y\"\nEnd\nPlayerAIType A\n LibraryMap = \"Z\"\nEnd\n");
	REQUIRE(l.store.playerAITypes().size() == 2);
	CHECK(l.store.playerAITypes()[0].name == "A");
	CHECK(l.store.playerAITypes()[0].libraryMap == "Z");
	CHECK(l.store.playerAITypes()[1].libraryMap == "Y");
}

TEST_CASE("skirmish ai data: the retail files parse completely")
{
	OPENBFME_REQUIRE_START(s);
	const SkirmishAIStore &ai = s->world->skirmishAI();
	CHECK(ai.dataBlockSeen());
	// 8 armies (no side repeated), 8 dozer blocks for 8 sides
	CHECK(ai.armies().size() == 8);
	CHECK(ai.armiesDiscarded() == 0);
	CHECK(ai.dozerAssignments().size() == 8);
	size_t baseEntries = 0;
	for (const char *side : { "Men", "Elves", "Dwarves", "Isengard", "Mordor", "Wild", "Angmar", "Arnor" })
	{
		INFO(side);
		const ArmyDefinition *army = ai.findArmy(side);
		REQUIRE(army != nullptr);
		CHECK(!army->members.empty());
		for (int phase = 0; phase < 3; ++phase)
		{
			float sum = 0.0f;
			for (const ArmyMemberDefinition &m : army->members)
			{
				sum += m.percentageOfArmy[phase];
			}
			CHECK(sum == doctest::Approx(100.0f).epsilon(0.001));
		}
		CHECK(ai.findDozer(side) != nullptr);
		const std::vector<NameKeyType> *bases = ai.basesOfSide(side);
		REQUIRE(bases != nullptr);
		baseEntries += bases->size();
	}
	CHECK(baseEntries == 104); // every AIBase block of the file is listed under its side
	CHECK(*ai.findDozer("Men") == "MenPorter");
	CHECK(*ai.findDozer("Dwarves") == "DwarvenPorter");
	const ArmyDefinition *men = ai.findArmy("Men");
	CHECK(men->economyTemplate == "GondorFarm");
	CHECK(men->wallNodeTemplate == "MenWallHubSmall");
	CHECK((men->tacticalAITargets == std::vector<int>{ 1, 0, 2, 3 }));
	CHECK((men->maxTeamsPerTarget == std::vector<int>{ 1, 2, 1, 1 }));
	CHECK(men->mustUseCommandPointPercentage[0] == doctest::Approx(0.9f));
	const SkirmishAIData &d = ai.data();
	CHECK(d.difficultyTuning[0].economyMaxFarms == 3);
	CHECK(d.difficultyTuning[0].economyUpgrade.denominator == 1050);
	CHECK(d.difficultyTuning[1].economyUpgrade.numerator == 10);
	CHECK(d.difficultyTuning[3].economyUpgrade.denominator == 1); // BRUTAL has no block: the ctor's row
	CHECK(d.brutalCheats.buildCostReduction == doctest::Approx(0.15f));
	CHECK(d.combatChains[11].unit == 11); // BATTLE_TOWER's chain (the block is named ArcherCombatChain)
	CHECK(!d.makeAllSkirmishSidesAIControlled);
	// PlayerAITypes.ini: 17 blocks with distinct names
	CHECK(ai.playerAITypes().size() == 17);
	REQUIRE(ai.findPlayerAIType("MordorSkirmishAI") != nullptr);
	CHECK(ai.findPlayerAIType("MordorSkirmishAI")->libraryMap == "Libraries\\AI_Men Of The West\\AI_Men Of The West.map");
	StateHasher h1, h2;
	ai.crc(h1);
	ai.crc(h2);
	CHECK(h1.value() == h2.value());
}

TEST_CASE("skirmish ai data: the store hash covers every parsed field and the layouts (each mutation moves it)")
{
	Loader l;
	l.load("SkirmishAIData TheSkirmishAIData\n DisableBaseBuilding = No\nEnd\n"
		   "ArmyDefinition A\n Side = Men\n PhaseDuration_Rush = 120\n ArmyMemberDefinition X\n  Unit = U1\n  PercentageOfArmyPhase1 = 30\n End\nEnd\n"
		   "AIDozerAssignment D1\n Side = Men\n Unit = P1\nEnd\n"
		   "AIBase B1\n Side = Men\n Map = M1\n GameMapToUseOn = \"<ANY>\"\nEnd\n"
		   "PlayerAIType T\n LibraryMap = \"L\"\nEnd\n");
	auto hash = [&] {
		StateHasher h;
		l.store.crc(h);
		return h.value();
	};
	auto moved = [&](auto mutate) {
		const std::uint32_t h0 = hash();
		mutate();
		return hash() != h0;
	};
	SkirmishAIData &d = l.store.data();
	ArmyDefinition &army = const_cast<ArmyDefinition &>(*l.store.findArmy("Men"));
	AIBaseTemplate &base = const_cast<AIBaseTemplate &>(*l.store.findBase((*l.store.basesOfSide("Men"))[0]));
	CHECK(moved([&] { d.disableBaseBuilding = true; }));
	CHECK(moved([&] { d.disableEconomyBuilding = true; }));
	CHECK(moved([&] { d.disableUnitBuilding = true; }));
	CHECK(moved([&] { d.disableScienceUpgrading = true; }));
	CHECK(moved([&] { d.disableUnitUpgrading = true; }));
	CHECK(moved([&] { d.disableTacticalAI = true; }));
	CHECK(moved([&] { d.disableTeamBuilding = true; }));
	CHECK(moved([&] { d.disableWallBuilding = true; }));
	CHECK(moved([&] { d.anyTypeTemplateDisabledSlots.push_back(2); }));
	CHECK(moved([&] { d.teamIdleCheckRadius = 51.0f; }));
	CHECK(moved([&] { d.timeBetweenEnemyChangeHI = 1.0f; }));
	CHECK(moved([&] { d.defaultTargetThreatRadius = 1.0f; }));
	CHECK(moved([&] { d.ringOwnershipBias = 7; }));
	CHECK(moved([&] { d.logicFramesTillAISelfDestructs = 7; }));
	CHECK(moved([&] { d.difficultyTuning[1].economyMaxFarms = 4; }));
	CHECK(moved([&] { d.combatChains[2].targetPriorityModifiers[3] = 2.0f; }));
	CHECK(moved([&] { army.phaseDurationRush = 121.0f; }));
	CHECK(moved([&] { army.phaseDurationMidGame = 1.0f; }));
	CHECK(moved([&] { army.defaultUnitPriority = 1.0f; }));
	CHECK(moved([&] { army.economyTemplate = "Farm"; }));
	CHECK(moved([&] { army.tacticalAITargets.push_back(0); }));
	CHECK(moved([&] { army.maxTeamsPerTarget.push_back(1); }));
	CHECK(moved([&] { army.percentToSave[1] = 5.0f; }));
	CHECK(moved([&] { army.heroBuildOrder.push_back("H"); }));
	CHECK(moved([&] { army.unitUpgradePriorityHigh = 1.0f; }));
	CHECK(moved([&] { army.members[0].percentageOfArmy[2] = 3.0f; }));
	CHECK(moved([&] { base.allowsArbitraryRotation = false; }));
	CHECK(moved([&] { base.playerPositions.push_back(1); }));
	CHECK(moved([&] { base.gameMapToUseOn = "x.map"; }));
	CHECK(moved([&] { base.structures.push_back(AIBaseStructure{}); }));
	CHECK(moved([&] { base.structures.back().priority = 9.0f; }));
	CHECK(moved([&] { base.structures.back().slot = 2; }));
	CHECK(moved([&] { base.structures.back().angle = 0.5f; }));
	CHECK(moved([&] { const_cast<PlayerAIType &>(l.store.playerAITypes()[0]).libraryMap = "M"; }));
}

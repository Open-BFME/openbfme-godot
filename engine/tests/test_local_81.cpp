// OpenBFME unit tests (fork issue 705): retail 2.01 script actions and conditions, exercised by synthetic data (no retail files).
// GPL-3.0.

#include "doctest.h"

#include "PathfindTestUtil.h"
#include "ProdTestUtil.h"
#include "GameLogic/AI/AIWorld.h"
#include "ScriptTestUtil.h"

#include "Common/BuildAssistant.h"
#include "Common/SpecialPower.h"
#include "Common/Thing/ThingFactory.h"
#include "GameClient/ControlBarCommands.h"
#include "GameClient/MapChunks.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/BuildAssistantSell.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/ObjectTypes.h"
#include "GameLogic/ScriptEngine/ScriptActions.h"
#include "GameLogic/ScriptEngine/ScriptConditions.h"

#include <cmath>
#include <vector>

using namespace prodtest;
using namespace scripttest;

namespace
{

const char kLocal81Ini[] =
	"SpecialPower SpecialAbilityL81Pos\n"
	"  Enum = SPECIAL_GENERAL_TARGETLESS\n"
	"  ReloadTime = 1000\n"
	"End\n"
	"SpecialPower SpecialAbilityL81Obj\n"
	"  Enum = SPECIAL_HERO_MODE\n"
	"  ReloadTime = 1000\n"
	"End\n"
	"CommandButton Command_L81Sell\n"
	"  Command = SELL\n"
	"End\n"
	"CommandButton Command_L81PosPower\n"
	"  Command = SPECIAL_POWER\n"
	"  SpecialPower = SpecialAbilityL81Pos\n"
	"  Options = NEED_TARGET_POS\n"
	"End\n"
	"CommandButton Command_L81ObjPower\n"
	"  Command = SPECIAL_POWER\n"
	"  SpecialPower = SpecialAbilityL81Obj\n"
	"  Options = NEED_TARGET_ENEMY_OBJECT\n"
	"End\n"
	"CommandSet L81SellSet\n"
	"  1 = Command_L81Sell\n"
	"End\n"
	"CommandSet L81HeroSet\n"
	"  1 = Command_L81PosPower\n"
	"  2 = Command_L81ObjPower\n"
	"End\n"
	"Object L81Fortress\n"
	"  KindOf = STRUCTURE SELECTABLE IMMOBILE\n"
	"  BuildCost = 1000\n"
	"  CommandSet = L81SellSet\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"End\n"
	"Object L81Factory\n"
	"  KindOf = STRUCTURE SELECTABLE IMMOBILE\n"
	"  BuildCost = 500\n"
	"  CommandSet = L81SellSet\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 300\n"
	"  End\n"
	"End\n"
	"Object L81OtherFactory\n"
	"  KindOf = STRUCTURE SELECTABLE IMMOBILE\n"
	"  BuildCost = 500\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 300\n"
	"  End\n"
	"End\n"
	"Object L81Hero\n"
	"  KindOf = HERO INFANTRY SELECTABLE\n"
	"  CommandSet = L81HeroSet\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 200\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  Behavior = SpecialPowerModule ModuleTag_SPPos\n"
	"    SpecialPowerTemplate = SpecialAbilityL81Pos\n"
	"    StartsPaused = No\n"
	"  End\n"
	"  Behavior = SpecialPowerModule ModuleTag_SPObj\n"
	"    SpecialPowerTemplate = SpecialAbilityL81Obj\n"
	"    StartsPaused = No\n"
	"  End\n"
	"End\n"
	"Object L81Transport\n"
	"  KindOf = STRUCTURE GARRISON SELECTABLE IMMOBILE\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 400\n"
	"  End\n"
	"  Behavior = GarrisonContain ModuleTag_Contain\n"
	"    ContainMax = 5\n"
	"  End\n"
	"End\n"
	"Object L81Shrub\n"
	"  KindOf = SHRUBBERY IMMOBILE\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 50\n"
	"  End\n"
	"End\n";

struct Local81Host : ScriptEngineHost
{
	ProdWorld &w;
	explicit Local81Host(ProdWorld &world) : w(world) {}
	Object *createObject(const ThingTemplate &tmpl, Team &team, const Coord3D &pos, float angle) override
	{
		Object *o = w.logic->newObject(&tmpl, &team, ObjectStatusMaskType{});
		if (o)
		{
			o->setPosition(&pos);
			o->setOrientation(angle);
			w.logic->partition().registerObject(*o);
		}
		return o;
	}
};

struct Local81Fx : ProdWorld
{
	SpecialPowerStore powers;
	SpecialPowerStore *savedPowers = nullptr;
	AIWorldConfig aiCfg;
	pathtest::SyntheticTerrain terrain{ 100, 100 };
	std::unique_ptr<AIWorld> ai;
	std::vector<ScriptList> lists = std::vector<ScriptList>(3);
	Local81Host host{ *this };
	Player *p0 = nullptr;
	Player *p1 = nullptr;

	Local81Fx()
	{
		aiCfg.pathfind = pathtest::testConfig();
		ai = std::make_unique<AIWorld>(*logic, aiCfg, w.fx.env.macros);
		ai->attach();
		ai->newMap(terrain);
		savedPowers = TheSpecialPowerStore;
		TheSpecialPowerStore = &powers;
		w.fx.env.blocks.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); });
		p0 = players.findPlayerWithName("Alice");
		p1 = players.findPlayerWithName("Bob");
		REQUIRE(p0 != nullptr);
		REQUIRE(p1 != nullptr);
		load(kLocal81Ini);
		logic->settings().buildRulesLoaded = true;
		logic->settings().bodyThresholdsLoaded = true;
		logic->settings().unitDamagedThreshold = 0.65f;
		logic->settings().unitReallyDamagedThreshold = 0.4f;
		logic->partition().setRegion(0.0f, 0.0f, 1000.0f, 1000.0f);
		logic->scriptEngine().setHost(&host);
	}
	~Local81Fx()
	{
		logic->reset();
		ai.reset();
		TheSpecialPowerStore = savedPowers;
	}

	Object *spawn(const char *tmpl, Team *team, float x, float y)
	{
		const ThingTemplate *tt = logic->things().findTemplate(tmpl);
		REQUIRE_MESSAGE(tt != nullptr, tmpl);
		Object *o = logic->newObject(tt, team, ObjectStatusMaskType{});
		REQUIRE(o != nullptr);
		Coord3D pos{ x, y, 0.0f };
		o->setPosition(&pos);
		logic->partition().registerObject(*o);
		return o;
	}

	void runOnce(std::unique_ptr<Script> s, int sideIdx = 1)
	{
		lists[(size_t)sideIdx].items.clear();
		lists[(size_t)sideIdx].items.push_back(item(std::move(s)));
		std::vector<ScriptEngine::SideScripts> sides(3);
		const char *names[] = { "", "Alice", "Bob" };
		for (size_t i = 0; i < 3; ++i)
		{
			sides[i].sideName = names[i];
			sides[i].lists.push_back(&lists[i]);
		}
		logic->scriptEngine().newGame(sides, nullptr, nullptr, false);
		logic->scriptEngine().update();
	}
	void execAction(ScriptActionRec a)
	{
		ScriptActions::execute(logic->scriptEngine(), a);
	}
};

int findNameInTable(const char *const *table, const char *name)
{
	for (int i = 0; table[i]; ++i)
	{
		if (std::strcmp(table[i], name) == 0)
		{
			return i;
		}
	}
	return -1;
}

int kindOfIndex(const char *name)
{
	return findNameInTable(TheKindOfNames, name);
}

} // namespace

TEST_CASE("LOCAL-81 CREATE_NAMED_ON_TEAM_AT_OBJECTTYPE creates named hero at nearest fortress (RW 0x7C5B3B)")
{
	Local81Fx fx;
	Team *teamA = fx.p0->getDefaultTeam();
	fx.spawn("L81Hero", teamA, 100.0f, 100.0f);
	fx.spawn("L81Fortress", teamA, 150.0f, 120.0f);
	fx.spawn("L81Fortress", teamA, 800.0f, 800.0f);

	fx.execAction(act("CREATE_NAMED_ON_TEAM_AT_OBJECTTYPE", {
		sp(14, "Ringheld"),
		sp(15, "L81Hero"),
		sp(3, "Alice/" + teamA->getName()),
		sp(15, "L81Fortress"),
	}));

	Object *hero = fx.logic->scriptEngine().getUnitNamed("Ringheld");
	REQUIRE(hero != nullptr);
	CHECK(hero->getTeam() == teamA);
	CHECK(hero->getPosition()->x == 150.0f);
	CHECK(hero->getPosition()->y == 120.0f);

	// RW 0x7C5B6E: calling again while Ringheld is alive must not create a second one
	int beforeCount = 0;
	for (Object *o = fx.logic->getFirstObject(); o; o = o->getNextObject())
	{
		++beforeCount;
	}
	fx.execAction(act("CREATE_NAMED_ON_TEAM_AT_OBJECTTYPE", {
		sp(14, "Ringheld"),
		sp(15, "L81Hero"),
		sp(3, "Alice/" + teamA->getName()),
		sp(15, "L81Fortress"),
	}));
	int afterCount = 0;
	for (Object *o = fx.logic->getFirstObject(); o; o = o->getNextObject())
	{
		++afterCount;
	}
	CHECK(afterCount == beforeCount);
}

TEST_CASE("LOCAL-81 TEAM_ENTER_NAMED orders team AI members to enter named target (RW 0x7BFA20)")
{
	Local81Fx fx;
	Team *teamA = fx.p0->getDefaultTeam();
	Object *unit = fx.spawn("L81Hero", teamA, 100.0f, 100.0f);
	Object *building = fx.spawn("L81Transport", teamA, 140.0f, 100.0f);
	building->setName("TargetBldg");

	REQUIRE(unit->getAIUpdateInterface() != nullptr);
	
	fx.execAction(act("TEAM_ENTER_NAMED", {
		sp(3, "Alice/" + teamA->getName()),
		sp(14, "TargetBldg"),
	}));

	// RW 0x7BFA20 -> RW 0x7724BE -> RW 0x66C5A4: AI received CMD_FROM_SCRIPT enter order targeting building
	CHECK(unit->getAIUpdateInterface()->lastCommandSource() == CMD_FROM_SCRIPT);
	CHECK(fx.logic->scriptEngine().stats().unportedActions.empty());
}

TEST_CASE("LOCAL-81 PLAYER_ENABLE_FACTORIES clears DISABLED_SCRIPT_DISABLED on matching template only (RW 0x7BBE76 / 0x6AC7C5)")
{
	Local81Fx fx;
	Object *f1 = fx.spawn("L81Factory", fx.p0->getDefaultTeam(), 100.0f, 100.0f);
	Object *f2 = fx.spawn("L81OtherFactory", fx.p0->getDefaultTeam(), 200.0f, 100.0f);
	Object *enemyF = fx.spawn("L81Factory", fx.p1->getDefaultTeam(), 300.0f, 100.0f);

	f1->setScriptStatus(1, true);
	f2->setScriptStatus(1, true);
	enemyF->setScriptStatus(1, true);
	REQUIRE(f1->isDisabledByType(DISABLED_SCRIPT_DISABLED));
	REQUIRE(f2->isDisabledByType(DISABLED_SCRIPT_DISABLED));
	REQUIRE(enemyF->isDisabledByType(DISABLED_SCRIPT_DISABLED));

	fx.execAction(act("PLAYER_ENABLE_FACTORIES", {
		sp(11, "Alice"),
		sp(15, "L81Factory"),
	}));

	CHECK_FALSE(f1->isDisabledByType(DISABLED_SCRIPT_DISABLED));
	CHECK(f2->isDisabledByType(DISABLED_SCRIPT_DISABLED));
	CHECK(enemyF->isDisabledByType(DISABLED_SCRIPT_DISABLED));
}

TEST_CASE("LOCAL-81 PLAYER_SELL_EVERYTHING queues player's sellable structures for sale (RW 0x7BBD3B / 0x6ABFF9)")
{
	Local81Fx fx;
	Object *fort = fx.spawn("L81Fortress", fx.p0->getDefaultTeam(), 100.0f, 100.0f);
	Object *enemyFort = fx.spawn("L81Fortress", fx.p1->getDefaultTeam(), 300.0f, 100.0f);

	REQUIRE_FALSE(fort->testStatus((unsigned)findNameInTable(TheObjectStatusNames, "SOLD")));
	REQUIRE_FALSE(enemyFort->testStatus((unsigned)findNameInTable(TheObjectStatusNames, "SOLD")));

	fx.execAction(act("PLAYER_SELL_EVERYTHING", {
		sp(11, "Alice"),
	}));

	CHECK(fort->testStatus((unsigned)findNameInTable(TheObjectStatusNames, "SOLD")));
	CHECK_FALSE(enemyFort->testStatus((unsigned)findNameInTable(TheObjectStatusNames, "SOLD")));
}

TEST_CASE("LOCAL-81 TEAM_SET_PLAYERS_NEAREST_UNIT_OF_TYPE_TO_REFERENCE picks nearest owned unit to team centre (RW 0x7C50BB)")
{
	Local81Fx fx;
	Team *teamA = fx.p0->getDefaultTeam();
	fx.spawn("L81Hero", teamA, 100.0f, 100.0f);
	Object *nearFort = fx.spawn("L81Fortress", teamA, 180.0f, 100.0f);
	fx.spawn("L81Fortress", teamA, 700.0f, 100.0f);
	fx.spawn("L81Fortress", fx.p1->getDefaultTeam(), 110.0f, 100.0f); // closer, but enemy

	fx.execAction(act("TEAM_SET_PLAYERS_NEAREST_UNIT_OF_TYPE_TO_REFERENCE", {
		sp(61, "L81Fortress"),
		sp(3, "Alice/" + teamA->getName()),
		sp(54, "InnAI"),
	}));

	Object *ref = fx.logic->scriptEngine().getUnitNamed("InnAI");
	CHECK(ref == nearFort);
}

TEST_CASE("LOCAL-81 NAMED_USE_COMMANDBUTTON_ON_NEAREST_KINDOF fires ready command button at nearest enemy KindOf (RW 0x7BE6EE)")
{
	Local81Fx fx;
	Object *hero = fx.spawn("L81Hero", fx.p0->getDefaultTeam(), 100.0f, 100.0f);
	hero->setName("RingHolder");
	fx.spawn("L81Shrub", fx.p0->getDefaultTeam(), 110.0f, 100.0f); // ally, must be skipped (RW 0xC1676C mask 4 = ENEMIES)
	fx.spawn("L81Shrub", fx.p1->getDefaultTeam(), 250.0f, 100.0f); // enemy

	const SpecialPowerTemplate *spPos = TheSpecialPowerStore->findSpecialPowerTemplate("SpecialAbilityL81Pos");
	REQUIRE(spPos != nullptr);
	SpecialPowerModuleInterface *mod = SpecialPowerModules::findModule(*hero, spPos);
	REQUIRE(mod != nullptr);
	REQUIRE(mod->isReady());

	const int shrubBit = kindOfIndex("SHRUBBERY");
	REQUIRE(shrubBit != -1);

	fx.execAction(act("NAMED_USE_COMMANDBUTTON_ON_NEAREST_KINDOF", {
		sp(14, "RingHolder"),
		sp(42, "Command_L81PosPower"),
		ip(27, shrubBit),
	}));

	// Firing the special power starts its reload cooldown (RW 0x7BE7FE -> Object::doCommandButtonAtPosition)
	CHECK_FALSE(mod->isReady());
}

TEST_CASE("LOCAL-81 SKIRMISH_SPECIAL_POWER_READY checks ready, non-disabled, built units of player (RW 0x7E9C33)")
{
	Local81Fx fx;
	Object *hero = fx.spawn("L81Hero", fx.p0->getDefaultTeam(), 100.0f, 100.0f);

	auto checkReady = [&]() -> bool {
		ScriptCondition c = cond("SKIRMISH_SPECIAL_POWER_READY", {
			sp(11, "Alice"),
			sp(31, "SpecialAbilityL81Pos"),
		});
		return ScriptConditions::evaluate(fx.logic->scriptEngine(), c);
	};

	// Ready by default (StartsPaused = No, ReloadTime = 1000)
	CHECK(checkReady());

	// Disabled object is skipped (RW 0x7E9D22 -> +0x1C8)
	hero->setDisabled(DISABLED_SCRIPT_DISABLED, (UnsignedInt)UPDATE_SLEEP_FOREVER);
	CHECK_FALSE(checkReady());
	hero->clearDisabled(DISABLED_SCRIPT_DISABLED);
	CHECK(checkReady());

	// Under construction object is skipped (RW 0x7E9D15 -> testStatus(2))
	hero->setStatus(OBJECT_STATUS_UNDER_CONSTRUCTION, true);
	CHECK_FALSE(checkReady());
	hero->setStatus(OBJECT_STATUS_UNDER_CONSTRUCTION, false);
	CHECK(checkReady());

	// Unknown power returns false
	ScriptCondition unknown = cond("SKIRMISH_SPECIAL_POWER_READY", {
		sp(11, "Alice"),
		sp(31, "SpecialAbilityDoesNotExist"),
	});
	CHECK_FALSE(ScriptConditions::evaluate(fx.logic->scriptEngine(), unknown));
}

TEST_CASE("LOCAL-81 REGION_CAMPS_SHOULD_UNPACK evaluates to false without unported warning (RW 0x7ED40B)")
{
	Local81Fx fx;
	ScriptCondition c = cond("REGION_CAMPS_SHOULD_UNPACK", {});
	CHECK_FALSE(ScriptConditions::evaluate(fx.logic->scriptEngine(), c));
	CHECK(fx.logic->scriptEngine().stats().unportedConditions.empty());
}

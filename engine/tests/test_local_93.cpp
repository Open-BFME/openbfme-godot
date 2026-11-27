// OpenBFME. GPL-3.0.
// Local 93 (fork issue 719): AIData ForceHordesToLowLOD (+0xBB: RW 0x86A104 / 0x86B169 / 0x8A1B9F) and
// UseLowLODTrees / LowLodTreeName / LowLodTreeNameNoGrab / LowLodTreeNameNoHarvest / LowLodTreeScale
// (+0xD8..+0xE8: RW 0x62DCE4).

#include "doctest.h"

#include "CombatTestUtil.h"
#include "Common/Player.h"
#include "Common/StateHash.h"
#include "ProdTestUtil.h"
#include "GameClient/MapHordeSpawn.h"
#include "GameClient/MapObjectDrawables.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDrawModules.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Contain/TransportContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "ObjectTestUtil.h"

namespace
{
std::uint32_t hashSettings(const GameLogicSettings &s)
{
	StateHasher h;
	s.crc(h);
	return h.value();
}

const char *const kProdLocal93Commands =
	"CommandButton Command_ConstructLocal93Soldier\n"
	"  Command = UNIT_BUILD\n"
	"  Object = Local93Soldier\n"
	"End\n"
	"CommandSet Local93BarracksSet\n"
	"  1 = Command_ConstructLocal93Soldier\n"
	"End\n";

struct Local93ProdWorld : combattest::CombatWorld
{
	CommandStore store;
	CommandStore *saved = nullptr;
	explicit Local93ProdWorld(const char *ini) : combattest::CombatWorld(ini), saved(TheCommandStore)
	{
		TheCommandStore = &store;
		store.setThingFactory(&w.things);
		w.fx.env.blocks.registerBlock("CommandButton", [](INI *i) { CommandStore::parseCommandButtonDefinitionGlobal(i); });
		w.fx.env.blocks.registerBlock("CommandSet", [](INI *i) { CommandStore::parseCommandSetDefinitionGlobal(i); });
		const std::string err = w.load(kProdLocal93Commands, INI_LOAD_OVERWRITE, "local93_commands.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		logic->productionSettings() = prodtest::defaultSettings();
	}
	~Local93ProdWorld() { TheCommandStore = saved; }
};

const char *const kProdLocal93Ini =
	"Object Local93Barracks\n"
	"  KindOf = STRUCTURE SELECTABLE\n"
	"  CommandSet = Local93BarracksSet\n"
	"  Body = ActiveBody BodyTag\n"
	"    MaxHealth = 1000\n"
	"  End\n"
	"  Behavior = ProductionUpdate ModuleTag_Prod\n"
	"    GiveNoXP = Yes\n"
	"  End\n"
	"  Behavior = QueueProductionExitUpdate ModuleTag_Exit\n"
	"    UnitCreatePoint = X:0.0 Y:0.0 Z:0.0\n"
	"    NaturalRallyPoint = X:40.0 Y:0.0 Z:0.0\n"
	"  End\n"
	"End\n"
	"Object Local93Soldier\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  BuildTime = 0.2\n"
	"  BuildCost = 10\n"
	"  TransportSlotCount = 1\n"
	"  Body = ActiveBody BodyTag\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"End\n"
	"Object Local93Hero\n"
	"  KindOf = INFANTRY HERO SELECTABLE\n"
	"  TransportSlotCount = 1\n"
	"  Body = ActiveBody BodyTag\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"End\n"
	"Object Local93Monster\n"
	"  KindOf = MONSTER SELECTABLE\n"
	"  TransportSlotCount = 1\n"
	"  Body = ActiveBody BodyTag\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"End\n"
	"Object Local93Horde\n"
	"  KindOf = HORDE SELECTABLE\n"
	"  Body = ImmortalBody BodyTag\n"
	"    MaxHealth = 1000\n"
	"  End\n"
	"  Behavior = HordeContain ModuleTag_Horde\n"
	"    ObjectStatusOfContained =\n"
	"    InitialPayload = Local93Soldier 1\n"
	"    InitialPayload = Local93Hero 1\n"
	"    InitialPayload = Local93Monster 1\n"
	"    Slots = 5\n"
	"    PassengerFilter = NONE +INFANTRY +MONSTER\n"
	"    RankInfo = RankNumber:1 UnitType:Local93Soldier Position:X:0 Y:0\n"
	"  End\n"
	"End\n"
	"Object Local93Transport\n"
	"  KindOf = VEHICLE SELECTABLE\n"
	"  Body = ActiveBody BodyTag\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"  Behavior = TransportContain ModuleTag_Trans\n"
	"    InitialPayload = Local93Soldier 1\n"
	"    Slots = 2\n"
	"    PassengerFilter = NONE +INFANTRY\n"
	"  End\n"
	"End\n";

const char *const kTreesLocal93Ini =
	"Object Local93TreeFull\n"
	"  KindOf = TREE IMMOBILE\n"
	"  IsGrabbable = Yes\n"
	"  IsHarvestable = Yes\n"
	"  Scale = 1.5\n"
	"End\n"
	"Object Local93TreeNoGrab\n"
	"  KindOf = TREE IMMOBILE\n"
	"  IsGrabbable = No\n"
	"  IsHarvestable = Yes\n"
	"  Scale = 1.4\n"
	"End\n"
	"Object Local93TreeNoHarvest\n"
	"  KindOf = TREE IMMOBILE\n"
	"  IsGrabbable = No\n"
	"  IsHarvestable = No\n"
	"  Scale = 1.3\n"
	"End\n"
	"Object Local93LowTreeFull\n"
	"  KindOf = TREE IMMOBILE\n"
	"  Scale = 1.0\n"
	"End\n"
	"Object Local93LowTreeNoGrab\n"
	"  KindOf = TREE IMMOBILE\n"
	"  Scale = 1.0\n"
	"End\n"
	"Object Local93LowTreeNoHarvest\n"
	"  KindOf = TREE IMMOBILE\n"
	"  Scale = 1.0\n"
	"End\n";

struct Local93MapWorld : objtest::World
{
	Local93MapWorld()
	{
		W3DDrawModules::registerTypedDrawModuleData(modules);
		MapHordeSpawn::bindHordeContainData(modules);
	}
};

WorldHeightMap flatHeightMap(int w, int h, int raw = 256)
{
	WorldHeightMap m;
	m.m_width = w;
	m.m_height = h;
	m.m_borderSize = 0;
	m.m_dataSize = w * h;
	m.m_data.assign((size_t)w * h, (std::uint16_t)raw);
	return m;
}
} // namespace

TEST_CASE("LOCAL-93 AIData ForceHordesToLowLOD and LowLodTree* defaults, parse, and crc (RW 0x6FFBEF)")
{
	GameLogicSettings def;
	// RW 0x6FFBEF constructor defaults: +0xBB = 1, +0xD8 = 0, +0xDC..+0xE4 = "", +0xE8 = 0.55f (RW 0xBDAE10)
	CHECK(def.forceHordesToLowLOD == true);
	CHECK(def.useLowLODTrees == false);
	CHECK(def.lowLodTreeName.empty());
	CHECK(def.lowLodTreeNameNoGrab.empty());
	CHECK(def.lowLodTreeNameNoHarvest.empty());
	CHECK(def.lowLodTreeScale == 0.55f);

	const std::uint32_t hDefault = hashSettings(def);

	GameLogicSettings a;
	std::string err;
	const char *iniA =
		"AIData\n"
		"  ForceHordesToLowLOD = No\n"
		"  UseLowLODTrees = Yes\n"
		"  LowLodTreeName = TreeF03\n"
		"  LowLodTreeNameNoGrab = GenericHarvestOnlyOptimizedTreeA\n"
		"  LowLodTreeNameNoHarvest = PTStump02A\n"
		"  LowLodTreeScale = 1.2\n"
		"End\n";
	REQUIRE(GameLogicSettingsLoader::scanAIData(iniA, a, &err));
	CHECK(err.empty());
	CHECK(a.forceHordesToLowLOD == false);
	CHECK(a.useLowLODTrees == true);
	CHECK(a.lowLodTreeName == "TreeF03");
	CHECK(a.lowLodTreeNameNoGrab == "GenericHarvestOnlyOptimizedTreeA");
	CHECK(a.lowLodTreeNameNoHarvest == "PTStump02A");
	CHECK(a.lowLodTreeScale == 1.2f);
	CHECK(a.unappliedFields.count("AIData.ForceHordesToLowLOD") == 0);
	CHECK(a.unappliedFields.count("AIData.UseLowLODTrees") == 0);
	CHECK(a.unappliedFields.count("AIData.LowLodTreeName") == 0);
	CHECK(a.unappliedFields.count("AIData.LowLodTreeNameNoGrab") == 0);
	CHECK(a.unappliedFields.count("AIData.LowLodTreeNameNoHarvest") == 0);
	CHECK(a.unappliedFields.count("AIData.LowLodTreeScale") == 0);
	CHECK(hashSettings(a) != hDefault);
}

TEST_CASE("LOCAL-93 ForceHordesToLowLOD sets CREATE_DRAWABLE_WITH_LOW_DETAIL on produced units (RW 0x8A1B9F)")
{
	auto run = [](bool forceLowLod) -> bool {
		Local93ProdWorld w(kProdLocal93Ini);
		w.logic->settings().forceHordesToLowLOD = forceLowLod;
		Object *barracks = w.unit("Local93Barracks", 'A', 50.0f, 50.0f);
		REQUIRE(barracks != nullptr);
		barracks->getControllingPlayer()->getMoney()->deposit(500);
		auto *pu = dynamic_cast<ProductionUpdate *>(barracks->findModule("ProductionUpdate"));
		REQUIRE(pu != nullptr);
		const ThingTemplate *soldierTmpl = w.logic->things().findTemplate("Local93Soldier");
		REQUIRE(soldierTmpl != nullptr);
		REQUIRE(pu->queueCreateUnit(soldierTmpl, -1, pu->requestUniqueUnitID(), -1, false, std::string(), false));
		for (int i = 0; i < 5; ++i) w.logic->runLogicFrame();
		Object *produced = nullptr;
		for (Object *o = w.logic->getFirstObject(); o; o = o->getNextObject())
		{
			if (o && !o->isDestroyed() && o->getTemplate()->getName() == "Local93Soldier")
			{
				produced = o;
				break;
			}
		}
		REQUIRE(produced != nullptr);
		return produced->testStatus(OBJECT_STATUS_CREATE_DRAWABLE_WITH_LOW_DETAIL);
	};

	// RW 0x8A25DC .. 0x8A25F4: AIData +0xBB sets bit 55 CREATE_DRAWABLE_WITH_LOW_DETAIL
	CHECK(run(true) == true);
	CHECK(run(false) == false);
}

TEST_CASE("LOCAL-93 ForceHordesToLowLOD on horde payload skips HERO, MONSTER, and non-horde transports (RW 0x86A104)")
{
	struct Result
	{
		bool hordeSoldierLowLod = false;
		bool hordeHeroLowLod = false;
		bool hordeMonsterLowLod = false;
		bool transportSoldierLowLod = false;
	};
	auto run = [](bool forceLowLod) -> Result {
		Local93ProdWorld w(kProdLocal93Ini);
		w.logic->settings().forceHordesToLowLOD = forceLowLod;

		Object *horde = w.unit("Local93Horde", 'A', 50.0f, 50.0f);
		REQUIRE(horde != nullptr);
		Object *transport = w.unit("Local93Transport", 'A', 150.0f, 50.0f);
		REQUIRE(transport != nullptr);

		Result r;
		ContainModuleInterface *hc = horde->getContain();
		REQUIRE(hc != nullptr);
		REQUIRE(hc->getContainedItemsList() != nullptr);
		for (Object *m : *hc->getContainedItemsList())
		{
			const std::string &n = m->getTemplate()->getName();
			if (n == "Local93Soldier") r.hordeSoldierLowLod = m->testStatus(OBJECT_STATUS_CREATE_DRAWABLE_WITH_LOW_DETAIL);
			else if (n == "Local93Hero") r.hordeHeroLowLod = m->testStatus(OBJECT_STATUS_CREATE_DRAWABLE_WITH_LOW_DETAIL);
			else if (n == "Local93Monster") r.hordeMonsterLowLod = m->testStatus(OBJECT_STATUS_CREATE_DRAWABLE_WITH_LOW_DETAIL);
		}
		ContainModuleInterface *tc = transport->getContain();
		REQUIRE(tc != nullptr);
		REQUIRE(tc->getContainedItemsList() != nullptr);
		REQUIRE(tc->getContainedItemsList()->size() == 1);
		r.transportSoldierLowLod = tc->getContainedItemsList()->front()->testStatus(OBJECT_STATUS_CREATE_DRAWABLE_WITH_LOW_DETAIL);
		return r;
	};

	const Result on = run(true);
	CHECK(on.hordeSoldierLowLod == true);      // RW 0x86A151: HordeContain + non-HERO + non-MONSTER + ForceHordesToLowLOD
	CHECK(on.hordeHeroLowLod == false);        // RW 0x86A137: KindOf HERO (+0x113 bit 2) excluded
	CHECK(on.hordeMonsterLowLod == false);     // RW 0x86A140: KindOf MONSTER (+0x109 bit 2) excluded
	CHECK(on.transportSoldierLowLod == false); // RW 0x86A11F: non-HordeContain excluded

	const Result off = run(false);
	CHECK(off.hordeSoldierLowLod == false);
}

TEST_CASE("LOCAL-93 UseLowLODTrees and LowLodTreeName* / LowLodTreeScale substitute TREE map objects at low LOD (RW 0x62DCE4)")
{
	Local93MapWorld world;
	REQUIRE(world.load(kTreesLocal93Ini).empty());

	LoadedMap map;
	map.heightMap = flatHeightMap(32, 32);
	auto addObj = [&](const char *name, float x, float y) {
		MapObject o;
		o.m_objectName = name;
		o.m_location = { x, y, 0.0f };
		map.chunks.objects.push_back(o);
	};
	addObj("Local93TreeFull", 40.0f, 40.0f);
	addObj("Local93TreeNoGrab", 80.0f, 40.0f);
	addObj("Local93TreeNoHarvest", 120.0f, 40.0f);

	TerrainLogic terrain;
	terrain.init(map.heightMap, map.chunks, nullptr);

	MapObjectOptions opts;
	opts.useTrees = true;
	opts.useLowLODTrees = true;
	opts.staticLODLevel = 0; // RW 0x62DD99: <= 0 enables low-LOD trees
	opts.lowLodTreeName = "Local93LowTreeFull";
	opts.lowLodTreeNameNoGrab = "Local93LowTreeNoGrab";
	opts.lowLodTreeNameNoHarvest = "Local93LowTreeNoHarvest";
	opts.lowLodTreeScale = 1.2f;

	// Case 1: active low-LOD substitution (RW 0x62E3E7 .. 0x62E433)
	MapObjectDrawables lowOut;
	MapObjectCreation::build(map, "local93", world.things, terrain, opts, lowOut);
	REQUIRE(lowOut.drawables.size() == 3);
	CHECK(lowOut.drawables[0].info->name == "Local93LowTreeFull");      // RW 0x62E406 (grabbable && harvestable)
	CHECK(lowOut.drawables[1].info->name == "Local93LowTreeNoGrab");    // RW 0x62E416 (!grabbable && harvestable)
	CHECK(lowOut.drawables[2].info->name == "Local93LowTreeNoHarvest"); // RW 0x62E41B (!harvestable)
	CHECK(lowOut.drawables[0].scale == 1.2f);          // RW 0x62E426
	CHECK(lowOut.drawables[1].scale == 1.2f);
	CHECK(lowOut.drawables[2].scale == 1.2f);

	// Case 2: high LOD (staticLODLevel > 0) keeps original templates and scales (RW 0x62DD99)
	opts.staticLODLevel = 1;
	MapObjectDrawables highOut;
	MapObjectCreation::build(map, "local93", world.things, terrain, opts, highOut);
	REQUIRE(highOut.drawables.size() == 3);
	CHECK(highOut.drawables[0].info->name == "Local93TreeFull");
	CHECK(highOut.drawables[1].info->name == "Local93TreeNoGrab");
	CHECK(highOut.drawables[2].info->name == "Local93TreeNoHarvest");
	CHECK(highOut.drawables[0].scale == 1.5f);
	CHECK(highOut.drawables[1].scale == 1.4f);
	CHECK(highOut.drawables[2].scale == 1.3f);

	// Case 3: UseLowLODTrees = false keeps originals even at staticLODLevel = 0 (RW 0x62DD8B)
	opts.staticLODLevel = 0;
	opts.useLowLODTrees = false;
	MapObjectDrawables offOut;
	MapObjectCreation::build(map, "local93", world.things, terrain, opts, offOut);
	REQUIRE(offOut.drawables.size() == 3);
	CHECK(offOut.drawables[0].info->name == "Local93TreeFull");

	// Case 4: missing one of the three low-LOD templates disables substitution for all (RW 0x62DD39 / 0x62DD5C)
	opts.useLowLODTrees = true;
	opts.lowLodTreeNameNoHarvest = "DoesNotExistTemplate";
	MapObjectDrawables missOut;
	MapObjectCreation::build(map, "local93", world.things, terrain, opts, missOut);
	REQUIRE(missOut.drawables.size() == 3);
	CHECK(missOut.drawables[0].info->name == "Local93TreeFull");
	CHECK(missOut.drawables[1].info->name == "Local93TreeNoGrab");
	CHECK(missOut.drawables[2].info->name == "Local93TreeNoHarvest");
}

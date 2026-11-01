// OpenBFME unit tests: lane MODS-3, the engine features of mods that change the game program (Common/ModEngineFeatures.h). GPL-3.0.

#include "doctest.h"
#include "EconTestUtil.h"

#include "Common/ModEngineFeatures.h"
#include "Common/Player.h"
#include "Common/PlayerTemplate.h"
#include "Common/Upgrade.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/Module/LogicModules.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/SpawnBehavior.h"
#include "GameLogic/Module/UpgradeModuleClasses.h"
#include "GameLogic/Object/Object.h"
#include "Common/Thing/KindOfTokens.h"

#include <memory>
#include <string>
#include <vector>

namespace
{
// every test leaves the process retail
struct Retail
{
	~Retail() { ModEngineFeatures::reset(); }
};
} // namespace

TEST_CASE("MODS-3: a known modified game binary turns its profile's features on; an unknown one is an error and turns nothing on")
{
	Retail guard;
	CHECK_FALSE(ModEngineFeatures::has(ModEngineFeature::EdainUnchainedHerobarKindOf));
	CHECK(ModEngineFeatures::identityFeatures().empty());

	std::string error;
	REQUIRE_MESSAGE(ModEngineFeatures::activate({ "5C31F5036CACB7690A6161B104A3EFDB" }, &error), error); // Edain Unchained 4.4.3's game.dat (any case)
	CHECK(ModEngineFeatures::has(ModEngineFeature::EdainUnchainedHerobarKindOf));
	CHECK(ModEngineFeatures::activeProfiles() == std::vector<std::string>{ "Edain Unchained 4.4.3 game.dat" });
	const std::vector<std::string> ids = ModEngineFeatures::identityFeatures();
	REQUIRE(ids.size() == (size_t)ModEngineFeature::Count);
	CHECK(ids.front() == "mod-engine:edain-unchained.herobar-kindof");

	// Patch 2.02's game binary: known, nothing to re-implement for the simulation (its changes are install checks and frame pacing)
	REQUIRE(ModEngineFeatures::activate({ "7a4068f87ea5c3b98e03ae4e83e2b806" }, &error));
	CHECK_FALSE(ModEngineFeatures::has(ModEngineFeature::EdainUnchainedHerobarKindOf)); // activate replaces the set
	CHECK(ModEngineFeatures::identityFeatures().empty());

	CHECK_FALSE(ModEngineFeatures::activate({ "5c31f5036cacb7690a6161b104a3efdb", "00000000000000000000000000000000" }, &error));
	CHECK(error == "this mod changes the game program in a way OpenBFME does not know yet (unknown game binary 00000000000000000000000000000000): its "
				   "engine changes would be missing");
	CHECK(ModEngineFeatures::activeProfiles().empty()); // a failure leaves every feature off (retail), the earlier set included
	CHECK_FALSE(ModEngineFeatures::has(ModEngineFeature::EdainUnchainedHerobarKindOf));
}

TEST_CASE("MODS-3: edain-unchained.herobar-kindof: HEROBAR is KindOf bit 222 and HEROBAR_GROUP bit 223 only with the feature")
{
	Retail guard;
	CHECK(KindOfTokens::indexOf("HEROBAR") == -1); // retail 2.01: 222 names
	CHECK(KindOfTokens::indexOf("HORDE_MONSTER") == 221);
	std::string error;
	REQUIRE(ModEngineFeatures::activate({ "5c31f5036cacb7690a6161b104a3efdb" }, &error));
	CHECK(KindOfTokens::indexOf("HEROBAR") == 222);
	CHECK(KindOfTokens::indexOf("herobar_group") == 223);
	CHECK(KindOfTokens::indexOf("HORDE_MONSTER") == 221);
	int n = 0;
	while (KindOfTokens::names()[n])
	{
		++n;
	}
	CHECK(n == 224);
}

namespace
{
const char *kCommandSetObjects =
	"Upgrade Upgrade_A\n  Type = OBJECT\nEnd\n"
	"Upgrade Upgrade_B\n  Type = OBJECT\nEnd\n"
	"Upgrade Upgrade_Level2\n  Type = OBJECT\nEnd\n"
	"Object Castle\n"
	"  KindOf = SELECTABLE STRUCTURE\n"
	"  CommandSet = BaseSet\n"
	"  Behavior = CommandSetUpgrade ModuleTag_Level2\n"
	"    TriggeredBy = Upgrade_Level2\n"
	"    CommandSet = Level2Set\n"
	"  End\n"
	"  Behavior = CommandSetUpgrade ModuleTag_Buttons\n"
	"    TriggeredBy = Upgrade_A\n"
	"    CommandButtons Btn_X:0 Btn_Y NoSuchButton\n"
	"  End\n"
	"  Behavior = CommandSetUpgrade ModuleTag_More\n"
	"    TriggeredBy = Upgrade_B\n"
	"    CommandButtons = Btn_Z:40\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 10\n"
	"  End\n"
	"End\n";
const char *kCommandSets =
	"CommandButton Btn_A\nEnd\nCommandButton Btn_B\nEnd\nCommandButton Btn_C\nEnd\nCommandButton Btn_L\nEnd\n"
	"CommandButton Btn_X\nEnd\nCommandButton Btn_Y\nEnd\nCommandButton Btn_Z\nEnd\n"
	"CommandSet BaseSet\n  1 = Btn_A\n  2 = Btn_B\n  40 = Btn_C\n  InitialVisible = 6\nEnd\n"
	"CommandSet Level2Set\n  1 = Btn_L\nEnd\n";

struct CommandFx
{
	Retail guard;
	CommandStore store;
	CommandStore *saved = TheCommandStore;
	std::unique_ptr<econtest::Fx> fx;
	explicit CommandFx(bool edain)
	{
		std::string error;
		if (edain)
		{
			REQUIRE(ModEngineFeatures::activate({ "5c31f5036cacb7690a6161b104a3efdb" }, &error));
		}
		TheCommandStore = &store;
	}
	void load()
	{
		fx = std::make_unique<econtest::Fx>(kCommandSetObjects);
		store.setThingFactory(&fx->w.things);
		fx->w.fx.env.blocks.registerBlock("CommandButton", [](INI *ini) { CommandStore::parseCommandButtonDefinitionGlobal(ini); });
		fx->w.fx.env.blocks.registerBlock("CommandSet", [](INI *ini) { CommandStore::parseCommandSetDefinitionGlobal(ini); });
		const std::string err = fx->w.load(kCommandSets);
		REQUIRE_MESSAGE(err.empty(), err);
	}
	~CommandFx() { TheCommandStore = saved; }
};

std::vector<std::string> slots(const CommandSet *set, int n)
{
	std::vector<std::string> out;
	for (int i = 0; i < n; ++i)
	{
		const CommandButton *b = set->getCommandButton(i);
		out.push_back(b ? b->m_name : "-");
	}
	return out;
}
} // namespace

TEST_CASE("MODS-3: edain-unchained.commandset-limit: CommandSet keys 34 .. 64 and the 64 InitialVisible default only with the feature")
{
	{
		CommandFx f(false);
		CHECK(CommandSet::slotCapacity() == 33);
		f.fx = std::make_unique<econtest::Fx>("Object Plain\nEnd\n");
		f.store.setThingFactory(&f.fx->w.things);
		f.fx->w.fx.env.blocks.registerBlock("CommandButton", [](INI *ini) { CommandStore::parseCommandButtonDefinitionGlobal(ini); });
		f.fx->w.fx.env.blocks.registerBlock("CommandSet", [](INI *ini) { CommandStore::parseCommandSetDefinitionGlobal(ini); });
		CHECK(f.fx->w.load("CommandButton Btn_A\nEnd\nCommandSet S\n  34 = Btn_A\nEnd\n") != ""); // retail 2.01: "34" is no field of RW 0xC4F3D8
		REQUIRE(f.fx->w.load("CommandSet T\n  33 = Btn_A\nEnd\n") == "");
		CHECK(f.store.findCommandSet("T")->m_initialVisible == 33);
	}
	CommandFx f(true);
	CHECK(CommandSet::slotCapacity() == 64);
	f.load();
	const CommandSet *base = f.store.findCommandSet("BaseSet");
	REQUIRE(base);
	CHECK(base->getCommandButton(39)->m_name == "Btn_C");
	CHECK(base->m_initialVisible == 6);
	CHECK(f.store.findCommandSet("Level2Set")->m_initialVisible == 64); // its ctor: push 0x40 (RW 0x80C97F)
}

TEST_CASE("MODS-3: edain-unchained.commandset-button-upgrade: CommandButtons merge into the template's (or the last active CommandSet's) set")
{
	CommandFx f(true);
	f.load();
	econtest::Fx &x = *f.fx;
	Object *o = x.make("Castle", x.teamOf("Alice"));
	CHECK(o->getCommandSetName() == "BaseSet");
	o->giveUpgrade(TheUpgradeCenter->findUpgrade("Upgrade_A"));
	CHECK(o->getCommandSetName() == "BaseSet+Btn_X:0 Btn_Y NoSuchButton");
	const CommandSet *merged = f.store.findCommandSet(o->getCommandSetName());
	REQUIRE(merged);
	// :0 is the 0-based slot 0 (INI key "1", overwritten); Btn_Y takes the first empty slot; the base's slot 39 is copied; count = highest + 1
	CHECK(slots(merged, 3) == std::vector<std::string>{ "Btn_X", "Btn_B", "Btn_Y" });
	CHECK(merged->getCommandButton(39)->m_name == "Btn_C");
	CHECK(merged->m_initialVisible == 40);
	REQUIRE(f.store.mergeNotes().size() == 1);
	CHECK(f.store.mergeNotes()[0].find("'NoSuchButton', which is not a CommandButton") != std::string::npos);

	o->giveUpgrade(TheUpgradeCenter->findUpgrade("Upgrade_Level2")); // a CommandSet module replaces the base; the lists stay
	CHECK(o->getCommandSetName() == "Level2Set+Btn_X:0 Btn_Y NoSuchButton");
	CHECK(slots(f.store.findCommandSet(o->getCommandSetName()), 3) == std::vector<std::string>{ "Btn_X", "Btn_Y", "-" });
	o->giveUpgrade(TheUpgradeCenter->findUpgrade("Upgrade_B"));
	CHECK(o->getCommandSetName() == "Level2Set+Btn_X:0 Btn_Y NoSuchButton+Btn_Z:40");
	CHECK(f.store.findCommandSet(o->getCommandSetName())->getCommandButton(40)->m_name == "Btn_Z");

	o->removeUpgrade(TheUpgradeCenter->findUpgrade("Upgrade_A"));
	o->updateUpgradeModules();
	CHECK(o->getCommandSetName() == "Level2Set+Btn_Z:40");
	o->removeUpgrade(TheUpgradeCenter->findUpgrade("Upgrade_B"));
	o->updateUpgradeModules();
	CHECK(o->getCommandSetName() == "Level2Set"); // no active list: the last active CommandSet module's set
	o->removeUpgrade(TheUpgradeCenter->findUpgrade("Upgrade_Level2"));
	o->updateUpgradeModules();
	// RW quirk (as the retail CommandSetUpgrade test): the removal clears the executed flag before the removal hook (its 0xEDB0E6, retail's RW 0x8B7CFF)
	// tests it, and no other module is active to rebuild: the last set stays
	CHECK(o->getCommandSetName() == "Level2Set");
}

TEST_CASE("MODS-3: without edain-unchained.commandset-button-upgrade CommandButtons is no field of CommandSetUpgrade")
{
	CommandFx f(false);
	f.fx = std::make_unique<econtest::Fx>("Upgrade Upgrade_A\n  Type = OBJECT\nEnd\n");
	CHECK(f.fx->w.load("Object Castle\n  Behavior = CommandSetUpgrade M\n    TriggeredBy = Upgrade_A\n    CommandButtons Btn_X:0\n  End\nEnd\n") != "");
}

namespace
{
const char *kSpawners =
	"Object Walker\n  KindOf = INFANTRY\n  Body = ActiveBody B\n    MaxHealth = 10\n  End\nEnd\n"
	"Object TwoSpawner\n"
	"  KindOf = STRUCTURE\n"
	"  Behavior = SpawnBehavior Spawn1\n    SpawnNumber = 1\n    SpawnReplaceDelay = 1000\n    SpawnTemplateName = Walker\n  End\n"
	"  Behavior = SpawnBehavior Spawn2\n    SpawnNumber = 1\n    SpawnReplaceDelay = 1000\n    SpawnTemplateName = Walker\n  End\n"
	"  Body = ActiveBody B\n    MaxHealth = 10\n  End\n"
	"End\n";
} // namespace

TEST_CASE("MODS-3: edain-unchained.spawn-union: the spawn interface of an object with two SpawnBehaviors reaches both (retail: the first only)")
{
	for (const bool edain : { false, true })
	{
		CAPTURE(edain);
		Retail guard;
		std::string error;
		if (edain)
		{
			REQUIRE(ModEngineFeatures::activate({ "5c31f5036cacb7690a6161b104a3efdb" }, &error));
		}
		econtest::Fx f(kSpawners);
		Object *o = f.make("TwoSpawner", f.teamOf("Alice"));
		auto *s1 = dynamic_cast<SpawnBehaviorInterface *>(o->findModuleByTag(f.w.keys.nameToKey("Spawn1")));
		auto *s2 = dynamic_cast<SpawnBehaviorInterface *>(o->findModuleByTag(f.w.keys.nameToKey("Spawn2")));
		REQUIRE(s1);
		REQUIRE(s2);
		REQUIRE(s1->isSpawnerActive());
		REQUIRE(s2->isSpawnerActive());
		SpawnBehaviorInterface *s = SpawnBehaviorInterface::of(*o);
		REQUIRE(s);
		CHECK((s == s1) == !edain); // the union is a proxy (its 0xEDC404 vtable), not a module
		s->stopSpawning();
		CHECK_FALSE(s1->isSpawnerActive());
		CHECK(s2->isSpawnerActive() == !edain);
		CHECK_FALSE(s->isSpawnerActive());
	}
}

TEST_CASE("MODS-3: edain-unchained.object-image-upgrade: ObjectImageUpgrade sets the object's portrait / button image, newest first, kept on removal")
{
	const char *objects =
		"Upgrade Upgrade_A\n  Type = OBJECT\nEnd\n"
		"Upgrade Upgrade_B\n  Type = OBJECT\nEnd\n"
		"Object Hero\n"
		"  KindOf = SELECTABLE INFANTRY HERO\n"
		"  SelectPortrait = HPHero\n"
		"  ButtonImage = HIHero\n"
		"  Behavior = ObjectImageUpgrade Image1\n    TriggeredBy = Upgrade_A\n    SelectPortrait = HPHero_Armor\n    ButtonImage = HIHero_Armor\n  End\n"
		"  Behavior = ObjectImageUpgrade Image2\n    TriggeredBy = Upgrade_B\n    SelectPortrait = HPHero_King\n  End\n"
		"  Body = ActiveBody B\n    MaxHealth = 10\n  End\n"
		"End\n";
	{
		Retail guard;
		logictest::LogicWorld w(RandomAlgorithm::ZH_CarryChain, nullptr);
		LogicModules::registerAll(w.w.modules);
		CHECK(w.w.load(objects) != ""); // retail 2.01: no such module
	}
	Retail guard;
	std::string error;
	REQUIRE(ModEngineFeatures::activate({ "5c31f5036cacb7690a6161b104a3efdb" }, &error));
	econtest::Fx f(objects);
	Object *o = f.make("Hero", f.teamOf("Alice"));
	CHECK(ObjectImageUpgrade::imageOf(*o, true).empty());
	o->giveUpgrade(TheUpgradeCenter->findUpgrade("Upgrade_A"));
	CHECK(ObjectImageUpgrade::imageOf(*o, true) == "HPHero_Armor");
	CHECK(ObjectImageUpgrade::imageOf(*o, false) == "HIHero_Armor");
	f.frames(1);
	o->giveUpgrade(TheUpgradeCenter->findUpgrade("Upgrade_B"));
	CHECK(ObjectImageUpgrade::imageOf(*o, true) == "HPHero_King");  // the newest
	CHECK(ObjectImageUpgrade::imageOf(*o, false) == "HIHero_Armor"); // an empty name leaves the earlier override
	o->removeUpgrade(TheUpgradeCenter->findUpgrade("Upgrade_B"));
	o->updateUpgradeModules();
	CHECK(ObjectImageUpgrade::imageOf(*o, true) == "HPHero_King"); // removal is `ret` (0xEF3330)
}

TEST_CASE("MODS-3: edain-unchained.spell-store-upgrade: PurchaseScienceCommandSetUpgrade gives the spell store of the first completed upgrade, game-wide")
{
	Retail guard;
	std::string error;
	REQUIRE(ModEngineFeatures::activate({ "5c31f5036cacb7690a6161b104a3efdb" }, &error));
	const char *templates =
		"PlayerTemplate FactionArnor\n"
		"  Side = Arnor\n"
		"  PurchaseScienceCommandSetUpgrade = Upgrade_Missing NoSuchStore\n"
		"  PurchaseScienceCommandSetUpgrade = Upgrade_Arnor WrongStore\n"
		"  PurchaseScienceCommandSetUpgrade = Upgrade_Arnor ArnorStore\n" // the same upgrade: the set is replaced
		"  PurchaseScienceCommandSetUpgrade = Upgrade_Lonely\n"             // one token: ignored
		"End\n"
		"PlayerTemplate FactionAngmar\n"
		"  Side = Angmar\n"
		"  PurchaseScienceCommandSetUpgrade = Upgrade_Angmar AngmarStore\n" // another template's line joins the same list
		"End\n";
	econtest::Fx f("Upgrade Upgrade_Arnor\n  Type = PLAYER\nEnd\nUpgrade Upgrade_Angmar\n  Type = PLAYER\nEnd\nUpgrade Upgrade_Missing\n  Type = PLAYER\nEnd\n", templates);
	CHECK(f.templates.purchaseScienceSetUpgrades() == std::vector<std::pair<std::string, std::string>>{
		{ "Upgrade_Missing", "NoSuchStore" }, { "Upgrade_Arnor", "ArnorStore" }, { "Upgrade_Angmar", "AngmarStore" } });
	CommandStore store;
	CommandStore *saved = TheCommandStore;
	TheCommandStore = &store;
	f.w.fx.env.blocks.registerBlock("CommandButton", [](INI *ini) { CommandStore::parseCommandButtonDefinitionGlobal(ini); });
	f.w.fx.env.blocks.registerBlock("CommandSet", [](INI *ini) { CommandStore::parseCommandSetDefinitionGlobal(ini); });
	REQUIRE(f.w.load("CommandSet ArnorStore\nEnd\nCommandSet AngmarStore\nEnd\n") == "");
	Player *alice = f.player("Alice");
	REQUIRE(alice);
	const PlayerTemplate *pt = alice->getPlayerTemplate();
	REQUIRE(pt);
	const std::string stock = alice->science().mode().skirmishOrMultiplayer ? pt->m_purchaseScienceCommandSetMP : pt->m_purchaseScienceCommandSet;
	CHECK(f.templates.purchaseScienceCommandSetFor(*alice, &store) == stock);
	alice->addCompletedUpgrade("Upgrade_Missing"); // its set does not exist: the next entry
	CHECK(f.templates.purchaseScienceCommandSetFor(*alice, &store) == stock);
	alice->addCompletedUpgrade("Upgrade_Angmar");
	CHECK(f.templates.purchaseScienceCommandSetFor(*alice, &store) == "AngmarStore");
	alice->addCompletedUpgrade("Upgrade_Arnor"); // earlier in the list: the first hit wins
	CHECK(f.templates.purchaseScienceCommandSetFor(*alice, &store) == "ArnorStore");
	CHECK(f.templates.purchaseScienceCommandSetFor(*alice, &store, true) == stock); // the purchasable indicator's stock RW 0x71F933
	TheCommandStore = saved;
}

TEST_CASE("MODS-3: edain-unchained.unique-production-id: one id counter for every building, the game's (reset, saved and hashed with it)")
{
	const char *objects = "Object Barracks\n  KindOf = STRUCTURE\n  Behavior = ProductionUpdate P\n  End\n  Body = ActiveBody B\n    MaxHealth = 10\n  End\nEnd\n";
	for (const bool edain : { false, true })
	{
		CAPTURE(edain);
		Retail guard;
		std::string error;
		if (edain)
		{
			REQUIRE(ModEngineFeatures::activate({ "5c31f5036cacb7690a6161b104a3efdb" }, &error));
		}
		econtest::Fx f(objects);
		Object *a = f.make("Barracks", f.teamOf("Alice"));
		Object *b = f.make("Barracks", f.teamOf("Alice"));
		REQUIRE(a->getProductionUpdate());
		REQUIRE(b->getProductionUpdate());
		const std::uint32_t h0 = f.hash();
		CHECK(a->getProductionUpdate()->requestUniqueUnitID() == 1);
		CHECK(b->getProductionUpdate()->requestUniqueUnitID() == (edain ? 2u : 1u)); // retail: each building's own post-increment (RW 0x8A18FA)
		CHECK(a->getProductionUpdate()->requestUniqueUnitID() == (edain ? 3u : 2u));
		CHECK(f.hash() != h0);
	}
}

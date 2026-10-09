// OpenBFME. GPL-3.0.
// Lane UPGRADE-1 on the retail data (SKIPs when ROTWK_INSTALL / BFME2_INSTALL are unset): the UpgradeCenter of the real INI load, the unit upgrades of
// the seven factions (armour, blades, arrows) on live objects, a building level upgrade, the CostModifierUpgrade factor of the build cost, determinism.

#include "doctest.h"

#include "Common/BuildAssistant.h"
#include "Common/PlayerList.h"
#include "Common/StateHash.h"
#include "Common/Team.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Economy.h"
#include "GameLogic/EconomySettings.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/UpgradeModuleClasses.h"
#include "GameLogic/Module/UpgradeModules.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/UpgradeCommands.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameClient/LiveGame.h"
#include "GameClient/MapCreationHooks.h"
#include "GameClient/MapObjectDrawables.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"
#include "RetailTestMount.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <vector>

namespace
{
struct Shared
{
	retailtest::Mount *mount = nullptr;
	std::unique_ptr<RetailObjectWorld> world;
	std::string error;
};

Shared &shared()
{
	static Shared s;
	static bool built = false;
	if (!built)
	{
		built = true;
		s.mount = retailtest::pureMount();
		if (s.mount && s.mount->fs)
		{
			s.world = std::make_unique<RetailObjectWorld>(*s.mount->fs);
			if (!s.world->load(&s.error))
			{
				s.world.reset();
			}
		}
	}
	return s;
}

#define REQUIRE_RETAIL_WORLD(sh)                                                    \
	Shared &sh = shared();                                                          \
	if (!sh.mount || !sh.mount->fs)                                                 \
	{                                                                               \
		MESSAGE("SKIP: ROTWK_INSTALL / BFME2_INSTALL not set (retail upgrade test)"); \
		return;                                                                     \
	}                                                                               \
	REQUIRE_MESSAGE(sh.world, sh.error);                                            \
	auto contextScope = sh.world->enterContext()

int nameIndex(const char *const *names, const char *name)
{
	for (int i = 0; names[i]; ++i)
	{
		if (std::strcmp(names[i], name) == 0)
		{
			return i;
		}
	}
	return -1;
}

// a skirmish game of one player of the faction on the shared world
struct Game
{
	TeamFactory teams;
	PlayerList players;
	std::unique_ptr<GameLogic> logic;
	Player *player = nullptr;
	explicit Game(RetailObjectWorld &world, retailtest::Mount &mount, const std::string &faction)
		: players(world.nameKeys(), world.playerTemplates(), teams)
	{
		SkirmishSetup setup;
		setup.players.push_back({ "Tester", faction, true, 0, 0, 0 });
		setup.startingMoney = 100000;
		setup.defaultStartingCash = 100000;
		REQUIRE(players.setupSkirmish(setup).empty());
		logic = std::make_unique<GameLogic>(world.things(), world.modules(), players, RandomAlgorithm::ZH_CarryChain);
		logic->random().seedRandom(1);
		std::string err;
		REQUIRE_MESSAGE(GameLogicSettingsLoader::load(*mount.fs, logic->settings(), &err), err);
		logic->productionSettings() = world.productionSettings();
		logic->setUpgradeTypes(&world.upgradeTypes());
		REQUIRE_MESSAGE(EconomySettings::load(*mount.fs, logic->economy().settings(), &err), err);
		logic->economy().initAllCommandPoints();
		player = players.findPlayerWithName("Tester");
		REQUIRE(player);
	}
	Object *make(RetailObjectWorld &world, const std::string &name)
	{
		const ThingTemplate *tt = world.things().findTemplate(name);
		REQUIRE_MESSAGE(tt, name);
		Object *o = logic->newObject(tt, player->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE(o);
		return o;
	}
	std::uint32_t hash() const
	{
		return logic->computeStateHash();
	}
};

// the templates whose behaviour list has a module of class `cls` triggered by `upgrade`
std::vector<const ThingTemplate *> triggeredTemplates(RetailObjectWorld &world, const char *cls, const UpgradeTemplate *upgrade, size_t limit)
{
	std::vector<const ThingTemplate *> out;
	for (const ThingTemplate *t : world.things().templates())
	{
		for (const ThingTemplate::Nugget &n : t->getFinalOverride()->behaviorModules().nuggets())
		{
			const UpgradeModuleData *d = dynamic_cast<const UpgradeModuleData *>(n.data.get());
			if (n.name == cls && d && d->m_activationMask.test((unsigned)upgrade->getMaskBit()))
			{
				out.push_back(t);
				break;
			}
		}
		if (out.size() >= limit)
		{
			break;
		}
	}
	return out;
}

struct FactionUpgrades
{
	const char *faction, *armor, *blades, *arrows, *armorUnit;
};
// retail facts (the census lists, workspace/rebuild/census): each faction's armour / blade / arrow research and a unit it upgrades
const FactionUpgrades kFactions[] = {
	{ "FactionMen", "Upgrade_GondorHeavyArmor", "Upgrade_GondorForgedBlades", "Upgrade_GondorFireArrows", "GondorFighter" },
	{ "FactionElves", "Upgrade_ElvenHeavyArmor", "Upgrade_ElvenForgedBlades", "Upgrade_ElvenSilverthornArrows", nullptr },
	{ "FactionDwarves", "Upgrade_DwarvenMithrilMail", "Upgrade_DwarvenForgedBlades", "Upgrade_DwarvenFireArrows", nullptr },
	{ "FactionIsengard", "Upgrade_IsengardHeavyArmor", "Upgrade_IsengardForgedBlades", "Upgrade_IsengardFireArrows", nullptr },
	{ "FactionMordor", "Upgrade_MordorHeavyArmor", "Upgrade_MordorForgedBlades", "Upgrade_MordorFireArrows", nullptr },
	{ "FactionWild", "Upgrade_WildHeavyArmor", "Upgrade_WildForgedBlades", "Upgrade_WildFireArrows", nullptr },
	{ "FactionAngmar", "Upgrade_AngmarDarkIronArmor", "Upgrade_AngmarDarkIronBlades", "Upgrade_AngmarIceArrows", nullptr },
};
} // namespace

TEST_CASE("upgrade retail: TheUpgradeCenter holds every Upgrade block of the INI load (the Create-A-Hero include too) and the veterancy upgrades")
{
	REQUIRE_RETAIL_WORLD(sh);
	const UpgradeCenter &c = sh.world->upgrades();
	MESSAGE("upgrades: " << c.size());
	CHECK(c.size() == 1055); // 3 veterancy + 506 blocks of default\upgrade.ini / upgrade.ini (DefaultUpgrade included) + 546 of createaheroupgrades.inc
	CHECK(c.findUpgrade("Upgrade_Veterancy_HEROIC")->getMaskBit() == 2);
	CHECK(c.findUpgrade("DefaultUpgrade")->getMaskBit() == 3);
	CHECK(sh.world->upgradeTypes().size() == c.size());
	for (const SubsystemLoadReport::FileError &e : sh.world->report().errors)
	{
		CHECK_MESSAGE(e.file != "UpgradeCenter", e.message);
	}
	// the two blocks without Type take DefaultUpgrade's PLAYER (RW 0x66FC5C copies it)
	CHECK(c.findUpgrade("Upgrade_FireArrows")->getUpgradeType() == UPGRADE_TYPE_PLAYER);
	CHECK(c.findUpgrade("Upgrade_HeavyArmor")->getUpgradeType() == UPGRADE_TYPE_PLAYER);
	for (const FactionUpgrades &f : kFactions)
	{
		for (const char *u : { f.armor, f.blades, f.arrows })
		{
			const UpgradeTemplate *t = c.findUpgrade(u);
			REQUIRE_MESSAGE(t, u);
			MESSAGE(u << ": type " << t->getUpgradeType() << ", cost " << t->getBuildCost() << ", time " << t->getBuildTimeSeconds() << " s");
			CHECK(t->getUpgradeType() == UPGRADE_TYPE_OBJECT); // RotWK: each horde buys its own armour / blades / arrows (the forge's technology is the PLAYER upgrade)
			CHECK(t->getBuildCost() > 0);
			CHECK(t->getBuildTimeSeconds() > 0.0f);
		}
	}
	CHECK(c.findUpgrade("Upgrade_TechnologyGondorHeavyArmor")->getUpgradeType() == UPGRADE_TYPE_PLAYER);
	CHECK(c.findUpgrade("Upgrade_GondorHeavyArmor")->getBuildCost() == 300); // GONDOR_PERSONAL_HEAVY_ARMOR_BUILDCOST
	CHECK(c.findUpgrade("Upgrade_GondorHeavyArmor")->getBuildTimeSeconds() == doctest::Approx(10.0f));
}

TEST_CASE("upgrade retail: every faction's armour, blade and arrow research reaches its units (armour set, weapon set, model condition)")
{
	REQUIRE_RETAIL_WORLD(sh);
	const UpgradeCenter &c = sh.world->upgrades();
	const int playerUpgradeArmor = nameIndex(TheArmorSetNames, "PLAYER_UPGRADE");
	REQUIRE(playerUpgradeArmor >= 0);
	for (const FactionUpgrades &f : kFactions)
	{
		CAPTURE(f.faction);
		Game g(*sh.world, *sh.mount, f.faction);
		const UpgradeTemplate *armor = c.findUpgrade(f.armor);
		const UpgradeTemplate *blades = c.findUpgrade(f.blades);
		const UpgradeTemplate *arrows = c.findUpgrade(f.arrows);
		const auto armorUnits = triggeredTemplates(*sh.world, "ArmorUpgrade", armor, 4);
		auto bladeUnits = triggeredTemplates(*sh.world, "WeaponSetUpgrade", blades, 4);
		if (bladeUnits.empty())
		{
			bladeUnits = triggeredTemplates(*sh.world, "SubObjectsUpgrade", blades, 4);
		}
		const auto arrowUnits = triggeredTemplates(*sh.world, "WeaponSetUpgrade", arrows, 4);
		MESSAGE(f.faction << ": " << armorUnits.size() << " armour, " << bladeUnits.size() << " blade, " << arrowUnits.size() << " arrow templates (first 4)");
		CHECK(!armorUnits.empty());
		CHECK(!arrowUnits.empty());
		std::vector<Object *> armored, bladed, arrowed;
		for (const ThingTemplate *t : armorUnits)
		{
			armored.push_back(g.make(*sh.world, t->getName()));
		}
		for (const ThingTemplate *t : bladeUnits)
		{
			bladed.push_back(g.make(*sh.world, t->getName()));
		}
		for (const ThingTemplate *t : arrowUnits)
		{
			arrowed.push_back(g.make(*sh.world, t->getName()));
		}
		for (Object *o : armored)
		{
			CHECK_FALSE(o->getUpgradeMask().test((unsigned)armor->getMaskBit()));
		}
		// the unit's own research completes: Object::giveUpgrade (RW 0x693817) -> updateUpgradeModules
		const std::uint32_t before = g.hash();
		auto executed = [](Object *o) {
			int n = 0;
			for (const auto &m : o->modules())
			{
				if (UpgradeMux *mux = m->getUpgrade())
				{
					n += mux->isAlreadyUpgraded();
				}
			}
			return n;
		};
		int armorHits = 0, bladeHits = 0, arrowHits = 0;
		for (Object *o : armored)
		{
			const int n0 = executed(o);
			o->giveUpgrade(armor);
			armorHits += executed(o) > n0;
		}
		CHECK(g.hash() != before);
		for (Object *o : bladed)
		{
			const int n0 = executed(o);
			o->giveUpgrade(blades);
			bladeHits += executed(o) > n0;
		}
		for (Object *o : arrowed)
		{
			const int n0 = executed(o);
			o->giveUpgrade(arrows);
			arrowHits += executed(o) > n0;
		}
		MESSAGE(f.faction << ": upgrade modules ran on " << armorHits << " armour, " << bladeHits << " blade, " << arrowHits << " arrow objects");
		CHECK(armorHits > 0);
		CHECK(bladeHits > 0);
		CHECK(arrowHits > 0);
		if (f.armorUnit)
		{
			// Gondor soldiers: heavy armour sets PLAYER_UPGRADE on the armour set and ARMORSET_PLAYER_UPGRADE on the model; forged blades the weapon set
			g.player->addUpgrade(armor, Player::UPGRADE_STATUS_COMPLETE); // a player-held bit reaches objects made later: initObject (RW 0x693D38)
			g.player->addUpgrade(blades, Player::UPGRADE_STATUS_COMPLETE);
			Object *s = g.make(*sh.world, f.armorUnit);
			CHECK((s->armorSetFlags() & (1u << playerUpgradeArmor)) != 0);
			CHECK(s->testModelCondition(ArmorUpgrade::modelConditionOfArmorSetFlag(playerUpgradeArmor)));
			REQUIRE(s->getWeapons());
			CHECK(s->getWeapons()->weaponSetFlags()[0] + s->getWeapons()->weaponSetFlags()[1] + s->getWeapons()->weaponSetFlags()[2] + s->getWeapons()->weaponSetFlags()[3] != 0);
			// removing the research takes the armour away again (RW 0x6AE60C -> 0x691438 -> 0x8D2688)
			g.player->removeUpgrade(armor);
			CHECK((s->armorSetFlags() & (1u << playerUpgradeArmor)) == 0);
		}
	}
	const UpgradeModuleClasses::Stats &st = UpgradeModuleClasses::stats();
	MESSAGE("S-484 counts: fades " << st.subObjectFadesNotPorted << ", texture swaps " << st.textureSwapsNotPorted << ", recolours " << st.houseRecolorsNotPorted);
}

TEST_CASE("upgrade retail: a building level upgrade (OBJECT) runs the building's own upgrade modules; the logic report carries S-480 .. S-485")
{
	REQUIRE_RETAIL_WORLD(sh);
	const UpgradeCenter &c = sh.world->upgrades();
	Game g(*sh.world, *sh.mount, "FactionMen");
	Object *barracks = g.make(*sh.world, "GondorBarracks");
	const UpgradeTemplate *level2 = c.findUpgrade("Upgrade_GondorBarracksLevel2");
	REQUIRE(level2);
	CHECK(level2->getUpgradeType() == UPGRADE_TYPE_OBJECT);
	int executedBefore = 0, executedAfter = 0;
	for (const auto &m : barracks->modules())
	{
		if (UpgradeMux *mux = m->getUpgrade())
		{
			executedBefore += mux->isAlreadyUpgraded();
		}
	}
	const std::uint32_t before = g.hash();
	barracks->giveUpgrade(level2);
	CHECK(barracks->hasUpgrade(level2));
	CHECK(g.hash() != before);
	for (const auto &m : barracks->modules())
	{
		if (UpgradeMux *mux = m->getUpgrade())
		{
			executedAfter += mux->isAlreadyUpgraded();
		}
	}
	MESSAGE("GondorBarracks level 2: " << executedBefore << " -> " << executedAfter << " executed upgrade modules, command set '" << barracks->getCommandSetName() << "'");
	CHECK(executedAfter > executedBefore);
	const GameLogic::Report r = g.logic->report();
	for (const char *id : { "[S-480]", "[S-481]", "[S-482]", "[S-483]", "[S-484]", "[S-485]", "[S-486]" })
	{
		bool found = false;
		for (const std::string &s : r.stops)
		{
			found = found || s.rfind(id, 0) == 0;
		}
		CHECK_MESSAGE(found, id);
	}
}

TEST_CASE("upgrade retail: the same research on two games gives the same state hash (determinism)")
{
	REQUIRE_RETAIL_WORLD(sh);
	const UpgradeCenter &c = sh.world->upgrades();
	std::uint32_t hashes[2] = {};
	for (int run = 0; run < 2; ++run)
	{
		Game g(*sh.world, *sh.mount, "FactionMen");
		g.make(*sh.world, "GondorFighter");
		Object *b = g.make(*sh.world, "GondorBarracks");
		g.player->addUpgrade(c.findUpgrade("Upgrade_GondorHeavyArmor"), Player::UPGRADE_STATUS_COMPLETE);
		g.player->addUpgrade(c.findUpgrade("Upgrade_GondorForgedBlades"), Player::UPGRADE_STATUS_IN_PRODUCTION);
		b->giveUpgrade(c.findUpgrade("Upgrade_GondorBarracksLevel2"));
		for (int i = 0; i < 10; ++i)
		{
			g.logic->runLogicFrame();
		}
		hashes[run] = g.hash();
	}
	CHECK(hashes[0] == hashes[1]);
}

TEST_CASE("upgrade retail: research through the production queue (forge technology PLAYER upgrade, horde OBJECT upgrade): cost, time, cancel, the effect on the members")
{
	REQUIRE_RETAIL_WORLD(sh);
	const UpgradeCenter &c = sh.world->upgrades();
	Game g(*sh.world, *sh.mount, "FactionMen");
	g.logic->setFrameAdvance(true);
	const UpgradeTemplate *tech = c.findUpgrade("Upgrade_TechnologyGondorHeavyArmor");
	const UpgradeTemplate *armor = c.findUpgrade("Upgrade_GondorHeavyArmor");
	REQUIRE(tech);
	REQUIRE(armor);
	Object *forge = g.make(*sh.world, "GondorForge");
	ProductionUpdateInterface *fpu = forge->getProductionUpdate();
	REQUIRE(fpu);
	const int techCost = tech->calcCostToBuild(g.player, forge);
	const int techFrames = tech->calcTimeToBuild(g.player);
	MESSAGE("Upgrade_TechnologyGondorHeavyArmor: cost " << techCost << ", " << techFrames << " frames");
	CHECK(techCost == tech->getBuildCost());
	CHECK(techFrames == SimMath::cvttss2si(SimMath::sseMul(5.0f, tech->getBuildTimeSeconds())));
	// queue and cancel: the stored cost comes back, the in-production record goes
	const std::uint32_t money0 = g.player->getMoney()->countMoney();
	CHECK(fpu->canQueueUpgrade(tech) == 0);
	REQUIRE(fpu->queueUpgrade(tech));
	CHECK(g.player->getMoney()->countMoney() == money0 - (std::uint32_t)techCost);
	CHECK(g.player->hasUpgradeInProduction(tech));
	CHECK(fpu->isUpgradeInQueue(tech));
	CHECK_FALSE(fpu->queueUpgrade(tech)); // already queued / in production
	fpu->cancelUpgrade(tech);
	CHECK(g.player->getMoney()->countMoney() == money0);
	CHECK_FALSE(g.player->hasUpgradeInProduction(tech));
	CHECK(fpu->getProductionCount() == 0);
	// research to completion: the upgrade completes on the frame its progress reaches 100 %
	REQUIRE(fpu->queueUpgrade(tech));
	int frames = 0;
	while (!g.player->hasUpgradeComplete(tech) && frames < techFrames + 20)
	{
		g.logic->runLogicFrame();
		++frames;
	}
	MESSAGE("technology complete after " << frames << " logic frames");
	CHECK(g.player->hasUpgradeComplete(tech));
	CHECK(frames >= techFrames);
	CHECK(frames <= techFrames + 2);
	CHECK(fpu->getProductionCount() == 0);

	// the horde buys its own heavy armour (OBJECT): the horde object is affected through its members, and the members get the armour set
	Object *horde = g.make(*sh.world, "GondorFighterHorde");
	ContainModuleInterface *contain = horde->getContain();
	REQUIRE(contain);
	REQUIRE(contain->getContainedItemsList());
	const size_t members = contain->getContainedItemsList()->size();
	MESSAGE("GondorFighterHorde: " << members << " members");
	REQUIRE(members > 0);
	CHECK(horde->affectedByUpgrade(armor));
	ProductionUpdateInterface *hpu = horde->getProductionUpdate();
	REQUIRE(hpu);
	const int armorCost = armor->calcCostToBuild(g.player, horde);
	const std::uint32_t money1 = g.player->getMoney()->countMoney();
	REQUIRE(hpu->queueUpgrade(armor));
	CHECK(g.player->getMoney()->countMoney() == money1 - (std::uint32_t)armorCost);
	CHECK(horde->getUpgradeCostPaid() == doctest::Approx((float)armorCost));
	const int armorFrames = armor->calcTimeToBuild(g.player);
	for (int i = 0; i < armorFrames + 2; ++i)
	{
		g.logic->runLogicFrame();
	}
	CHECK(horde->hasUpgrade(armor));
	CHECK(hpu->getProductionCount() == 0);
	// lane FX-2: the completion called the horde upgrade's UpgradeFX (FX_PorterDeliverHeavyArmor) on the horde (RW 0x8A223B); the technology has none
	REQUIRE(g.logic->fxEvents().perSite().count("UpgradeFX"));
	CHECK(g.logic->fxEvents().perSite().at("UpgradeFX") == 1);
	CHECK(armor->m_upgradeFX == "FX_PorterDeliverHeavyArmor");
	CHECK(tech->m_upgradeFX.empty());
	const int playerUpgradeArmor = nameIndex(TheArmorSetNames, "PLAYER_UPGRADE");
	int armored = 0;
	for (Object *m : *contain->getContainedItemsList())
	{
		CHECK(m->hasUpgrade(armor));
		armored += (m->armorSetFlags() & (1u << playerUpgradeArmor)) != 0;
	}
	MESSAGE("members with the PLAYER_UPGRADE armour set: " << armored << " of " << members);
	CHECK(armored == (int)members);
	CHECK_FALSE(horde->affectedByUpgrade(armor)); // nothing left to upgrade
	CHECK_FALSE(hpu->queueUpgrade(armor));        // the horde has it

	// the command path: MSG_QUEUE_UPGRADE with the selection group and the mask bit in argument 1 (RW 0x77A6FD), MSG_CANCEL_UPGRADE with it in argument 0
	const UpgradeTemplate *blades = c.findUpgrade("Upgrade_TechnologyGondorForgedBlades");
	REQUIRE(blades);
	CommandList list;
	GameLogicDispatch dispatch(*g.logic);
	dispatch.attach(list);
	UpgradeCommands::registerHandlers(dispatch);
	const int pi = g.player->getPlayerIndex();
	GameMessage sel(MSG_CREATE_SELECTED_GROUP, pi);
	sel.appendBooleanArgument(true);
	sel.appendObjectIDArgument(forge->getID());
	list.append(sel);
	GameMessage q(MSG_QUEUE_UPGRADE, pi);
	q.appendObjectIDArgument(forge->getID());
	q.appendIntegerArgument(blades->getMaskBit());
	list.append(q);
	g.logic->runLogicFrame();
	CHECK(fpu->isUpgradeInQueue(blades));
	CHECK(g.player->hasUpgradeInProduction(blades));
	GameMessage cm(MSG_CANCEL_UPGRADE, pi);
	cm.appendIntegerArgument(blades->getMaskBit());
	list.append(cm);
	g.logic->runLogicFrame();
	CHECK_FALSE(fpu->isUpgradeInQueue(blades));
	CHECK_FALSE(g.player->hasUpgradeInProduction(blades));
	CHECK(dispatch.errors().empty());
}

TEST_CASE("upgrade retail: a live object's OnCreated handler grants its upgrade through ObjectGrantUpgrade (a Mordor mountain troll switches to rock throwing)")
{
	REQUIRE_RETAIL_WORLD(sh);
	MapObjectOptions options;
	std::string err;
	REQUIRE_MESSAGE(MapObjectGameData::load(*sh.mount->fs, options, &err), err);
	REQUIRE_MESSAGE(MapObjectGameData::loadPlayerTemplates(*sh.mount->fs, options, &err), err);
	REQUIRE_MESSAGE(MapCreationHooks::load(*sh.mount->fs, options.creationScripts, &err), err);
	const UpgradeTemplate *rock = sh.world->upgrades().findUpgrade("Upgrade_SwitchToRockThrowing");
	REQUIRE(rock);
	CHECK(rock->getUpgradeType() == UPGRADE_TYPE_OBJECT);
	ArchiveW3DFileSource source(*sh.mount->fs);
	WW3DAssetManager assets(source);
	LiveGame game(*sh.world, *sh.mount->fs, assets, options);
	LiveGame::Options o;
	o.mapName = "map good celduin";
	o.seed = 7;
	REQUIRE_MESSAGE(game.load(o, &err), err);
	{
		const LiveGame::Report rep = game.report();
		MESSAGE("map good celduin: " << rep.loop.upgradesGranted << " upgrades granted by objectGrantUpgrade<n> / objectUpgradesList");
		CHECK(rep.loop.upgradesGranted > 0);
		for (const auto &kv : rep.loop.unportedKeys)
		{
			MESSAGE("map good celduin unported key: " << kv.first << " " << kv.second);
		}
	}
	const ThingTemplate *troll = sh.world->things().findTemplate("MordorMountainTroll");
	REQUIRE(troll);
	Player *owner = nullptr;
	for (int i = 0; i < game.logic().players().getPlayerCount() && !owner; ++i)
	{
		Player *p = game.logic().players().getNthPlayer(i);
		if (p && p->getDefaultTeam() && p != game.logic().players().getNeutralPlayer())
		{
			owner = p;
		}
	}
	REQUIRE(owner);
	// LiveGame installed LuaScriptEngine::sendObjectCreated: the troll's AILuaEventsList TrollFunctions runs OnTrollCreated, which calls
	// ObjectGrantUpgrade(self, "Upgrade_SwitchToRockThrowing") -> LiveScripting::grantUpgrade -> Object::giveUpgrade (RW 0x736EB3 -> 0x69388B)
	Object *t = game.logic().newObject(troll, owner->getDefaultTeam(), ObjectStatusMaskType{});
	REQUIRE(t);
	CHECK(t->hasUpgrade(rock));
}

TEST_CASE("upgrade retail: the structure upgrades of every side (fortress improvements, building levels) run the structures' upgrade modules")
{
	REQUIRE_RETAIL_WORLD(sh);
	const UpgradeCenter &c = sh.world->upgrades();
	const int kStructure = nameIndex(TheKindOfNames, "STRUCTURE");
	REQUIRE(kStructure >= 0);
	struct SideResult
	{
		int structures = 0, grants = 0, ran = 0;
		std::set<std::string> levelUpgrades, fortressUpgrades;
	};
	std::map<std::string, SideResult> sides;
	const char *const ported[] = { "SubObjectsUpgrade", "ModelConditionUpgrade", "CommandSetUpgrade", "StatusBitsUpgrade", "ArmorUpgrade", "WeaponSetUpgrade", nullptr };
	for (const FactionUpgrades &f : kFactions)
	{
		Game g(*sh.world, *sh.mount, f.faction);
		const std::string side = g.player->getPlayerTemplate() ? g.player->getPlayerTemplate()->m_side : std::string();
		REQUIRE_FALSE(side.empty());
		SideResult &r = sides[f.faction];
		for (const ThingTemplate *t : sh.world->things().templates())
		{
			const ThingTemplate *tt = t->getFinalOverride();
			const FieldValue *sv = tt->findField("Side");
			const std::string *tside = sv ? std::get_if<std::string>(sv) : nullptr;
			if (!tside || *tside != side)
			{
				continue;
			}
			if (!MaskTest(g.logic->templateInfo(tt).kindOf, (unsigned)kStructure))
			{
				continue;
			}
			// the OBJECT upgrades a ported module of the structure is triggered by, named as a level or a fortress improvement
			std::vector<const UpgradeTemplate *> ups;
			for (const ThingTemplate::Nugget &n : tt->behaviorModules().nuggets())
			{
				bool isPorted = false;
				for (int i = 0; ported[i]; ++i)
				{
					isPorted = isPorted || n.name == ported[i];
				}
				const UpgradeModuleData *d = dynamic_cast<const UpgradeModuleData *>(n.data.get());
				if (!isPorted || !d)
				{
					continue;
				}
				for (unsigned bit = 0; bit < UpgradeMaskType::BITS; ++bit)
				{
					const UpgradeTemplate *u = d->m_activationMask.test(bit) ? c.findUpgradeByMaskBit((int)bit) : nullptr;
					if (u && u->getUpgradeType() == UPGRADE_TYPE_OBJECT &&
						(u->getUpgradeName().find("Level") != std::string::npos || u->getUpgradeName().find("Fortress") != std::string::npos) &&
						std::find(ups.begin(), ups.end(), u) == ups.end())
					{
						ups.push_back(u);
					}
				}
			}
			if (ups.empty())
			{
				continue;
			}
			++r.structures;
			Object *o = g.make(*sh.world, tt->getName());
			for (const UpgradeTemplate *u : ups)
			{
				int before = 0, after = 0;
				for (const auto &m : o->modules())
				{
					if (UpgradeMux *mux = m->getUpgrade())
					{
						before += mux->isAlreadyUpgraded();
					}
				}
				o->giveUpgrade(u);
				for (const auto &m : o->modules())
				{
					if (UpgradeMux *mux = m->getUpgrade())
					{
						after += mux->isAlreadyUpgraded();
					}
				}
				++r.grants;
				r.ran += after > before;
				(u->getUpgradeName().find("Level") != std::string::npos ? r.levelUpgrades : r.fortressUpgrades).insert(u->getUpgradeName());
			}
		}
		MESSAGE(f.faction << " (" << side << "): " << r.structures << " structures, " << r.grants << " level / fortress upgrades granted, " << r.ran
							<< " ran a module; " << r.levelUpgrades.size() << " level upgrades, " << r.fortressUpgrades.size() << " fortress upgrades");
		CHECK(r.structures > 0);
		CHECK(!r.levelUpgrades.empty());
		CHECK(r.ran > 0);
	}
	// pinned on the retail data (structures, grants, grants that ran a module; a grant that runs nothing meets a ConflictsWith or an executed module)
	const std::map<std::string, std::array<int, 3>> expected = {
		// lane BUILD-3: a ChildObject's `KindOf = +...` now edits its parent's set (RW 0x65621C): DwarvenMineShaft and MordorSlaughterHouse are STRUCTUREs again
		// (Dwarves were { 15, 34, 33 }, Mordor { 11, 32, 31 })
		{ "FactionMen", { 24, 49, 47 } }, { "FactionElves", { 18, 47, 45 } }, { "FactionDwarves", { 16, 37, 36 } } /* XP-1: a LevelUpUpgrade now runs */, { "FactionIsengard", { 14, 32, 29 } },
		{ "FactionMordor", { 12, 35, 34 } }, { "FactionWild", { 11, 29, 28 } }, { "FactionAngmar", { 27, 46, 44 } },
	};
	for (const auto &kv : expected)
	{
		CAPTURE(kv.first);
		const SideResult &r = sides[kv.first];
		CHECK(std::array<int, 3>{ { r.structures, r.grants, r.ran } } == kv.second);
	}
}

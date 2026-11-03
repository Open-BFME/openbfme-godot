// OpenBFME unit tests. GPL-3.0.
// Lane MODCOMPAT-MOD1: SlaveWatcherBehavior (RW 0x887857 / 0x887A35 / 0x887AF5 / 0x8879C5) and its readers: ObjectCreationUpgrade's link and DestroyWhenSold
// (RW 0x8B86D6 / 0x8B87C5), SlaveAttackNugget (RW 0x910DCF / 0x910EA1 / 0x910F78). Binary facts read from the RotWK binary (RW_GAME_DAT), the retail data
// (ROTWK_INSTALL) and live objects of a started skirmish. The expectations are retail's rules (each check names its address).

#include "doctest.h"

#include "BuildTestUtil.h"
#include "PeImage.h"
#include "StartTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/StateHash.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Combat/WeaponDelivery.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ObjectCreationUpgrade.h"
#include "GameLogic/Module/SlaveWatcherBehavior.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponStores.h"
#include "GameLogic/WeaponNugget.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
bool startWatchGame(starttest::Shared &s, const std::string &faction0, const std::string &faction1, std::uint32_t seed, buildtest::Game &out, std::string *error)
{
	static std::vector<MapCacheEntry> cache;
	if (cache.empty() && !IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, error))
	{
		return false;
	}
	startgolden::Spec a{ 0 }, b{ 1 };
	a.tmpl = buildtest::templateIndex(s, faction0);
	b.tmpl = buildtest::templateIndex(s, faction1);
	a.color = 0;
	b.color = 1;
	a.start = 0;
	b.start = 4;
	NewGameMessage message;
	message.game = startgolden::makeInfo({ a, b });
	message.game.slots[1].state = SLOT_EASY_AI;
	message.game.mapName = "maps/map mp evendim/map mp evendim.map";
	message.game.seed = seed;
	message.game.startingCash = 5000;
	if (!NewGame::prepareNewGame(message, s.world->playerTemplates(), s.settings, cache, RandomAlgorithm::ZH_CarryChain, out.start, error))
	{
		return false;
	}
	out.source = std::make_unique<ArchiveW3DFileSource>(*s.mount->fs);
	out.assets = std::make_unique<WW3DAssetManager>(*out.source);
	out.live = std::make_unique<LiveGame>(*s.world, *s.mount->fs, *out.assets, s.options);
	LiveGame::Options o;
	o.start = &out.start;
	return out.live->load(o, error);
}

const Player *playerAtStart(GameLogic &logic, int start)
{
	for (int i = 0; i < logic.players().getPlayerCount(); ++i)
	{
		const Player *p = logic.players().getNthPlayer(i);
		if (p && p->getPlayerTemplate() && p->getPlayerTemplate()->m_playableSide && p->getMultiplayerStartIndex() == start)
		{
			return p;
		}
	}
	return nullptr;
}

Object *firstOf(GameLogic &logic, const Player *owner, const char *kindOfName)
{
	for (Object *x = logic.getFirstObject(); x; x = x->getNextObject())
	{
		if (x->getControllingPlayer() == owner && x->isKindOfName(kindOfName) && !x->isEffectivelyDead())
		{
			return x;
		}
	}
	return nullptr;
}

template <class T>
const T *dataOf(const ThingTemplate *tt)
{
	for (const ThingTemplate::Nugget &n : tt->getFinalOverride()->behaviorModules().nuggets())
	{
		if (const T *d = dynamic_cast<const T *>(n.data.get()))
		{
			return d;
		}
	}
	return nullptr;
}

bool hasUpgrade(const Object &o, const char *name)
{
	const UpgradeTemplate *u = TheUpgradeCenter ? TheUpgradeCenter->findUpgrade(name) : nullptr;
	REQUIRE_MESSAGE(u, name);
	return o.hasUpgrade(u);
}

void give(Object &o, const char *name)
{
	const UpgradeTemplate *u = TheUpgradeCenter ? TheUpgradeCenter->findUpgrade(name) : nullptr;
	REQUIRE_MESSAGE(u, name);
	o.giveUpgrade(u);
}

// an Angmar catapult expansion of `owner` with its free troll sling (MakeTheFreeTreb -> Upgrade_HasWallTrebuchet -> MakeTheFreeTreb2's ThingToSpawn)
Object *expansionWithSlave(buildtest::Game &g, const Player &owner, const Coord3D &at, Object *&slave)
{
	std::string err;
	Object *e = g.live->createObject("AngmarCatapultExpansion", owner.getPlayerIndex(), at, 0.0f, &err);
	REQUIRE_MESSAGE(e, err);
	for (int i = 0; i < 3; ++i)
	{
		g.live->advance(0.2);
	}
	give(*e, "Upgrade_TrebuchetTurret");
	for (int i = 0; i < 5; ++i)
	{
		g.live->advance(0.2);
	}
	SlaveWatcherBehavior *w = SlaveWatcherBehavior::of(*e);
	REQUIRE(w);
	slave = e->logic().findObjectByID(w->getSlaveID());
	REQUIRE(slave);
	return e;
}
} // namespace

// ---- binary facts ------------------------------------------------------------------------------------------------------------------------------------------

TEST_CASE("slave watcher: the field table RW 0xC609F8, the update slot RW 0xC60A48 and SlaveAttackNugget's vtable RW 0xC7B9B0")
{
	const retailtest::PeImage *pe = retailtest::PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("slave watcher binary facts (set RW_GAME_DAT)");
		return;
	}
	struct Row
	{
		const char *name;
		std::uint32_t parser;
		std::uint32_t offset;
	};
	const Row rows[] = { { "RemoveUpgrade", 0x42EE5E, 0x0C }, { "GrantUpgrade", 0x42EE5E, 0x08 }, { "ShareUpgrades", 0x42E558, 0x10 }, { "LetSlaveLive", 0x42E558, 0x11 } };
	for (std::uint32_t i = 0; i < 4; ++i)
	{
		CHECK(pe->cstring(pe->u32At(0xC609F8 + 16u * i)) == rows[i].name);
		CHECK(pe->u32At(0xC609F8 + 16u * i + 4u) == rows[i].parser);
		CHECK(pe->u32At(0xC609F8 + 16u * i + 12u) == rows[i].offset);
	}
	CHECK(pe->u32At(0xC609F8 + 64u) == 0u);
	CHECK(pe->u32At(0xC60A48) == 0x887A35u); // the update interface's slot 0
	// SlaveAttackNugget: slot 1 isApplicable, slot 2 the upgrade test, slots 5 / 6 the victim / position deliveries
	CHECK(pe->u32At(0xC7B9B0 + 4u) == 0x910DCFu);
	CHECK(pe->u32At(0xC7B9B0 + 8u) == 0x911BFDu);
	CHECK(pe->u32At(0xC7B9B0 + 20u) == 0x910EA1u);
	CHECK(pe->u32At(0xC7B9B0 + 24u) == 0x910F78u);
}

// ---- the retail data -----------------------------------------------------------------------------------------------------------------------------------

TEST_CASE("slave watcher retail: the catapult expansions share their upgrades and get the buy button back; the eagle may live on")
{
	OPENBFME_REQUIRE_START(s);
	RetailObjectWorld &w = *s->world;
	const SlaveWatcherBehaviorModuleData *d = dataOf<SlaveWatcherBehaviorModuleData>(w.things().findTemplate("AngmarCatapultExpansion"));
	REQUIRE(d);
	CHECK(d->m_removeUpgrade == "Upgrade_HasWallTrebuchet");
	CHECK(d->m_grantUpgrade == "Upgrade_WallTrebuchetButtonEnable");
	CHECK(d->m_shareUpgrades);
	CHECK_FALSE(d->m_letSlaveLive);
	const SlaveWatcherBehaviorModuleData *eagle = dataOf<SlaveWatcherBehaviorModuleData>(w.things().findTemplate("ElvenCitadel"));
	REQUIRE(eagle);
	CHECK(eagle->m_letSlaveLive);
	CHECK_FALSE(eagle->m_shareUpgrades); // the constructor's default (RW 0x887966)
	const std::vector<std::string> unported = w.modules().unportedModuleClassNames();
	CHECK(std::find(unported.begin(), unported.end(), "SlaveWatcherBehavior") == unported.end());
}

// ---- a live game -----------------------------------------------------------------------------------------------------------------------------------------

TEST_CASE("slave watcher live: an Angmar expansion's free troll sling is watched; its death brings the buy button back (RW 0x887AF5 / 0x887A35)")
{
	OPENBFME_REQUIRE_START(s);
	buildtest::Game g;
	std::string err;
	REQUIRE_MESSAGE(startWatchGame(*s, "FactionAngmar", "FactionMen", 4421, g, &err), err);
	GameLogic &logic = g.live->logic();
	const Player *human = playerAtStart(logic, 0);
	REQUIRE(human);
	Object *fortress = firstOf(logic, human, "COMMANDCENTER");
	REQUIRE(fortress);
	const Coord3D base = *fortress->getPosition();
	Object *slave = nullptr;
	Object *e = expansionWithSlave(g, *human, Coord3D{ base.x + 500.0f, base.y, base.z }, slave);
	SlaveWatcherBehavior *watcher = SlaveWatcherBehavior::of(*e);
	REQUIRE(watcher);
	CHECK(slave->getTemplate()->getName() == "AngmarTrollSlingFortress");
	CHECK(hasUpgrade(*e, "Upgrade_HasWallTrebuchet"));
	CHECK_FALSE(hasUpgrade(*e, "Upgrade_WallTrebuchetButtonEnable"));
	// ShareUpgrades: the slave holds the expansion's object upgrades (RW 0x68CC6C)
	CHECK(hasUpgrade(*slave, "Upgrade_TrebuchetTurret"));
	CHECK(watcher->sharedMask() == e->getUpgradeMask());

	// the snapshot round-trips
	{
		StateHasher before;
		watcher->crc(before);
		XferSave saved;
		watcher->xfer(saved);
		XferLoad load(saved.data());
		watcher->xfer(load);
		CHECK(load.atEnd());
		StateHasher after;
		watcher->crc(after);
		CHECK(after.value() == before.value());
		std::vector<std::uint8_t> truncated = saved.data();
		truncated.pop_back();
		XferLoad lt(truncated);
		CHECK_THROWS_AS(watcher->xfer(lt), XferException);
		XferLoad again(saved.data());
		watcher->xfer(again);
	}

	// SlaveAttackNugget at a position (slot 6, RW 0x910F78): the slave's primary weapon is locked and it is ordered to shoot there once
	const WeaponTemplate *expansionWeapon = TheWeaponStore ? TheWeaponStore->findWeaponTemplate("AngmarStoneThrowerExpansionWeapon") : nullptr;
	REQUIRE(expansionWeapon);
	REQUIRE(slave->getWeapons());
	const Coord3D spot{ base.x + 700.0f, base.y + 150.0f, base.z };
	unsigned long long unported = 0;
	DeliverNuggets(logic, e->getID(), *expansionWeapon, WeaponBonus{}, nullptr, &spot, false, &unported);
	CHECK(unported == 0u);
	CHECK(slave->getWeapons()->isCurWeaponLocked());
	REQUIRE(slave->getAIUpdateInterface());
	CHECK(slave->getAIUpdateInterface()->isAttacking());

	// the slave dies: the watcher takes RemoveUpgrade away and gives GrantUpgrade (the buy button), then sleeps
	slave->kill(DAMAGE_UNRESISTABLE, 0);
	for (int i = 0; i < 3; ++i)
	{
		g.live->advance(0.2);
	}
	CHECK(watcher->getSlaveID() == INVALID_ID);
	CHECK(watcher->releases() == 1u);
	CHECK_FALSE(hasUpgrade(*e, "Upgrade_HasWallTrebuchet"));
	CHECK(hasUpgrade(*e, "Upgrade_WallTrebuchetButtonEnable"));
}

TEST_CASE("slave watcher live: DestroyWhenSold kills the slave (RW 0x8B87C5); a deleted expansion takes its slave along (RW 0x8879C5)")
{
	OPENBFME_REQUIRE_START(s);
	buildtest::Game g;
	std::string err;
	REQUIRE_MESSAGE(startWatchGame(*s, "FactionAngmar", "FactionMen", 4422, g, &err), err);
	GameLogic &logic = g.live->logic();
	const Player *human = playerAtStart(logic, 0);
	REQUIRE(human);
	Object *fortress = firstOf(logic, human, "COMMANDCENTER");
	REQUIRE(fortress);
	const Coord3D base = *fortress->getPosition();
	Object *slave = nullptr;
	Object *e = expansionWithSlave(g, *human, Coord3D{ base.x + 500.0f, base.y, base.z }, slave);
	// the trigger upgrade goes (a sale takes the upgrades): MakeTheFreeTreb's removal kills the watched slave with DeathAnimAndDuration's DEATH_2
	const UpgradeTemplate *turret = TheUpgradeCenter->findUpgrade("Upgrade_TrebuchetTurret");
	REQUIRE(turret);
	e->removeUpgrade(turret);
	CHECK(slave->isEffectivelyDead());
	const ObjectID slaveID = slave->getID();
	for (int i = 0; i < 3; ++i)
	{
		g.live->advance(0.2);
	}
	CHECK(SlaveWatcherBehavior::of(*e)->releases() == 1u);

	Object *slave2 = nullptr;
	Object *e2 = expansionWithSlave(g, *human, Coord3D{ base.x - 500.0f, base.y, base.z }, slave2);
	CHECK(slave2->getID() != slaveID);
	CHECK_FALSE(slave2->isEffectivelyDead());
	const ObjectID slave2ID = slave2->getID();
	logic.destroyObject(e2); // deleted (not killed): the destructor's kill (LetSlaveLive No)
	g.live->advance(0.2);
	const Object *after = logic.findObjectByID(slave2ID); // by id: the dead sling may be gone already
	CHECK((after == nullptr || after->isEffectivelyDead()));
}

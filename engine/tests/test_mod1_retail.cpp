// OpenBFME. GPL-3.0.
// Lane MODULES-1 on the retail data (SKIPs when ROTWK_INSTALL / BFME2_INSTALL are unset): each module this lane ports, on a live object of a retail template that
// declares it, shows its effect; the same scenario on two games gives the same state hash, and the module's state reaches the hash.
//
// The templates are found by module class in the loaded data (no template name is hard-coded where a search can find one); the test prints which one it used.

#include "doctest.h"

#include "Common/PlayerList.h"
#include "Common/StateHash.h"
#include "Common/Team.h"
#include "Common/Upgrade.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/Economy.h"
#include "GameLogic/EconomySettings.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/ExtraCreateModules.h"
#include "GameLogic/Module/ExtraDieModules.h"
#include "GameLogic/Module/ExtraUpdateModules.h"
#include "GameLogic/Module/ExtraUpgradeModules.h"
#include "GameLogic/Module/FlammableUpdate.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Damage.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/Object/AttributeModifierPool.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/Weapon.h"
#include "RetailTestMount.h"

#include <functional>
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

#define REQUIRE_RETAIL_WORLD(sh)                                                     \
	Shared &sh = shared();                                                           \
	if (!sh.mount || !sh.mount->fs)                                                  \
	{                                                                                \
		MESSAGE("SKIP: ROTWK_INSTALL / BFME2_INSTALL not set (retail module test)"); \
		return;                                                                      \
	}                                                                                \
	REQUIRE_MESSAGE(sh.world, sh.error);                                             \
	auto contextScope = sh.world->enterContext()

struct Game
{
	TeamFactory teams;
	PlayerList players;
	std::unique_ptr<GameLogic> logic;
	Player *player = nullptr;
	Player *enemy = nullptr;
	Game(RetailObjectWorld &world, retailtest::Mount &mount, const std::string &faction = "FactionMen")
		: players(world.nameKeys(), world.playerTemplates(), teams)
	{
		SkirmishSetup setup;
		setup.players.push_back({ "Tester", faction, true, 0, 0, 0 });
		setup.players.push_back({ "Enemy", "FactionMordor", false, 1, 0, 1 });
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
		// lane MODULES-2: the partition region a map's terrain extent would give (RW 0x62FCCD); without one getClosestObject's range clamps to 0 (only coincident or overlapping candidates, S-1020)
		logic->partition().setRegion(0.0f, 0.0f, 2000.0f, 2000.0f);
		player = players.findPlayerWithName("Tester");
		enemy = players.findPlayerWithName("Enemy");
		REQUIRE(player);
		REQUIRE(enemy);
	}
	Object *make(const ThingTemplate *tt, Player *owner = nullptr, float x = 500.0f, float y = 500.0f)
	{
		REQUIRE(tt);
		Object *o = logic->newObject(tt, (owner ? owner : player)->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE(o);
		const Coord3D pos{ x, y, 0.0f };
		o->setPosition(&pos);
		return o;
	}
	void run(int frames)
	{
		for (int i = 0; i < frames; ++i)
		{
			logic->runLogicFrame();
		}
	}
	std::uint32_t hash() const { return logic->computeStateHash(); }
};

// the first template (registry order, defaults and cinematic copies skipped) one of whose behaviour modules of class `cls` has data of type D for which `accept` holds
template <class D>
const ThingTemplate *findTemplate(RetailObjectWorld &world, const char *cls, const std::function<bool(const ThingTemplate &, const D &)> &accept, const D **out = nullptr)
{
	for (const ThingTemplate *t : world.things().templates())
	{
		const std::string &name = t->getName();
		if (name.rfind("Default", 0) == 0 || name.rfind("CINE_", 0) == 0)
		{
			continue; // prefer a template a game uses over the defaults and the cinematic copies
		}
		const ThingTemplate *f = t->getFinalOverride();
		for (const ThingTemplate::Nugget &n : f->behaviorModules().nuggets())
		{
			const D *d = dynamic_cast<const D *>(n.data.get());
			if (n.name == cls && d && accept(*f, *d))
			{
				if (out)
				{
					*out = d;
				}
				return t;
			}
		}
	}
	return nullptr;
}

template <class M>
M *moduleOf(Object *o)
{
	for (const auto &m : o->modules())
	{
		if (M *x = dynamic_cast<M *>(m.get()))
		{
			return x;
		}
	}
	return nullptr;
}

// a stop line of the game's report that starts with `prefix`
bool reportHas(Game &g, const std::string &prefix)
{
	for (const std::string &line : g.logic->report().stops)
	{
		if (line.rfind(prefix, 0) == 0)
		{
			return true;
		}
	}
	return false;
}

AttributeModifierPool *poolOf(Object *o)
{
	return moduleOf<AttributeModifierPool>(o);
}
} // namespace

TEST_CASE("modules retail: AttributeModifierUpgrade puts its ModifierList on the object when the upgrade arrives; removal follows retail's mux order")
{
	REQUIRE_RETAIL_WORLD(sh);
	const AttributeModifierUpgradeModuleData *data = nullptr;
	const ThingTemplate *tt = findTemplate<AttributeModifierUpgradeModuleData>(*sh.world, "AttributeModifierUpgrade",
		[](const ThingTemplate &, const AttributeModifierUpgradeModuleData &d) { return !d.m_triggeredBy.empty() && !d.m_attributeModifier.empty() && d.m_conflictsWith.empty(); }, &data);
	REQUIRE(tt);
	REQUIRE(data);
	const UpgradeTemplate *u = sh.world->upgrades().findUpgrade(data->m_triggeredBy[0]);
	REQUIRE_MESSAGE(u, data->m_triggeredBy[0]);
	MESSAGE("AttributeModifierUpgrade: " << tt->getName() << ", " << data->m_triggeredBy[0] << " -> " << data->m_attributeModifier);
	std::uint32_t hashes[2] = {};
	for (int run = 0; run < 2; ++run)
	{
		Game g(*sh.world, *sh.mount);
		Object *o = g.make(tt);
		AttributeModifierPool *pool = poolOf(o);
		REQUIRE(pool);
		CHECK_FALSE(pool->hasList(data->m_attributeModifier));
		AttributeModifierUpgrade *amu = moduleOf<AttributeModifierUpgrade>(o);
		REQUIRE(amu);
		CHECK_FALSE(amu->isAlreadyUpgraded());
		const std::uint32_t before = g.hash();
		if (u->getUpgradeType() == UPGRADE_TYPE_PLAYER)
		{
			g.player->addUpgrade(u, Player::UPGRADE_STATUS_COMPLETE);
		}
		else
		{
			o->giveUpgrade(u);
		}
		CHECK(amu->isAlreadyUpgraded());
		CHECK(pool->hasList(data->m_attributeModifier));
		CHECK(g.hash() != before);
		g.run(5);
		hashes[run] = g.hash();
	}
	CHECK(hashes[0] == hashes[1]);
}

TEST_CASE("modules retail: CreateObjectDie runs its ObjectCreationList when the object dies (and not before); deterministic")
{
	REQUIRE_RETAIL_WORLD(sh);
	REQUIRE(TheObjectCreationListStore);
	const CreateObjectDieModuleData *data = nullptr;
	const ThingTemplate *tt = findTemplate<CreateObjectDieModuleData>(*sh.world, "CreateObjectDie",
		[&](const ThingTemplate &, const CreateObjectDieModuleData &d) {
			if (!d.m_upgradeRequired.empty() || d.m_creationList.empty() || d.m_dieMux.m_requiredStatus != ObjectStatusMaskType{})
			{
				return false;
			}
			const ObjectCreationList *ocl = TheObjectCreationListStore->findObjectCreationList(d.m_creationList);
			if (!ocl)
			{
				return false;
			}
			for (const OCLNugget &n : ocl->nuggets())
			{
				if (n.kind == OCLNugget::CREATE_OBJECT && !n.objectNames.empty() && sh.world->things().findTemplate(n.objectNames[0]))
				{
					return true;
				}
			}
			return false;
		},
		&data);
	REQUIRE(tt);
	MESSAGE("CreateObjectDie: " << tt->getName() << " -> " << data->m_creationList);
	std::uint32_t hashes[2] = {};
	for (int run = 0; run < 2; ++run)
	{
		Game g(*sh.world, *sh.mount);
		Object *o = g.make(tt);
		CreateObjectDie *cod = moduleOf<CreateObjectDie>(o);
		REQUIRE(cod);
		g.run(2);
		const size_t before = g.logic->getObjectCount();
		CHECK(cod->createdCount() == 0);
		const std::uint32_t h0 = g.hash();
		o->kill(DEATH_NORMAL);
		CHECK(cod->createdCount() > 0);
		CHECK(g.logic->getObjectCount() >= before + cod->createdCount());
		CHECK(g.hash() != h0);
		CHECK(reportHas(g, "[S-980] CreateObjectDie"));
		g.run(5);
		hashes[run] = g.hash();
	}
	CHECK(hashes[0] == hashes[1]);
}

TEST_CASE("modules retail: FireWeaponWhenDeadBehavior fires its DeathWeapon at the death (after DelayTime when it has one); deterministic")
{
	REQUIRE_RETAIL_WORLD(sh);
	REQUIRE(TheWeaponStore);
	for (const bool delayed : { false, true })
	{
		const FireWeaponWhenDeadBehaviorModuleData *data = nullptr;
		const ThingTemplate *tt = findTemplate<FireWeaponWhenDeadBehaviorModuleData>(*sh.world, "FireWeaponWhenDeadBehavior",
			[&](const ThingTemplate &, const FireWeaponWhenDeadBehaviorModuleData &d) {
				return d.m_startsActive && (d.m_delayTime > 1) == delayed && d.m_conflictsWith.empty() && d.m_dieMux.m_requiredStatus == ObjectStatusMaskType{} &&
					!d.m_deathWeapon.empty() && TheWeaponStore->findWeaponTemplate(d.m_deathWeapon);
			},
			&data);
		REQUIRE(tt);
		MESSAGE("FireWeaponWhenDeadBehavior: " << tt->getName() << " fires " << data->m_deathWeapon << " (DelayTime " << data->m_delayTime << " frames)");
		std::uint32_t hashes[2] = {};
		for (int run = 0; run < 2; ++run)
		{
			Game g(*sh.world, *sh.mount);
			Object *o = g.make(tt);
			FireWeaponWhenDeadBehavior *fw = moduleOf<FireWeaponWhenDeadBehavior>(o);
			REQUIRE(fw);
			CHECK(fw->isAlreadyUpgraded()); // StartsActive gives the module its own upgrade (RW 0x855388)
			g.run(2);
			const ObjectID id = o->getID();
			o->kill(DEATH_NORMAL);
			if (!delayed)
			{
				CHECK(fw->shots() == 1);
			}
			else
			{
				CHECK(fw->shots() == 0);
				CHECK(fw->delayCounter() == data->m_delayTime);
				// the object must outlive the delay (a slow death); count the frames until the shot
				int frames = 0;
				while (g.logic->findObjectByID(id) && fw->shots() == 0 && frames < (int)data->m_delayTime + 5)
				{
					g.run(1);
					++frames;
				}
				REQUIRE(g.logic->findObjectByID(id));
				CHECK(fw->shots() == 1);
				CHECK(frames == (int)data->m_delayTime);
			}
			CHECK_FALSE(reportHas(g, "[S-980] FireWeaponWhenDeadBehavior")); // lane DECOMP-1: the DeathWeapon is the full temporary weapon (RW 0x6CF530), nothing to report
			g.run(3);
			hashes[run] = g.hash();
		}
		CHECK(hashes[0] == hashes[1]);
	}
}

TEST_CASE("modules retail: DeletionUpdate removes the object silently after its random lifetime (RW 0x88B737 draw); deterministic")
{
	REQUIRE_RETAIL_WORLD(sh);
	const DeletionUpdateModuleData *data = nullptr;
	const ThingTemplate *tt = findTemplate<DeletionUpdateModuleData>(*sh.world, "DeletionUpdate",
		[](const ThingTemplate &, const DeletionUpdateModuleData &d) { return d.m_maxLifetime > 1 && d.m_maxLifetime < 600; }, &data);
	REQUIRE(tt);
	MESSAGE("DeletionUpdate: " << tt->getName() << " lives " << data->m_minLifetime << " .. " << data->m_maxLifetime << " frames");
	unsigned dieFrames[2] = {};
	std::uint32_t hashes[2] = {};
	for (int run = 0; run < 2; ++run)
	{
		Game g(*sh.world, *sh.mount);
		const unsigned born = g.logic->getFrame();
		Object *o = g.make(tt);
		DeletionUpdate *du = moduleOf<DeletionUpdate>(o);
		REQUIRE(du);
		const ObjectID id = o->getID();
		dieFrames[run] = du->dieFrame();
		CHECK(du->dieFrame() >= born + (data->m_minLifetime > 0 ? data->m_minLifetime : 1));
		CHECK(du->dieFrame() <= born + (data->m_maxLifetime > 0 ? data->m_maxLifetime : 1));
		int frames = 0;
		while (g.logic->findObjectByID(id) && frames < (int)data->m_maxLifetime + 5)
		{
			g.run(1);
			++frames;
		}
		CHECK_FALSE(g.logic->findObjectByID(id));
		CHECK(g.logic->getFrame() >= dieFrames[run]);
		CHECK(g.logic->getFrame() <= dieFrames[run] + 1);
		hashes[run] = g.hash();
	}
	CHECK(dieFrames[0] == dieFrames[1]);
	CHECK(hashes[0] == hashes[1]);
}

TEST_CASE("modules retail: ExperienceLevelCreate leaves a retail object at its LevelToGrant (it starts there); LockWeaponCreate locks its slot for good")
{
	REQUIRE_RETAIL_WORLD(sh);
	// every retail object with the module starts at its LevelToGrant already (its ExperienceLevel data: the ring heroes and the summoned hordes have one level
	// of that rank), so the module's gainExpForLevel(LevelToGrant - rank = 0) changes nothing on retail data: the test pins that
	const ExperienceLevelCreateModuleData *elc = nullptr;
	const ThingTemplate *elcT = findTemplate<ExperienceLevelCreateModuleData>(*sh.world, "ExperienceLevelCreate",
		[](const ThingTemplate &, const ExperienceLevelCreateModuleData &d) { return !d.m_mpOnly && d.m_levelToGrant > 1; }, &elc);
	REQUIRE(elcT);
	MESSAGE("ExperienceLevelCreate: " << elcT->getName() << " -> level " << elc->m_levelToGrant);
	std::uint32_t hashes[2] = {};
	for (int run = 0; run < 2; ++run)
	{
		Game g(*sh.world, *sh.mount);
		Object *o = g.make(elcT);
		ExperienceTracker *xp = o->getExperienceTracker();
		REQUIRE(xp);
		CHECK(xp->getRank() == elc->m_levelToGrant);
		std::uint32_t h0 = g.hash();
		o->friend_onBuildComplete(); // RW 0x68D252: the create modules' slot 1; LevelToGrant - rank = 0 levels
		CHECK(xp->getRank() == elc->m_levelToGrant);
		CHECK(g.hash() == h0);
		g.run(3);
		hashes[run] = g.hash();
	}
	CHECK(hashes[0] == hashes[1]);

	const LockWeaponCreateModuleData *lwc = nullptr;
	const ThingTemplate *lwcT = findTemplate<LockWeaponCreateModuleData>(*sh.world, "LockWeaponCreate",
		[](const ThingTemplate &t, const LockWeaponCreateModuleData &) { return t.getName().find("Horde") == std::string::npos; }, &lwc);
	REQUIRE(lwcT);
	MESSAGE("LockWeaponCreate: " << lwcT->getName() << " locks slot " << lwc->m_slotToLock);
	Game g(*sh.world, *sh.mount);
	Object *o = g.make(lwcT);
	REQUIRE(o->getWeapons());
	const std::uint32_t h0 = g.hash();
	o->friend_onBuildComplete();
	CHECK(o->getWeapons()->isCurWeaponLocked());
	CHECK(o->getWeapons()->curSlot() == lwc->m_slotToLock);
	CHECK(g.hash() != h0);
}

TEST_CASE("modules retail: InheritUpgradeCreate copies the upgrade of a matching object of its player within Radius at build completion")
{
	REQUIRE_RETAIL_WORLD(sh);
	const InheritUpgradeCreateModuleData *data = nullptr;
	const ThingTemplate *tt = findTemplate<InheritUpgradeCreateModuleData>(*sh.world, "InheritUpgradeCreate",
		[&](const ThingTemplate &, const InheritUpgradeCreateModuleData &d) {
			return d.m_upgrade.any() && d.m_radius > 0.0f && !d.m_objectFilter.includeNames.empty() && sh.world->things().findTemplate(d.m_objectFilter.includeNames[0]);
		},
		&data);
	REQUIRE(tt);
	const ThingTemplate *sourceT = sh.world->things().findTemplate(data->m_objectFilter.includeNames[0]);
	const UpgradeTemplate *u = nullptr;
	for (unsigned bit = 0; bit < UpgradeMaskType::BITS && !u; ++bit)
	{
		if (data->m_upgrade.test(bit))
		{
			u = sh.world->upgrades().findUpgradeByMaskBit((int)bit);
		}
	}
	REQUIRE(u);
	MESSAGE("InheritUpgradeCreate: " << tt->getName() << " inherits " << u->getUpgradeName() << " from " << sourceT->getName() << " within " << data->m_radius);
	for (const bool inRange : { true, false })
	{
		Game g(*sh.world, *sh.mount);
		Object *source = g.make(sourceT, nullptr, 500.0f, 500.0f);
		source->giveUpgrade(u);
		REQUIRE(source->hasUpgrade(u));
		const float x = inRange ? 500.0f + data->m_radius * 0.5f : 500.0f + data->m_radius * 2.0f + 10.0f;
		Object *o = g.make(tt, nullptr, x, 500.0f);
		CHECK_FALSE(o->hasUpgrade(u));
		o->friend_onBuildComplete();
		CHECK(o->hasUpgrade(u) == inRange);
		CHECK_FALSE(reportHas(g, "[S-981] InheritUpgradeCreate")); // lane MODULES-2: the scan runs on ThePartitionManager (RW 0x8BD76C, type 1)
		// a second build completion does nothing (the create module's flag)
		InheritUpgradeCreate *m = moduleOf<InheritUpgradeCreate>(o);
		REQUIRE(m);
		const unsigned n = m->inherited();
		o->friend_onBuildComplete();
		CHECK(m->inherited() == n);
	}
}

TEST_CASE("modules retail: MonitorConditionUpdate swaps the command set while a monitored model condition holds and restores it after; deterministic")
{
	REQUIRE_RETAIL_WORLD(sh);
	const MonitorConditionUpdateModuleData *data = nullptr;
	const ThingTemplate *tt = findTemplate<MonitorConditionUpdateModuleData>(*sh.world, "MonitorConditionUpdate",
		[](const ThingTemplate &t, const MonitorConditionUpdateModuleData &d) {
			bool any = false;
			for (std::uint32_t w : d.m_modelConditionFlags)
			{
				any = any || w != 0;
			}
			return any && !d.m_modelConditionCommandSet.empty() && t.getName().find("Horde") == std::string::npos;
		},
		&data);
	REQUIRE(tt);
	int bit = -1;
	for (int i = 0; i < 19 * 32 && bit < 0; ++i)
	{
		if ((data->m_modelConditionFlags[(size_t)i >> 5] >> (i & 31)) & 1u)
		{
			bit = i;
		}
	}
	MESSAGE("MonitorConditionUpdate: " << tt->getName() << " shows " << data->m_modelConditionCommandSet << " under model condition bit " << bit);
	std::uint32_t hashes[2] = {};
	for (int run = 0; run < 2; ++run)
	{
		Game g(*sh.world, *sh.mount);
		Object *o = g.make(tt);
		g.run(2);
		const std::string base = o->getCommandSetName();
		REQUIRE(base != data->m_modelConditionCommandSet);
		Object::ModelConditionBits none{}, set{};
		set[(size_t)bit >> 5] |= 1u << (bit & 31);
		const std::uint32_t h0 = g.hash();
		o->clearAndSetModelConditionFlags(none, set);
		g.run(1);
		CHECK(o->getCommandSetName() == data->m_modelConditionCommandSet);
		MonitorConditionUpdate *m = moduleOf<MonitorConditionUpdate>(o);
		REQUIRE(m);
		CHECK(m->savedCommandSet() == base);
		CHECK(g.hash() != h0);
		CHECK(reportHas(g, "[S-982] MonitorConditionUpdate"));
		o->clearAndSetModelConditionFlags(set, none);
		g.run(1);
		CHECK(o->getCommandSetName() == base);
		CHECK(m->savedCommandSet().empty());
		hashes[run] = g.hash();
	}
	CHECK(hashes[0] == hashes[1]);
}

namespace
{
// a FLAME hit of `amount` on `victim` from `source` (the damage pipeline: armour, body, the damage modules)
void flameHit(Object *victim, const Object *source, float amount)
{
	DamageInfo d;
	d.m_input.m_sourceID = source ? source->getID() : INVALID_ID;
	d.m_input.m_damageType = 6; // FLAME (TheDamageNames)
	d.m_input.m_deathType = DEATH_BURNED;
	d.m_input.m_amount = amount;
	victim->attemptDamage(d);
}
} // namespace

TEST_CASE("modules retail: FlammableUpdate ignites past its FlameDamageLimit, burns in AflameDamageDelay ticks, ends BURNED; FireSpreadUpdate sets a neighbour alight")
{
	REQUIRE_RETAIL_WORLD(sh);
	const FlammableUpdateModuleData *fd = nullptr;
	const ThingTemplate *tt = findTemplate<FlammableUpdateModuleData>(*sh.world, "FlammableUpdate",
		[](const ThingTemplate &t, const FlammableUpdateModuleData &d) {
			bool spread = false;
			for (const ThingTemplate::Nugget &n : t.behaviorModules().nuggets())
			{
				const FireSpreadUpdateModuleData *s = dynamic_cast<const FireSpreadUpdateModuleData *>(n.data.get());
				spread = spread || (s && s->m_spreadTryRange > 0.0f && s->m_maxSpreadDelay < 100);
			}
			return spread && d.m_aflameDamageAmount > 0 && d.m_aflameDamageDelay > 0 && d.m_aflameDuration > d.m_aflameDamageDelay &&
				d.m_aflameDuration < 300 && d.m_damageType == 6;
		},
		&fd);
	REQUIRE(tt);
	MESSAGE("FlammableUpdate: " << tt->getName() << " limit " << fd->m_flameDamageLimit << ", aflame " << fd->m_aflameDuration << " frames, " << fd->m_aflameDamageAmount
								<< " every " << fd->m_aflameDamageDelay << ", burned after " << fd->m_burnedDelay);
	std::uint32_t hashes[2] = {};
	for (int run = 0; run < 2; ++run)
	{
		Game g(*sh.world, *sh.mount);
		Object *a = g.make(tt, nullptr, 500.0f, 500.0f);
		Object *b = g.make(tt, nullptr, 520.0f, 500.0f); // a neighbour within the spread range
		Object *burner = g.make(tt, g.enemy, 900.0f, 900.0f);
		FlammableUpdate *fa = moduleOf<FlammableUpdate>(a);
		FlammableUpdate *fb = moduleOf<FlammableUpdate>(b);
		FireSpreadUpdate *sa = moduleOf<FireSpreadUpdate>(a);
		REQUIRE(fa);
		REQUIRE(fb);
		REQUIRE(sa);
		g.run(1);
		// a hit below the limit does not ignite; the limit counts down
		flameHit(a, burner, 1.0f);
		CHECK(fa->status() == FlammableUpdate::FS_NORMAL);
		CHECK(fa->remainingFlameDamage() < fd->m_flameDamageLimit);
		for (int i = 0; i < 20 && fa->status() == FlammableUpdate::FS_NORMAL; ++i)
		{
			flameHit(a, burner, fd->m_flameDamageLimit);
		}
		REQUIRE(fa->status() == FlammableUpdate::FS_AFLAME);
		CHECK(a->testStatus(10)); // AFLAME
		CHECK(a->testModelCondition(CombatNames::modelCondition("AFLAME")));
		const unsigned ignited = g.logic->getFrame();
		int frames = 0;
		while (fa->status() == FlammableUpdate::FS_AFLAME && frames < (int)fd->m_aflameDuration + 5 && g.logic->findObjectByID(a->getID()))
		{
			g.run(1);
			++frames;
		}
		MESSAGE("burned for " << frames << " frames, " << fa->aflameDamageTicks() << " damage ticks, spread " << sa->spreads() << ", neighbour status " << fb->status());
		CHECK(fa->aflameDamageTicks() >= (unsigned)(fd->m_aflameDuration / fd->m_aflameDamageDelay) - 1);
		CHECK(fa->aflameDamageTicks() <= (unsigned)(fd->m_aflameDuration / fd->m_aflameDamageDelay) + 1);
		CHECK(g.logic->getFrame() - ignited >= fd->m_aflameDuration);
		// lane COMBAT-4: a tree the fire killed sinks with DISABLED_HELD (RotWK's SlowDeathBehavior update RW 0x860B39), and FlammableUpdate's update has the default
		// disabled mask (RW 0x6530FA: the global RW 0xDE8B8C, only ever zeroed), so a dead, sinking tree stays AFLAME: the burn's end is checked on a live one
		if (a->isEffectivelyDead() && (a->getDisabledMask() & (1u << 3)) != 0)
		{
			CHECK(fa->status() == FlammableUpdate::FS_AFLAME);
		}
		else
		{
			CHECK(fa->status() == (fd->m_setBurnedStatus ? FlammableUpdate::FS_BURNED : FlammableUpdate::FS_NORMAL));
			CHECK_FALSE(a->testStatus(10));
		}
		CHECK(sa->spreads() >= 1);
		CHECK(fb->status() != FlammableUpdate::FS_NORMAL);
		CHECK(reportHas(g, "[S-983] FlammableUpdate"));
		hashes[run] = g.hash();
	}
	CHECK(hashes[0] == hashes[1]);
}

TEST_CASE("modules retail: UpgradeDie takes its upgrade back from the producer (OBJECT) or the player (PLAYER) when the object dies")
{
	REQUIRE_RETAIL_WORLD(sh);
	const UpgradeDieModuleData *data = nullptr;
	const ThingTemplate *tt = findTemplate<UpgradeDieModuleData>(*sh.world, "UpgradeDie",
		[&](const ThingTemplate &, const UpgradeDieModuleData &d) { return sh.world->upgrades().findUpgrade(d.m_upgradeToRemove) != nullptr; }, &data);
	REQUIRE(tt);
	const UpgradeTemplate *u = sh.world->upgrades().findUpgrade(data->m_upgradeToRemove);
	MESSAGE("UpgradeDie: " << tt->getName() << " removes " << u->getUpgradeName() << (u->getUpgradeType() == UPGRADE_TYPE_PLAYER ? " (PLAYER)" : " (OBJECT)"));
	Game g(*sh.world, *sh.mount);
	Object *producer = g.make(tt, nullptr, 300.0f, 300.0f);
	Object *o = g.make(tt);
	o->setProducer(producer);
	if (u->getUpgradeType() == UPGRADE_TYPE_PLAYER)
	{
		g.player->addUpgrade(u, Player::UPGRADE_STATUS_COMPLETE);
		REQUIRE(g.player->hasUpgradeComplete(u));
	}
	else
	{
		producer->giveUpgrade(u);
		REQUIRE(producer->hasUpgrade(u));
	}
	const std::uint32_t h0 = g.hash();
	o->kill(DEATH_NORMAL);
	if (u->getUpgradeType() == UPGRADE_TYPE_PLAYER)
	{
		CHECK_FALSE(g.player->hasUpgradeComplete(u));
	}
	else
	{
		CHECK_FALSE(producer->hasUpgrade(u));
	}
	CHECK(g.hash() != h0);
}

TEST_CASE("modules retail: HeroDie makes its special power ready at once on the player's objects when the hero dies")
{
	REQUIRE_RETAIL_WORLD(sh);
	const HeroDieModuleData *data = nullptr;
	const ThingTemplate *tt = findTemplate<HeroDieModuleData>(*sh.world, "HeroDie",
		[](const ThingTemplate &, const HeroDieModuleData &d) { return d.m_specialPowerTemplate != nullptr; }, &data);
	REQUIRE(tt);
	MESSAGE("HeroDie: " << tt->getName() << " recharges " << data->m_specialPowerTemplateName);
	// a template whose special power module holds the power (the hero itself, or another object of the player)
	const ThingTemplate *holderT = nullptr;
	for (const ThingTemplate *t : sh.world->things().templates())
	{
		for (const ThingTemplate::Nugget &n : t->getFinalOverride()->behaviorModules().nuggets())
		{
			const SpecialPowerModuleData *d = dynamic_cast<const SpecialPowerModuleData *>(n.data.get());
			if (!holderT && d && d->m_specialPowerTemplate == data->m_specialPowerTemplate)
			{
				holderT = t;
			}
		}
	}
	REQUIRE(holderT);
	MESSAGE("holder: " << holderT->getName());
	Game g(*sh.world, *sh.mount);
	Object *hero = g.make(tt);
	Object *other = g.make(holderT, nullptr, 700.0f, 700.0f);
	g.run(2);
	SpecialPowerModuleInterface *sp = SpecialPowerModules::findModule(*other, data->m_specialPowerTemplate);
	REQUIRE(sp);
	sp->setReadyFrame(g.logic->getFrame() + 1000);
	const std::uint32_t h0 = g.hash();
	hero->kill(DEATH_NORMAL);
	CHECK(sp->getReadyFrame() == g.logic->getFrame());
	CHECK(g.hash() != h0);
	CHECK_FALSE(reportHas(g, "[S-980] HeroDie")); // lane MODULES-2: the walk order is unobservable (RW 0x8C642D only sets the ready frame)
}

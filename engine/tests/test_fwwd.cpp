// OpenBFME unit tests (lane MODS-3): FireWeaponWhenDamagedBehavior (RW 0x885A6F / 0x885CD5 / 0x885D90) and Edain Unchained's FireAtAttacker
// (ModEngineFeature::EdainUnchainedFireAtAttacker). The synthetic cases need no retail files; the retail case SKIPs when ROTWK_INSTALL / BFME2_INSTALL
// are unset. GPL-3.0.

#include "doctest.h"

#include "CombatTestUtil.h"
#include "PathfindTestUtil.h"
#include "RetailTestMount.h"

#include "Common/ModEngineFeatures.h"
#include "Common/PlayerList.h"
#include "Common/StateHash.h"
#include "Common/Team.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Economy.h"
#include "GameLogic/EconomySettings.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/FireWeaponWhenDamagedBehavior.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponNugget.h"

#include <memory>
#include <string>
#include <vector>

using namespace combattest;

namespace
{
// every test leaves the process retail
struct Retail
{
	~Retail() { ModEngineFeatures::reset(); }
};

const char kEdainGameDat[] = "5c31f5036cacb7690a6161b104a3efdb"; // Edain Unchained 4.4.3's game.dat (Common/ModEngineFeatures.h)

DamageInfo hit(const Object *source, float amount, int type)
{
	DamageInfo d;
	d.m_input.m_sourceID = source ? source->getID() : 0;
	d.m_input.m_amount = amount;
	d.m_input.m_damageType = type;
	return d;
}

const char kWeaponsAndArmor[] =
	"Armor FwwdNoArmor\n  Armor = DEFAULT 100%\nEnd\n"
	"Weapon FwwdRingPristine\n  AttackRange = 10\n  DelayBetweenShots = 1000\n  RadiusDamageAffects = ENEMIES\n"
	"  DamageNugget\n    Damage = 7\n    Radius = 30.0\n    DelayTime = 0\n    DamageType = MAGIC\n    DeathType = NORMAL\n  End\nEnd\n"
	"Weapon FwwdRingDamaged\n  AttackRange = 10\n  DelayBetweenShots = 1000\n  RadiusDamageAffects = ENEMIES\n"
	"  DamageNugget\n    Damage = 11\n    Radius = 30.0\n    DelayTime = 0\n    DamageType = MAGIC\n    DeathType = NORMAL\n  End\nEnd\n"
	"Weapon FwwdTick\n  AttackRange = 10\n  DelayBetweenShots = 1000\n  RadiusDamageAffects = ENEMIES\n"
	"  DamageNugget\n    Damage = 1\n    Radius = 30.0\n    DelayTime = 0\n    DamageType = MAGIC\n    DeathType = NORMAL\n  End\nEnd\n"
	"Weapon FwwdBolt\n  AttackRange = 10\n  DelayBetweenShots = 1000\n"
	"  DamageNugget\n    Damage = 9\n    Radius = 0.0\n    DelayTime = 0\n    DamageType = MAGIC\n    DeathType = NORMAL\n  End\nEnd\n";

std::string probe(const std::string &name, const std::string &fields)
{
	return "Object " + name + "\n  KindOf = STRUCTURE\n  ArmorSet\n    Conditions = None\n    Armor = FwwdNoArmor\n  End\n"
		"  Body = StructureBody ModuleTag_Body\n    MaxHealth = 200\n    MaxHealthDamaged = 150\n    MaxHealthReallyDamaged = 50\n  End\n"
		"  Behavior = FireWeaponWhenDamagedBehavior ModuleTag_FWWD\n" + fields + "  End\n"
		"  Geometry = CYLINDER\n  GeometryMajorRadius = 10\n  GeometryHeight = 10\nEnd\n";
}

const char kAttacker[] =
	"Object FwwdAttacker\n  KindOf = INFANTRY\n  ArmorSet\n    Conditions = None\n    Armor = FwwdNoArmor\n  End\n"
	"  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 100\n  End\n"
	"  Geometry = CYLINDER\n  GeometryMajorRadius = 4\n  GeometryHeight = 10\nEnd\n";

struct FwwdWorld : CombatWorld
{
	explicit FwwdWorld(const std::string &objects)
	{
		FireWeaponWhenDamagedBehavior::registerModule(w.modules);
		std::string err = w.load(kWeaponsAndArmor, INI_LOAD_OVERWRITE, "fwwdweapon.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		err = w.load(std::string(kAttacker) + objects, INI_LOAD_OVERWRITE, "fwwd.ini");
		REQUIRE_MESSAGE(err.empty(), err);
	}
};

FireWeaponWhenDamagedBehavior *fwwdOf(Object *o)
{
	for (const auto &m : o->modules())
	{
		if (FireWeaponWhenDamagedBehavior *x = dynamic_cast<FireWeaponWhenDamagedBehavior *>(m.get()))
		{
			return x;
		}
	}
	return nullptr;
}

const FireWeaponWhenDamagedBehaviorModuleData *fwwdData(const ThingTemplate *t)
{
	for (const ThingTemplate::Nugget &n : t->getFinalOverride()->behaviorModules().nuggets())
	{
		if (const auto *d = dynamic_cast<const FireWeaponWhenDamagedBehaviorModuleData *>(n.data.get()))
		{
			return d;
		}
	}
	return nullptr;
}
} // namespace

// RW 0x6533E5 / table RW 0xC06698: the defaults (DamageTypes -1, DamageAmount 0) and RW 0x73A5C9's flag list (it starts from every type, so "+X" alone
// changes nothing: Edain's "DamageTypes = +HERO +HERO_RANGED" is every type)
TEST_CASE("fwwd: the data is RW 0xC06698 over the upgrade table; DamageTypes starts from every type")
{
	Retail guard;
	FwwdWorld f(probe("FwwdDefaults", "") +
		probe("FwwdParsed", "    StartsActive = Yes\n    ReactionWeaponPristine = FwwdRingPristine\n    ReactionWeaponRubble = None\n"
							"    ContinuousWeaponDamaged = FwwdTick\n    DamageTypes = +HERO +HERO_RANGED\n    DamageAmount = 2.5\n    Permanent = Yes\n") +
		probe("FwwdOnly", "    DamageTypes = NONE +SLASH +FORCE\n"));
	const FireWeaponWhenDamagedBehaviorModuleData *d0 = fwwdData(f.w.get("FwwdDefaults"));
	REQUIRE(d0);
	CHECK(d0->m_startsActive == false);
	CHECK(d0->m_damageTypes == 0xFFFFFFFFu);
	CHECK(d0->m_damageAmount == 0.0f);
	CHECK(d0->m_fireAtAttacker == false);
	const FireWeaponWhenDamagedBehaviorModuleData *d1 = fwwdData(f.w.get("FwwdParsed"));
	REQUIRE(d1);
	CHECK(d1->m_startsActive);
	CHECK(d1->m_reactionWeapon[0] == "FwwdRingPristine");
	CHECK(d1->m_reactionWeapon[3].empty());
	CHECK(d1->m_continuousWeapon[1] == "FwwdTick");
	CHECK(d1->m_damageTypes == 0xFFFFFFFFu);
	CHECK(d1->m_damageAmount == 2.5f);
	CHECK(d1->m_permanent); // the upgrade table RW 0xC76AD8 (offset 8) parses; the module's mux slot 7 (RW 0x9188EB) still answers false
	// SLASH is index 2 -> bit 1; FORCE is index 0 -> bit (0 - 1) mod 32 = 31 (x86 shl by cl, RW 0x73A62D)
	CHECK(fwwdData(f.w.get("FwwdOnly"))->m_damageTypes == ((1u << 1) | (1u << 31)));
}

// RW 0x885CD5: the type filter, the amount filter (DamageAmount > dealt: nothing), the damage state's weapon, READY_TO_FIRE (the weapon reloads for
// DelayBetweenShots), the fire at the own position (RW 0x6CF3D2)
TEST_CASE("fwwd: a reaction weapon fires at the object's position on a hit of the right type and amount, by damage state, when ready")
{
	Retail guard;
	FwwdWorld f(probe("FwwdProbe", "    StartsActive = Yes\n    ReactionWeaponPristine = FwwdRingPristine\n    ReactionWeaponDamaged = FwwdRingDamaged\n"
								   "    DamageTypes = NONE +SLASH\n    DamageAmount = 5\n"));
	Object *p = f.unit("FwwdProbe", 'B', 400, 300);
	Object *a = f.unit("FwwdAttacker", 'A', 420, 300); // inside the ring's 30
	Object *far = f.unit("FwwdAttacker", 'A', 480, 300); // outside
	FireWeaponWhenDamagedBehavior *fw = fwwdOf(p);
	REQUIRE(fw);
	CHECK(fw->isAlreadyUpgraded()); // StartsActive: RW 0x855388
	REQUIRE(fw->reactionWeapon(0));
	REQUIRE(fw->reactionWeapon(1));
	CHECK(fw->reactionWeapon(2) == nullptr);
	CHECK(fw->continuousWeapon(0) == nullptr);
	f.frames(2);

	DamageInfo h1 = hit(a, 20.0f, DAMAGE_SLASH);
	p->attemptDamage(h1);
	CHECK(f.health(p) == 180.0f);
	CHECK(fw->reactionShots() == 1);
	CHECK(f.health(a) == 93.0f); // FwwdRingPristine: 7
	CHECK(f.health(far) == 100.0f);

	DamageInfo h2 = hit(a, 20.0f, DAMAGE_SLASH); // the weapon reloads (DelayBetweenShots 1000 = 5 frames)
	p->attemptDamage(h2);
	CHECK(fw->reactionShots() == 1);
	CHECK(f.health(a) == 93.0f);
	f.frames(6);

	DamageInfo pierce = hit(a, 20.0f, DAMAGE_PIERCE); // not in DamageTypes; 140: DAMAGED
	p->attemptDamage(pierce);
	CHECK(f.health(p) == 140.0f);
	CHECK(p->getBodyModule()->getDamageState() == BODY_DAMAGED);
	CHECK(fw->reactionShots() == 1);
	DamageInfo small = hit(a, 4.0f, DAMAGE_SLASH); // DamageAmount 5 > 4 dealt
	p->attemptDamage(small);
	CHECK(fw->reactionShots() == 1);
	DamageInfo exact = hit(a, 5.0f, DAMAGE_SLASH); // 5 > 5 is false: it fires the DAMAGED weapon
	p->attemptDamage(exact);
	CHECK(fw->reactionShots() == 2);
	CHECK(f.health(a) == 82.0f); // FwwdRingDamaged: 11
	f.frames(6);
	DamageInfo really = hit(a, 100.0f, DAMAGE_SLASH); // 31: REALLYDAMAGED, no weapon for it
	p->attemptDamage(really);
	CHECK(p->getBodyModule()->getDamageState() == BODY_REALLYDAMAGED);
	CHECK(fw->reactionShots() == 2);
	CHECK(fw->attackerShots() == 0);
	CHECK(f.health(a) == 82.0f);
}

// RW 0x885A6F / 0x8554D6 / 0x885D90: not StartsActive: nothing until the upgrade; the upgrade wakes the update, which fires the continuous weapon every
// time it is ready
TEST_CASE("fwwd: without StartsActive nothing fires until TriggeredBy; then the continuous weapon fires whenever it is ready")
{
	Retail guard;
	FwwdWorld f(probe("FwwdTicker", "    ReactionWeaponPristine = FwwdRingPristine\n    ContinuousWeaponPristine = FwwdTick\n"));
	Object *p = f.unit("FwwdTicker", 'B', 400, 300);
	Object *a = f.unit("FwwdAttacker", 'A', 420, 300);
	FireWeaponWhenDamagedBehavior *fw = fwwdOf(p);
	REQUIRE(fw);
	CHECK_FALSE(fw->isAlreadyUpgraded());
	f.frames(3);
	DamageInfo h = hit(a, 20.0f, DAMAGE_SLASH);
	p->attemptDamage(h);
	CHECK(fw->reactionShots() == 0);
	CHECK(fw->continuousShots() == 0);
	fw->giveSelfUpgrade(); // the mux's grant (RW 0x855388): the implementation wakes the update next frame
	f.frames(11);
	// ready at once, then every 5 frames (DelayBetweenShots 1000): frames 1, 6, 11 of the 11
	CHECK(fw->continuousShots() == 3);
	CHECK(f.health(a) == 97.0f);

	// the snapshot: the module's weapons and counters round-trip
	StateHasher before;
	fw->crc(before);
	XferSave saved;
	fw->xfer(saved);
	f.frames(5);
	CHECK(fw->continuousShots() == 4);
	{
		XferLoad load(saved.data());
		fw->xfer(load);
	}
	StateHasher after;
	fw->crc(after);
	CHECK(after.value() == before.value());
	CHECK(fw->continuousShots() == 3);
	std::vector<std::uint8_t> truncated = saved.data();
	truncated.pop_back();
	XferLoad lt(truncated);
	CHECK_THROWS_AS(fw->xfer(lt), XferException);
}

// Edain Unchained's FireAtAttacker (0xEE60F0): with the feature the field parses, and Yes fires at the damage's source through RW 0x6CF3AE (a detonation at
// the victim: FwwdBolt's Radius 0 hits it); a source that is gone falls back to the own position. Without the feature the field is unknown (retail's error)
TEST_CASE("fwwd: Edain Unchained's FireAtAttacker fires the reaction weapon at the attacker only with its engine feature")
{
	Retail guard;
	const std::string fields = "    StartsActive = Yes\n    ReactionWeaponPristine = FwwdBolt\n    DamageTypes = NONE +HERO\n    DamageAmount = 1\n";
	{
		CombatWorld plain;
		FireWeaponWhenDamagedBehavior::registerModule(plain.w.modules);
		REQUIRE(plain.w.load(kWeaponsAndArmor, INI_LOAD_OVERWRITE, "fwwdweapon.ini").empty());
		const std::string err = plain.w.load(probe("FwwdAtAttacker", fields + "    FireAtAttacker = Yes\n"), INI_LOAD_OVERWRITE, "fwwd.ini");
		CHECK(err.rfind("Unknown field 'FireAtAttacker' in block 'Object'.", 0) == 0); // retail's unknown-field error (no engine feature)
	}
	for (const bool atAttacker : { false, true })
	{
		std::string error;
		REQUIRE_MESSAGE(ModEngineFeatures::activate({ kEdainGameDat }, &error), error);
		REQUIRE(ModEngineFeatures::has(ModEngineFeature::EdainUnchainedFireAtAttacker));
		FwwdWorld f(probe("FwwdAtAttacker", fields + (atAttacker ? "    FireAtAttacker = Yes\n" : "    FireAtAttacker = No\n")));
		CHECK(fwwdData(f.w.get("FwwdAtAttacker"))->m_fireAtAttacker == atAttacker);
		Object *p = f.unit("FwwdAtAttacker", 'B', 400, 300);
		Object *a = f.unit("FwwdAttacker", 'A', 450, 300);
		FireWeaponWhenDamagedBehavior *fw = fwwdOf(p);
		REQUIRE(fw);
		f.frames(2);
		DamageInfo h = hit(a, 20.0f, DAMAGE_HERO);
		p->attemptDamage(h);
		CHECK(fw->attackerShots() == (atAttacker ? 1u : 0u));
		CHECK(fw->reactionShots() == (atAttacker ? 0u : 1u));
		CHECK(f.health(a) == (atAttacker ? 91.0f : 100.0f)); // FwwdBolt: 9 at the attacker; at the own position Radius 0 reaches no one
		f.frames(6);
		DamageInfo anonymous = hit(nullptr, 20.0f, DAMAGE_HERO); // no source object: retail's fire at the own position
		p->attemptDamage(anonymous);
		CHECK(fw->attackerShots() == (atAttacker ? 1u : 0u));
		CHECK(fw->reactionShots() == (atAttacker ? 1u : 2u));
		ModEngineFeatures::reset();
	}
}

// ---- retail data ---------------------------------------------------------------------------------------------------------------------------------------
namespace
{
struct RetailShared
{
	retailtest::Mount *mount = nullptr;
	std::unique_ptr<RetailObjectWorld> world;
	std::string error;
};

RetailShared &retailShared()
{
	static RetailShared s;
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

// the first retail weapon (store order) that fires one plain DamageNugget at once around its target: a fixed delay, no clip, no upgrade gate, no scalar,
// no taper, enemies in its radius
const WeaponTemplate *plainRingWeapon(const DamageNugget **out)
{
	for (const std::shared_ptr<WeaponTemplate> &t : TheWeaponStore->templates())
	{
		if (t->m_nuggets.size() != 1 || t->m_delayBetweenShotsMin != t->m_delayBetweenShotsMax || t->m_delayBetweenShotsMin == 0 || t->m_clipSize != 0 ||
			(t->m_affectsMask & WEAPON_AFFECTS_ENEMIES) == 0)
		{
			continue;
		}
		const DamageNugget *n = dynamic_cast<const DamageNugget *>(t->m_nuggets[0].get());
		if (n && n->m_delayTime == 0 && n->m_radius >= 20.0f && n->m_minRadius == 0.0f && n->m_damage >= 1.0f && n->m_damageScalar.empty() && // tolerance: a search predicate, not an assertion
			n->m_requiredUpgradeNames.empty() && n->m_forbiddenUpgradeNames.empty() && n->m_damageTaperOff == -1.0f && !n->m_drainLife &&
			n->m_damageType != DAMAGE_HEALING && n->m_damageType != DAMAGE_UNRESISTABLE)
		{
			*out = n;
			return t.get();
		}
	}
	return nullptr;
}
} // namespace

// RotWK 2.01's data declares the module on no template (RotWK and BFME2 INI.big / _patch201ini.big, maps: 0 "FireWeaponWhenDamagedBehavior"); a probe object
// loaded into the retail world with a retail weapon shows the retail conditions (the type, the amount, READY_TO_FIRE) and the retail damage reaching an enemy
TEST_CASE("fwwd retail: no 2.01 template uses the module; a probe with a retail reaction weapon hits an enemy for the weapon's damage")
{
	RetailShared &sh = retailShared();
	if (!sh.mount || !sh.mount->fs)
	{
		MESSAGE("SKIP: ROTWK_INSTALL / BFME2_INSTALL not set (retail FireWeaponWhenDamagedBehavior test)");
		return;
	}
	REQUIRE_MESSAGE(sh.world, sh.error);
	auto contextScope = sh.world->enterContext();
	int uses = 0;
	for (const ThingTemplate *t : sh.world->things().templates())
	{
		for (const auto &m : t->moduleList())
		{
			uses += m.first == "FireWeaponWhenDamagedBehavior";
		}
	}
	CHECK(uses == 0);

	REQUIRE(TheWeaponStore);
	const DamageNugget *nugget = nullptr;
	const WeaponTemplate *wt = plainRingWeapon(&nugget);
	REQUIRE(wt);
	MESSAGE("reaction weapon: " << wt->getName() << " (" << nugget->m_damage << " " << std::string(TheDamageNames[nugget->m_damageType]) << ", radius " << nugget->m_radius << ")");
	const std::string text = std::string("Armor FwwdNoArmor\n  Armor = DEFAULT 100%\nEnd\n") +
		probe("FwwdRetailProbe", "    StartsActive = Yes\n    ReactionWeaponPristine = " + wt->getName() + "\n    DamageTypes = NONE +HERO\n    DamageAmount = 10\n") +
		"Object FwwdRetailAttacker\n  KindOf = INFANTRY\n  ArmorSet\n    Conditions = None\n    Armor = FwwdNoArmor\n  End\n"
		"  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 10000\n  End\n  Geometry = CYLINDER\n  GeometryMajorRadius = 4\n  GeometryHeight = 10\nEnd\n";
	{
		std::vector<std::uint8_t> bytes(text.begin(), text.end());
		INI ini(sh.world->iniEnvironment());
		ini.loadMemory("data\\ini\\object\\fwwdprobe.ini", bytes, INI_LOAD_OVERWRITE);
	}
	std::uint32_t hashes[2] = {};
	for (int run = 0; run < 2; ++run)
	{
		TeamFactory teams;
		PlayerList players(sh.world->nameKeys(), sh.world->playerTemplates(), teams);
		SkirmishSetup setup;
		setup.players.push_back({ "Tester", "FactionMen", true, 0, 0, 0 });
		setup.players.push_back({ "Enemy", "FactionMordor", false, 1, 0, 1 });
		REQUIRE(players.setupSkirmish(setup).empty());
		GameLogic logic(sh.world->things(), sh.world->modules(), players, RandomAlgorithm::ZH_CarryChain);
		logic.random().seedRandom(1);
		std::string err;
		REQUIRE_MESSAGE(GameLogicSettingsLoader::load(*sh.mount->fs, logic.settings(), &err), err);
		logic.setUpgradeTypes(&sh.world->upgradeTypes());
		REQUIRE_MESSAGE(EconomySettings::load(*sh.mount->fs, logic.economy().settings(), &err), err);
		// TheAI on a flat synthetic terrain (the move tests' setup, Mod2TestUtil.h): the radius damage reads the objects' pathfinder geometry
		AIWorldConfig cfg;
		REQUIRE_MESSAGE(AIWorldConfigLoader::load(*sh.mount->fs, cfg, &err), err);
		pathtest::SyntheticTerrain terrain(100, 100);
		std::unique_ptr<AIWorld> ai = std::make_unique<AIWorld>(logic, cfg, sh.world->iniMacros());
		ai->attach();
		ai->newMap(terrain);
		logic.partition().setRegion(0.0f, 0.0f, 1000.0f, 1000.0f); // the terrain's extent (RW 0x62FCCD)
		Player *tester = players.findPlayerWithName("Tester");
		Player *enemy = players.findPlayerWithName("Enemy");
		REQUIRE(tester);
		REQUIRE(enemy);
		auto make = [&](const char *name, Player *owner, float x) {
			Object *o = logic.newObject(sh.world->things().findTemplate(name), owner->getDefaultTeam(), ObjectStatusMaskType{});
			REQUIRE(o);
			const Coord3D pos{ x, 300.0f, 0.0f };
			o->setPosition(&pos);
			return o;
		};
		Object *p = make("FwwdRetailProbe", tester, 300.0f);
		Object *a = make("FwwdRetailAttacker", enemy, 305.0f);
		FireWeaponWhenDamagedBehavior *fw = fwwdOf(p);
		REQUIRE(fw);
		REQUIRE(fw->reactionWeapon(0));
		for (int i = 0; i < 2; ++i)
		{
			logic.runLogicFrame();
		}
		DamageInfo slash = hit(a, 20.0f, DAMAGE_SLASH); // not HERO
		p->attemptDamage(slash);
		CHECK(fw->reactionShots() == 0);
		DamageInfo weak = hit(a, 9.0f, DAMAGE_HERO); // DamageAmount 10 > 9
		p->attemptDamage(weak);
		CHECK(fw->reactionShots() == 0);
		DamageInfo heroHit = hit(a, 10.0f, DAMAGE_HERO);
		p->attemptDamage(heroHit);
		CHECK(fw->reactionShots() == 1);
		CHECK(a->getBodyModule()->getHealth() == 10000.0f - nugget->m_damage);
		DamageInfo again = hit(a, 10.0f, DAMAGE_HERO); // the weapon reloads: not READY_TO_FIRE
		p->attemptDamage(again);
		CHECK(fw->reactionShots() == 1);
		CHECK(a->getBodyModule()->getHealth() == 10000.0f - nugget->m_damage);
		for (int i = 0; i < 3; ++i)
		{
			logic.runLogicFrame();
		}
		hashes[run] = logic.computeStateHash();
		logic.reset();
		ai.reset();
	}
	CHECK(hashes[0] == hashes[1]);
}

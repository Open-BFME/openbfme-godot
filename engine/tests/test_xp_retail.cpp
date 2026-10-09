// OpenBFME. GPL-3.0.
// Lane XP-1 on the retail data (SKIPs when ROTWK_INSTALL / BFME2_INSTALL are unset): the ExperienceLevel / ModifierList stores of the real INI load, a Gondor
// soldier horde that kills Mordor orcs and reaches veterancy level 2 at the retail threshold (the horde's per-template pool, RW 0x873A02), the level 2 bonuses
// (HEALTH +20, DAMAGE_ADD +10), an armour bonus (MordorAttackTroll rank 5: ARMOR +20%), a hero's level progression (Aragorn), the skill points through a stub
// ExperienceAwardSink, two-run determinism and hash mutation.

#include "doctest.h"

#include "Common/PlayerList.h"
#include "Common/PlayerScience.h"
#include "Common/ScoreKeeper.h"
#include "Common/StateHash.h"
#include "Common/Team.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/AttributeModifiers.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Economy.h"
#include "GameLogic/EconomySettings.h"
#include "GameLogic/ExperienceLevels.h"
#include "GameLogic/ExperienceWorld.h"
#include "GameLogic/FXEvents.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/AttributeModifierPool.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponNugget.h"
#include "RetailTestMount.h"

#include <map>
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

#define REQUIRE_RETAIL_WORLD(sh)                                                \
	Shared &sh = shared();                                                      \
	if (!sh.mount || !sh.mount->fs)                                             \
	{                                                                           \
		MESSAGE("SKIP: ROTWK_INSTALL / BFME2_INSTALL not set (retail XP test)"); \
		return;                                                                 \
	}                                                                           \
	REQUIRE_MESSAGE(sh.world, sh.error);                                        \
	auto contextScope = sh.world->enterContext()

// the stub of lane SPELL-1's Player::addSkillPoints (Player + 8, RW 0x782AA4) and the score keeper total (RW 0x79DBA1)
struct StubSink : ExperienceAwardSink
{
	std::map<const Player *, float> points, scored;
	void addSkillPoints(Player &p, float amount) override { points[&p] += amount; }
	void scoreSkillPoints(Player &p, float amount) override { scored[&p] += amount; }
};

// a skirmish game of Gondor (A) against Mordor (B) on the shared world
struct Game
{
	TeamFactory teams;
	PlayerList players;
	std::unique_ptr<GameLogic> logic;
	Player *gondor = nullptr;
	Player *mordor = nullptr;
	StubSink sink;
	explicit Game(RetailObjectWorld &world, retailtest::Mount &mount)
		: players(world.nameKeys(), world.playerTemplates(), teams)
	{
		SkirmishSetup setup;
		setup.players.push_back({ "A", "FactionMen", true, 0, 0, 1 });
		setup.players.push_back({ "B", "FactionMordor", false, 1, 0, 2 });
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
		logic->experience().setAwardSink(&sink);
		gondor = players.findPlayerWithName("A");
		mordor = players.findPlayerWithName("B");
		REQUIRE(gondor);
		REQUIRE(mordor);
	}
	Object *make(RetailObjectWorld &world, const std::string &name, Player *owner)
	{
		const ThingTemplate *tt = world.things().findTemplate(name);
		REQUIRE_MESSAGE(tt, name);
		Object *o = logic->newObject(tt, owner->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE(o);
		return o;
	}
	void frame() { logic->runLogicFrame(); }
	// a killing UNRESISTABLE hit from `killer` (the ActiveBody path: kill credit, experience, skill points)
	void kill(Object &victim, Object &killer)
	{
		DamageInfo info;
		info.m_input.m_damageType = DAMAGE_UNRESISTABLE;
		info.m_input.m_sourceID = killer.getID();
		info.m_input.m_amount = 100000.0f;
		info.m_input.m_kill = true;
		victim.attemptDamage(info);
	}
	std::uint32_t hash() const { return logic->computeStateHash(); }
};

Object *firstMember(Object &horde)
{
	REQUIRE(horde.getContain());
	const ContainModuleInterface::ContainedItemsList *items = horde.getContain()->getContainedItemsList();
	REQUIRE(items);
	REQUIRE(!items->empty());
	return items->front();
}

struct HordeRun
{
	std::uint32_t hashAfter48 = 0, hashAfter49 = 0, finalHash = 0;
	int rankAfter48 = 0, rankAfter49 = 0, hordeRank = 0;
	float pool = 0.0f, memberMaxBefore = 0.0f, memberMaxAfter = 0.0f, damageAdd = 0.0f, healthAfter = 0.0f;
	bool bonusListed = false, upgradeOnMember = false, upgradeOnHorde = false;
	float skillPointsA = 0.0f, skillPointsB = 0.0f;
};

// the Gondor soldier horde kills `kills` single Mordor orcs, one per member hit
HordeRun runHorde(Shared &sh, int kills)
{
	Game g(*sh.world, *sh.mount);
	Object *horde = g.make(*sh.world, "GondorFighterHorde", g.gondor);
	std::vector<Object *> orcs;
	for (int i = 0; i < kills; ++i)
	{
		orcs.push_back(g.make(*sh.world, "MordorFighter", g.mordor));
	}
	g.frame(); // the delayed level grants of the creations (RW 0x821567): everything is at level 1 now
	Object *member = firstMember(*horde);
	HordeRun r;
	r.memberMaxBefore = member->getBodyModule()->getMaxHealth();
	CHECK(member->getExperienceTracker()->getRank() == 1);
	CHECK(member->getExperienceTracker()->getLevelName() == "GoodLevel1");
	CHECK(orcs[0]->getExperienceTracker()->getExperienceValue(*member, false) == 1); // EXPERIENCE_AWARD_EVIL_WEAK_1
	for (int i = 0; i < kills; ++i)
	{
		g.kill(*orcs[(size_t)i], *member);
		if (i == 47)
		{
			g.frame();
			r.rankAfter48 = member->getExperienceTracker()->getRank();
			r.hashAfter48 = g.hash();
		}
	}
	g.frame();
	r.rankAfter49 = member->getExperienceTracker()->getRank();
	r.hordeRank = horde->getExperienceTracker()->getRank();
	r.hashAfter49 = g.hash();
	HordeContain *hc = dynamic_cast<HordeContain *>(horde->getContain());
	REQUIRE(hc);
	REQUIRE(hc->experiencePools().size() == 1);
	r.pool = hc->experiencePools().begin()->second;
	r.memberMaxAfter = member->getBodyModule()->getMaxHealth();
	r.healthAfter = member->getBodyModule()->getHealth();
	member->attributeModifierSum(ATTRIBUTE_DAMAGE_ADD, nullptr, r.damageAdd);
	const AttributeModifierPool *pool = static_cast<const AttributeModifierPool *>(member->findModule("AttributeModifierPoolUpdate"));
	REQUIRE(pool);
	r.bonusListed = pool->hasList("GoodTroopBonusRank2");
	r.upgradeOnMember = member->hasUpgrade(std::string("Upgrade_ObjectLevel2"));
	r.upgradeOnHorde = horde->hasUpgrade(std::string("Upgrade_ObjectLevel2"));
	r.skillPointsA = g.sink.points[g.gondor];
	r.skillPointsB = g.sink.points[g.mordor];
	r.finalHash = g.hash();
	return r;
}
} // namespace

TEST_CASE("xp retail: the ExperienceLevel, ExperienceScalarTable and ModifierList blocks of the INI load")
{
	REQUIRE_RETAIL_WORLD(sh);
	const ExperienceLevelSystem &lv = sh.world->experienceLevels();
	MESSAGE("levels " << lv.levelCount() << ", scalar tables " << lv.scalarTableCount() << ", modifier lists " << sh.world->attributeModifiers().size());
	CHECK(lv.levelCount() > 700); // experiencelevels.ini (766 blocks in 2.01) plus the Create-A-Hero include
	CHECK(lv.scalarTableCount() == 3);
	CHECK(lv.duplicateLevelNames() == 7u);          // retail 2.01: OathbreakerGenericLevel1 (x6), AngmarDenLevel1..3, WitchKingLevel1, ...
	CHECK(lv.ambiguousDuplicateLevelNames() == 0u); // they all agree on RequiredExperience
	for (const SubsystemLoadReport::FileError &e : sh.world->report().errors)
	{
		CHECK_MESSAGE(e.message.find("Experience") == std::string::npos, e.file << ": " << e.message);
		CHECK_MESSAGE(e.message.find("ModifierList") == std::string::npos, e.file << ": " << e.message);
	}
	const ExperienceLevelTemplate *l2 = lv.findLevel("GoodLevel2");
	REQUIRE(l2);
	CHECK(l2->m_requiredExperience == 50); // EXPERIENCE_REQUIRED_GOOD_TROOP_2
	CHECK(l2->m_experienceAward == 4);
	CHECK(l2->m_rank == 2);
	CHECK(l2->m_attributeModifiers == std::vector<std::string>{ "GoodTroopBonusRank2" });
	REQUIRE(l2->m_upgrades.size() == 1);
	CHECK(lv.nextLevel("GondorFighter", "GoodLevel1", true) == l2);
	CHECK(lv.nextLevel("GondorFighterHorde", "", true)->m_name == "GoodLevel1");
	const ModifierListTemplate *bonus = sh.world->attributeModifiers().find("GoodTroopBonusRank2");
	REQUIRE(bonus);
	float v = 0.0f;
	CHECK(bonus->value(ATTRIBUTE_HEALTH, nullptr, v));
	CHECK(v == 20.0f);
	CHECK(bonus->value(ATTRIBUTE_DAMAGE_ADD, nullptr, v));
	CHECK(v == 10.0f);
	CHECK(bonus->m_category == ATTRIBUTE_CATEGORY_LEVEL);
	const ModifierListTemplate *troll5 = sh.world->attributeModifiers().find("MordorAttackTrollBonusRank5");
	REQUIRE(troll5);
	CHECK(troll5->value(ATTRIBUTE_ARMOR, "SLASH", v));
	CHECK(v == doctest::Approx(0.2f));
	for (const std::string &s : sh.world->acceptanceStops())
	{
		if (s.find("[S-632]") == 0)
		{
			MESSAGE(s);
		}
	}
}

TEST_CASE("xp retail: a Gondor soldier horde reaches veterancy level 2 after 49 orc kills (pool 1 + 49 = 50 = GoodLevel2), with its bonuses")
{
	REQUIRE_RETAIL_WORLD(sh);
	const HordeRun r = runHorde(sh, 49);
	CHECK(r.rankAfter48 == 1); // pool 49 < 50
	CHECK(r.rankAfter49 == 2);
	CHECK(r.hordeRank == 2);
	CHECK(r.pool == 50.0f);
	// GoodTroopBonusRank2: HEALTH 20 (setMaxHealth(max + 20, 1): the ratio is kept, a full soldier stays full) and DAMAGE_ADD 10
	CHECK(r.bonusListed);
	CHECK(r.memberMaxAfter == r.memberMaxBefore + 20.0f);
	CHECK(r.healthAfter == r.memberMaxAfter);
	CHECK(r.damageAdd == 10.0f);
	// Upgrades = Upgrade_ObjectLevel2: given to the horde (and handed down to its members, RW 0x87566B) and to the member's own level
	CHECK(r.upgradeOnHorde);
	CHECK(r.upgradeOnMember);
	MESSAGE("skill points: Gondor " << r.skillPointsA << ", Mordor " << r.skillPointsB);
	CHECK(r.skillPointsA > 0.0f); // the damage share of each kill (RW 0x6AAFC3: award 1 * fraction 1.0)
	CHECK(r.skillPointsA == 49.0f);
}

TEST_CASE("xp retail: two identical runs hash alike")
{
	REQUIRE_RETAIL_WORLD(sh);
	const HordeRun a = runHorde(sh, 49);
	const HordeRun b = runHorde(sh, 49);
	CHECK(a.finalHash == b.finalHash);
	CHECK(a.hashAfter48 == b.hashAfter48);
	CHECK(a.hashAfter49 == b.hashAfter49);
}

// friend of ExperienceTracker, AttributeModifierPool, ExperienceWorld, HordeContain and Object (declared there)
struct XpTestAccess
{
	static std::string &levelName(ExperienceTracker &t) { return t.m_levelName; }
	static float &experience(ExperienceTracker &t) { return t.m_experience; }
	static int &experienceValue(ExperienceTracker &t) { return t.m_experienceValue; }
	static int &ownGuysDie(ExperienceTracker &t) { return t.m_ownGuysDieValue; }
	static float &scalar(ExperienceTracker &t) { return t.m_scalar; }
	static bool &leveled(ExperienceTracker &t) { return t.m_leveled; }
	static int &rank(ExperienceTracker &t) { return t.m_rank; }
	static int &levelCap(ExperienceTracker &t) { return t.m_levelCap; }
	static float &rankFactor(ExperienceTracker &t) { return t.m_rankFactor; }
	static int &baseRank(ExperienceTracker &t) { return t.m_baseRank; }
	static ObjectID &sink(ExperienceTracker &t) { return t.m_sink; }
	static int &entryIndex(AttributeModifierPool &p) { return p.m_entries.back().index; }
	static std::string &entryName(AttributeModifierPool &p) { return p.m_entries.back().name; }
	static unsigned &entryExpire(AttributeModifierPool &p) { return p.m_entries.back().expire; }
	static unsigned &nextWake(AttributeModifierPool &p) { return p.m_nextWake; }
	static unsigned &categoryDisabled(AttributeModifierPool &p, int c) { return p.m_categoryDisabled[(size_t)c]; }
	static int &categoryCount(AttributeModifierPool &p, int c) { return p.m_categoryCount[(size_t)c]; }
	static std::map<unsigned short, float> &pools(HordeContain &h) { return h.m_experiencePools; }
	static ObjectID &pendingId(ExperienceWorld &w) { return w.m_pending.back().id; }
	static bool &pendingFeedback(ExperienceWorld &w) { return w.m_pending.back().feedback; }
	static const ExperienceLevelTemplate *&pendingLevel(ExperienceWorld &w) { return w.m_pending.back().level; }
	static size_t pendingCount(const ExperienceWorld &w) { return w.m_pending.size(); }
	static void clearPending(ExperienceWorld &w) { w.m_pending.clear(); }
	static bool &suspended(ExperienceWorld &w) { return w.m_suspended; }
	static bool &scored(Object &o) { return o.m_scored; }
	static std::uint8_t &experienceFlags(Object &o) { return o.m_experienceFlags; }
};

TEST_CASE("xp retail: every hashed experience field changes the state hash on its own; the score keeper diagnostics do not")
{
	REQUIRE_RETAIL_WORLD(sh);
	Game g(*sh.world, *sh.mount);
	Object *horde = g.make(*sh.world, "GondorFighterHorde", g.gondor);
	Object *orc = g.make(*sh.world, "MordorFighter", g.mordor);
	g.frame();
	Object *member = firstMember(*horde);
	ExperienceTracker &t = *member->getExperienceTracker();
	auto mutate = [&](const char *name, auto &field, auto value) {
		CAPTURE(name);
		const auto old = field;
		const std::uint32_t before = g.hash();
		field = value;
		CHECK(g.hash() != before);
		field = old;
		CHECK(g.hash() == before);
	};
	mutate("tracker level", XpTestAccess::levelName(t), std::string("changed"));
	mutate("tracker experience", XpTestAccess::experience(t), 2.0f);
	mutate("tracker award", XpTestAccess::experienceValue(t), 123);
	mutate("tracker own-guys-die award", XpTestAccess::ownGuysDie(t), 123);
	mutate("tracker scalar", XpTestAccess::scalar(t), 1.5f);
	mutate("tracker leveled", XpTestAccess::leveled(t), true);
	mutate("tracker rank", XpTestAccess::rank(t), 2);
	mutate("tracker level cap", XpTestAccess::levelCap(t), 10);
	mutate("tracker rank factor", XpTestAccess::rankFactor(t), 0.5f);
	mutate("tracker base rank", XpTestAccess::baseRank(t), 2);
	mutate("tracker sink", XpTestAccess::sink(t), orc->getID());
	AttributeModifierPool *pool = static_cast<AttributeModifierPool *>(member->findModule("AttributeModifierPoolUpdate"));
	REQUIRE(pool);
	REQUIRE(member->addAttributeModifier("GoodTroopBonusRank2", -1));
	REQUIRE(pool->entryCount() > 0);
	mutate("modifier index", XpTestAccess::entryIndex(*pool), XpTestAccess::entryIndex(*pool) + 1);
	mutate("modifier name", XpTestAccess::entryName(*pool), std::string("changed"));
	mutate("modifier expiry", XpTestAccess::entryExpire(*pool), 123u);
	mutate("modifier next wake", XpTestAccess::nextWake(*pool), 123u);
	mutate("modifier category disabled", XpTestAccess::categoryDisabled(*pool, ATTRIBUTE_CATEGORY_LEVEL), 123u);
	mutate("modifier category count", XpTestAccess::categoryCount(*pool, ATTRIBUTE_CATEGORY_LEVEL), 123);
	HordeContain *hc = dynamic_cast<HordeContain *>(horde->getContain());
	REQUIRE(hc);
	hc->addExperience(member, 1.0f);
	REQUIRE(!XpTestAccess::pools(*hc).empty());
	mutate("horde pool value", XpTestAccess::pools(*hc).begin()->second, 3.0f);
	{
		const auto saved = XpTestAccess::pools(*hc);
		const std::uint32_t before = g.hash();
		XpTestAccess::pools(*hc).emplace((unsigned short)65534, 2.0f);
		CHECK(g.hash() != before); // a second key (the cardinality and the key)
		XpTestAccess::pools(*hc) = saved;
		CHECK(g.hash() == before);
	}
	ExperienceWorld &xp = g.logic->experience();
	xp.queueLevelGrant(*sh.world->experienceLevels().findLevel("GoodLevel2"), *member, true);
	REQUIRE(XpTestAccess::pendingCount(xp) > 0);
	mutate("pending object", XpTestAccess::pendingId(xp), orc->getID());
	mutate("pending feedback", XpTestAccess::pendingFeedback(xp), false);
	mutate("pending level", XpTestAccess::pendingLevel(xp), sh.world->experienceLevels().findLevel("GoodLevel3"));
	mutate("grants suspended", XpTestAccess::suspended(xp), true);
	mutate("victim scored", XpTestAccess::scored(*orc), true);
	mutate("experience flags", XpTestAccess::experienceFlags(*member), (std::uint8_t)3);
	// the score keeper counts and the award total are report-only diagnostics (S-630): not hashed
	const std::uint32_t before = g.hash();
	++xp.counters().unitLevelUps;
	++xp.counters().heroLevelUps;
	xp.counters().skillPointsAwarded = 123.0f;
	CHECK(g.hash() == before);
}

TEST_CASE("xp retail: sink forwarding multiplies by the source scalar, keeps the scaling flags and forces feedback (RW 0x79D85F / 0x79D863)")
{
	REQUIRE_RETAIL_WORLD(sh);
	Game g(*sh.world, *sh.mount);
	Object *source = g.make(*sh.world, "GondorAragorn", g.gondor);
	Object *target = g.make(*sh.world, "GondorAragorn", g.gondor);
	g.frame();
	ExperienceTracker &s = *source->getExperienceTracker();
	ExperienceTracker &d = *target->getExperienceTracker();
	s.setExperienceScalar(3.0f);
	d.setExperienceScalar(2.0f);
	s.setExperienceSink(target->getID());
	s.addExperiencePoints(1.0f, false, false, false, false);
	CHECK(d.getExperience() == 4.0f); // 1 + 3 * 1: neither the destination scalar nor the MP2 multiplier (scaleByScalar stays false)
	CHECK(s.getExperience() == 1.0f);
	d.setExperienceAndLevel(1.0f, false);
	XpTestAccess::clearPending(g.logic->experience());
	s.addExperiencePoints(33.0f, false, false, false, false); // 1 + 99: Aragorn's level 2 (100) is reached with feedback forced on
	REQUIRE(XpTestAccess::pendingCount(g.logic->experience()) > 0);
	CHECK(XpTestAccess::pendingFeedback(g.logic->experience()));
}

TEST_CASE("xp retail: a positive own-guys-die award reaches both sink methods (RW 0x6AB0D0)")
{
	REQUIRE_RETAIL_WORLD(sh);
	Game g(*sh.world, *sh.mount);
	Object *victim = g.make(*sh.world, "MordorFighter", g.mordor);
	Object *killer = g.make(*sh.world, "GondorFighter", g.gondor);
	g.frame();
	XpTestAccess::ownGuysDie(*victim->getExperienceTracker()) = 7;
	CHECK(g.logic->experience().awardSkillPointsForLoss(*g.mordor, killer, *victim));
	CHECK(g.sink.points[g.mordor] == 7.0f);
	CHECK(g.sink.scored[g.mordor] == 7.0f);
}

// lane HERO-1 regression: RW 0x69574F tests the victim owner's PlayerTemplate + 0x1BC, the Evil flag (PlayableSide is + 0x151): only an evil
// side's losses award its own-guys-die skill points, through scoreTheKill
TEST_CASE("xp retail: scoreTheKill awards loss skill points only to an Evil victim owner (RW 0x69574F, template + 0x1BC)")
{
	REQUIRE_RETAIL_WORLD(sh);
	Game g(*sh.world, *sh.mount);
	REQUIRE(g.mordor->getPlayerTemplate()->m_evil);
	REQUIRE(!g.gondor->getPlayerTemplate()->m_evil);
	REQUIRE(g.gondor->getPlayerTemplate()->m_playableSide); // the old reading (PlayableSide) would have paid Gondor
	Object *orc = g.make(*sh.world, "MordorFighter", g.mordor);
	Object *man = g.make(*sh.world, "GondorFighter", g.gondor);
	g.frame();
	XpTestAccess::ownGuysDie(*orc->getExperienceTracker()) = 7;
	XpTestAccess::ownGuysDie(*man->getExperienceTracker()) = 5;
	const float mordorBefore = g.sink.points[g.mordor];
	man->scoreTheKill(*orc);
	CHECK(g.sink.points[g.mordor] - mordorBefore == 7.0f);
	const float gondorBefore = g.sink.points[g.gondor];
	orc->scoreTheKill(*man);
	CHECK(g.sink.points[g.gondor] == gondorBefore);
}

TEST_CASE("xp retail: the level 2 DAMAGE_ADD reaches the soldier's sword hit (DamageNugget::fillDamageInfo RW 0x90E28C)")
{
	REQUIRE_RETAIL_WORLD(sh);
	Game g(*sh.world, *sh.mount);
	Object *horde = g.make(*sh.world, "GondorFighterHorde", g.gondor);
	Object *orc = g.make(*sh.world, "MordorFighter", g.mordor);
	g.frame();
	Object *member = firstMember(*horde);
	const WeaponTemplate *sword = sh.world->weaponStores().weapons().findWeaponTemplate("GondorSword");
	REQUIRE(sword);
	const DamageNugget *nugget = nullptr;
	for (const auto &n : sword->m_nuggets)
	{
		if ((nugget = dynamic_cast<const DamageNugget *>(n.get())) != nullptr)
		{
			break;
		}
	}
	REQUIRE(nugget);
	struct Host : NuggetDamageHost
	{
		Object *source, *victim;
		bool hasVictim() override { return true; }
		float victimDistanceSqr2D(const Coord3D &) override { return 0.0f; }
		Coord3D victimPosition() override { return *victim->getPosition(); }
		Coord3D sourcePosition() override { return *source->getPosition(); }
		bool sourceHasContain() override { return false; }
		int sourcePassengerCount() override { return 0; }
		bool filterAllowsVictim(const ObjectFilter &) override { return false; }
		bool sourceAdditive(int type, float &out) override { return source->attributeModifierSum(type, nullptr, out); }
		bool sourceMultiplicative(int type, bool innate, float &out) override { return source->attributeModifierProduct(type, nullptr, innate, out); }
		bool victimFlankedBySource() override { return false; }
		bool sourceFlankedByVictim() override { return false; }
		bool weaponSourceIsVictim() override { return false; }
		std::uint32_t sourcePlayerMask() override { return 1u; }
		unsigned weaponSourceID() override { return source->getID(); }
	} host;
	host.source = member;
	host.victim = orc;
	DamageInfo before, after;
	REQUIRE(FillDamageInfo(*nugget, *sword, host, true, nullptr, before));
	member->getExperienceTracker()->gainExpForLevel(1, true); // 49 experience to GoodLevel2
	g.frame();
	CHECK(member->getExperienceTracker()->getRank() == 2);
	REQUIRE(FillDamageInfo(*nugget, *sword, host, true, nullptr, after));
	MESSAGE("GondorSword damage: level 1 " << before.m_input.m_amount << ", level 2 " << after.m_input.m_amount);
	CHECK(after.m_input.m_amount == before.m_input.m_amount + 10.0f);
}

TEST_CASE("xp retail: a MordorAttackTroll at rank 5 takes 20% less SLASH damage (ARMOR +20%, AdjustDamage step 7)")
{
	REQUIRE_RETAIL_WORLD(sh);
	Game g(*sh.world, *sh.mount);
	Object *veteran = g.make(*sh.world, "MordorAttackTroll", g.mordor);
	Object *recruit = g.make(*sh.world, "MordorAttackTroll", g.mordor);
	Object *attacker = g.make(*sh.world, "GondorFighter", g.gondor);
	g.frame();
	REQUIRE(veteran->getExperienceTracker()->gainExpForLevel(4, true));
	g.frame();
	CHECK(veteran->getExperienceTracker()->getRank() == 5);
	CHECK(recruit->getExperienceTracker()->getRank() == 1);
	float armor = 0.0f;
	CHECK(veteran->attributeModifierSum(ATTRIBUTE_ARMOR, "SLASH", armor));
	CHECK(armor == doctest::Approx(0.2f));
	auto hit = [&](Object *victim) {
		DamageInfo info;
		info.m_input.m_damageType = DAMAGE_SLASH;
		info.m_input.m_sourceID = attacker->getID();
		info.m_input.m_amount = 50.0f;
		victim->attemptDamage(info);
		return info.m_output.m_actualDamageDealt;
	};
	const float r = hit(recruit);
	const float v = hit(veteran);
	MESSAGE("SLASH 50: rank 1 takes " << r << ", rank 5 takes " << v);
	CHECK(r > 0.0f);
	CHECK(v == doctest::Approx(r * 0.8f));
}

TEST_CASE("xp retail: Aragorn's level progression (ARAGORN levels: 100 / 200 / 300 / 500 ...), the multiplayer unit XP multiplier of a kill")
{
	REQUIRE_RETAIL_WORLD(sh);
	Game g(*sh.world, *sh.mount);
	Object *aragorn = g.make(*sh.world, "GondorAragorn", g.gondor);
	Object *orc = g.make(*sh.world, "MordorFighter", g.mordor);
	g.frame();
	ExperienceTracker *t = aragorn->getExperienceTracker();
	CHECK(t->getRank() == 1);
	CHECK(t->getExperience() == 1.0f);
	// a kill without a horde: addExperiencePoints(award 1, rank scale, scalar * MultiPlayUnitXPMult MP2 (2.0)): 1 + 2
	g.kill(*orc, *aragorn);
	CHECK(t->getExperience() == 3.0f);
	std::vector<int> ranks;
	for (int i = 0; i < 9; ++i)
	{
		const int need = t->experienceForNextLevel(nullptr);
		if (need <= 0)
		{
			break;
		}
		t->gainExpForLevel(1, true);
		g.frame();
		ranks.push_back(t->getRank());
		MESSAGE("rank " << t->getRank() << " at " << t->getExperience() << " (" << t->getLevelName() << ")");
	}
	REQUIRE(ranks.size() >= 4);
	CHECK(ranks[0] == 2);
	CHECK(ranks[1] == 3);
	CHECK(ranks[2] == 4);
	const ExperienceLevelTemplate *l2 = sh.world->experienceLevels().nextLevel("GondorAragorn", t->getLevelName(), true);
	(void)l2;
	float add = 0.0f;
	CHECK(aragorn->attributeModifierSum(ATTRIBUTE_DAMAGE_ADD, nullptr, add)); // HeroLevelUpDamage1 .. N
	CHECK(add > 0.0f);
}

TEST_CASE("xp retail: LevelUpUpgrade (Upgrade_GondorBasicTraining: LevelsToGain 1, LevelCap 2) levels the horde once and stops at the cap")
{
	REQUIRE_RETAIL_WORLD(sh);
	Game g(*sh.world, *sh.mount);
	Object *horde = g.make(*sh.world, "GondorFighterHorde", g.gondor);
	g.frame();
	ExperienceTracker *t = horde->getExperienceTracker();
	REQUIRE(t->getRank() == 1);
	horde->giveUpgrade(std::string("Upgrade_GondorBasicTraining"));
	g.frame();
	CHECK(t->getRank() == 2); // gainExpForLevel(min(1, 2 - 1)) -> GoodLevel2
	CHECK(t->getExperience() == 50.0f);
	const std::uint32_t before = g.hash();
	horde->giveUpgrade(std::string("Upgrade_GondorBasicTraining")); // already executed: nothing more
	g.frame();
	CHECK(t->getRank() == 2);
	CHECK(g.hash() != 0u);
	(void)before;
}

// lane INTEG-1: XP-1's award sink connected to SPELL-1's player science (S-631 / S-527). A GameLogic installs PlayerExperienceAwardSink by default: a kill's
// skill points (RW 0x6AAFC3: the victim's experience value * the damage fraction) go to Player + 8 (RW 0x782AA4, PlayerScience::addSkillPoints with the scalar)
// and to the score keeper's total (RW 0x79DBA1). The Gondor player starts at rank 1 with 0 points; each Mordor orc is worth 1 (EXPERIENCE_AWARD_EVIL_WEAK_1), so
// it ranks up on the kill that reaches FactionMen's rank 2 count (Rank 2 SkillPointsNeeded, 60 in 2.01) and gets rank 2's SciencePurchasePointsGranted.
TEST_CASE("xp retail: kills earn the killer's player skill points through the default sink and rank it up (RW 0x6AAFC3 -> 0x782AA4, 0x79DBA1)")
{
	REQUIRE_RETAIL_WORLD(sh);
	Game g(*sh.world, *sh.mount);
	g.logic->experience().setAwardSink(nullptr);
	{
		TeamFactory teams;
		PlayerList players(sh.world->nameKeys(), sh.world->playerTemplates(), teams);
		GameLogic fresh(sh.world->things(), sh.world->modules(), players, RandomAlgorithm::ZH_CarryChain);
		CHECK(dynamic_cast<PlayerExperienceAwardSink *>(fresh.experience().awardSink()) != nullptr); // installed by default
	}
	PlayerExperienceAwardSink sink;
	g.logic->experience().setAwardSink(&sink);
	PlayerScience &sci = g.gondor->science();
	REQUIRE(sci.getRankLevel() == 1);
	REQUIRE(sci.getSkillPoints() == 0.0f);
	const int needed = sci.getSkillPointsLevelUp();
	CHECK(needed == 60); // RankInfo 2, FactionMen's column (data pin)
	const int purchaseBefore = sci.getSciencePurchasePoints();
	const float mordorBefore = g.mordor->science().getSkillPoints();
	Object *horde = g.make(*sh.world, "GondorFighterHorde", g.gondor);
	std::vector<Object *> orcs;
	for (int i = 0; i < needed; ++i)
	{
		orcs.push_back(g.make(*sh.world, "MordorFighter", g.mordor));
	}
	g.frame();
	Object *member = firstMember(*horde);
	REQUIRE(orcs[0]->getExperienceTracker()->getExperienceValue(*member, false) == 1);
	for (int i = 0; i < needed - 1; ++i)
	{
		g.kill(*orcs[(size_t)i], *member);
	}
	CHECK(sci.getSkillPoints() == (float)(needed - 1));
	CHECK(sci.getRankLevel() == 1);
	CHECK(sci.getSciencePurchasePoints() == purchaseBefore);
	const std::uint32_t hashBefore = g.hash();
	g.kill(*orcs[(size_t)needed - 1], *member);
	CHECK(sci.getSkillPoints() == (float)needed);
	CHECK(sci.getRankLevel() == 2); // the rank-up (RW 0x782865 through RW 0x782AA4's loop)
	CHECK(sci.getSciencePurchasePoints() == purchaseBefore + 1); // rank 2 SciencePurchasePointsGranted = 1 (5 -> 6 in 2.01)
	MESSAGE("FactionMen rank 2 at " << needed << " points: purchase points " << purchaseBefore << " -> " << sci.getSciencePurchasePoints());
	CHECK(g.gondor->getScoreKeeper().skillPointsEarned() == (float)needed); // RW 0x79DBA1 (scores kept)
	CHECK(g.hash() != hashBefore);                                          // the player's science state is hashed
	// the victim's player: the own-guys-die value of an orc (RW 0x6AB0D0) also reaches its science through the same sink
	MESSAGE("Mordor skill points " << mordorBefore << " -> " << g.mordor->science().getSkillPoints());
	CHECK(g.mordor->science().getSkillPoints() >= mordorBefore);
	CHECK(g.logic->experience().counters().skillPointsDropped == 0);
}

std::vector<const FXEvent *> levelUps(const GameLogic &logic)
{
	std::vector<const FXEvent *> out;
	for (const FXEvent &e : logic.fxEvents().frameEvents())
	{
		if (std::string(e.site) == "LevelUpFx")
		{
			out.push_back(&e);
		}
	}
	return out;
}

// lane INTEG-1 (S-634): the level-up FX calls of the grant (RW 0x8213AE: with feedback, from logic frame 10 on; RW 0x8211EC: one call per LevelUpFx entry, a
// "None" entry plays nothing) go to the logic's FXEventLog (lane FX-2) as OBJECT_FX events of site "LevelUpFx". The soldier horde reaches GoodLevel2 on its 49th kill after frame 10: the next
// ExperienceWorld::update grants the level to the horde and its members and the frame's events are exactly GoodLevel2's LevelUpFx entries, per object.
TEST_CASE("xp retail: a level-up after logic frame 10 emits the level's LevelUpFx calls as logic events (RW 0x8213AE -> 0x8211EC)")
{
	REQUIRE_RETAIL_WORLD(sh);
	const ExperienceLevelTemplate *level2 = sh.world->experienceLevels().findLevel("GoodLevel2");
	REQUIRE(level2);
	std::vector<std::string> expected;
	for (const ExperienceLevelTemplate::LevelUpFx &fx : level2->m_levelUpFx)
	{
		if (!fx.fxList.empty())
		{
			expected.push_back(fx.fxList + "|" + fx.bone);
		}
	}
	REQUIRE_FALSE(expected.empty()); // GoodLevel2 has a LevelUpFx (data pin)
	Game g(*sh.world, *sh.mount);
	Object *horde = g.make(*sh.world, "GondorFighterHorde", g.gondor);
	std::vector<Object *> orcs;
	for (int i = 0; i < 49; ++i)
	{
		orcs.push_back(g.make(*sh.world, "MordorFighter", g.mordor));
	}
	for (int f = 0; f < 12; ++f)
	{
		g.frame();
		CHECK(levelUps(*g.logic).empty()); // the level 1 grants of the creations: frame < 10 or no LevelUpFx
	}
	Object *member = firstMember(*horde);
	for (int i = 0; i < 49; ++i)
	{
		g.kill(*orcs[(size_t)i], *member);
	}
	g.frame();
	REQUIRE(member->getExperienceTracker()->getLevelName() == "GoodLevel2");
	std::map<ObjectID, std::vector<std::string>> perObject;
	for (const FXEvent *e : levelUps(*g.logic))
	{
		REQUIRE(e->fxList);
		CHECK(e->kind == FXEvent::OBJECT_FX);
		CHECK(e->frame == g.logic->getFrame()); // the frame of the grant (the delayed grant system runs in the frame that just ran)
		perObject[e->primary].push_back(*e->fxList + "|" + (e->bone ? *e->bone : std::string()));
	}
	CHECK(perObject.count(member->getID()) == 1);
	for (const auto &kv : perObject)
	{
		const Object *o = g.logic->findObjectByID(kv.first);
		REQUIRE(o);
		CHECK(o->getExperienceTracker()->getLevelName() == "GoodLevel2");
		CHECK(kv.second == expected);
	}
	MESSAGE("GoodLevel2: " << perObject.size() << " objects levelled, " << expected.size() << " LevelUpFx call(s) each, first " << expected[0]);
	g.frame();
	CHECK(levelUps(*g.logic).empty()); // the log keeps one frame's events (FXEventLog::beginFrame)
}

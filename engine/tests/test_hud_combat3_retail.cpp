// OpenBFME retail tests for COMBAT-3 (the owner's feedback G4 / G7): a cavalry charge knocks infantry back (Object::doKnockback RW 0x692223 through PhysicsBehavior's
// fling), the victims lie stunned and stand up (RW 0x792A69 / 0x79350E / 0x793372), the charge loses speed at each crush and pikemen in their porcupine formation
// stop it (CRUSHED_DECELERATE 1000% from the formation's AttributeModifiers), the crush levels follow their attribute modifiers, and Grond's troll crew stands on its
// bones with the bone's PASSENGER_VARIATION. They share the HUD tests' retail world (the file name sorts with the test_hud_* files, see
// test_hud_combat_retail.cpp) and SKIP when ROTWK_INSTALL / BFME2_INSTALL are unset.

#include "doctest.h"
#include "HudTestUtil.h"

#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Economy.h"
#include "GameLogic/EconomySettings.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/HordeContain.h"
#include "GameLogic/Module/PhysicsBehavior.h"
#include "GameLogic/Module/SquishCollide.h"
#include "GameLogic/Module/StancesBehavior.h"
#include "GameLogic/Object/AttributeModifierPool.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Contain/SiegeEngineContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/RetailObjectWorld.h"

#include "PathfindTestUtil.h"
#include "RetailTestMount.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace
{
using hudtest::SharedWorld;
using hudtest::shared;

bool haveWorld()
{
	return hudtest::haveWorld("combat3 retail");
}

// two enemy sides on a flat arena with the retail economy and AI data (the HORDE-2 arena)
struct Arena
{
	std::unique_ptr<RetailObjectWorld::ContextScope> context;
	TeamFactory teams;
	PlayerList players;
	GameLogic logic;
	AIWorldConfig cfg;
	pathtest::SyntheticTerrain terrain;
	std::unique_ptr<AIWorld> ai;

	Arena(SharedWorld &s, const char *factionA, const char *factionB, unsigned seed = 7)
		: context(s.world->enterContext())
		, players(s.world->nameKeys(), s.world->playerTemplates(), teams)
		, logic(s.world->things(), s.world->modules(), players, RandomAlgorithm::ZH_CarryChain)
		, terrain(120, 120)
	{
		SkirmishSetup setup;
		setup.startingMoney = 0;
		setup.players.push_back({ "A", factionA, true, 0, 0, 1 });
		setup.players.push_back({ "B", factionB, false, 1, 0, 2 });
		const std::vector<std::string> errors = players.setupSkirmish(setup);
		REQUIRE_MESSAGE(errors.empty(), (errors.empty() ? "" : errors[0]));
		std::string err;
		REQUIRE_MESSAGE(GameLogicSettingsLoader::load(*s.mount->fs, logic.settings(), &err), err);
		REQUIRE_MESSAGE(EconomySettings::load(*s.mount->fs, logic.economy().settings(), &err), err);
		logic.random().seedRandom(seed);
		logic.economy().initAllCommandPoints();
		REQUIRE_MESSAGE(AIWorldConfigLoader::load(*s.mount->fs, cfg, &err), err);
		ai = std::make_unique<AIWorld>(logic, cfg, s.world->iniMacros());
		ai->attach();
		ai->newMap(terrain);
	}
	~Arena()
	{
		logic.reset();
		ai.reset();
	}
	Player *player(int i) { return players.findPlayerWithName(i == 0 ? "A" : "B"); }
	Object *place(const char *templateName, int side, float x, float y, float angle)
	{
		const ThingTemplate *t = shared().world->things().findTemplate(templateName);
		REQUIRE_MESSAGE(t != nullptr, "retail template " << templateName);
		Object *o = logic.newObject(t, player(side)->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE_MESSAGE(o != nullptr, templateName);
		Coord3D p{ x, y, 0.0f };
		o->setPosition(&p);
		o->setOrientation(angle);
		return o;
	}
	std::vector<ObjectID> memberIds(Object *horde)
	{
		std::vector<ObjectID> out;
		for (const Object *m : *horde->getContain()->getContainedItemsList())
		{
			out.push_back(m->getID());
		}
		return out;
	}
	void frames(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			logic.runLogicFrame();
		}
	}
	CombatState::Counters &counters() { return logic.combat().counters(); }
};

int cond(const char *name)
{
	return CombatNames::modelCondition(name);
}

// the horde's AI locomotor speed (per frame)
float hordeSpeed(Object *h)
{
	AIUpdateInterface *ai = h->getAIUpdateInterface();
	Locomotor *loco = ai ? ai->curLocomotor() : nullptr;
	return loco ? loco->speed() : 0.0f;
}

void togglePorcupine(Arena &a, Object *h)
{
	GameLogicDispatch dispatch(a.logic);
	HordeCommands::registerHandlers(dispatch);
	StancesBehavior::registerHandlers(dispatch);
	GameMessage toggle(MSG_HORDE_TOGGLE_FORMATION, a.player(1)->getPlayerIndex());
	toggle.appendObjectIDArgument(h->getID());
	dispatch.dispatch(toggle);
}

// a charge of `charger` (side 0, facing east at x = 400) into `victim` (side 1 at x = 600, facing the charge)
struct Charge
{
	unsigned long long crushes = 0, knockbacks = 0, decelerations = 0;
	int firstCrushFrame = -1;
	int victimsFlailing = 0;       ///< victims seen with STUNNED_FLAILING
	int victimsLying = 0;          ///< victims seen with STUNNED
	int victimsStandingUp = 0;     ///< victims seen with STUNNED_STANDING_UP
	int victimsSplatted = 0;       ///< victims that landed dead (SPLATTED, RW 0x793372)
	int victimsFlungSurvived = 0;  ///< flailing victims alive at the end
	float farthestThrow = 0.0f;    ///< the largest one-frame move of a flailing victim
	float speedBefore = 0.0f;      ///< the charging horde's speed the frame before its first crush
	float lowestAfter = 1.0e9f;    ///< its lowest speed in the 10 frames after the first crush
	std::vector<float> speeds;     ///< the charging horde's speed per frame
	std::vector<std::uint32_t> hashes;
	bool porcupineListOnMembers = false;
};

Charge charge(const char *charger, const char *factionB, const char *victim, bool porcupine, int frames, unsigned seed = 7)
{
	Charge out;
	Arena a(shared(), "FactionMen", factionB, seed);
	Object *h1 = a.place(victim, 1, 600.0f, 600.0f, 3.14159265f);
	a.frames(2);
	if (porcupine)
	{
		togglePorcupine(a, h1);
		a.frames(30);
		out.porcupineListOnMembers = true;
		for (const Object *m : *h1->getContain()->getContainedItemsList())
		{
			const AttributeModifierPool *pool = static_cast<const AttributeModifierPool *>(const_cast<Object *>(m)->findModule("AttributeModifierPoolUpdate"));
			out.porcupineListOnMembers = out.porcupineListOnMembers && pool && pool->hasList("GondorTowerShieldGuardHordePorcupine");
		}
	}
	Object *h0 = a.place(charger, 0, 300.0f, 600.0f, 0.0f);
	a.frames(2);
	const std::vector<ObjectID> m1 = a.memberIds(h1);
	std::map<ObjectID, Coord3D> last;
	std::set<ObjectID> flailing, lying, standing, splatted;
	REQUIRE(h0->getAIUpdateInterface()->aiAttackObject(h1, CMD_FROM_PLAYER));
	unsigned long long lastCrushes = a.counters().crushes;
	for (int f = 0; f < frames; ++f)
	{
		const float before = hordeSpeed(h0);
		a.logic.runLogicFrame();
		const float speed = hordeSpeed(h0);
		out.speeds.push_back(speed);
		if (a.counters().crushes > lastCrushes && out.firstCrushFrame < 0)
		{
			out.firstCrushFrame = f;
			out.speedBefore = before;
		}
		lastCrushes = a.counters().crushes;
		if (out.firstCrushFrame >= 0 && f <= out.firstCrushFrame + 10 && speed < out.lowestAfter)
		{
			out.lowestAfter = speed;
		}
		for (ObjectID id : m1)
		{
			const Object *o = a.logic.findObjectByID(id);
			if (!o)
			{
				continue;
			}
			const Coord3D p = *o->getPosition();
			if (o->testModelCondition(cond("STUNNED_FLAILING")))
			{
				flailing.insert(id);
				auto it = last.find(id);
				if (it != last.end())
				{
					const float dx = p.x - it->second.x, dy = p.y - it->second.y;
					out.farthestThrow = std::max(out.farthestThrow, std::sqrt(dx * dx + dy * dy));
				}
			}
			if (o->testModelCondition(cond("STUNNED")))
			{
				lying.insert(id);
			}
			if (o->testModelCondition(cond("STUNNED_STANDING_UP")))
			{
				standing.insert(id);
			}
			if (o->testModelCondition(cond("SPLATTED")))
			{
				splatted.insert(id);
			}
			last[id] = p;
		}
		out.hashes.push_back(a.logic.computeStateHash());
	}
	out.crushes = a.counters().crushes;
	out.knockbacks = a.counters().crushKnockbacks;
	out.decelerations = a.counters().crushDecelerations;
	out.victimsFlailing = (int)flailing.size();
	out.victimsLying = (int)lying.size();
	out.victimsStandingUp = (int)standing.size();
	out.victimsSplatted = (int)splatted.size();
	for (ObjectID id : flailing)
	{
		const Object *o = a.logic.findObjectByID(id);
		out.victimsFlungSurvived += (o && !o->isEffectivelyDead()) ? 1 : 0;
	}
	return out;
}

std::string speedLine(const Charge &c)
{
	std::string s;
	const int from = c.firstCrushFrame < 3 ? 0 : c.firstCrushFrame - 3;
	for (int f = from; f < (int)c.speeds.size() && f < from + 16; ++f)
	{
		char b[32];
		std::snprintf(b, sizeof b, " %.2f", c.speeds[(size_t)f]);
		s += b;
	}
	return s;
}
} // namespace

// GondorFighter: PhysicsBehavior GravityMult 1, ShockStunnedTimeLow / High 1400 / 2400 ms (7 / 12 frames: ceil(ms * 0.005)), ShockStandingTime 1233 ms (7 frames).
TEST_CASE("combat3 retail: a knocked back soldier flies flailing, lies stunned for ShockStunnedTime, stands up for ShockStandingTime and cannot move meanwhile")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared(), "FactionMen", "FactionMordor");
	Object *s = a.place("GondorFighter", 1, 600.0f, 600.0f, 0.0f);
	a.frames(3);
	PhysicsBehavior *phys = PhysicsBehavior::find(*s);
	REQUIRE(phys);
	CHECK(phys->data()->m_shockStunnedTimeLow == 7u);
	CHECK(phys->data()->m_shockStunnedTimeHigh == 12u);
	CHECK(phys->data()->m_shockStandingTime == 7u);
	const Coord3D start = *s->getPosition();
	// the Rohirrim's CrushKnockback 40 per second (8 per frame) along +x, CrushZFactor 1
	REQUIRE(ObjectKnockback::apply(*s, 0.0f, 8.0f, 1.0f));
	CHECK(phys->isStunned());
	CHECK(s->testModelCondition(cond("STUNNED_FLAILING")));
	CHECK(s->getAIUpdateInterface()->locomotorHost().physicsMotionDisabled()); // RW 0x5E3A1B
	CHECK_FALSE(ObjectKnockback::apply(*s, 0.0f, 8.0f, 1.0f));                 // a stunned object is not knocked again
	int flightFrames = 0, lyingFrames = 0, standingFrames = 0, stunnedTimer = -1;
	float highest = 0.0f;
	for (int f = 0; f < 60; ++f)
	{
		a.logic.runLogicFrame();
		if (s->testModelCondition(cond("STUNNED_FLAILING")))
		{
			++flightFrames;
			highest = std::max(highest, s->getPosition()->z);
		}
		if (s->testModelCondition(cond("STUNNED")))
		{
			if (stunnedTimer < 0)
			{
				stunnedTimer = phys->stunTimer();
			}
			++lyingFrames;
		}
		if (s->testModelCondition(cond("STUNNED_STANDING_UP")))
		{
			++standingFrames;
		}
	}
	const float moved = s->getPosition()->x - start.x;
	std::printf("  info: knockback (8, 0, 8): %d frames flailing (highest z %.1f), thrown %.1f along x, stun timer %d, %d frames lying, %d standing up\n", flightFrames,
		highest, moved, stunnedTimer, lyingFrames, standingFrames);
	CHECK(flightFrames >= 2);
	CHECK(highest > 1.0f);
	CHECK(moved > 20.0f);
	CHECK(std::fabs(s->getPosition()->y - start.y) < 0.01f);
	CHECK(stunnedTimer >= 7);
	CHECK(stunnedTimer <= 12);
	CHECK(lyingFrames == stunnedTimer);
	CHECK(standingFrames == 7);
	CHECK_FALSE(phys->isStunned());
	CHECK_FALSE(s->testModelCondition(cond("STUNNED")));
	CHECK_FALSE(s->testModelCondition(cond("STUNNED_STANDING_UP")));
	CHECK_FALSE(s->getAIUpdateInterface()->locomotorHost().physicsMotionDisabled());
}

TEST_CASE("combat3 retail: ShockwaveResistance 100 or more refuses the knockback; without a PhysicsBehavior nothing happens")
{
	if (!haveWorld())
	{
		return;
	}
	int resistant = 0, checked = 0;
	std::string example;
	auto hasModule = [](const ThingTemplate &t, const char *cls) {
		for (const ThingTemplate::Nugget &n : t.behaviorModules().nuggets())
		{
			if (n.name == cls)
			{
				return true;
			}
		}
		return false;
	};
	for (const ThingTemplate *t : shared().world->things().templates())
	{
		const FieldValue *v = t->findField("ShockwaveResistance");
		const float *f = v ? std::get_if<float>(v) : nullptr;
		if (f && *f >= 100.0f && example.empty() && hasModule(*t, "PhysicsBehavior") && hasModule(*t, "AIUpdateInterface"))
		{
			example = t->getName();
		}
		resistant += (f && *f >= 100.0f) ? 1 : 0;
		++checked;
	}
	std::printf("  info: %d of %d templates have ShockwaveResistance >= 100 (example %s)\n", resistant, checked, example.c_str());
	REQUIRE(resistant > 0);
	REQUIRE_FALSE(example.empty());
	Arena a(shared(), "FactionMen", "FactionMordor");
	Object *o = a.place(example.c_str(), 1, 600.0f, 600.0f, 0.0f);
	a.frames(3);
	const Coord3D p = *o->getPosition();
	CHECK_FALSE(ObjectKnockback::apply(*o, 0.0f, 8.0f, 1.0f));
	a.frames(5);
	CHECK(o->getPosition()->x == p.x);
	CHECK_FALSE(o->testModelCondition(cond("STUNNED_FLAILING")));
}

// RotWK 2.01: RohanRohirrimHorde (CrushKnockback 40, CrushZFactor 1, CrushDecelerationPercent 30%, MinCrushVelocityPercent 50%) into GondorFighterHorde.
TEST_CASE("combat3 retail: a Rohirrim charge knocks Gondor soldiers flying; they lie stunned and stand up; the riders lose speed at each crush")
{
	if (!haveWorld())
	{
		return;
	}
	const Charge c = charge("RohanRohirrimHorde", "FactionMen", "GondorFighterHorde", false, 160);
	std::printf("  info: Rohirrim into Gondor soldiers: first crush at %d, %llu crushes, %llu knockbacks, %llu decelerations; victims flailing %d (landed dead %d, alive at the end"
		" %d), lying %d, standing up %d; farthest one-frame throw %.1f; horde speed%s\n",
		c.firstCrushFrame, c.crushes, c.knockbacks, c.decelerations, c.victimsFlailing, c.victimsSplatted, c.victimsFlungSurvived, c.victimsLying, c.victimsStandingUp,
		c.farthestThrow, speedLine(c).c_str());
	REQUIRE(c.firstCrushFrame >= 0);
	CHECK(c.knockbacks > 0);
	CHECK(c.victimsFlailing > 0);
	// RohirrimCrush (ROHIRRIM_CRUSH_DAMAGE) kills a footman: he flies dead and lands SPLATTED (DYING); a survivor lies STUNNED, then stands up
	CHECK(c.victimsSplatted + c.victimsFlungSurvived > 0);
	CHECK(c.victimsLying >= c.victimsFlungSurvived);
	CHECK(c.farthestThrow > 3.0f);
	CHECK(c.decelerations > 0);
	CHECK(c.lowestAfter < c.speedBefore);
}

// GondorTowerShieldGuardHorde's porcupine formation (GondorTowerShieldGuardHordePorcupine: AttributeModifiers = GondorTowerShieldGuardHordePorcupine, the list
// CRUSHED_DECELERATE 1000%): each crush slows the riders by ten times CrushDecelerationPercent, so the charge stops at the first pikeman.
TEST_CASE("combat3 retail: pikemen in their porcupine formation stop a Rohirrim charge (the formation's CRUSHED_DECELERATE 1000%)")
{
	if (!haveWorld())
	{
		return;
	}
	const Charge open = charge("RohanRohirrimHorde", "FactionMen", "GondorTowerShieldGuardHorde", false, 160);
	const Charge pikes = charge("RohanRohirrimHorde", "FactionMen", "GondorTowerShieldGuardHorde", true, 160);
	std::printf("  info: Rohirrim into tower guards: open formation %llu crushes (speed %.2f -> lowest %.2f), porcupine %llu crushes (speed %.2f -> lowest %.2f);"
		" speeds open%s / porcupine%s\n",
		open.crushes, open.speedBefore, open.lowestAfter, pikes.crushes, pikes.speedBefore, pikes.lowestAfter, speedLine(open).c_str(), speedLine(pikes).c_str());
	CHECK(pikes.porcupineListOnMembers);
	REQUIRE(open.firstCrushFrame >= 0);
	REQUIRE(pikes.firstCrushFrame >= 0);
	// the frame after the first crush: the open formation slows the riders by a little, the porcupine to the locomotor's minimum at once
	const float openAfter = open.speeds[(size_t)open.firstCrushFrame + 1], pikesAfter = pikes.speeds[(size_t)pikes.firstCrushFrame + 1];
	CHECK(pikesAfter < openAfter);
	CHECK(pikes.lowestAfter < open.lowestAfter);
	CHECK(pikes.crushes * 3 < open.crushes);
}

TEST_CASE("combat3 retail: CRUSHABLE_LEVEL and CRUSHER_LEVEL modifiers change the crush levels (RW 0x68D4D0 / 0x695070)")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared(), "FactionMen", "FactionMordor");
	Object *soldier = a.place("GondorFighter", 1, 600.0f, 600.0f, 0.0f);
	Object *rider = a.place("RohanRohirrim", 0, 400.0f, 600.0f, 0.0f);
	a.frames(3);
	CHECK(ObjectCrush::crushableLevel(*soldier) == 0);
	CHECK(ObjectCrush::crusherLevel(*rider) == 1);
	REQUIRE(soldier->addAttributeModifier("FanaticStoneWall", -1)); // CRUSHABLE_LEVEL 3
	REQUIRE(rider->addAttributeModifier("GlorfindelWindRider", -1)); // CRUSHER_LEVEL -1
	a.frames(1);
	CHECK(ObjectCrush::crushableLevel(*soldier) == 3);
	CHECK(ObjectCrush::crusherLevel(*rider) == 0);
}

TEST_CASE("combat3 retail: the same charge twice gives the same state hash in every frame; the stun is in the hash")
{
	if (!haveWorld())
	{
		return;
	}
	const Charge a = charge("RohanRohirrimHorde", "FactionMen", "GondorFighterHorde", false, 80);
	const Charge b = charge("RohanRohirrimHorde", "FactionMen", "GondorFighterHorde", false, 80);
	REQUIRE(a.hashes.size() == b.hashes.size());
	bool same = true;
	for (size_t i = 0; i < a.hashes.size(); ++i)
	{
		same = same && a.hashes[i] == b.hashes[i];
	}
	CHECK(same);
	// the stun's timer and flag change the hash
	Arena x(shared(), "FactionMen", "FactionMordor");
	Object *s = x.place("GondorFighter", 1, 600.0f, 600.0f, 0.0f);
	x.frames(3);
	const std::uint32_t h0 = x.logic.computeStateHash();
	PhysicsBehavior *phys = PhysicsBehavior::find(*s);
	REQUIRE(phys);
	phys->standUp(); // STUNNED, the flag and the timer (RW 0x792AFF)
	CHECK(x.logic.computeStateHash() != h0);
	CHECK(phys->isStunned());
	CHECK(phys->stunTimer() == 7);
}

// RotWK 2.01 MordorGrond: SiegeEngineContain PassengerBonePrefix "PassengerBone:Crew KindOf:MONSTER", BoneSpecificConditionState 2 / 4 -> PASSENGER_VARIATION_1,
// 1 / 3 -> _2, 6 -> _3, 5 -> _4; InitialCrew MordorGrondCrew 6. The trolls pick their push / pull animations by those conditions.
TEST_CASE("combat3 retail: Grond's six trolls stand on its Crew bones with their bone's PASSENGER_VARIATION and are drawn")
{
	if (!haveWorld())
	{
		return;
	}
	hudtest::Rig r(shared(), "map mp fall back 4p", "FactionMordor");
	float mx = 0, my = 0;
	REQUIRE(r.logic().terrain() != nullptr);
	REQUIRE(r.logic().terrain()->getExtent(0, mx, my));
	const Coord3D c0 = r.freeSpot(mx * 0.5f, my * 0.5f, 500.0f);
	Object *grond = r.make("MordorGrond", c0.x, c0.y);
	r.frame(2);
	SiegeEngineContain *g = dynamic_cast<SiegeEngineContain *>(grond->getContain());
	REQUIRE(g != nullptr);
	REQUIRE(g->crewList()->size() == 6u);
	const std::map<std::string, const char *> expect = { { "CREW01", "PASSENGER_VARIATION_2" }, { "CREW02", "PASSENGER_VARIATION_1" },
		{ "CREW03", "PASSENGER_VARIATION_2" }, { "CREW04", "PASSENGER_VARIATION_1" }, { "CREW05", "PASSENGER_VARIATION_4" }, { "CREW06", "PASSENGER_VARIATION_3" } };
	std::set<std::string> bones;
	std::set<std::pair<int, int>> spots;
	for (Object *c : *g->crewList())
	{
		std::string bone = g->riderBone(c->getID());
		for (char &ch : bone)
		{
			ch = (char)std::toupper((unsigned char)ch);
		}
		bones.insert(bone);
		auto it = expect.find(bone);
		REQUIRE_MESSAGE(it != expect.end(), "crew bone " << bone);
		int variations = 0;
		for (const char *v : { "PASSENGER_VARIATION_1", "PASSENGER_VARIATION_2", "PASSENGER_VARIATION_3", "PASSENGER_VARIATION_4", "PASSENGER_VARIATION_5" })
		{
			variations += c->testModelCondition(cond(v)) ? 1 : 0;
		}
		CHECK_MESSAGE(c->testModelCondition(cond(it->second)), bone << " " << it->second);
		CHECK(variations == 1);
		CHECK_FALSE(c->isDrawableHidden());
		const float dx = c->getPosition()->x - grond->getPosition()->x, dy = c->getPosition()->y - grond->getPosition()->y;
		CHECK(std::sqrt(dx * dx + dy * dy) > 5.0f);
		CHECK(std::sqrt(dx * dx + dy * dy) < 150.0f);
		spots.insert({ (int)std::lround(c->getPosition()->x), (int)std::lround(c->getPosition()->y) });
	}
	CHECK(bones.size() == 6u);
	CHECK(spots.size() == 6u);
	// JUST_BUILT (Model None) lasts BuildFadeInOnCreateTime 16 s; then the trolls have their model
	int clearedAt = -1;
	for (int f = 0; f < 400 && clearedAt < 0; ++f)
	{
		r.frame(1);
		bool any = false;
		for (Object *c : *g->crewList())
		{
			any = any || c->testModelCondition(cond("JUST_BUILT"));
		}
		clearedAt = any ? -1 : f + 3;
	}
	std::printf("  info: Grond's crew: JUST_BUILT cleared at frame %d (grond JUST_BUILT %d)\n", clearedAt, (int)grond->testModelCondition(cond("JUST_BUILT")));
	CHECK(clearedAt > 0);
	CHECK(clearedAt <= 90);
	// Grond rolls: the trolls go with it on their bones
	grond->getAIUpdateInterface()->aiMoveToPosition(Coord3D{ grond->getPosition()->x + 300.0f, grond->getPosition()->y, 0.0f }, CMD_FROM_PLAYER);
	r.frame(10);
	std::string conds;
	for (Object *c : *g->crewList())
	{
		const float dx = c->getPosition()->x - grond->getPosition()->x, dy = c->getPosition()->y - grond->getPosition()->y;
		CHECK(std::sqrt(dx * dx + dy * dy) < 150.0f);
		conds += " " + g->riderBone(c->getID()) + ":";
		for (const char *n : { "MOVING", "TRANSPORT_MOVING", "PASSENGER", "PASSENGER_VARIATION_1", "PASSENGER_VARIATION_2", "PASSENGER_VARIATION_3", "PASSENGER_VARIATION_4" })
		{
			if (c->testModelCondition(cond(n)))
			{
				conds += std::string(" ") + n;
			}
		}
	}
	std::printf("  info: Grond moving (MOVING %d), its crew:%s\n", (int)grond->testModelCondition(cond("MOVING")), conds.c_str());
}

TEST_CASE("combat3 retail: the stops S-580 / S-782 / S-1600 / S-1601 are in the reports; the new crush counters are hashed")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared(), "FactionMen", "FactionMordor");
	Object *s = a.place("GondorFighter", 1, 600.0f, 600.0f, 0.0f);
	a.frames(3);
	REQUIRE(ObjectKnockback::apply(*s, 0.0f, 8.0f, 1.0f)); // the first fling notes S-782
	const std::vector<std::string> combat = a.logic.combat().report();
	const GameLogic::Report rep = a.logic.report();
	auto has = [](const std::vector<std::string> &lines, const char *id) {
		for (const std::string &l : lines)
		{
			if (l.compare(0, std::string(id).size(), id) == 0)
			{
				return true;
			}
		}
		return false;
	};
	CHECK(has(combat, "[S-580]"));
	CHECK(has(combat, "[S-1600]"));
	CHECK(has(combat, "[S-1601]"));
	CHECK(has(rep.stops, "[S-782]"));
	const std::uint32_t h0 = a.logic.computeStateHash();
	a.counters().crushKnockbacks += 1;
	CHECK(a.logic.computeStateHash() != h0);
	a.counters().crushKnockbacks -= 1;
	a.counters().crushBumpAttacksNotPorted += 1;
	CHECK(a.logic.computeStateHash() != h0);
	a.counters().crushBumpAttacksNotPorted -= 1;
	CHECK(a.logic.computeStateHash() == h0);
}

// Sol r1: RW 0x69681E caps the CONTAINING horde's locomotor after a member's failed crush, not the member's own
TEST_CASE("combat3 retail: a rider's bump holds the speed of its horde's locomotor (RW 0x696800 / 0x69681E)")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared(), "FactionMen", "FactionMordor");
	Object *horde = a.place("RohanRohirrimHorde", 0, 400.0f, 600.0f, 0.0f);
	Object *victim = a.place("MordorFighter", 1, 450.0f, 600.0f, 3.14159265f);
	a.frames(5);
	const std::vector<ObjectID> ids = a.memberIds(horde);
	REQUIRE_FALSE(ids.empty());
	Object *member = a.logic.findObjectByID(ids[0]);
	REQUIRE(member->getContainedBy() == horde);
	Locomotor *hordeLoco = horde->getAIUpdateInterface()->curLocomotor();
	REQUIRE(member->getAIUpdateInterface()->curLocomotor());
	REQUIRE(hordeLoco);
	const unsigned frame = a.logic.getFrame();
	const float full = hordeLoco->getCurrentMaxSpeed(horde->getAIUpdateInterface()->locomotorHost(), frame);
	REQUIRE(full > 1.0f);
	// the horde's cap of 0.5 ends after this frame; the bump re-caps the horde's locomotor (not the member's) for LOGICFRAMES_PER_SECOND frames at its current maximum
	// (0.5, RW 0x5E4137 on the horde's locomotor): the old code capped the member's and the horde's expired (21 at frame + 3)
	hordeLoco->setDesiredSpeedCap(0.5f, frame + 1);
	ObjectCrush::onBump(*member, *victim);
	CHECK(hordeLoco->getCurrentMaxSpeed(horde->getAIUpdateInterface()->locomotorHost(), frame + 3) == 0.5f);
}

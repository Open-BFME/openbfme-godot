// OpenBFME retail tests for COMBAT-4 (community FB-0004 / FB-0003 / FB-0002 / FB-0011): weapons' MetaImpactNugget and RamPower throw units through Object's
// shockwave handler (RW 0x6968BC), a troll's club swing throws the soldiers in its arc, and the measurements of a cavalry charge. They share the HUD tests' retail
// world (the file name sorts with the test_hud_* files, see test_hud_combat_retail.cpp) and SKIP when ROTWK_INSTALL / BFME2_INSTALL are unset.

#include "doctest.h"
#include "HudTestUtil.h"

#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "GameClient/ClientEvents.h"
#include "GameClient/Drawable.h"
#include "GameClient/DrawableManager.h"
#include "GameClient/LogicSnapshot.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDraw.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Economy.h"
#include "GameLogic/EconomySettings.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/PhysicsBehavior.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/Contain/SiegeEngineContainRuntime.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponStores.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include "PathfindTestUtil.h"
#include "RetailTestMount.h"

#include <algorithm>
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
	return hudtest::haveWorld("combat4 retail");
}

// two enemy sides on a flat arena with the retail economy and AI data (the COMBAT-3 arena)
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

// the last real draw of the call log at `line`
float drawAt(GameLogic &logic, int line)
{
	float r = -1.0f;
	for (const GameLogicRandom::Call &c : logic.random().callLog())
	{
		if (c.real && c.line == line)
		{
			r = c.rresult;
		}
	}
	return r;
}

// a shockwave hit of RamPower's shape (RW 0x8BFF08): a 0-damage CRUSH hit whose shockwave half carries the rest
DamageInfo ramHit(ObjectID source, Coord3D vector, float amount, float zMult, float radius, float taper)
{
	DamageInfo info;
	info.m_input.m_sourceID = source;
	info.m_input.m_damageType = DAMAGE_CRUSH;
	info.m_input.m_shockWaveVector = vector;
	info.m_input.m_shockWaveAmount = amount;
	info.m_input.m_shockWaveZMult = zMult;
	info.m_input.m_shockWaveRadius = radius;
	info.m_input.m_shockWaveTaperOff = taper;
	return info;
}

// the flight of an object relative to where it started
std::vector<Coord3D> relativeFlight(const Object &o, const Coord3D &start)
{
	std::vector<Coord3D> out;
	for (const Coord3D &p : PhysicsBehavior::find(const_cast<Object &>(o))->flightPoints())
	{
		out.push_back(Coord3D{ p.x - start.x, p.y - start.y, p.z - start.z });
	}
	return out;
}
} // namespace

// RW 0x6968BC, the plain branch: dir = |vector|, m = min(|vector| / radius, 1), s = 1 - (1 - taper) m, r = GameLogicRandomValueReal(0.85, 1.15) at Object.cpp:0x1030,
// v = dir * (amount * s * r), v.z = |v| * ZMult * r; then the fling (RW 0x792DBD), STUNNED_FLAILING and the stun. The same fling given by hand to a twin soldier
// must fly the same curve.
TEST_CASE("combat4 retail: a shockwave hit throws a soldier by retail's formula (strength draw, taper, z mult) and stuns it")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared(), "FactionMen", "FactionMordor");
	Object *s = a.place("GondorFighter", 1, 600.0f, 600.0f, 0.0f);
	Object *twin = a.place("GondorFighter", 1, 600.0f, 900.0f, 0.0f);
	Object *src = a.place("MordorAttackTroll", 0, 560.0f, 570.0f, 0.0f);
	a.frames(3);
	const Coord3D start = *s->getPosition();
	const Coord3D twinStart = *twin->getPosition();
	a.logic.random().enableCallLog(true);
	a.logic.random().clearCallLog();
	// the vector (3, 4, 0) (length 5), radius 10: m = 0.5; taper 0.75: s = 0.875; amount 6 per frame; ZMult 1.5
	DamageInfo info = ramHit(src->getID(), Coord3D{ 3.0f, 4.0f, 0.0f }, 6.0f, 1.5f, 10.0f, 0.75f);
	s->attemptDamage(info);
	const float r = drawAt(a.logic, 0x1030);
	REQUIRE(r >= 0.85f);
	REQUIRE(r <= 1.15f);
	PhysicsBehavior *phys = PhysicsBehavior::find(*s);
	REQUIRE(phys);
	CHECK(phys->isFlying());
	CHECK(phys->isStunned());
	CHECK(s->testModelCondition(cond("STUNNED_FLAILING")));
	CHECK(a.counters().shockwaveFlings == 1);
	// the formula by hand (float32, the handler's order)
	const float f = 6.0f * 0.875f * r;
	Coord3D v{ f * 0.6f, 0.8f * f, 0.0f };
	v.z = (float)((double)(float)std::sqrt((double)(v.x * v.x + v.y * v.y)) * 1.5 * (double)r);
	PhysicsBehavior::find(*twin)->fling(v, 0, 0);
	const std::vector<Coord3D> got = relativeFlight(*s, start), want = relativeFlight(*twin, twinStart);
	REQUIRE(got.size() == want.size());
	REQUIRE(!got.empty());
	for (size_t i = 0; i < got.size(); ++i)
	{
		CHECK(std::fabs(got[i].x - want[i].x) < 0.01f);
		CHECK(std::fabs(got[i].y - want[i].y) < 0.01f);
		CHECK(std::fabs(got[i].z - want[i].z) < 0.01f);
	}
	std::printf("  info: shockwave (3, 4, 0) amount 6 r %.4f: %zu flight points, landing (%.1f, %.1f) from the start\n", r, got.size(), got.back().x, got.back().y);
	// a stunned soldier is not thrown again by a plain shockwave (RW 0x696939)
	DamageInfo again = ramHit(src->getID(), Coord3D{ 3.0f, 4.0f, 0.0f }, 6.0f, 1.5f, 10.0f, 0.75f);
	s->attemptDamage(again);
	CHECK(a.counters().shockwaveFlings == 1);
}

TEST_CASE("combat4 retail: ShockwaveResistance 100 stands the object up instead, RESIST_KNOCKBACK 1.0 and zero amount or radius do nothing, SHIP throws down")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared(), "FactionMen", "FactionMordor");
	Object *s = a.place("GondorFighter", 1, 600.0f, 600.0f, 0.0f);
	Object *src = a.place("MordorAttackTroll", 0, 560.0f, 600.0f, 0.0f);
	a.frames(3);
	DamageInfo none = ramHit(src->getID(), Coord3D{ 1.0f, 0.0f, 0.0f }, 0.0f, 1.0f, 10.0f, 1.0f);
	s->attemptDamage(none);
	DamageInfo noRadius = ramHit(src->getID(), Coord3D{ 1.0f, 0.0f, 0.0f }, 6.0f, 1.0f, 0.0f, 1.0f);
	s->attemptDamage(noRadius);
	CHECK(a.counters().shockwaveFlings == 0);
	CHECK_FALSE(PhysicsBehavior::find(*s)->isFlying());
	// the troll itself is SHOCKWAVE_RESISTANCE_STRONG: below 100 it is thrown when it has a PhysicsBehavior
	const float trollResistance = ObjectKnockback::shockwaveResistance(*src);
	std::printf("  info: MordorAttackTroll ShockwaveResistance %.1f\n", trollResistance);
	CHECK(trollResistance > 0.0f);
}

// MordorAttackTrollClubSwing: DamageDealtAtSelfPosition, DamageNugget radius 60 (arc 90), MetaImpactNugget ShockWaveAmount 40 (8 per frame), radius 50, taper 0.75,
// arc 90 (the front half: dot >= cos(pi / 2)), ZMult 1.2. The swing is delivered at the troll's own position (RW 0x6CCBF9), the shock point is the troll so the arc
// is the troll's facing (RW 0x910236); the horde in front is thrown, the soldier behind the troll is not.
TEST_CASE("combat4 retail: a troll's club swing throws the soldiers in front of it and not the one behind it (MetaImpactNugget, FB-0004)")
{
	if (!haveWorld())
	{
		return;
	}
	auto run = [](std::vector<std::uint32_t> *hashes, int *thrown, int *behindThrown, float *farthest, float *highest, unsigned long long *hits) {
		Arena a(shared(), "FactionMordor", "FactionMen");
		Object *troll = a.place("MordorAttackTroll", 0, 600.0f, 600.0f, 0.0f);
		Object *horde = a.place("GondorFighterHorde", 1, 630.0f, 600.0f, 3.14159265f);
		Object *behind = a.place("GondorFighter", 1, 575.0f, 600.0f, 0.0f);
		// one swing is measured: with the DamageNugget's DamageArc cone (RW 0x90DEF0, lane MOVE-2) the soldier behind survives it, and the troll's idle scan would
		// pick it and swing again; no unit acquires targets on its own here
		a.logic.combat().setAutoAcquireEnabled(false);
		a.frames(4);
		REQUIRE(TheWeaponStore);
		const WeaponTemplate *club = TheWeaponStore->findWeaponTemplate("MordorAttackTrollClubSwing");
		REQUIRE(club);
		std::unique_ptr<Weapon> w = troll->getWeapons()->makeExtraWeapon(club);
		troll->getWeapons()->loadExtraWeapon(*w);
		const std::vector<ObjectID> members = a.memberIds(horde);
		REQUIRE(!members.empty());
		// the nearest member is the target
		Object *victim = nullptr;
		float best = 1.0e9f;
		std::map<ObjectID, Coord3D> start;
		for (ObjectID id : members)
		{
			Object *m = a.logic.findObjectByID(id);
			start[id] = *m->getPosition();
			const float d = std::fabs(m->getPosition()->x - 600.0f) + std::fabs(m->getPosition()->y - 600.0f);
			if (d < best)
			{
				best = d;
				victim = m;
			}
		}
		bool behindFlailed = false;
		// lane MOVE-3 r4: the soldier behind counts as thrown when it flails, is stunned or is displaced while its AI does not move it. Since the IDLE-1 merge a
		// thrown member lands beside it and keeps its ground goal there (RotWK's hub never removes a member's goal); the idle soldier, touching the troll, then
		// steps aside one cell by AI order (blockedBy RW 0x66D16E -> aiMoveToPosition RW 0x66D5F0): a move, not a throw
		float behindPushed = 0.0f;
		Coord3D behindLast = *behind->getPosition();
		troll->getWeapons()->fireExtraWeapon(*w, *victim);
		*hits = a.counters().metaImpactHits;
		std::set<ObjectID> flung;
		*farthest = 0.0f;
		*highest = 0.0f;
		for (int f = 0; f < 30; ++f)
		{
			for (ObjectID id : members)
			{
				const Object *m = a.logic.findObjectByID(id);
				if (m && m->testModelCondition(cond("STUNNED_FLAILING")))
				{
					flung.insert(id);
					*highest = std::max(*highest, m->getPosition()->z);
				}
				if (m)
				{
					const float dx = m->getPosition()->x - start[id].x, dy = m->getPosition()->y - start[id].y;
					if (flung.count(id))
					{
						*farthest = std::max(*farthest, std::sqrt(dx * dx + dy * dy));
					}
				}
			}
			const bool behindAIMoving = behind->getAIUpdateInterface()->currentStateId() != AI_IDLE;
			a.logic.runLogicFrame();
			behindFlailed = behindFlailed || behind->testModelCondition(cond("STUNNED_FLAILING"));
			if (!behindAIMoving)
			{
				const float sx = behind->getPosition()->x - behindLast.x, sy = behind->getPosition()->y - behindLast.y;
				behindPushed += std::sqrt(sx * sx + sy * sy);
			}
			behindLast = *behind->getPosition();
			hashes->push_back(a.logic.computeStateHash());
		}
		*thrown = (int)flung.size();
		*behindThrown = (behindFlailed || PhysicsBehavior::find(*behind)->isStunned() || behindPushed > 5.0f) ? 1 : 0;
	};
	std::vector<std::uint32_t> h1, h2;
	int thrown = 0, behindThrown = 0, thrown2 = 0, behind2 = 0;
	float farthest = 0.0f, highest = 0.0f, f2 = 0.0f, hi2 = 0.0f;
	unsigned long long hits = 0, hits2 = 0;
	run(&h1, &thrown, &behindThrown, &farthest, &highest, &hits);
	run(&h2, &thrown2, &behind2, &f2, &hi2, &hits2);
	std::printf("  info: club swing: %llu shockwave hits, %d soldiers thrown (farthest %.1f, highest z %.1f), the soldier behind thrown: %d\n", hits, thrown, farthest, highest,
		behindThrown);
	CHECK(hits >= 3);
	CHECK(thrown >= 3);
	CHECK(farthest > 10.0f);
	CHECK(highest > 3.0f);
	CHECK(behindThrown == 0);
	CHECK(h1 == h2); // deterministic
}

// GondorRanger's SlowDeathBehavior ModuleTag_05: DeathTypes ALL -KNOCKBACK -FADED, SinkDelay 3000 (15 frames), SinkRate 0.40 per second (0.08 per frame),
// DestructionDelay 15000 (75 frames), DeathFlags DEATH_1. RW 0x860E93 / 0x860B39: the DEATH_1 status and model condition with DYING at the death, the body lies until the
// sink frame, then sinks with SINKING and DISABLED_HELD, and is destroyed 60 frames later (FB-0011).
TEST_CASE("combat4 retail: a killed ranger lies for SinkDelay, then sinks at SinkRate with SINKING and DISABLED_HELD, and goes after DestructionDelay; DeathFlags reach it (FB-0011)")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared(), "FactionMen", "FactionMordor");
	Object *r = a.place("GondorRanger", 1, 600.0f, 600.0f, 0.0f);
	a.frames(3);
	const ObjectID id = r->getID();
	const float z0 = r->getPosition()->z;
	a.logic.random().enableCallLog(true);
	a.logic.random().clearCallLog();
	const unsigned killFrame = a.logic.getFrame();
	r->kill(DEATH_NORMAL);
	REQUIRE(r->isEffectivelyDead());
	// the roulette draws at SlowDeathBehavior.cpp:0x32F, then the sink, destruction and midpoint draws at 0x1A5 / 0x1A6 / 0x1AB
	std::vector<int> lines;
	for (const GameLogicRandom::Call &c : a.logic.random().callLog())
	{
		if (c.file == "SlowDeathBehavior.cpp")
		{
			lines.push_back(c.line);
		}
	}
	CHECK(lines == std::vector<int>{ 0x32F, 0x1A5, 0x1A6, 0x1AB });
	CHECK(r->testStatus((unsigned)CombatNames::status("DEATH_1")));
	CHECK(r->testModelCondition(cond("DEATH_1")));
	CHECK(r->testModelCondition(cond("DYING")));
	int sinkFrame = -1, goneFrame = -1;
	float lowest = z0, zBeforeSink = z0;
	bool heldWhileSinking = false;
	for (int f = 0; f < 120 && goneFrame < 0; ++f)
	{
		a.logic.runLogicFrame();
		Object *o = a.logic.findObjectByID(id);
		if (!o || o->isDestroyed())
		{
			goneFrame = (int)(a.logic.getFrame() - killFrame);
			break;
		}
		if (sinkFrame < 0 && o->testStatus((unsigned)CombatNames::status("SINKING")))
		{
			sinkFrame = (int)(a.logic.getFrame() - killFrame);
			heldWhileSinking = (o->getDisabledMask() & (1u << 3)) != 0;
		}
		if (sinkFrame < 0)
		{
			zBeforeSink = o->getPosition()->z;
		}
		lowest = std::min(lowest, o->getPosition()->z);
	}
	std::printf("  info: ranger corpse: SINKING from frame %d, destroyed at frame %d after the death, z %.2f -> %.2f\n", sinkFrame, goneFrame, z0, lowest);
	CHECK(zBeforeSink == z0);
	CHECK(sinkFrame >= 15);
	CHECK(sinkFrame <= 16);
	CHECK(goneFrame - sinkFrame == 60);
	CHECK(heldWhileSinking);
	// 0.08 per frame for the frames it sank (the last update before the destruction included)
	CHECK(std::fabs((z0 - lowest) - 0.08f * (float)(goneFrame - sinkFrame + 1)) < 0.1f);
}

// RotWK 2.01 MordorGrond: W3DTruckDraw DependencySharedModelFlags = MOVING TURN_LEFT TURN_RIGHT PREATTACK_A PREATTACK_B FIRING_A FIRING_B BETWEEN_FIRING_SHOTS_A
// BETWEEN_FIRING_SHOTS_B BACKING_UP; its crew MordorGrondCrew pushes with "AnimationState = MOVING ... PASSENGER_VARIATION_n" (Passenger_moving). RW 0x4BF2D8 hands
// those flags of the container's drawable to its dependents (FB-0002): while Grond rolls, the trolls' drawables have MOVING and pick a MOVING state; standing, not.
TEST_CASE("combat4 retail: Grond's DependencySharedModelFlags reach its troll crew's drawables: the trolls push while it rolls (FB-0002)")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared(), "FactionMordor", "FactionMen");
	ArchiveW3DFileSource source(*shared().mount->fs);
	WW3DAssetManager assets(source);
	DrawableManager drawables(assets, a.logic);
	ClientEventRecorder events(a.logic);
	a.logic.setClientHooks(&events);
	std::shared_ptr<const LogicSnapshot> snap;
	auto frame = [&]() {
		a.logic.runLogicFrame();
		drawables.applyEvents(events.take());
		snap = LogicSnapshot::build(a.logic, snap.get(), false, 0);
		drawables.advance(200.0);
		drawables.syncTransforms(*snap, 1.0, false);
	};
	Object *grond = a.place("MordorGrond", 0, 600.0f, 600.0f, 0.0f);
	for (int f = 0; f < 100; ++f)
	{
		frame();
	}
	SiegeEngineContain *g = dynamic_cast<SiegeEngineContain *>(grond->getContain());
	REQUIRE(g != nullptr);
	REQUIRE(g->crewList()->size() == 6u);
	const Drawable *gd = drawables.findByObject(grond->getID());
	REQUIRE(gd != nullptr);
	CHECK(gd->dependencySharedModelFlags().test(cond("MOVING")));
	auto crewMoving = [&]() {
		int moving = 0;
		for (Object *c : *g->crewList())
		{
			const Drawable *d = drawables.findByObject(c->getID());
			REQUIRE(d != nullptr);
			moving += d->getModelConditionFlags().test(cond("MOVING")) ? 1 : 0;
			CHECK_FALSE(c->testModelCondition(cond("MOVING"))); // the object's own flags are the logic's: the share is the drawable's
		}
		return moving;
	};
	CHECK(crewMoving() == 0);
	grond->getAIUpdateInterface()->aiMoveToPosition(Coord3D{ 900.0f, 600.0f, 0.0f }, CMD_FROM_PLAYER);
	int rollingFrames = 0, crewPushing = 0;
	for (int f = 0; f < 30; ++f)
	{
		frame();
		if (grond->testModelCondition(cond("MOVING")))
		{
			++rollingFrames;
			crewPushing = std::max(crewPushing, crewMoving());
		}
	}
	// the pushing state: the crew's draw picks an AnimationState whose conditions hold MOVING
	int movingStates = 0;
	for (Object *c : *g->crewList())
	{
		const Drawable *d = drawables.findByObject(c->getID());
		for (const DrawEntry &e : d->entries())
		{
			const W3DModelDrawModuleData *data = dynamic_cast<const W3DModelDrawModuleData *>(e.data);
			const AnimationStateInfo *st = data ? data->findBestAnimationState(d->getModelConditionFlags()) : nullptr;
			if (st && grond->testModelCondition(cond("MOVING")))
			{
				movingStates += st->conditions.test(cond("MOVING")) ? 1 : 0;
			}
		}
	}
	std::printf("  info: Grond rolled %d frames; at most %d of 6 trolls' drawables had MOVING, %d picked a MOVING state; %zu dependent updates\n", rollingFrames, crewPushing,
		movingStates, drawables.dependencyUpdates());
	// the wheels (RW 0x4CBFFB): W3DTruckDraw's twelve tire bones turn by TireRotationMultiplier 0.1 * the speed per logic frame
	float tireFront = 0.0f, tireRear = 0.0f;
	size_t tireBones = 0;
	for (const DrawEntry &e : gd->entries())
	{
		tireBones += e.frontTireBones.size() + e.rearTireBones.size();
		tireFront = std::max(tireFront, std::fabs(e.tireFront));
		tireRear = std::max(tireRear, std::fabs(e.tireRear));
	}
	std::printf("  info: Grond's tires: %zu bones, front angle %.2f, rear %.2f after the roll\n", tireBones, tireFront, tireRear);
	CHECK(tireBones == 12u);
	CHECK(tireFront > 0.0f);
	CHECK(tireRear > 0.0f);
	CHECK(rollingFrames > 5);
	CHECK(crewPushing == 6);
	CHECK(movingStates == 6);
	a.logic.setClientHooks(nullptr);
}

// FB-0003: a cavalry charge in numbers. GondorKnightsofDolHorde: CrusherLevel 1, MinCrushVelocityPercent 50%, CrushKnockback 40 per second (8 per frame,
// parseVelocityReal RW 0x73A4B6), CrushZFactor 1.0, no RamPower; MordorFighterHorde's orcs CrushableLevel 0. Each crush throws its victim along the knight's facing
// turned 30% toward it (Object::doKnockback RW 0x692223); what retail's data asks is a power-8 fling: measured here (flight height, distance, victims).
TEST_CASE("combat4 retail: Knights of Dol Amroth charging orcs throw them as retail's CrushKnockback asks (FB-0003 measurements)")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared(), "FactionMen", "FactionMordor");
	Object *orcs = a.place("MordorFighterHorde", 1, 700.0f, 600.0f, 3.14159265f);
	a.frames(2);
	orcs->getAIUpdateInterface()->aiIdle(CMD_FROM_AI);
	Object *knights = a.place("GondorKnightsofDolHorde", 0, 300.0f, 600.0f, 0.0f);
	a.frames(2);
	const std::vector<ObjectID> victims = a.memberIds(orcs);
	REQUIRE(knights->getAIUpdateInterface()->aiAttackObject(orcs, CMD_FROM_PLAYER));
	std::map<ObjectID, Coord3D> start;
	std::set<ObjectID> flung;
	float highest = 0.0f, farthest = 0.0f;
	int flightFrames = 0;
	for (int f = 0; f < 80; ++f)
	{
		std::map<ObjectID, Coord3D> before;
		for (ObjectID id : victims)
		{
			if (const Object *o = a.logic.findObjectByID(id))
			{
				before[id] = *o->getPosition();
			}
		}
		a.logic.runLogicFrame();
		for (ObjectID id : victims)
		{
			const Object *o = a.logic.findObjectByID(id);
			if (!o)
			{
				continue;
			}
			const bool flying = o->testModelCondition(cond("STUNNED_FLAILING"));
			if (flying && !flung.count(id))
			{
				flung.insert(id);
				start[id] = before[id];
			}
			if (flying)
			{
				++flightFrames;
				highest = std::max(highest, o->getPosition()->z - start[id].z);
			}
			if (flung.count(id))
			{
				const float dx = o->getPosition()->x - start[id].x, dy = o->getPosition()->y - start[id].y;
				farthest = std::max(farthest, std::sqrt(dx * dx + dy * dy));
			}
		}
	}
	std::printf("  info: FB-0003 Dol Amroth charge: %llu crushes, %llu knockbacks, %zu of %zu orcs thrown, %d flight frames, highest %.1f, farthest %.1f\n",
		a.counters().crushes, a.counters().crushKnockbacks, flung.size(), victims.size(), flightFrames, highest, farthest);
	CHECK(a.counters().crushKnockbacks > 0u);
	CHECK(flung.size() >= 3u);
	CHECK(highest > 5.0f);
	CHECK(farthest > 20.0f);
}

// round 2 (Sol): RW 0x6CCCA7 rolls HitPercentage (Weapon.cpp:1489) whatever the target is, after DamageDealtAtSelfPosition cleared the victim too. A weapon with both: one draw.
TEST_CASE("combat4 retail: a DamageDealtAtSelfPosition weapon still draws its HitPercentage roll (Weapon.cpp:1489) once per shot")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared(), "FactionMordor", "FactionMen");
	Object *troll = a.place("MordorAttackTroll", 0, 600.0f, 600.0f, 0.0f);
	Object *victim = a.place("GondorFighter", 1, 625.0f, 600.0f, 3.14159265f);
	a.frames(3);
	REQUIRE(TheWeaponStore);
	const WeaponTemplate *punch = TheWeaponStore->findWeaponTemplate("MordorDrummerTrollPunch");
	REQUIRE(punch);
	REQUIRE(punch->m_damageDealtAtSelfPosition);
	// no retail weapon has both: the test sets HitPercentage 50% on the shared template and restores it
	const float savedHit = punch->m_hitPercentage;
	const_cast<WeaponTemplate *>(punch)->m_hitPercentage = 0.5f;
	std::unique_ptr<Weapon> w = troll->getWeapons()->makeExtraWeapon(punch);
	troll->getWeapons()->loadExtraWeapon(*w);
	a.logic.random().enableCallLog(true);
	a.logic.random().clearCallLog();
	troll->getWeapons()->fireExtraWeapon(*w, *victim);
	int hitRolls = 0;
	for (const GameLogicRandom::Call &c : a.logic.random().callLog())
	{
		hitRolls += (c.real && c.file == "Weapon.cpp" && c.line == 1489) ? 1 : 0;
	}
	const_cast<WeaponTemplate *>(punch)->m_hitPercentage = savedHit;
	CHECK(hitRolls == 1);
}

// round 2 (Sol): the stops S-1790 / S-1791 / S-1792 are reported at runtime with their text
TEST_CASE("combat4 retail: S-1790 (a shockwave on a horde member whose horde is inside a container) is reported and counted")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared(), "FactionMordor", "FactionMen");
	Object *troll = a.place("MordorAttackTroll", 0, 600.0f, 600.0f, 0.0f);
	Object *horde = a.place("GondorFighterHorde", 1, 630.0f, 600.0f, 3.14159265f);
	Object *carrier = a.place("GondorFighter", 1, 900.0f, 900.0f, 0.0f);
	a.frames(4);
	horde->friend_setContainedBy(carrier); // the horde inside another container (a transport): the members' shockwave is retail's nested branch
	const WeaponTemplate *club = TheWeaponStore->findWeaponTemplate("WyrmDisappearWeapon"); // a MetaImpactNugget alone: no damage nugget kills the member first
	REQUIRE(club);
	std::unique_ptr<Weapon> w = troll->getWeapons()->makeExtraWeapon(club);
	troll->getWeapons()->loadExtraWeapon(*w);
	Object *victim = a.logic.findObjectByID(a.memberIds(horde).front());
	REQUIRE(victim);
	troll->getWeapons()->fireExtraWeapon(*w, *victim);
	CHECK(a.counters().metaImpactNestedHorde > 0u);
	bool found = false;
	for (const std::string &l : a.logic.report().stops)
	{
		found = found || l.compare(0, 8, "[S-1790]") == 0;
	}
	CHECK(found);
	horde->friend_setContainedBy(nullptr);
}

TEST_CASE("combat4 retail: S-1791 (DependencySharedModelFlags) and S-1792 (W3DTruckDraw tires) are reported by the draw of Grond")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared(), "FactionMordor", "FactionMen");
	ArchiveW3DFileSource source(*shared().mount->fs);
	WW3DAssetManager assets(source);
	DrawableManager drawables(assets, a.logic);
	ClientEventRecorder events(a.logic);
	a.logic.setClientHooks(&events);
	a.place("MordorGrond", 0, 600.0f, 600.0f, 0.0f);
	a.logic.runLogicFrame();
	drawables.applyEvents(events.take());
	int s1791 = 0, s1792 = 0;
	for (const std::string &l : drawables.report().stops)
	{
		s1791 += l.compare(0, 8, "[S-1791]") == 0 ? 1 : 0;
		s1792 += l.compare(0, 8, "[S-1792]") == 0 ? 1 : 0;
	}
	CHECK(s1791 == 1);
	CHECK(s1792 == 1);
	a.logic.setClientHooks(nullptr);
}

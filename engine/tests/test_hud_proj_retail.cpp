// File name: shares the retail world of the HUD tests (hudtest::shared(); doctest runs the test_hud_* files in name order, a retail world stays current for the process-wide stores
// until the next one loads). OpenBFME retail tests for projectiles (lane PROJ-1). They run only when ROTWK_INSTALL and BFME2_INSTALL are set (otherwise SKIP): pure RotWK 2.01 +
// BFME2 1.06. An archer horde of each faction fires real projectile objects at an enemy horde on a flat arena; the projectiles are counted, their flight is followed object by
// object (the path point of every frame, the impact frame against the arc speed) and the damage of every impact is compared with what the victim's armour leaves of the warhead.

#include "doctest.h"
#include "HudTestUtil.h"

#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Economy.h"
#include "GameLogic/EconomySettings.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ProjectileModules.h"
#include "GameLogic/Module/StructureModules.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponNugget.h"

#include "PathfindTestUtil.h"
#include "RetailTestMount.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

namespace
{
using hudtest::SharedWorld;
using hudtest::shared;

bool haveWorld()
{
	return hudtest::haveWorld("proj retail");
}

struct Arena
{
	std::unique_ptr<RetailObjectWorld::ContextScope> context;
	TeamFactory teams;
	PlayerList players;
	GameLogic logic;
	AIWorldConfig cfg;
	pathtest::SyntheticTerrain terrain;
	std::unique_ptr<AIWorld> ai;

	Arena(SharedWorld &s, const char *factionA, const char *factionB)
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
		logic.random().seedRandom(7);
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
	Object *place(const char *templateName, int side, float x, float y)
	{
		const ThingTemplate *t = shared().world->things().findTemplate(templateName);
		REQUIRE_MESSAGE(t != nullptr, "retail template " << templateName);
		Object *o = logic.newObject(t, player(side)->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE_MESSAGE(o != nullptr, templateName);
		Coord3D p{ x, y, 0.0f };
		o->setPosition(&p);
		o->setOrientation(side == 0 ? 0.0f : 3.14159265f);
		return o;
	}
};

// the projectile objects: a unit can carry a BezierProjectileBehavior too (a leaping or thrown unit is its own projectile); only KindOf PROJECTILE objects count here
BezierProjectileBehavior *bezierOf(Object *o)
{
	if (!o->isKindOf((unsigned)CombatNames::kinds().projectile))
	{
		return nullptr;
	}
	for (const std::unique_ptr<BehaviorModule> &m : o->modules())
	{
		if (BezierProjectileBehavior *b = dynamic_cast<BezierProjectileBehavior *>(m.get()))
		{
			return b;
		}
	}
	return nullptr;
}

struct Flight
{
	ObjectID id = 0;
	ObjectID victim = 0;
	std::string projectile, warhead, weapon;
	int firstSeen = -1, gone = -1;
	int segments = 0;
	float speed = 0.0f;
	std::vector<Coord3D> path;
	Coord3D start{}, end{};
	bool onPath = true; // the position of every observed frame was a path point
	size_t lastStep = 0;
	float h1 = 0.0f, h2 = 0.0f;
	bool hitStored = false;
	bool scaleSpeed = false;
	float weaponSpeed = 0.0f, minSpeed = 0.0f, maxSpeed = 0.0f;
};

struct ArcherRun
{
	std::vector<Flight> flights;
	std::map<ObjectID, std::vector<std::pair<int, float>>> victimHealth; // frame -> health, per victim member
	int impactsChecked = 0;
	int impactsDamaging = 0;
	bool damageMatches = true;
	std::string damageWhy;
	unsigned long long launched = 0, detonated = 0;
	size_t projectilesLeft = 0;
	std::vector<std::uint32_t> hashes;
};

// the warhead's first damage nugget: the amount and the damage type the victim's armour is asked about
bool warheadDamage(const WeaponTemplate &w, float &amount, int &type)
{
	for (const std::shared_ptr<WeaponNugget> &n : w.m_nuggets)
	{
		if (n->kind() == NUGGET_DAMAGE)
		{
			const DamageNugget &dn = static_cast<const DamageNugget &>(*n);
			amount = dn.m_damage;
			type = dn.m_damageType;
			return true;
		}
	}
	return false;
}

ArcherRun runArchers(const char *factionA, const char *archers, const char *factionB, const char *victims, int frames)
{
	SharedWorld &s = shared();
	ArcherRun out;
	Arena a(s, factionA, factionB);
	Object *h[2] = { a.place(archers, 0, 500.0f, 500.0f), a.place(victims, 1, 700.0f, 500.0f) };
	a.logic.runLogicFrame();
	a.logic.runLogicFrame();
	std::vector<ObjectID> members;
	for (const Object *m : *h[1]->getContain()->getContainedItemsList())
	{
		members.push_back(m->getID());
	}
	// lane HERO-2: archers with DualWeaponBehavior (RW 0x85DF88) draw their close range weapon once the victims are within SwitchWeaponOnCloseRangeDistance:
	// their melee hits land on the same members as the arrows, so a member loses at least (not exactly) the volley's share
	bool closeRangeSwitch = false;
	for (const Object *m : *h[0]->getContain()->getContainedItemsList())
	{
		closeRangeSwitch = closeRangeSwitch || m->findModule("DualWeaponBehavior") != nullptr;
	}
	REQUIRE(h[0]->getAIUpdateInterface()->aiAttackObject(h[1], CMD_FROM_PLAYER));
	std::map<ObjectID, size_t> index;
	std::map<ObjectID, float> healthBefore;
	for (ObjectID id : members)
	{
		healthBefore[id] = a.logic.findObjectByID(id)->getBodyModule()->getHealth();
	}
	for (int f = 0; f < frames; ++f)
	{
		// the projectiles of the frame before: what they will do on this update is decided by their module; remember the victims' health to compare after
		std::map<ObjectID, float> before;
		for (ObjectID id : members)
		{
			const Object *o = a.logic.findObjectByID(id);
			before[id] = (o && !o->isEffectivelyDead()) ? o->getBodyModule()->getHealth() : 0.0f;
		}
		// projectiles about to detonate in this frame (path used up): their warhead damage lands on their stored victim
		struct Pending
		{
			ObjectID victim;
			float expected;
		};
		std::vector<Pending> pending;
		for (Object *o = a.logic.getFirstObject(); o; o = o->getNextObject())
		{
			BezierProjectileBehavior *b = bezierOf(o);
			if (!b || b->flightPath().empty() || b->currentStep() < b->flightPath().size() || !b->warhead())
			{
				continue;
			}
			float amount;
			int type;
			Object *victim = a.logic.findObjectByID(b->victimID());
			if (!victim || !b->warhead()->m_hitStoredTarget || victim->isEffectivelyDead() || !warheadDamage(*b->warhead(), amount, type))
			{
				continue;
			}
			DamageInfoInput in;
			in.m_amount = amount;
			in.m_damageType = type;
			in.m_sourceID = b->projectileGetLauncherID();
			pending.push_back({ victim->getID(), victim->getBodyModule()->estimateDamage(in) });
		}
		a.logic.runLogicFrame();
		out.hashes.push_back(a.logic.computeStateHash());
		// follow the projectiles
		std::map<ObjectID, bool> seen;
		for (Object *o = a.logic.getFirstObject(); o; o = o->getNextObject())
		{
			BezierProjectileBehavior *b = bezierOf(o);
			if (!b)
			{
				continue;
			}
			seen[o->getID()] = true;
			Flight *fl = nullptr;
			for (Flight &x : out.flights)
			{
				if (x.id == o->getID())
				{
					fl = &x;
				}
			}
			if (!fl)
			{
				out.flights.emplace_back();
				fl = &out.flights.back();
				fl->id = o->getID();
				fl->firstSeen = f;
				fl->victim = b->victimID();
				fl->projectile = o->getTemplate()->getName();
				fl->segments = b->segments();
				fl->speed = b->flightSpeed();
				fl->path = b->flightPath();
				fl->start = b->flightStart();
				fl->end = b->flightEnd();
				fl->h1 = b->data()->m_firstHeight;
				fl->h2 = b->data()->m_secondHeight;
				fl->hitStored = b->warhead() && b->warhead()->m_hitStoredTarget;
				if (b->warhead())
				{
					fl->warhead = b->warhead()->getName();
				}
			}
			// the position of this frame is a path point (the update of the frame placed the projectile on the point of its step)
			if (!b->flightPath().empty() && b->currentStep() >= 1)
			{
				const Coord3D &p = b->flightPath()[b->currentStep() - 1];
				const Coord3D &q = *o->getPosition();
				fl->onPath = fl->onPath && p.x == q.x && p.y == q.y && p.z == q.z;
			}
		}
		for (Flight &fl : out.flights)
		{
			if (fl.gone < 0 && !seen.count(fl.id))
			{
				fl.gone = f;
			}
		}
		// the damage of the impacts of this frame
		std::map<ObjectID, float> expectedSum;
		for (const Pending &p : pending)
		{
			expectedSum[p.victim] += p.expected;
		}
		for (const auto &kv : expectedSum)
		{
			const Object *o = a.logic.findObjectByID(kv.first);
			const float after = (o && !o->isEffectivelyDead()) ? o->getBodyModule()->getHealth() : 0.0f;
			const float lost = before[kv.first] - after;
			const float want = std::min(kv.second, before[kv.first]);
			++out.impactsChecked;
			if (lost > 0.0f)
			{
				++out.impactsDamaging;
			}
			// other sources can hit the same member in the same frame only through the pending list of this volley: the member lost at least this volley's share
			const bool off = closeRangeSwitch ? lost + 0.01f + 0.001f * want < want : std::fabs(lost - want) > 0.01f + 0.001f * want;
			if (off)
			{
				out.damageMatches = false;
				out.damageWhy = "frame " + std::to_string(f) + " victim " + std::to_string(kv.first) + " lost " + std::to_string(lost) + " expected " + std::to_string(want);
			}
		}
	}
	out.launched = a.logic.combat().counters().projectilesLaunched;
	out.detonated = a.logic.combat().counters().projectilesDetonated;
	size_t left = 0;
	for (Object *o = a.logic.getFirstObject(); o; o = o->getNextObject())
	{
		left += bezierOf(o) ? 1u : 0u;
	}
	out.projectilesLeft = left;
	// the template data of every flight (read inside the context)
	for (Flight &fl : out.flights)
	{
		(void)fl;
	}
	return out;
}

struct Faction
{
	const char *faction, *archers, *enemyFaction, *victims;
};
const Faction kFactions[] = {
	{ "FactionMen", "GondorArcherHorde", "FactionMordor", "MordorFighterHorde" },
	{ "FactionElves", "ElvenLorienArcherHorde", "FactionIsengard", "IsengardFighterHorde" },
	{ "FactionDwarves", "DwarvenAxeThrowerHorde", "FactionAngmar", "AngmarDarkDunedainHorde" },
	{ "FactionIsengard", "IsengardUrukCrossbowHorde", "FactionMen", "GondorFighterHorde" },
	{ "FactionMordor", "MordorArcherHorde", "FactionElves", "ElvenLorienWarriorHorde" },
	{ "FactionWild", "GoblinArcherHorde", "FactionDwarves", "DwarvenPhalanxHorde" },
	{ "FactionAngmar", "AngmarDarkRangerHorde", "FactionWild", "GoblinFighterHorde" },
};

struct SiegeRun
{
	std::vector<Flight> flights;
	std::vector<int> detonationFrames;     // frames in which a projectile object disappeared
	std::vector<int> damageFrames;         // frames in which a victim member lost health
	std::vector<float> healthLost;         // per damage frame, the sum over the members
	unsigned long long launched = 0, detonated = 0, bounces = 0, groundHits = 0, fxUnplayed = 0;
	std::string projectile;
	int maxBounce = 0;
	bool onPath = true;
	unsigned long long landed = 0;
	int stuckAtEnd = 0;       // landed projectiles still in the world at the end
	int stuckWithoutLifetime = 0; // ... of those, the ones with no LifetimeUpdate or FinalStuckTime 0 (nothing removes them)
};

SiegeRun runSiege(const char *factionA, const char *siegeUnit, const char *factionB, const char *victims, float gap, int frames)
{
	SharedWorld &s = shared();
	SiegeRun out;
	Arena a(s, factionA, factionB);
	Object *u = a.place(siegeUnit, 0, 400.0f, 500.0f);
	Object *h = a.place(victims, 1, 400.0f + gap, 500.0f);
	a.logic.runLogicFrame();
	a.logic.runLogicFrame();
	std::vector<ObjectID> members;
	for (const Object *m : *h->getContain()->getContainedItemsList())
	{
		members.push_back(m->getID());
	}
	REQUIRE(u->getAIUpdateInterface()->aiAttackObject(h, CMD_FROM_PLAYER));
	std::map<ObjectID, int> firstSeen;
	std::map<ObjectID, int> bounceOf;
	for (int f = 0; f < frames; ++f)
	{
		float before = 0.0f;
		for (ObjectID id : members)
		{
			const Object *o = a.logic.findObjectByID(id);
			before += (o && !o->isEffectivelyDead()) ? o->getBodyModule()->getHealth() : 0.0f;
		}
		a.logic.runLogicFrame();
		std::map<ObjectID, bool> seen;
		for (Object *o = a.logic.getFirstObject(); o; o = o->getNextObject())
		{
			BezierProjectileBehavior *b = bezierOf(o);
			if (!b)
			{
				continue;
			}
			seen[o->getID()] = true;
			if (!firstSeen.count(o->getID()))
			{
				firstSeen[o->getID()] = f;
				out.projectile = o->getTemplate()->getName();
			}
			bounceOf[o->getID()] = std::max(bounceOf[o->getID()], b->bounceIndex());
			out.maxBounce = std::max(out.maxBounce, b->bounceIndex());
			if (!b->flightPath().empty() && b->currentStep() >= 1)
			{
				const Coord3D &p = b->flightPath()[b->currentStep() - 1];
				const Coord3D &q = *o->getPosition();
				out.onPath = out.onPath && p.x == q.x && p.y == q.y && p.z == q.z;
			}
		}
		for (const auto &kv : firstSeen)
		{
			if (!seen.count(kv.first) && std::find(out.detonationFrames.begin(), out.detonationFrames.end(), -1 - (int)kv.first) == out.detonationFrames.end())
			{
				out.detonationFrames.push_back(f);
				out.detonationFrames.push_back(-1 - (int)kv.first); // marker: this id is accounted for
			}
		}
		float after = 0.0f;
		for (ObjectID id : members)
		{
			const Object *o = a.logic.findObjectByID(id);
			after += (o && !o->isEffectivelyDead()) ? o->getBodyModule()->getHealth() : 0.0f;
		}
		if (after < before)
		{
			out.damageFrames.push_back(f);
			out.healthLost.push_back(before - after);
		}
	}
	out.launched = a.logic.combat().counters().projectilesLaunched;
	out.detonated = a.logic.combat().counters().projectilesDetonated;
	out.bounces = a.logic.combat().counters().projectileBounces;
	out.groundHits = a.logic.combat().counters().projectileGroundHits;
	out.fxUnplayed = a.logic.combat().counters().projectileFxUnplayed;
	out.landed = a.logic.combat().counters().projectilesLanded;
	for (Object *o = a.logic.getFirstObject(); o; o = o->getNextObject())
	{
		BezierProjectileBehavior *b = bezierOf(o);
		if (!b || !b->flightPath().empty() || b->hasDetonated() || o->isDestroyed())
		{
			continue;
		}
		++out.stuckAtEnd;
		bool lifetime = false;
		for (const std::unique_ptr<BehaviorModule> &m : o->modules())
		{
			lifetime = lifetime || dynamic_cast<LifetimeUpdate *>(m.get()) != nullptr;
		}
		out.stuckWithoutLifetime += (lifetime && b->data()->m_finalStuckTime > 0) ? 0 : 1;
	}
	return out;
}

struct Siege
{
	const char *faction, *unit, *enemyFaction, *victims;
	float gap;
};
const Siege kSiege[] = {
	{ "FactionMen", "GondorTrebuchet", "FactionMordor", "MordorFighterHorde", 450.0f }, // GondorTrebuchetRock: range 500, MinimumAttackRange 300 (the horde radius counts)
	{ "FactionMordor", "MordorCatapult", "FactionMen", "GondorFighterHorde", 300.0f },
	{ "FactionDwarves", "DwarvenCatapult", "FactionMordor", "MordorFighterHorde", 300.0f },
	{ "FactionIsengard", "IsengardBallista", "FactionMen", "GondorFighterHorde", 300.0f },
};
} // namespace

TEST_CASE("proj retail: every faction's archer horde fires real projectile objects - counted, followed point by point, impact frame from the arc speed, damage from the armour")
{
	if (!haveWorld())
	{
		return;
	}
	for (const Faction &fc : kFactions)
	{
		const std::string label = std::string(fc.archers) + " at " + fc.victims;
		INFO(label);
		const ArcherRun r = runArchers(fc.faction, fc.archers, fc.enemyFaction, fc.victims, 260);
		// (1) real projectiles: one object per shot, every one of them detonated and removed
		CHECK(r.launched >= 6);
		CHECK(r.flights.size() == (size_t)r.launched);
		CHECK(r.detonated == r.launched - r.projectilesLeft);
		// (2) each flight: on its path in every observed frame, detonating one update after its last path point (the launch frame already took two), the segment count from the arc speed
		int fullFlights = 0;
		for (const Flight &fl : r.flights)
		{
			CHECK(fl.onPath);
			if (fl.gone < 0)
			{
				continue;
			}
			++fullFlights;
			CHECK(fl.gone - fl.firstSeen == fl.segments - 1); // the launch frame takes path points 0 and 1 (RW 0x85EF34, lane PROJ-2)
			const float dx = fl.end.x - fl.start.x, dy = fl.end.y - fl.start.y, dz = fl.end.z - fl.start.z;
			const float chord = std::sqrt(dx * dx + dy * dy + dz * dz);
			CHECK(fl.speed > 0.0f);
			CHECK(fl.segments >= std::max(2, (int)std::ceil(chord / fl.speed - 1e-3f)));
			CHECK(fl.segments <= (int)std::ceil((chord + 2.0f * (fl.h1 + fl.h2) + 1.0f) / fl.speed) + 1);
			CHECK(fl.path.size() == (size_t)fl.segments);
		}
		CHECK(fullFlights >= 6);
		// (3) the damage of every impact: the victim loses exactly what its armour leaves of the warhead (clipped to its health)
		CHECK_MESSAGE(r.damageMatches, r.damageWhy);
		CHECK(r.impactsChecked >= 4);
		CHECK(r.impactsDamaging >= 4);
		std::printf("  info: %s: %llu projectiles (%d flights followed to the impact), first flight %d segments at %.2f per frame (%s), %d impacts checked\n", label.c_str(), r.launched, fullFlights,
			r.flights.empty() ? 0 : r.flights[0].segments, r.flights.empty() ? 0.0f : r.flights[0].speed, r.flights.empty() ? "" : r.flights[0].projectile.c_str(), r.impactsChecked);
	}
}

TEST_CASE("proj retail: the same volleys twice give the same state hash in every frame")
{
	if (!haveWorld())
	{
		return;
	}
	const ArcherRun a = runArchers("FactionMen", "GondorArcherHorde", "FactionMordor", "MordorFighterHorde", 140);
	const ArcherRun b = runArchers("FactionMen", "GondorArcherHorde", "FactionMordor", "MordorFighterHorde", 140);
	REQUIRE(a.hashes.size() == b.hashes.size());
	for (size_t i = 0; i < a.hashes.size(); ++i)
	{
		REQUIRE_MESSAGE(a.hashes[i] == b.hashes[i], "frame " << i);
	}
	CHECK(a.launched > 0);
	CHECK(a.launched == b.launched);
}

TEST_CASE("proj retail: the siege engines throw real stones and bolts - flights followed, bounces counted, health only lost where a projectile detonated")
{
	if (!haveWorld())
	{
		return;
	}
	for (const Siege &sg : kSiege)
	{
		const std::string label = std::string(sg.unit) + " at " + sg.victims;
		INFO(label);
		const SiegeRun r = runSiege(sg.faction, sg.unit, sg.enemyFaction, sg.victims, sg.gap, 400);
		CHECK(r.launched >= 2);
		CHECK(r.onPath);
		CHECK(r.detonated >= 1);
		MESSAGE(label << ": landed and still in the world after 400 frames: " << r.stuckAtEnd); // a landed stone leaves through its own modules; FinalStuckTime's AI busy command is S-363
		CHECK(r.stuckWithoutLifetime == 0);
		CHECK(r.groundHits >= r.detonated); // every detonation after a path end; a bouncing stone ends several paths
		CHECK(r.bounces + r.detonated <= r.groundHits + 0u * r.bounces + r.launched); // a path end bounces, or detonates (the stone in flight at the end is neither)
		// health is lost only on a frame after a projectile detonated (or a crowd's pending damage of that frame)
		for (int df : r.damageFrames)
		{
			bool afterAnImpact = false;
			for (int dt : r.detonationFrames)
			{
				afterAnImpact = afterAnImpact || (dt >= 0 && dt == df);
			}
			CHECK_MESSAGE(afterAnImpact, "health lost at frame " << df << " without a projectile detonating in it");
		}
		std::printf("  info: %s: %llu landed, %d landed projectiles still in the world after 400 frames (%d of them with nothing that removes them)\n", label.c_str(), r.landed, r.stuckAtEnd, r.stuckWithoutLifetime);
		std::printf("  info: %s: projectile %s, %llu launched, %llu detonated, %llu ground hits, %llu bounces, highest bounce %d, %zu damage frames, %.0f health lost\n", label.c_str(), r.projectile.c_str(), r.launched,
			r.detonated, r.groundHits, r.bounces, r.maxBounce, r.damageFrames.size(), std::accumulate(r.healthLost.begin(), r.healthLost.end(), 0.0f));
	}
}

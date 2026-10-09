// OpenBFME. Lane MOVE-2 (owner feedback FEEDBACK-1 F4, 2026-10-07: "infantry seem to wander around and still get lost / jerk a tad"): the member distance
// from its slot over time, on several retail maps and horde types.
//
// Retail data. On each map a set of hordes is created side by side on the map's diagonal (at 35 % of the extent) and each is ordered on its own across the
// map (to 65 %, offset sideways as they started so the hordes do not share a destination), then left standing. Every logic frame each member's
// distance from its formation slot (HordeContainInterface::getMemberFormationPosition, RW 0x877D89: the slot for the horde's current transform) and its step
// are recorded. Measured per horde:
//   * march: the mean / 95th percentile / largest slot distance while the horde object moves;
//   * straggle: the longest run of frames one member spent more than kFarSlot from its slot and kFarHorde from the horde while the horde moved;
//   * settle: the frames from the horde's arrival until no member moves any more (-1: still moving at the end of the run);
//   * lost: the members still more than kFarSlot from their slots and kFarHorde from the horde's centre kSettleFrames after the arrival (a member whose slot
//     the pathfinder pulled toward the horde, RW 0x6F0889, is with its horde);
//   * jerk: logic step reversals (consecutive steps against each other, both longer than 0.5) and speed dips (a member whose step falls under a third of
//     the horde's while the frames before and after carry more than two thirds of it: a stop-and-go) while the horde moves.
// With MOVE2_DUMP=<file> the per-frame records are written as CSV (map, horde, member, frame, moving, x, y, slot x, slot y, distance) for
// tools/move/slot_report.py.

#include "doctest.h"
#include "HudTestUtil.h"

#include "GameLogic/AI/AIAttack.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/HordeContain.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{
const double kFarSlot = 40.0;   ///< a member this far from its slot is separated from its horde
const int kSettleFrames = 75;   ///< 15 s after the arrival
const double kFarHorde = 80.0;  ///< a member this far from its horde's centre is not with it (the formations are at most about 60 deep)

struct MemberSample
{
	UnsignedInt frame = 0;
	bool hordeMoving = false;
	double x = 0, y = 0, sx = 0, sy = 0, dist = 0;
	double hordeDist = 0; ///< from the horde object's position
	double hordeStep = 0; ///< the horde object's step into this frame
};

struct HordeTrack
{
	std::string name;
	ObjectID id = INVALID_ID;
	std::map<ObjectID, std::vector<MemberSample>> members;
	std::vector<bool> moving; ///< per recorded frame
	std::vector<bool> fighting; ///< per recorded frame: the horde or one of its members is in an attack state (AI_ATTACK_OBJECT)
	int arrival = -1;         ///< index of the frame the horde last stopped
	Coord3D last{ 0, 0, 0 };  ///< the horde object's position of the previous record
	bool haveLast = false;
	std::set<ObjectID> backingAway; ///< lane COMBAT-4 r3: members with a back-up record (fleeing a scarer) when the run ended
};

struct HordeReport
{
	int members = 0;
	double marchMean = 0, marchP95 = 0, marchMax = 0;
	int straggleFrames = 0;
	int settle = -1;
	int lost = 0;
	int backingAway = 0; ///< lane COMBAT-4 r3: members far from their slots because they back away from a scarer (not lost)
	int reversals = 0;
	int dips = 0;
	int marchFrames = 0;
	int fightFrames = 0; ///< frames after the arrival in which the horde or a member attacked (its settle then measures a fight, not a re-form)
	double endMax = 0;
};

double len2(double x, double y)
{
	return std::sqrt(x * x + y * y);
}

void moveTo(LiveGame &game, int player, ObjectID id, const Coord3D &where)
{
	GameMessage sel(MSG_CREATE_SELECTED_GROUP, player);
	sel.appendBooleanArgument(true);
	sel.appendObjectIDArgument(id);
	game.commands().append(sel);
	GameMessage mv(MSG_DO_MOVETO, player);
	mv.appendLocationArgument(where);
	game.commands().append(mv);
}

void record(GameLogic &logic, HordeTrack &t)
{
	Object *h = logic.findObjectByID(t.id);
	if (!h || !h->getContain())
	{
		return;
	}
	AIUpdateInterface *ai = h->getAIUpdateInterface();
	const bool moving = ai && ai->isMoving();
	const size_t idx = t.moving.size();
	t.moving.push_back(moving);
	bool fight = ai && ai->currentStateId() == (unsigned)AI_ATTACK_OBJECT;
	if (const auto *list = h->getContain()->getContainedItemsList())
	{
		for (const Object *m : *list)
		{
			const AIUpdateInterface *ma = m ? m->getAIUpdateInterface() : nullptr;
			fight = fight || (ma && ma->currentStateId() == (unsigned)AI_ATTACK_OBJECT);
		}
	}
	t.fighting.push_back(fight);
	const Coord3D hp = *h->getPosition();
	const double hstep = t.haveLast ? len2(hp.x - t.last.x, hp.y - t.last.y) : 0.0;
	t.last = hp;
	t.haveLast = true;
	HordeContainInterface *hc = h->getContain()->getHordeContainInterface();
	REQUIRE(hc != nullptr);
	if (const auto *list = h->getContain()->getContainedItemsList())
	{
		for (Object *m : *list)
		{
			if (!m || m->isDestroyed() || hc->getMemberSlot(m) < 0)
			{
				continue;
			}
			const Coord3D s = hc->getMemberFormationPosition(m);
			MemberSample ms;
			ms.frame = (UnsignedInt)idx;
			ms.hordeMoving = moving;
			ms.x = m->getPosition()->x;
			ms.y = m->getPosition()->y;
			ms.sx = s.x;
			ms.sy = s.y;
			ms.dist = len2(ms.sx - ms.x, ms.sy - ms.y);
			ms.hordeDist = len2(hp.x - ms.x, hp.y - ms.y);
			ms.hordeStep = hstep;
			t.members[m->getID()].push_back(ms);
		}
	}
}

HordeReport analyse(HordeTrack &t)
{
	HordeReport r;
	// the arrival: the first frame of the last run of standing frames
	int arrival = -1;
	for (int i = (int)t.moving.size() - 1; i >= 0; --i)
	{
		if (t.moving[(size_t)i])
		{
			arrival = i + 1;
			break;
		}
	}
	t.arrival = arrival;
	std::vector<double> march;
	r.members = (int)t.members.size();
	for (auto &mm : t.members)
	{
		const std::vector<MemberSample> &s = mm.second;
		int run = 0;
		for (size_t k = 0; k < s.size(); ++k)
		{
			const MemberSample &a = s[k];
			if (a.hordeMoving)
			{
				march.push_back(a.dist);
				run = (a.dist > kFarSlot && a.hordeDist > kFarHorde) ? run + 1 : 0;
				r.straggleFrames = std::max(r.straggleFrames, run);
			}
			if (k >= 2 && a.hordeMoving && s[k - 1].frame + 1 == a.frame && s[k - 2].frame + 2 == a.frame)
			{
				const double ax = s[k - 1].x - s[k - 2].x, ay = s[k - 1].y - s[k - 2].y;
				const double bx = a.x - s[k - 1].x, by = a.y - s[k - 1].y;
				if (len2(ax, ay) > 0.5 && len2(bx, by) > 0.5 && ax * bx + ay * by < 0.0)
				{
					++r.reversals;
				}
			}
			if (k >= 2 && k + 1 < s.size() && a.hordeMoving && a.hordeStep > 1.0 && s[k + 1].frame == a.frame + 1 && s[k - 2].frame + 2 == a.frame)
			{
				const double before = len2(s[k - 1].x - s[k - 2].x, s[k - 1].y - s[k - 2].y); // the step just before (review r1: was two frames before)
				const double now = len2(a.x - s[k - 1].x, a.y - s[k - 1].y);
				const double after = len2(s[k + 1].x - a.x, s[k + 1].y - a.y);
				const double ref = a.hordeStep;
				if (now < ref / 3.0 && before > ref * 2.0 / 3.0 && after > ref * 2.0 / 3.0)
				{
					++r.dips;
				}
			}
		}
		// lost: far from its slot and from its horde (a member whose slot the pathfinder moved toward the horde, RW 0x6F0889, stands between the two)
		if (arrival >= 0 && !s.empty() && (int)s.back().frame >= arrival + kSettleFrames && s.back().dist > kFarSlot && s.back().hordeDist > kFarHorde)
		{
			// lane COMBAT-4 r3: a member backing away from a scarer (on "fall back 4p" the GoblinFighterHorde stops next to a creep CaveTroll_Slaved, SCARY, whose club
			// swing throws and frightens the goblins) stands where its back-up record sends it (RW 0x874155 .. 0x874671): the emotion, not a lost member
			if (t.backingAway.count(mm.first) != 0)
			{
				++r.backingAway;
			}
			else
			{
				++r.lost;
			}
		}
		if (!s.empty())
		{
			r.endMax = std::max(r.endMax, s.back().dist);
		}
	}
	for (size_t i = arrival >= 0 ? (size_t)arrival : t.fighting.size(); i < t.fighting.size(); ++i)
	{
		r.fightFrames += t.fighting[i] ? 1 : 0;
	}
	r.marchFrames = 0;
	for (bool b : t.moving)
	{
		r.marchFrames += b ? 1 : 0;
	}
	if (!march.empty())
	{
		std::sort(march.begin(), march.end());
		double sum = 0;
		for (double d : march)
		{
			sum += d;
		}
		r.marchMean = sum / (double)march.size();
		r.marchP95 = march[(size_t)(0.95 * (double)(march.size() - 1))];
		r.marchMax = march.back();
	}
	if (arrival >= 0)
	{
		// settled: from this frame on no member moves more than 0.1 a frame
		int lastMove = arrival - 1;
		for (const auto &mm : t.members)
		{
			const std::vector<MemberSample> &s = mm.second;
			for (size_t k = 1; k < s.size(); ++k)
			{
				if ((int)s[k].frame >= arrival && len2(s[k].x - s[k - 1].x, s[k].y - s[k - 1].y) > 0.1)
				{
					lastMove = std::max(lastMove, (int)s[k].frame);
				}
			}
		}
		const int lastFrame = (int)t.moving.size() - 1;
		r.settle = lastMove >= lastFrame ? -1 : lastMove + 1 - arrival;
	}
	return r;
}

struct MapRun
{
	std::vector<HordeTrack> tracks;
	std::vector<HordeReport> reports;
	std::vector<std::string> notes; ///< per horde: where it started, was sent and ended
};

struct Route
{
	const char *map;
	float ax, ay, bx, by; ///< the march's ends as fractions of the extent
};

MapRun runMap(const Route &route, const std::vector<std::string> &hordes, int frames)
{
	MapRun out;
	hudtest::Rig rig(hudtest::shared(), route.map);
	const int ours = rig.local->getPlayerIndex();
	rig.game->advance(0.2);
	// the march: between the route's ends (fractions of the pathfinder's grid, 10 units a cell)
	const ICoord2D *ext = rig.game->ai().pathfinder().getExtent();
	const Coord3D a{ (float)(ext->x * 10.0 * route.ax), (float)(ext->y * 10.0 * route.ay), 0.0f };
	const Coord3D b{ (float)(ext->x * 10.0 * route.bx), (float)(ext->y * 10.0 * route.by), 0.0f };
	const double dx = b.x - a.x, dy = b.y - a.y, d = len2(dx, dy);
	REQUIRE(d > 400.0);
	const double ux = dx / d, uy = dy / d; // along the march
	const double px = -uy, py = ux;        // sideways
	const double spacing = 110.0;
	for (size_t i = 0; i < hordes.size(); ++i)
	{
		const double side = ((double)i - (double)(hordes.size() - 1) * 0.5) * spacing;
		const Coord3D start{ (float)(a.x + dx * 0.25 + px * side), (float)(a.y + dy * 0.25 + py * side), 0.0f };
		std::string err;
		Object *h = rig.game->createObject(hordes[i], ours, start, (float)std::atan2(uy, ux), &err);
		REQUIRE_MESSAGE(h, hordes[i] << ": " << err);
		HordeTrack t;
		t.name = hordes[i];
		t.id = h->getID();
		out.tracks.push_back(t);
	}
	for (int i = 0; i < 5; ++i)
	{
		rig.game->advance(0.2);
	}
	for (size_t i = 0; i < hordes.size(); ++i)
	{
		const double side = ((double)i - (double)(hordes.size() - 1) * 0.5) * spacing;
		const Coord3D dest{ (float)(a.x + dx * 0.75 + px * side), (float)(a.y + dy * 0.75 + py * side), 0.0f };
		moveTo(*rig.game, ours, out.tracks[i].id, dest);
	}
	for (int f = 0; f < frames; ++f)
	{
		rig.game->advance(0.2);
		for (HordeTrack &t : out.tracks)
		{
			record(rig.logic(), t);
		}
	}
	for (size_t i = 0; i < out.tracks.size(); ++i)
	{
		// lane COMBAT-4 r3: the members backing away from a scarer at the end (HordeContain's back-up records, RW 0x874155 .. 0x874671): their position is the
		// emotion's, not their slot's
		if (const Object *hh = rig.logic().findObjectByID(out.tracks[i].id))
		{
			if (const HordeContain *hc = dynamic_cast<const HordeContain *>(const_cast<Object *>(hh)->getContain()->getHordeContainInterface()))
			{
				for (const auto &rec : hc->backUpRecords())
				{
					out.tracks[i].backingAway.insert(rec.first);
				}
			}
		}
		out.reports.push_back(analyse(out.tracks[i]));
		const Object *h = rig.logic().findObjectByID(out.tracks[i].id);
		char buf[200];
		std::snprintf(buf, sizeof buf, "end (%.0f, %.0f)", h ? h->getPosition()->x : -1.0, h ? h->getPosition()->y : -1.0);
		out.notes.push_back(buf);
	}
	return out;
}

void dumpCsv(FILE *f, const char *mapName, const MapRun &run)
{
	for (const HordeTrack &t : run.tracks)
	{
		for (const auto &mm : t.members)
		{
			for (const MemberSample &s : mm.second)
			{
				std::fprintf(f, "%s,%s,%u,%u,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n", mapName, t.name.c_str(), mm.first, s.frame, s.hordeMoving ? 1 : 0, s.x, s.y, s.sx, s.sy,
					s.dist, s.hordeDist);
			}
		}
	}
}

const std::vector<std::string> &hordeSet()
{
	static const std::vector<std::string> s = { "GondorFighterHorde", "GondorArcherHorde", "MordorFighterHorde", "IsengardFighterHorde", "ElvenLorienWarriorHorde",
		"DwarvenPhalanxHorde", "GoblinFighterHorde", "RohanRohirrimHorde" };
	return s;
}
} // namespace

TEST_CASE("move2 retail: horde members stay near their slots on the march, settle when the horde stops and none is lost (slot-distance report)")
{
	if (!hudtest::haveWorld("move2 slot distance"))
	{
		return;
	}
	FILE *dump = nullptr;
	if (const char *path = std::getenv("MOVE2_DUMP"))
	{
		dump = std::fopen(path, "w");
		REQUIRE(dump != nullptr);
		std::fprintf(dump, "map,horde,member,frame,moving,x,y,sx,sy,dist,hordedist\n");
	}
	std::vector<Route> routes = { { "map mp fall back 4p", 0.2f, 0.2f, 0.8f, 0.8f }, { "map mp harlindon", 0.2f, 0.2f, 0.8f, 0.8f },
		{ "map mp amon sul fortress", 0.2f, 0.2f, 0.8f, 0.8f } };
	if (const char *r = std::getenv("MOVE2_ROUTE")) // a route to try: "<map>,ax,ay,bx,by"
	{
		static std::string name;
		Route t{};
		char buf[128] = { 0 };
		REQUIRE(std::sscanf(r, "%127[^,],%f,%f,%f,%f", buf, &t.ax, &t.ay, &t.bx, &t.by) == 5);
		name = buf;
		t.map = name.c_str();
		routes = { t };
	}
	for (const Route &route : routes)
	{
		const char *mapName = route.map;
		MapRun run = runMap(route, hordeSet(), 450);
		if (dump)
		{
			dumpCsv(dump, mapName, run);
		}
		for (size_t i = 0; i < run.tracks.size(); ++i)
		{
			const HordeReport &r = run.reports[i];
			MESSAGE("MOVE2 " << std::string(mapName) << " " << run.tracks[i].name << " members=" << r.members << " march_frames=" << r.marchFrames << " march mean="
							<< r.marchMean << " p95=" << r.marchP95 << " max=" << r.marchMax << " straggle=" << r.straggleFrames << " settle=" << r.settle
							<< " lost=" << r.lost << " backing_away=" << r.backingAway << " end_max=" << r.endMax << " reversals=" << r.reversals << " dips=" << r.dips << " fight=" << r.fightFrames << " " << run.notes[i]);
			INFO(mapName << " " << run.tracks[i].name);
			CHECK(r.members > 0);
			CHECK(r.marchFrames > 30); // every horde marched
			// none lost, none straggling: before MOVE-2 a MordorFighterHorde soldier never left its spawn on fall back 4p (on a cliff cell, 1809 from its slot at the
			// end), two GoblinFighterHorde soldiers stayed 636 behind on amon sul fortress, an archer that attacked while its horde marched off stayed for good
			CHECK(r.lost == 0);
			CHECK(r.straggleFrames <= 10);
			// every horde that lives to the end comes to rest within the run (lane MOVE-2 r3: since the MOD-4 merge the creep lairs on the fall back 4p route
			// spawn cave trolls, which may destroy a horde: its "settle" is then -1; r4: since the troll punch keeps its DamageArc cone, RW 0x90DEF0, they
			// kill a few members per punch instead of the whole horde, so a horde that loses members is still in that fight at the end)
			unsigned lastFrame = 0;
			bool memberDied = false;
			for (const auto &mm : run.tracks[i].members)
			{
				lastFrame = mm.second.empty() ? lastFrame : std::max(lastFrame, mm.second.back().frame);
			}
			for (const auto &mm : run.tracks[i].members)
			{
				memberDied = memberDied || mm.second.empty() || mm.second.back().frame < lastFrame;
			}
			if (run.notes[i].find("end (-1, -1)") == std::string::npos && !memberDied)
			{
				CHECK(r.settle >= 0);
			}
		}
	}
	if (dump)
	{
		std::fclose(dump);
	}
}

TEST_CASE("move2: the slot destination's (S-1500) and the hand-off's (S-1501) inferences and the melee engagement (S-1502) are reported; S-892 is applied")
{
	int s1500 = 0, s1501 = 0, s1502 = 0, s892 = 0;
	for (const std::string &l : SkirmishAIManager::stopLines())
	{
		s1500 += l.rfind("[S-1500]", 0) == 0 ? 1 : 0;
		s1501 += l.rfind("[S-1501]", 0) == 0 ? 1 : 0;
		s1502 += l.rfind("[S-1502]", 0) == 0 ? 1 : 0;
		s892 += (l.rfind("[S-892]", 0) == 0 && l.find("applied it in full") != std::string::npos) ? 1 : 0;
	}
	CHECK(s1500 == 1);
	CHECK(s1501 == 1);
	CHECK(s1502 == 1);
	CHECK(s892 == 1);
}

namespace
{
struct MeleeReport
{
	int members = 0;          ///< members of the attacking side at the order
	int neverEngaged = 0;     ///< members alive at the end that never attacked (AI_ATTACK_OBJECT or IS_MELEE_ATTACKING) while the enemy lived
	double engagedShare = 0;  ///< mean share of the living members attacking, over the frames of the melee (from the first member's attack)
	int meleeFrames = 0;
	int shuffleSteps = 0;     ///< member steps longer than 0.5 in melee frames while that member did not attack
	int firstContact = -1;    ///< the frame index of the first attack
	unsigned long long hits = 0; ///< damage applications during the run (both sides)
	int deadAttackers = 0, deadDefenders = 0;
};

// lane MOVE-2 r2 (community feedback FEEDBACK-2 G2, devblog #2 at 0:17: "soldiers stay stuck behind their own front line re-positioning instead of moving
// through the gaps between their own soldiers to engage"): `attacker` and `defender` hordes face each other 260 apart on fall back 4p's centre; both attack.
MeleeReport runMelee(const char *attacker, const char *defender, int frames)
{
	MeleeReport r;
	hudtest::Rig rig(hudtest::shared());
	Player *enemy = rig.game->players().findPlayerWithName("Player_2");
	REQUIRE(enemy != nullptr);
	const ICoord2D *ext = rig.game->ai().pathfinder().getExtent();
	const float cx = (float)(ext->x * 5), cy = (float)(ext->y * 5);
	Object *a = rig.make(attacker, cx - 130.0f, cy);
	Object *d = rig.make(defender, cx + 130.0f, cy, enemy);
	const ObjectID aid = a->getID(), did = d->getID();
	for (int i = 0; i < 3; ++i)
	{
		rig.game->advance(0.2);
	}
	std::vector<ObjectID> members;
	for (const Object *m : *a->getContain()->getContainedItemsList())
	{
		members.push_back(m->getID());
	}
	r.members = (int)members.size();
	const size_t defenderCount = d->getContain() ? d->getContain()->getContainCount() : 0;
	const unsigned long long hits0 = rig.logic().combat().counters().damageApplications;
	REQUIRE(a->getAIUpdateInterface()->aiAttackObject(d, CMD_FROM_PLAYER));
	d->getAIUpdateInterface()->aiAttackObject(a, CMD_FROM_PLAYER);
	static const int meleeAttacking = ObjectTemplateInfoBuilder::objectStatusIndex("IS_MELEE_ATTACKING");
	std::map<ObjectID, bool> engaged;
	std::map<ObjectID, Coord3D> last;
	double shareSum = 0.0;
	for (int f = 0; f < frames; ++f)
	{
		rig.game->advance(0.2);
		Object *dh = rig.logic().findObjectByID(did);
		const bool enemyAlive = dh && !dh->isEffectivelyDead() && dh->getContain() && dh->getContain()->getContainCount() > 0;
		int alive = 0, attacking = 0;
		for (ObjectID id : members)
		{
			Object *m = rig.logic().findObjectByID(id);
			if (!m || m->isEffectivelyDead())
			{
				continue;
			}
			++alive;
			const AIUpdateInterface *ai = m->getAIUpdateInterface();
			const bool att = (ai && ai->currentStateId() == (unsigned)AI_ATTACK_OBJECT) || (meleeAttacking >= 0 && m->testStatus((unsigned)meleeAttacking));
			if (att && enemyAlive)
			{
				engaged[id] = true;
				attacking += 1;
				if (r.firstContact < 0)
				{
					r.firstContact = f;
				}
			}
			const Coord3D p = *m->getPosition();
			if (r.firstContact >= 0 && enemyAlive && !att && last.count(id) && len2(p.x - last[id].x, p.y - last[id].y) > 0.5)
			{
				++r.shuffleSteps;
			}
			last[id] = p;
		}
		if (r.firstContact >= 0 && enemyAlive && alive > 0)
		{
			shareSum += (double)attacking / (double)alive;
			++r.meleeFrames;
		}
	}
	(void)aid;
	for (ObjectID id : members)
	{
		Object *m = rig.logic().findObjectByID(id);
		if (m && !m->isEffectivelyDead() && !engaged.count(id))
		{
			++r.neverEngaged;
		}
	}
	r.engagedShare = r.meleeFrames ? shareSum / r.meleeFrames : 0.0;
	r.hits = rig.logic().combat().counters().damageApplications - hits0;
	for (ObjectID id : members)
	{
		const Object *m = rig.logic().findObjectByID(id);
		r.deadAttackers += (!m || m->isEffectivelyDead()) ? 1 : 0;
	}
	if (Object *dh = rig.logic().findObjectByID(did))
	{
		r.deadDefenders = (int)(defenderCount - (dh->getContain() ? dh->getContain()->getContainCount() : 0));
	}
	else
	{
		r.deadDefenders = (int)defenderCount;
	}
	return r;
}
} // namespace

TEST_CASE("move2 retail: horde melee engagement (G2): the members reach the enemy instead of shuffling behind their own front line")
{
	if (!hudtest::haveWorld("move2 melee engagement"))
	{
		return;
	}
	const std::pair<const char *, const char *> pairs[] = { { "GondorFighterHorde", "MordorFighterHorde" }, { "MordorFighterHorde", "GondorFighterHorde" },
		{ "IsengardFighterHorde", "ElvenLorienWarriorHorde" }, { "GondorFighterHorde", "IsengardFighterHorde" } };
	for (const auto &p : pairs)
	{
		const MeleeReport r = runMelee(p.first, p.second, 150);
		MESSAGE("MOVE2 MELEE " << std::string(p.first) << " vs " << std::string(p.second) << " members=" << r.members << " first_contact=" << r.firstContact << " melee_frames=" << r.meleeFrames
							   << " engaged_share=" << r.engagedShare << " never_engaged=" << r.neverEngaged << " shuffle_steps=" << r.shuffleSteps << " hits=" << r.hits
							   << " dead=" << r.deadAttackers << "/" << r.deadDefenders);
		CHECK(r.firstContact >= 0);
	}
}


// lane MOVE-2 r3 (ANIM-1's note): an archer horde firing at an enemy horde: the frames a member's MOVING model condition flips, and how far the horde object walks
TEST_CASE("move2 probe: archer horde firing (MOVING flips, horde drift)" * doctest::skip())
{
	hudtest::Rig rig(hudtest::shared());
	Player *enemy = rig.game->players().findPlayerWithName("Player_2");
	const ICoord2D *ext = rig.game->ai().pathfinder().getExtent();
	const float cx = (float)(ext->x * 5), cy = (float)(ext->y * 5);
	const char *attacker = std::getenv("PROBE_A") ? std::getenv("PROBE_A") : "GondorArcherHorde";
	Object *a = rig.make(attacker, cx - 160.0f, cy);
	Object *d = rig.make("MordorFighterHorde", cx + 160.0f, cy, enemy);
	rig.game->advance(0.2);
	rig.game->advance(0.2);
	const int moving = AIUpdateInterface::modelConditionBit("MOVING");
	const Coord3D h0 = *a->getPosition();
	if (std::getenv("PROBE_MOVE"))
	{
		a->getAIUpdateInterface()->aiMoveToPosition(Coord3D{ cx - 160.0f, cy + 600.0f, 0.0f }, CMD_FROM_PLAYER);
	}
	else
	{
		REQUIRE(a->getAIUpdateInterface()->aiAttackObject(d, CMD_FROM_PLAYER));
	}
	std::map<ObjectID, int> last, flips;
	for (int f = 0; f < 60; ++f)
	{
		rig.game->advance(0.2);
		std::string line;
		for (const Object *m : *a->getContain()->getContainedItemsList())
		{
			const int mv = m->testModelCondition(moving) ? 1 : 0;
			if (last.count(m->getID()) && last[m->getID()] != mv)
			{
				++flips[m->getID()];
			}
			last[m->getID()] = mv;
			line += mv ? 'M' : '.';
			line += (char)('0' + (int)std::min(9u, m->getAIUpdateInterface()->currentStateId() % 10));
		}
		MESSAGE("f" << f << " horde " << a->getPosition()->x << "," << a->getPosition()->y << " moving " << a->getAIUpdateInterface()->isMoving() << " state " << a->getAIUpdateInterface()->currentStateId() << " " << line);
	}
	int total = 0;
	for (const auto &fl : flips)
	{
		total += fl.second;
	}
	MESSAGE("PROBE flips " << total << " drift " << std::sqrt((a->getPosition()->x - h0.x) * (a->getPosition()->x - h0.x) + (a->getPosition()->y - h0.y) * (a->getPosition()->y - h0.y)));
}

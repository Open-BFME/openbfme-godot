// OpenBFME retail tests for GARRISON-2's TurretAI (the turret of an AI module, RW 0x8DC000 .. 0x8DE000): the Rohirrim carry their bow on a turret (Turret
// TurretTurnRate 360, ControlledWeaponSlots SECONDARY); in bow mode (WEAPONSET_TOGGLE_1) the rider turns to shoot while the horse runs. Every
// turreted AI draws the idle scan's interval at creation (TurretAI.cpp line 0x524). They share the HUD tests' retail world and SKIP when ROTWK_INSTALL /
// BFME2_INSTALL are unset.

#include "doctest.h"
#include "HudTestUtil.h"

#include "Common/Thing/ThingTemplate.h"
#include "Common/StateHash.h"
#include "Common/Thing/RawModuleData.h"
#include "GameLogic/AI/TurretAI.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Object.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace hudtest;

namespace
{
std::vector<Object *> membersOf(Object &o)
{
	std::vector<Object *> out;
	if (ContainModuleInterface *c = o.getContain())
	{
		if (const ContainModuleInterface::ContainedItemsList *items = c->getContainedItemsList())
		{
			out.assign(items->begin(), items->end());
		}
	}
	return out;
}

TurretAI *turretOf(Object &o)
{
	AIUpdateInterface *ai = o.getAIUpdateInterface();
	return ai ? ai->turret() : nullptr;
}

Coord3D centre(Rig &r, float clear)
{
	float mx = 0, my = 0;
	REQUIRE(r.logic().terrain() != nullptr);
	REQUIRE(r.logic().terrain()->getExtent(0, mx, my));
	return r.freeSpot(mx * 0.5f, my * 0.5f, clear);
}

void setBowMode(Object &horde)
{
	const int bit = CombatNames::weaponSetBit("WEAPONSET_TOGGLE_1");
	if (ObjectWeapons *w = horde.getWeapons())
	{
		w->setWeaponSetFlag(bit, true);
	}
	for (Object *m : membersOf(horde))
	{
		if (ObjectWeapons *w = m->getWeapons())
		{
			w->setWeaponSetFlag(bit, true);
		}
	}
}
} // namespace

TEST_CASE("turret retail: every Rohirrim rider has the AI's turret (TurretTurnRate 360 -> 1.2566 rad per frame, SECONDARY), made with one idle scan draw (line 0x524)")
{
	if (!haveWorld("turret retail"))
	{
		return;
	}
	SharedWorld &s = shared();
	Rig r(s);
	const Coord3D c0 = centre(r, 400.0f);
	r.logic().random().clearCallLog();
	r.logic().random().enableCallLog(true);
	Object *horde = r.make("RohanRohirrimHorde", c0.x, c0.y);
	r.frame(2);
	size_t draws = 0;
	for (const auto &c : r.logic().random().callLog())
	{
		draws += (c.file == "TurretAI.cpp" && c.line == 0x524) ? 1u : 0u;
	}
	r.logic().random().enableCallLog(false);
	const std::vector<Object *> members = membersOf(*horde);
	REQUIRE(!members.empty());
	CHECK(turretOf(*horde) == nullptr); // the horde object's AI has no turret
	for (Object *m : members)
	{
		TurretAI *t = turretOf(*m);
		REQUIRE(t != nullptr);
		CHECK(t->data().m_turretWeaponSlots == (1u << 1));
		CHECK(t->data().m_turnRate == doctest::Approx(1.2566371f));
		CHECK(t->data().m_minIdleScanInterval == 9999999u); // RW 0x8DC182's default
		CHECK(t->currentStateId() == (unsigned)TURRETAI_IDLE);
		CHECK(t->getTurretAngle() == 0.0f);
		CHECK_FALSE(t->isOwnersCurWeaponOnTurret()); // the lance (PRIMARY) is not on the turret
	}
	std::printf("  info: Rohirrim: %zu riders with a turret, %zu idle scan draws at creation\n", members.size(), draws);
	CHECK(draws == members.size());
	const GameLogic::Report rep = r.logic().report();
	bool stop = false;
	for (const std::string &line : rep.stops)
	{
		stop = stop || line.rfind("[S-1106]", 0) == 0;
	}
	CHECK(stop);
	// the census: every 2.01 AI module's Turret block parses (a bad one would refuse the object), each controls a weapon slot
	std::map<std::string, int> slots;
	int turreted = 0;
	for (const ThingTemplate *tt : s.world->things().templates())
	{
		for (const ThingTemplate::Nugget &n : tt->behaviorModules().nuggets())
		{
			const RawModuleData *raw = dynamic_cast<const RawModuleData *>(n.data.get());
			if (!raw || n.name.find("AI") == std::string::npos)
			{
				continue;
			}
			std::vector<std::string> lines;
			for (const RawModuleData::Line &l : raw->lines())
			{
				lines.push_back(l.text);
			}
			TurretAIData d;
			std::string error;
			const bool has = TurretAIData::parseFromModuleLines(lines, d, error);
			CHECK_MESSAGE(error.empty(), tt->getName() << ": " << error);
			if (has)
			{
				++turreted;
				++slots[d.m_turretWeaponSlots == 1u ? "PRIMARY" : d.m_turretWeaponSlots == 2u ? "SECONDARY" : "other"];
				CHECK(d.m_turnRate == doctest::Approx(1.2566371f));
			}
		}
	}
	std::printf("  info: 2.01 templates with an AI turret: %d (PRIMARY %d, SECONDARY %d, other %d)\n", turreted, slots["PRIMARY"], slots["SECONDARY"], slots["other"]);
	CHECK(turreted > 0);
	CHECK(slots["other"] == 0);
}

TEST_CASE("turret retail: Rohirrim in bow mode attack warg riders, then ride on: the riders turn their turret to the target and shoot while the horses run, the arrows hurt it")
{
	if (!haveWorld("turret retail"))
	{
		return;
	}
	SharedWorld &s = shared();
	Rig r(s);
	const Coord3D c0 = centre(r, 900.0f);
	// the Rohirrim are the computer player's (a human player's units do not attack what its shroud fogs, S-565)
	Player *enemy = r.game->players().findPlayerWithName("Player_2");
	REQUIRE(enemy != nullptr);
	Object *horde = r.make("RohanRohirrimHorde", c0.x, c0.y, enemy);
	Object *wargs = r.make("IsengardWargRiderHorde", c0.x + 260.0f, c0.y);
	const ObjectID wargsId = wargs->getID();
	r.frame(3);
	setBowMode(*horde);
	r.frame(1);
	const std::vector<Object *> members = membersOf(*horde);
	std::vector<ObjectID> memberIds;
	for (Object *m : members)
	{
		memberIds.push_back(m->getID());
	}
	REQUIRE(!members.empty());
	for (Object *m : members)
	{
		CHECK(turretOf(*m)->isOwnersCurWeaponOnTurret() == (m->getWeapons()->curSlot() == 1));
	}
	float before = 0.0f;
	for (Object *o : membersOf(*wargs))
	{
		before += o->getBodyModule() ? o->getBodyModule()->getHealth() : 0.0f;
	}
	REQUIRE(horde->getAIUpdateInterface()->aiAttackObject(wargs, CMD_FROM_AI));
	r.frame(20);
	// the Rohirrim ride on past the wargs: the riders keep their target and turn their bows to it while the horses run
	horde->getAIUpdateInterface()->aiMoveToPosition(Coord3D{ c0.x + 150.0f, c0.y + 600.0f, 0.0f }, CMD_FROM_AI);
	std::map<ObjectID, Coord3D> last;
	std::map<ObjectID, unsigned long long> lastShots;
	for (Object *m : members)
	{
		last[m->getID()] = *m->getPosition();
		lastShots[m->getID()] = 0;
	}
	unsigned long long shots = 0, shotsWhileMoving = 0, turnFrames = 0;
	float maxOffAxis = 0.0f;
	for (int f = 0; f < 250; ++f)
	{
		r.frame(1);
		for (ObjectID id : memberIds) // a member may be gone: only its id is read
		{
			Object *m = r.logic().findObjectByID(id);
			if (!m || m->isEffectivelyDead())
			{
				continue;
			}
			TurretAI *t = turretOf(*m);
			const unsigned long long n = t->stats().shots;
			const Coord3D p = *m->getPosition();
			const Coord3D q = last[m->getID()];
			const float moved = std::sqrt((p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y));
			if (n > lastShots[m->getID()])
			{
				shots += n - lastShots[m->getID()];
				shotsWhileMoving += moved > 1.0f ? n - lastShots[m->getID()] : 0u;
				maxOffAxis = std::fabs(t->getTurretAngle()) > maxOffAxis ? std::fabs(t->getTurretAngle()) : maxOffAxis;
			}
			lastShots[m->getID()] = n;
			last[m->getID()] = p;
		}
	}
	for (ObjectID id : memberIds)
	{
		if (Object *m = r.logic().findObjectByID(id))
		{
			turnFrames += turretOf(*m)->stats().turnFrames;
		}
	}
	wargs = r.logic().findObjectByID(wargsId);
	float after = 0.0f;
	if (wargs && !wargs->isDestroyed())
	{
		for (Object *o : membersOf(*wargs))
		{
			after += (o->getBodyModule() && !o->isEffectivelyDead()) ? o->getBodyModule()->getHealth() : 0.0f;
		}
	}
	std::printf("  info: Rohirrim bow mode: %llu turret shots, %llu while the horse moved, %llu turning frames, the largest turret angle at a shot %.2f rad; wargs' "
	            "health %.0f -> %.0f\n",
	            shots, shotsWhileMoving, turnFrames, maxOffAxis, before, after);
	CHECK(shots > 0u);
	CHECK(shotsWhileMoving > 0u);
	// merge ANIM-1 / BUILD-4 / WIN-1 / GARRISON-3 on MOD-4 + AUDIO-4: the multi-frame turn counter is not pinned: at TurretTurnRate 360 degrees a second (72 a frame)
	// the targets can lie within one frame's turn, so it may stay 0 (lane MOVE-2 r2 reached the same conclusion and checks the turret's angle at its shots instead)
	CHECK(after < before);
}

TEST_CASE("turret retail: the turret is in the AI's state hash (its angle, its target, its machine)")
{
	if (!haveWorld("turret retail"))
	{
		return;
	}
	SharedWorld &s = shared();
	Rig r(s);
	const Coord3D c0 = centre(r, 400.0f);
	Object *horde = r.make("RohanRohirrimHorde", c0.x, c0.y);
	r.frame(2);
	Object *m = membersOf(*horde).front();
	auto hashOf = [&]() {
		StateHasher h;
		m->getAIUpdateInterface()->crc(h);
		return h.value();
	};
	const std::uint32_t h0 = hashOf();
	TurretAI *t = turretOf(*m);
	CHECK(t->turnTowardsAngle(0.5f, 1.0f, 0.0f)); // 0.5 rad is within one frame's 1.2566
	CHECK(t->getTurretAngle() == 0.5f);
	const std::uint32_t h1 = hashOf();
	CHECK(h1 != h0);
	t->recenterTurret();
	CHECK(t->currentStateId() == (unsigned)TURRETAI_RECENTER);
	CHECK(hashOf() != h1);
}

// OpenBFME unit tests: the teleport forms of Object (lane BACKLOG4, stop S-813).
//
// TARGET: RW 0x696E63 Object::teleportTo(pos, keepAI) and RW 0x68DA67 (the transform form): setPosition, recordTransform twice, the drawable's forced
// gather RW 0x674B1F(1), setPosition, updateShroudNow, the horde's members (interface slot 0x1B8 RW 0x86D881 / 0x1B4 RW 0x86D8FE), and for keepAI == 0
// the AI's idle (RW 0x5E821A: source 2 with status 0x5A IS_LEAVING_FACTORY, else 0) and path reset (RW 0x66276B). DONOR: the BFME2 decomp's
// Object::teleportTo (Object_teleportTo.cpp), Object::rva0028D412, Rva002747F9::rva002747F9 and HordeContain::rva0046A712 / rva0046A78F.
//
// What the player sees: an object moved by a teleport is drawn AT its new place for the whole logic frame, never sliding across the jump. Each case
// has a control with the plain setPosition, which is drawn across the jump.

#include "doctest.h"

#include "MoveTestUtil.h"
#include "ProdTestUtil.h"

#include "GameClient/LogicSnapshot.h"
#include "GameClient/RenderInterpolation.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/ObjectTemplateInfo.h"

using namespace movetest;

namespace
{
bool same(const Coord3D &a, const Coord3D &b)
{
	return a.x == b.x && a.y == b.y && a.z == b.z;
}

const ObjectSnapshot &recordOf(const std::shared_ptr<const LogicSnapshot> &snap, const Object *o)
{
	const ObjectSnapshot *s = snap->find(o->getID());
	REQUIRE(s != nullptr);
	return *s;
}

// a walker that has moved for some frames, so its previous, recorded and current positions differ before the jump
struct Moving
{
	MoveWorld mw;
	Object *walker = nullptr;
	Moving()
	{
		mw.buildMap();
		walker = mw.spawn("Walker", 105.0f, 105.0f);
		mw.frames(2);
		mw.aiOf(walker)->aiMoveToPosition(Coord3D{ 505.0f, 105.0f, 0.0f }, CMD_FROM_PLAYER);
		mw.frames(6);
	}
};
} // namespace

TEST_CASE("backlog4 teleport: a teleported object is drawn at its new position for the whole frame; one moved by setPosition is drawn across the jump (S-813)")
{
	const Coord3D dest{ 705.0f, 605.0f, 0.0f };
	// control: the plain setPosition (RW 0x70C201)
	{
		Moving c;
		const Coord3D before = c.walker->getRecordedPosition(); // the frame's recorded transform (logic phase 2, RW 0x6260E1)
		REQUIRE(!same(before, dest));
		REQUIRE(c.mw.aiOf(c.walker)->isMoving());
		c.walker->setPosition(&dest);
		const std::shared_ptr<const LogicSnapshot> snap = LogicSnapshot::build(*c.mw.logic, nullptr, false, 0);
		const ObjectSnapshot &r = recordOf(snap, c.walker);
		CHECK(!r.teleported);
		CHECK(same(r.position, dest));
		CHECK(same(r.recordedPos, before)); // the frame's recorded transform is the place it left
		const RenderInterpolation::Pose start = RenderInterpolation::retailPose(r, snap->frame, 0.0);
		CHECK(same(start.position, before)); // drawn at the old place at the start of the frame: the slide across the jump
	}
	// the teleport form
	Moving t;
	const Coord3D before = *t.walker->getPosition();
	t.walker->teleportTo(&dest, true);
	CHECK(same(*t.walker->getPosition(), dest));
	CHECK(same(t.walker->getRecordedPosition(), dest)); // RW 0x6260E1 twice: the recorded and the previous pose are the new one
	CHECK(same(t.walker->getPreviousPosition(), dest));
	CHECK(t.walker->getRecordedFrame() == t.mw.logic->getFrame());
	const std::shared_ptr<const LogicSnapshot> snap = LogicSnapshot::build(*t.mw.logic, nullptr, false, 0);
	const ObjectSnapshot &r = recordOf(snap, t.walker);
	CHECK(r.teleported); // RW 0x674B1F(1) ran in this frame
	CHECK(!r.hasNext);   // RW 0x62618F: the pending position is gone with the record
	for (const double alpha : { 0.0, 0.25, 0.5, 0.75, 1.0 })
	{
		CAPTURE(alpha);
		CHECK(same(RenderInterpolation::retailPose(r, snap->frame, alpha).position, dest));
		CHECK(same(RenderInterpolation::smoothPose(r, snap->frame, alpha).position, dest));
	}
	CHECK(!same(before, dest));
	// the next frame's record is not a teleported one
	t.mw.frames(1);
	const std::shared_ptr<const LogicSnapshot> next = LogicSnapshot::build(*t.mw.logic, snap.get(), false, 0);
	CHECK(!recordOf(next, t.walker).teleported);
}

TEST_CASE("backlog4 teleport: keepAI == 0 idles the AI and drops its path (RW 0x5E821A, 0x66276B); keepAI == 1 leaves the move running")
{
	const Coord3D dest{ 305.0f, 405.0f, 0.0f };
	{
		Moving keep;
		AIUpdateInterface *ai = keep.mw.aiOf(keep.walker);
		REQUIRE(ai->isMoving());
		REQUIRE(!ai->isIdle());
		keep.walker->teleportTo(&dest, true);
		CHECK(!ai->isIdle()); // RW 0x696ED6: the flag skips the AI part
		keep.mw.frames(40);
		CHECK(!same(*keep.walker->getPosition(), dest)); // it kept walking to its goal from the new place
	}
	Moving drop;
	AIUpdateInterface *ai = drop.mw.aiOf(drop.walker);
	REQUIRE(ai->mover().path() != nullptr);
	drop.walker->teleportTo(&dest, false);
	CHECK(ai->isIdle());
	CHECK(ai->mover().path() == nullptr);
	CHECK(ai->lastCommandSource() == CMD_FROM_PLAYER); // RW 0x696EF7: source 0 without the status
	drop.mw.frames(40);
	CHECK(same(*drop.walker->getPosition(), dest)); // it stays where it was put
	CHECK(!ai->isMoving());

	// RW 0x696EE3 .. 0x696EF3: status 0x5A (IS_LEAVING_FACTORY) makes the idle an AI-sourced one (2)
	Moving leaving;
	const int kLeaving = ObjectTemplateInfoBuilder::objectStatusIndex("IS_LEAVING_FACTORY");
	REQUIRE(kLeaving == 0x5A);
	leaving.walker->setStatus((unsigned)kLeaving, true);
	leaving.walker->teleportTo(&dest, false);
	CHECK(leaving.mw.aiOf(leaving.walker)->isIdle());
	CHECK(leaving.mw.aiOf(leaving.walker)->lastCommandSource() == CMD_FROM_AI);
}

TEST_CASE("backlog4 teleport: a teleported horde re-places its members with the teleport form (slot 0x1B8 RW 0x86D881): none is drawn across the jump")
{
	MoveWorld mw;
	mw.buildMap();
	Object *horde = mw.spawn("Horde", 405.0f, 405.0f);
	mw.frames(4);
	HordeContain *hc = dynamic_cast<HordeContain *>(horde->getContain());
	REQUIRE(hc != nullptr);
	const std::vector<Object *> members(hc->getContainedItemsList()->begin(), hc->getContainedItemsList()->end());
	REQUIRE(members.size() == 15);
	std::vector<Coord3D> old;
	for (const Object *m : members)
	{
		old.push_back(*m->getPosition());
	}
	const Coord3D dest{ 805.0f, 705.0f, 0.0f };
	horde->teleportTo(&dest, false);
	const std::shared_ptr<const LogicSnapshot> snap = LogicSnapshot::build(*mw.logic, nullptr, false, 0);
	CHECK(recordOf(snap, horde).teleported);
	for (size_t i = 0; i < members.size(); ++i)
	{
		CAPTURE(i);
		const Object *m = members[i];
		const Coord3D slot = hc->getMemberFormationPosition(m); // slot 7 (RW 0x877D89) for the horde's new transform
		CHECK(same(*m->getPosition(), slot));
		CHECK(!same(*m->getPosition(), old[i]));
		const ObjectSnapshot &r = recordOf(snap, m);
		CHECK(r.teleported);
		CHECK(same(RenderInterpolation::retailPose(r, snap->frame, 0.0).position, slot));
		CHECK(same(RenderInterpolation::smoothPose(r, snap->frame, 0.5).position, slot));
		CHECK(m->getAIUpdateInterface()->isIdle()); // the member form is (pos, 0)
	}
}

TEST_CASE("backlog4 teleport: the transform form (RW 0x68DA67) records the new transform twice and gathers it")
{
	Moving t;
	const Coord3D dest{ 605.0f, 305.0f, 12.0f };
	const float basis[9] = { 0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f }; // a quarter turn about Z
	t.walker->teleportTransform(&dest, basis);
	const std::shared_ptr<const LogicSnapshot> snap = LogicSnapshot::build(*t.mw.logic, nullptr, false, 0);
	const ObjectSnapshot &r = recordOf(snap, t.walker);
	CHECK(r.teleported);
	CHECK(same(r.recordedPos, dest));
	CHECK(same(r.previousPos, dest));
	for (int i = 0; i < 9; ++i)
	{
		CAPTURE(i);
		CHECK(r.recordedBasis[i] == basis[i]);
		CHECK(r.basis[i] == basis[i]);
	}
	CHECK(same(RenderInterpolation::retailPose(r, snap->frame, 0.5).position, dest));
	CHECK(!t.mw.aiOf(t.walker)->isIdle()); // no AI part in this form
}

// ---- review round (Sol r1): the production exits (RW 0x8A3F7A queue exit, RW 0x8A28C7 / 0x8A287A ProductionUpdate) -------------------------------------

namespace
{
const char kB4Barracks[] =
	"Object B4Soldier\n"
	"  BuildCost = 100\n"
	"  BuildTime = 2.0\n"
	"  KindOf = SELECTABLE INFANTRY\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"End\n"
	"Object B4Barracks\n"
	"  KindOf = STRUCTURE SELECTABLE\n"
	"  CommandSet = B4BarracksSet\n"
	"  Behavior = ProductionUpdate ModuleTag_PU\n"
	"    MaxQueueEntries = 3\n"
	"  End\n"
	"  Behavior = QueueProductionExitUpdate ModuleTag_Q\n"
	"    UnitCreatePoint = X:0.0 Y:-20.0 Z:0.0\n"
	"    NaturalRallyPoint = X:30.0 Y:-50.0 Z:0.0\n"
	"    ExitDelay = 400\n"
	"  End\n"
	"End\n"
	"CommandButton Command_B4BuildSoldier\n  Command = UNIT_BUILD\n  Object = B4Soldier\n  Options = CANCELABLE\nEnd\n"
	"CommandSet B4BarracksSet\n  1 = Command_B4BuildSoldier\nEnd\n";
} // namespace

TEST_CASE("backlog4 teleport r1: a produced unit is put at the exit with the teleport form: on its first frame its recorded, previous and current poses are the exit's (RW 0x8A3F7A, 0x8A28C7)")
{
	prodtest::ProdWorld f;
	f.load(kB4Barracks);
	Object *barracks = f.make("B4Barracks", f.teamOf("Alice"));
	REQUIRE(barracks);
	const Coord3D at{ 1000.0f, 2000.0f, 0.0f };
	barracks->setPosition(&at);
	ProductionUpdateInterface *pu = barracks->getProductionUpdate();
	REQUIRE(pu);
	REQUIRE(pu->queueCreateUnit(f.w.get("B4Soldier"), -1, pu->requestUniqueUnitID(), -1, false, std::string(), false));
	Object *made = nullptr;
	for (int i = 0; i < 60 && !made; ++i)
	{
		f.frames(1);
		for (Object *o = f.logic->getFirstObject(); o; o = o->getNextObject())
		{
			made = o->getTemplate()->getName() == "B4Soldier" ? o : made;
		}
	}
	REQUIRE(made);
	const UnsignedInt frame = f.logic->getFrame();
	CHECK(made->wasTeleportGatheredAt(frame));
	// it was made at the create point (0, -20) in the barracks' frame; the teleport form recorded that pose twice
	const Coord3D exit{ 1000.0f, 1980.0f, 0.0f };
	CHECK(same(made->getRecordedPosition(), exit));
	CHECK(same(made->getPreviousPosition(), exit));
	const std::shared_ptr<const LogicSnapshot> snap = LogicSnapshot::build(*f.logic, nullptr, false, 0);
	const ObjectSnapshot &r = recordOf(snap, made);
	CHECK(r.teleported);
	CHECK(same(RenderInterpolation::retailPose(r, snap->frame, 0.0).position, exit)); // drawn at the exit from the frame's start, not from the origin
	f.frames(1);
	CHECK_FALSE(made->wasTeleportGatheredAt(f.logic->getFrame()));
}

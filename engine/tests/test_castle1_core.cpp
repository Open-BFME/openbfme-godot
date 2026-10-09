// OpenBFME unit tests (lane CASTLE-1): the castle after its unpack, on a synthetic base (no retail files). The rules are RotWK's (caveat S-001, static disassembly; see
// GameLogic/Module/CastleModules.h): registerOwnedObject RW 0x79AC19 sorts the members, RW 0x799ACB decides when the keep is gone, KeepDeathKillsEverything kills and destroys
// everything (RW 0x7999E2 with RW 0x797F16 / 0x797F49), an abandoned castle fades for FadeTime (RW 0x79CB47) and is packed (RW 0x79CCF2), a packed castle is captured by the
// units near it (RW 0x79B3C4).  GPL-3.0.

#include "doctest.h"

#include "ProdTestUtil.h"

#include "Common/StateHash.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Construction.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/ObjectTemplateInfo.h"

#include <vector>

using namespace prodtest;

namespace
{
const char kCastleObjects[] =
	"Object CKeep\n"
	"  KindOf = STRUCTURE SELECTABLE IMMOBILE CASTLE_KEEP\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"    MaxHealthDamaged = 50\n"
	"    MaxHealthReallyDamaged = 25\n"
	"  End\n"
	"  Behavior = CastleMemberBehavior ModuleTag_CM\n"
	"  End\n"
	"End\n"
	"Object CPlot\n"
	"  KindOf = STRUCTURE SELECTABLE IMMOBILE BASE_FOUNDATION\n"
	"  Behavior = FoundationAIUpdate ModuleTag_FA\n"
	"  End\n"
	"  Behavior = CastleMemberBehavior ModuleTag_CM\n"
	"  End\n"
	"End\n"
	"Object CWall\n"
	"  KindOf = STRUCTURE IMMOBILE\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"    MaxHealthDamaged = 50\n"
	"    MaxHealthReallyDamaged = 25\n"
	"  End\n"
	"  Behavior = CastleMemberBehavior ModuleTag_CM\n"
	"  End\n"
	"End\n"
	"Object CHall\n"
	"  KindOf = STRUCTURE SELECTABLE NEED_BASE_FOUNDATION\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"    MaxHealthDamaged = 50\n"
	"    MaxHealthReallyDamaged = 25\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"End\n"
	"Object CSoldier\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  CommandPoints = 3\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 50\n"
	"    MaxHealthDamaged = 25\n"
	"    MaxHealthReallyDamaged = 12\n"
	"  End\n"
	"End\n"
	"Object CFortress\n"
	"  KindOf = STRUCTURE IMMOBILE BASE_FOUNDATION CASTLE_CENTER\n"
	"  Behavior = CastleBehavior ModuleTag_castle\n"
	"    CastleToUnpackForFaction = Alpha Base_K\n"
	"    CastleToUnpackForFaction = Beta Base_K\n"
	"    FilterValidOwnedEntries = ANY +STRUCTURE +BASE_FOUNDATION\n"
	"    InstantUnpack = Yes\n"
	"    KeepDeathKillsEverything = Yes\n"
	"  End\n"
	"End\n"
	"Object COutpost\n"
	"  KindOf = STRUCTURE IMMOBILE BASE_FOUNDATION CASTLE_CENTER\n"
	"  Behavior = CastleBehavior ModuleTag_castle\n"
	"    CastleToUnpackForFaction = Alpha Base_K\n"
	"    FilterValidOwnedEntries = ANY +STRUCTURE +BASE_FOUNDATION\n"
	"    InstantUnpack = Yes\n"
	"    FadeTime = 1.0\n"
	"  End\n"
	"End\n"
	"Object CPrebuiltCamp\n"
	"  KindOf = IMMOBILE BASE_FOUNDATION CASTLE_CENTER\n"
	"  Behavior = CastleBehavior ModuleTag_castle\n"
	"    CastleToUnpackForFaction = Beta Base_P\n"
	"    FilterValidOwnedEntries = ANY +STRUCTURE +BASE_FOUNDATION\n"
	"    PreBuiltList = CHall 7\n"
	"    PreBuiltList = CHall -2\n"
	"    PreBuiltList = CHall 0\n"
	"    PreBuiltList = NoSuchHall 1\n"
	"    PreBuiltPlyr = Bob\n"
	"    InstantUnpack = Yes\n"
	"  End\n"
	"End\n"
	"Object CNobodysCamp\n"
	"  KindOf = IMMOBILE BASE_FOUNDATION CASTLE_CENTER\n"
	"  Behavior = CastleBehavior ModuleTag_castle\n"
	"    CastleToUnpackForFaction = Beta Base_P\n"
	"    PreBuiltList = CHall 0\n"
	"    PreBuiltPlyr = Nobody\n"
	"    InstantUnpack = Yes\n"
	"  End\n"
	"End\n"
	"Object CCampFlag\n"
	"  KindOf = IMMOBILE CASTLE_CENTER\n"
	"  Behavior = CastleBehavior ModuleTag_castle\n"
	"    CastleToUnpackForFaction = Beta Base_K\n"
	"    ScanDistance = 60.0\n"
	"  End\n"
	"End\n";

struct CastleFx : ProdWorld
{
	CastleFx()
	{
		load(kCastleObjects);
		logic->settings().buildRulesLoaded = true;
		CastleTemplate layout;
		layout.name = "Base_K";
		layout.version = 5;
		layout.entries.push_back({ "", "CKeep", 0.0f, 0.0f, 0.0f, 0.0f, 40, 1 });
		layout.entries.push_back({ "", "CPlot", 60.0f, 0.0f, 0.0f, 0.0f, 40, 1 });
		layout.entries.push_back({ "", "CWall", 0.0f, 60.0f, 0.0f, 0.0f, 40, 1 });
		logic->castleTemplates().add(layout);
		CastleTemplate three;
		three.name = "Base_P";
		three.version = 5;
		three.entries.push_back({ "", "CKeep", 0.0f, 0.0f, 0.0f, 0.0f, 40, 1 });
		three.entries.push_back({ "", "CPlot", 60.0f, 0.0f, 0.0f, 0.0f, 40, 1 });
		three.entries.push_back({ "", "CPlot", -60.0f, 0.0f, 0.0f, 0.0f, 40, 1 });
		three.entries.push_back({ "", "CPlot", 0.0f, -60.0f, 0.0f, 0.0f, 40, 1 });
		logic->castleTemplates().add(three);
	}
	Object *castle(const char *name, const char *player, float x, float y)
	{
		Object *c = make(name, teamOf(player));
		REQUIRE(c != nullptr);
		Coord3D at{ x, y, 0.0f };
		c->setPosition(&at);
		c->friend_onBuildComplete();
		return c;
	}
	Object *soldier(const char *player, float x, float y)
	{
		Object *s = make("CSoldier", teamOf(player));
		REQUIRE(s != nullptr);
		Coord3D at{ x, y, 0.0f };
		s->setPosition(&at);
		return s;
	}
	static CastleBehavior *cb(Object *o) { return dynamic_cast<CastleBehavior *>(o->findModule("CastleBehavior")); }
	Object *byId(ObjectID id) { return logic->findObjectByID(id); }
	static void kill(Object *o)
	{
		DamageInfo d;
		d.m_input.m_amount = 1.0e9f;
		d.m_input.m_damageType = DAMAGE_UNRESISTABLE;
		o->attemptDamage(d);
	}
	// a hall built at once on the castle's plot (RW 0x858701 instant)
	Object *hallOn(CastleBehavior *c)
	{
		REQUIRE(c->foundations().size() == 1);
		Object *plot = byId(c->foundations()[0]);
		REQUIRE(plot != nullptr);
		return Construction::constructOnPlot(*plot, *w.get("CHall"), *plot->getPosition(), 0.0f, *alice(), true);
	}
};

// an independent model of RW 0x79CF57: the updates a timer of `seconds` takes to expire (binary32 steps of 1 / 5; the test target has no FMA contraction)
int updatesToExpire(float seconds)
{
	volatile float t = seconds;
	const volatile float step = 1.0f / 5.0f;
	int n = 0;
	while (n < 1000)
	{
		++n;
		if (t > 0.0f)
		{
			t = t - step;
			if (0.0f > t)
			{
				return n;
			}
		}
	}
	return -1;
}
} // namespace

TEST_CASE("castle1: the unpack sorts the members like RW 0x79AC19: the keep, the foundations, the other members")
{
	CastleFx f;
	Object *c = f.castle("CFortress", "Alice", 1000.0f, 1000.0f);
	c->setName("BASE_FLAG_1");
	c->setBuildCostPaid(812.75f);
	f.frames(1);
	CastleBehavior *b = f.cb(c);
	REQUIRE(b->state() == CastleBehavior::STATE_UNPACKED);
	REQUIRE(b->ownedObjects().size() == 3);
	// RW 0x79C03A / 0x79C0FD: UNPACKING instead of PACKING; the keep takes the script name and the truncated price, the castle is "No Name"
	CHECK(c->testModelCondition(CombatNames::modelCondition("UNPACKING")));
	CHECK_FALSE(c->testModelCondition(CombatNames::modelCondition("PACKING")));
	CHECK(f.byId(b->keepId())->getName() == "BASE_FLAG_1");
	CHECK(f.byId(b->keepId())->getBuildCostPaid() == 812.0f);
	CHECK(c->getName() == "No Name");
	CHECK(b->keepId() == b->ownedObjects()[0]);
	REQUIRE(b->foundations().size() == 1);
	CHECK(b->foundations()[0] == b->ownedObjects()[1]);
	REQUIRE(b->members().size() == 1);
	CHECK(b->members()[0] == b->ownedObjects()[2]);
	CHECK(b->crew().empty());
	CHECK(b->defenseFoundations().empty());
	// the keep's member records itself as the occupant (RW 0x79ACED)
	CastleMemberBehavior *keepMember = dynamic_cast<CastleMemberBehavior *>(f.byId(b->keepId())->findModule("CastleMemberBehavior"));
	REQUIRE(keepMember);
	CHECK(keepMember->occupantId() == b->keepId());
	CHECK(keepMember->castleId() == c->getID());
}

TEST_CASE("castle1: KeepDeathKillsEverything: the keep's death kills the members and the plots' buildings, destroys the plots and the castle (RW 0x799B42)")
{
	CastleFx f;
	Object *c = f.castle("CFortress", "Alice", 1000.0f, 1000.0f);
	f.frames(1);
	CastleBehavior *b = f.cb(c);
	Object *hall = f.hallOn(b);
	REQUIRE(hall != nullptr);
	const ObjectID castleId = c->getID(), keepId = b->keepId(), plotId = b->foundations()[0], wallId = b->members()[0], hallId = hall->getID();
	f.frames(40); // past the base check delay (ftol(5 * 5.0) = 25 frames): nothing happens while the keep stands
	CHECK(b->state() == CastleBehavior::STATE_UNPACKED);
	CHECK_FALSE(b->keepGone());
	CastleFx::kill(f.byId(keepId));
	REQUIRE(f.byId(keepId)->isEffectivelyDead());
	f.frames(1);
	// everything is gone: the castle and the plot are destroyed (removed from the lookup at the end of the frame), the hall and the wall are dead
	CHECK(f.byId(castleId) == nullptr);
	CHECK(f.byId(plotId) == nullptr);
	const Object *wall = f.byId(wallId);
	CHECK((wall == nullptr || wall->isEffectivelyDead()));
	const Object *h = f.byId(hallId);
	CHECK((h == nullptr || h->isEffectivelyDead()));
}

TEST_CASE("castle1: before the base check delay the castle does not look at its keep (RW 0x799ACC)")
{
	CastleFx f;
	Object *c = f.castle("CFortress", "Alice", 1000.0f, 1000.0f);
	f.frames(1);
	CastleBehavior *b = f.cb(c);
	const ObjectID castleId = c->getID();
	CastleFx::kill(f.byId(b->keepId()));
	f.frames(10); // frame 11 < 25
	REQUIRE(f.byId(castleId) != nullptr);
	CHECK_FALSE(f.byId(castleId)->isDestroyed());
	f.frames(20);
	CHECK(f.byId(castleId) == nullptr);
}

TEST_CASE("castle1: without KeepDeathKillsEverything a castle whose keep is dead stands while a plot holds a building, then is abandoned, fades for FadeTime and is packed")
{
	CastleFx f;
	const CombatNames::Status &st = CombatNames::statuses();
	Object *c = f.castle("COutpost", "Alice", 1000.0f, 1000.0f);
	f.frames(1);
	CastleBehavior *b = f.cb(c);
	Object *hall = f.hallOn(b);
	REQUIRE(hall != nullptr);
	const ObjectID keepId = b->keepId(), plotId = b->foundations()[0], wallId = b->members()[0];
	f.frames(30);
	CastleFx::kill(f.byId(keepId));
	f.frames(5);
	CHECK(b->keepGone());
	CHECK(b->state() == CastleBehavior::STATE_UNPACKED); // the hall on the plot keeps the castle
	CastleFx::kill(hall); // DestroyDie: the hall leaves the game at the end of the frame, then the plot holds nothing
	for (int i = 0; i < 3 && b->state() != CastleBehavior::STATE_ABANDONED; ++i)
	{
		f.frames(1);
	}
	REQUIRE(b->state() == CastleBehavior::STATE_ABANDONED);
	// RW 0x79CC12 ..: PACKING instead of UNPACKING, no JUST_BUILT, selectable again; the wall cannot attack (RW 0x79A017)
	CHECK(c->testModelCondition(CombatNames::modelCondition("PACKING")));
	CHECK_FALSE(c->testModelCondition(CombatNames::modelCondition("JUST_BUILT")));
	CHECK_FALSE(c->testStatus((unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("UNSELECTABLE")));
	CHECK(f.byId(wallId)->testStatus((unsigned)st.noAttack));
	CHECK(b->timer() == 1.0f);
	// the castle's own crc covers the timer: it moves every frame of the fade
	StateHasher h0;
	b->crc(h0);
	const int fade = updatesToExpire(1.0f);
	CHECK(fade == 6); // 1.0 - 5 * 0.2 is 1.49e-8 in binary32, still above 0
	f.frames(1);
	StateHasher h1;
	b->crc(h1);
	CHECK(h0.value() != h1.value());
	f.frames(fade - 2);
	CHECK(b->state() == CastleBehavior::STATE_ABANDONED);
	f.frames(1);
	// packed: every member destroyed, the lists empty, back in state 0 with the UnpackDelayTime (data default 2.0, RW 0x79C543)
	CHECK(b->state() == CastleBehavior::STATE_IDLE);
	CHECK(b->keepId() == INVALID_ID);
	CHECK(b->foundations().empty());
	CHECK(b->members().empty());
	CHECK(b->timer() == 2.0f);
	f.frames(1);
	CHECK(f.byId(plotId) == nullptr);
	CHECK(f.byId(wallId) == nullptr);
	CHECK(f.byId(keepId) == nullptr);
	CHECK(f.byId(c->getID()) == c); // the castle itself stays (a camp to take again)
}

TEST_CASE("castle1: a packed camp is taken by the units near it unless two enemies contest it, and only by a side it can unpack for (RW 0x79B3C4 / 0x7998FF)")
{
	CastleFx f;
	Object *camp = f.castle("CCampFlag", "Alice", 1000.0f, 1000.0f);
	CastleBehavior *b = f.cb(camp);
	f.frames(6);
	CHECK(b->state() == CastleBehavior::STATE_IDLE);
	CHECK(camp->getControllingPlayer() == f.alice());
	// Bob's soldier near the flag: Beta has a base in the list, Bob takes it at the next scan (every 5 frames)
	Object *bob = f.soldier("Bob", 1030.0f, 1000.0f);
	f.frames(5);
	CHECK(camp->getControllingPlayer() == f.players.findPlayerWithName("Bob"));
	// an Alice soldier joins: the two are enemies, the scan is contested and nothing changes (no PlyrCivilian in this world to fall back to)
	Object *alice = f.soldier("Alice", 970.0f, 1000.0f);
	f.frames(5);
	CHECK(camp->getControllingPlayer() == f.players.findPlayerWithName("Bob"));
	// Bob's soldier dies: Alice alone, but Alpha has no base in this camp's list: Bob keeps it
	CastleFx::kill(bob);
	f.frames(5);
	CHECK(camp->getControllingPlayer() == f.players.findPlayerWithName("Bob"));
	// a soldier beyond ScanDistance does not count
	CastleFx::kill(alice);
	f.soldier("Alice", 1100.0f, 1000.0f);
	f.frames(5);
	CHECK(camp->getControllingPlayer() == f.players.findPlayerWithName("Bob"));
}

TEST_CASE("castle1: two identical worlds give the same hash every frame through the keep's death, and the death changes the hash")
{
	auto run = [](bool killKeep, std::vector<std::uint32_t> &out) {
		CastleFx f;
		Object *c = f.castle("COutpost", "Alice", 1000.0f, 1000.0f);
		f.frames(1);
		CastleBehavior *b = f.cb(c);
		const ObjectID keepId = b->keepId();
		for (int i = 0; i < 60; ++i)
		{
			if (killKeep && i == 30)
			{
				CastleFx::kill(f.byId(keepId));
			}
			f.frames(1);
			out.push_back(f.logic->computeStateHash());
		}
	};
	std::vector<std::uint32_t> a, b, c;
	run(true, a);
	run(true, b);
	run(false, c);
	CHECK(a == b);
	CHECK(a[29] == c[29]);
	CHECK(a[30] != c[30]);
}

TEST_CASE("castle1: stops S-950 .. S-952 are in the live report, and the unported crew notes S-951 at runtime")
{
	CastleFx f;
	const GameLogic::Report rep = f.logic->report();
	for (int id = 950; id <= 952; ++id)
	{
		const std::string tag = "[S-" + std::to_string(id) + "]";
		bool found = false;
		for (const std::string &line : rep.stops)
		{
			found = found || line.rfind(tag, 0) == 0;
		}
		INFO(tag);
		CHECK(found);
	}
	// a castle whose FilterCrew takes the wall: the wall joins the crew list and the unported preparation is noted
	f.load("Object CCrewFortress\n  KindOf = IMMOBILE BASE_FOUNDATION CASTLE_CENTER\n  Behavior = CastleBehavior ModuleTag_castle\n    CastleToUnpackForFaction = Alpha Base_K\n"
		   "    FilterValidOwnedEntries = ANY +STRUCTURE +BASE_FOUNDATION\n    FilterCrew = ANY +STRUCTURE\n    InstantUnpack = Yes\n  End\nEnd\n");
	Object *c = f.castle("CCrewFortress", "Alice", 1000.0f, 1000.0f);
	f.frames(1);
	CastleBehavior *b = f.cb(c);
	CHECK(b->crew().size() == 1);
	CHECK(b->members().empty());
	bool noted = false;
	for (const std::string &line : f.logic->report().stops)
	{
		noted = noted || (line.rfind("[S-951]", 0) == 0 && line.find("0x798397") != std::string::npos && line.find("crew member") != std::string::npos);
	}
	CHECK(noted);
}

TEST_CASE("castle1: PreBuiltList builds one instant structure per entry on the pad of that index (clamped; -2: the first free pad) for PreBuiltPlyr (RW 0x79C265 / 0x79A5EC / 0x79A518)")
{
	CastleFx f;
	Player *bob = f.players.findPlayerWithName("Bob");
	Object *camp = f.castle("CPrebuiltCamp", "Alice", 1000.0f, 1000.0f); // placed for Alice: PreBuiltPlyr gives it to Bob
	f.frames(1);
	CastleBehavior *b = f.cb(camp);
	REQUIRE(b->state() == CastleBehavior::STATE_UNPACKED);
	CHECK(camp->getControllingPlayer() == bob);
	REQUIRE(b->foundations().size() == 3);
	// entry 7 is clamped to the last pad (2), -2 takes the first free pad (0), 0 finds pad 0 taken (instant builds skip only a pad whose foundation holds something), the unknown
	// template makes nothing
	std::vector<ObjectID> occupants;
	for (ObjectID pad : b->foundations())
	{
		const CastleMemberBehavior *m = dynamic_cast<const CastleMemberBehavior *>(f.byId(pad)->findModule("CastleMemberBehavior"));
		REQUIRE(m);
		occupants.push_back(m->occupantId());
	}
	REQUIRE(occupants[0] != INVALID_ID);
	CHECK(occupants[1] == INVALID_ID);
	REQUIRE(occupants[2] != INVALID_ID);
	for (ObjectID id : { occupants[0], occupants[2] })
	{
		const Object *hall = f.byId(id);
		REQUIRE(hall);
		CHECK(hall->getTemplate()->getName() == "CHall");
		CHECK(hall->getControllingPlayer() == bob);
		CHECK_FALSE(hall->isUnderConstruction());
	}
	int halls = 0;
	for (Object *o = f.logic->getFirstObject(); o; o = o->getNextObject())
	{
		halls += o->getTemplate()->getName() == "CHall" ? 1 : 0;
	}
	CHECK(halls == 2);
	CHECK(f.logic->report().errors.empty());
	// PreBuiltPlyr names no player: nothing is unpacked, the flag clears and the castle is in state 4 all the same (RW 0x79C2B8 .. 0x79C383)
	Object *nobody = f.castle("CNobodysCamp", "Alice", 3000.0f, 1000.0f);
	f.frames(1);
	CastleBehavior *nb = f.cb(nobody);
	CHECK(nb->state() == CastleBehavior::STATE_UNPACKED);
	CHECK(nb->ownedObjects().empty());
	CHECK(nobody->getControllingPlayer() == f.alice());
}

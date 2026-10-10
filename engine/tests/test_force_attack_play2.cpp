// OpenBFME. PLAY-2 tests: the force-attack commands of the logic (RW GameLogicDispatch 0x779A3D: MSG_DO_FORCE_ATTACK_GROUND RW 0x77AF93, MSG_DO_FORCE_ATTACK_OBJECT
// RW 0x77AED3), aiAttackPosition / privateAttackPosition (RW 0x6961F1 / 0x66DE02) and the damage of a forced attack on one's own object (RW 0x90D77C: the
// RadiusDamageAffects mask also holds for the victim the shot was aimed at). Two peers fed the same encoded messages stay in lockstep.
// Synthetic data (combattest::CombatWorld), no retail files.
#include "doctest.h"
#include "CombatTestUtil.h"

#include "GameLogic/AI/AIAttack.h"
#include "GameLogic/AI/AIGroup.h"
#include "GameNetwork/NetPacket.h"
#include "GameLogic/Object/PartitionManager.h"

using namespace combattest;

namespace
{
// a catapult throwing a boulder whose warhead hits everything in 20 (StoneWarhead: ALLIES ENEMIES NEUTRALS), and one whose warhead spares allies
// (StoneWarheadEnemies: ENEMIES only, as the retail archers' GondorArcherBowWarhead: "ENEMIES NEUTRALS NOT_SIMILAR ;ALLIES")
// INTEG-2: the weapons carry NoVictimNeeded as retail's ground-capable weapons do (GondorTrebuchetRock, GondorArcherBowBombard): an attack on a position
// chooses its weapon with no victim, and RotWK's victimless pick (WeaponSet::chooseBestWeaponForTarget RW 0x6C8AA7 .. 0x6C8AFA, lane DECOMP-1) takes only
// a weapon with NoVictimNeeded (+ 0x16B); none: AIAttackState::onEnter fails (RW 0x74CF48 -> 0x74D0C9)
const char kCatapults[] =
	"Weapon StoneLob\n"
	"  AttackRange = 300\n"
	"  WeaponSpeed = 100\n"
	"  NoVictimNeeded = Yes\n"
	"  ProjectileNugget\n"
	"    ProjectileTemplateName = Boulder\n"
	"    WarheadTemplateName = StoneWarhead\n"
	"  End\n"
	"End\n"
	"Weapon StoneLobEnemies\n"
	"  AttackRange = 300\n"
	"  WeaponSpeed = 100\n"
	"  NoVictimNeeded = Yes\n"
	"  ProjectileNugget\n"
	"    ProjectileTemplateName = Boulder\n"
	"    WarheadTemplateName = StoneWarheadEnemies\n"
	"  End\n"
	"End\n"
	// lane PLAY-2 r2: a bow with a ContinueAttackRange (privateAttackPosition looks for an object near the spot, RW 0x66DE5D)
	"Weapon ContinueBow\n"
	"  AttackRange = 200\n"
	"  ContinueAttackRange = 60\n"
	"  WeaponSpeed = 100\n"
	"  DelayBetweenShots = 400\n"
	"  NoVictimNeeded = Yes\n"
	"  ProjectileNugget\n"
	"    ProjectileTemplateName = Arrow\n"
	"    WarheadTemplateName = BowWarhead\n"
	"  End\n"
	"End\n"
	"Object ContinueArcher\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK\n"
	"  VisionRange = 300\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY ContinueBow\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 60\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"    AutoAcquireEnemiesWhenIdle = No\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object Catapult\n"
	"  KindOf = VEHICLE SELECTABLE CAN_ATTACK\n"
	"  VisionRange = 300\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryMinorRadius = 10\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY StoneLob\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 200\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"    AutoAcquireEnemiesWhenIdle = No\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 30\n"
	"  End\n"
	"End\n"
	"Object FriendlyCatapult\n"
	"  KindOf = VEHICLE SELECTABLE CAN_ATTACK\n"
	"  VisionRange = 300\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryMinorRadius = 10\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY StoneLobEnemies\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 200\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"    AutoAcquireEnemiesWhenIdle = No\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 30\n"
	"  End\n"
	"End\n";

struct ForceWorld : CombatWorld
{
	ForceWorld() : CombatWorld(kCatapults) { combat().setAutoAcquireEnabled(false); }
	void groundMessage(int player, float x, float y)
	{
		GameMessage m(MSG_DO_FORCE_ATTACK_GROUND, player);
		m.appendLocationArgument(Coord3D{ x, y, logic->getGroundHeight(x, y) });
		send(m);
	}
	void forceObjectMessage(int player, Object *victim)
	{
		GameMessage m(MSG_DO_FORCE_ATTACK_OBJECT, player);
		m.appendObjectIDArgument(victim->getID());
		m.appendLocationArgument(*victim->getPosition()); // the client appends the position too (CommandXlat, RW 0x81E4B5)
		send(m);
	}
	unsigned long long shots(Object *o) { return o->getWeapons()->stats().shotsFired; }
	unsigned state(Object *o) { return o->getAIUpdateInterface()->currentStateId(); }
};
} // namespace

TEST_CASE("play2 force attack ground: the message makes the selection attack the spot (state 9); the boulder lands there and hurts what stands there, friend or foe")
{
	ForceWorld w;
	const int alice = w.playerIndex("Alice");
	Object *cat = w.unit("Catapult", 'A', 300, 300);
	Object *foe = w.unit("Dummy", 'B', 500, 300);
	Object *own = w.unit("Dummy", 'A', 505, 300);
	w.frames(2);
	w.select(alice, { cat });
	w.groundMessage(alice, 500.0f, 300.0f);
	w.frames(1);
	CHECK(w.aiCommands.stats().forceAttackGrounds == 1);
	CHECK(w.state(cat) == (unsigned)AI_ATTACK_POSITION);
	CHECK(cat->getAIUpdateInterface()->isAttacking());
	CHECK(cat->getAIUpdateInterface()->currentVictimId() == INVALID_ID); // a position, no victim
	w.runUntil([&] { return w.health(foe) < 100.0f; }, 200);
	CHECK(w.shots(cat) >= 1);
	CHECK(w.health(foe) < 100.0f);
	CHECK(w.health(own) < 100.0f); // StoneWarhead affects ALLIES: the player's own dummy at the spot is hit too
	// the attack goes on (no shot limit) until another order
	CHECK(w.state(cat) == (unsigned)AI_ATTACK_POSITION);
	cat->getAIUpdateInterface()->aiIdle(CMD_FROM_PLAYER);
	CHECK(w.state(cat) == (unsigned)AI_IDLE);
}

TEST_CASE("play2 force attack ground: malformed or foreign messages change nothing; an empty selection does nothing")
{
	ForceWorld w;
	const int alice = w.playerIndex("Alice"), bob = w.playerIndex("Bob");
	Object *cat = w.unit("Catapult", 'A', 300, 300);
	w.frames(2);
	GameMessage bad(MSG_DO_FORCE_ATTACK_GROUND, alice); // no location
	w.send(bad);
	w.frames(1);
	CHECK(w.aiCommands.stats().rejected == 1);
	w.groundMessage(alice, 400.0f, 300.0f); // nothing selected
	w.frames(1);
	CHECK(w.state(cat) == (unsigned)AI_IDLE);
	w.select(alice, { cat });
	w.groundMessage(bob, 400.0f, 300.0f); // Bob cannot order Alice's catapult
	w.frames(1);
	CHECK(w.state(cat) == (unsigned)AI_IDLE);
	CHECK(w.aiCommands.stats().forceAttackGrounds == 2);
}

TEST_CASE("play2 aiAttackPosition: the shot limit ends the attack, the same spot again is ignored (RW 0x66DFA3), a new spot restarts it")
{
	ForceWorld w;
	Object *cat = w.unit("Catapult", 'A', 300, 300);
	w.frames(2);
	AIUpdateInterface *ai = cat->getAIUpdateInterface();
	const Coord3D spot{ 450.0f, 300.0f, w.logic->getGroundHeight(450.0f, 300.0f) };
	REQUIRE(ai->aiAttackPosition(spot, 1, CMD_FROM_PLAYER));
	CHECK(cat->getWeapons()->currentWeapon()->maxShotCount() == 1);
	CHECK_FALSE(ai->aiAttackPosition(spot, 1, CMD_FROM_PLAYER)); // already attacking that spot
	w.runUntil([&] { return w.state(cat) != (unsigned)AI_ATTACK_POSITION; }, 200);
	CHECK(w.shots(cat) == 1);
	CHECK(w.state(cat) == (unsigned)AI_IDLE);
	const Coord3D other{ 460.0f, 300.0f, spot.z };
	CHECK(ai->aiAttackPosition(other, 2, CMD_FROM_PLAYER));
	w.runUntil([&] { return w.state(cat) != (unsigned)AI_ATTACK_POSITION; }, 300);
	CHECK(w.shots(cat) == 3);
	// an attack on an object lifts the limit again (RW 0x74D0FC)
	Object *foe = w.unit("Dummy", 'B', 400, 300);
	w.frames(1);
	REQUIRE(ai->aiAttackObject(foe, CMD_FROM_PLAYER));
	CHECK(cat->getWeapons()->currentWeapon()->maxShotCount() == 0x7FFFFFFF);
}

TEST_CASE("play2 aiAttackPosition: a ContinueAttackRange weapon attacks the object near the spot with the order's shot limit (RW 0x66DF02, Sol r1)")
{
	ForceWorld w;
	w.logic->partition().setRegion(0.0f, 0.0f, 1000.0f, 1000.0f); // the map's extent (RW 0x62FCCD): the ContinueAttackRange search walks the partition
	Object *archer = w.unit("ContinueArcher", 'A', 300, 300);
	Object *foe = w.unit("Dummy", 'B', 440, 300);
	w.frames(2);
	AIUpdateInterface *ai = archer->getAIUpdateInterface();
	const Coord3D spot{ 450.0f, 300.0f, w.logic->getGroundHeight(450.0f, 300.0f) };
	// one shot asked: the redirect to the dummy (10 from the spot, within 60) keeps it
	REQUIRE(ai->aiAttackPosition(spot, 1, CMD_FROM_PLAYER));
	CHECK(w.state(archer) == (unsigned)AI_ATTACK_OBJECT);
	CHECK(ai->currentVictimId() == foe->getID());
	CHECK(archer->getWeapons()->currentWeapon()->maxShotCount() == 1);
	w.runUntil([&] { return w.state(archer) != (unsigned)AI_ATTACK_OBJECT; }, 300);
	CHECK(w.shots(archer) == 1);
	CHECK(w.state(archer) == (unsigned)AI_IDLE);
	// nothing near the spot: the position attack, its limit 1 (RW 0x66DF16)
	const Coord3D empty{ 300.0f, 500.0f, w.logic->getGroundHeight(300.0f, 500.0f) };
	REQUIRE(ai->aiAttackPosition(empty, 5, CMD_FROM_PLAYER));
	CHECK(w.state(archer) == (unsigned)AI_ATTACK_POSITION);
	CHECK(archer->getWeapons()->currentWeapon()->maxShotCount() == 1);
	w.runUntil([&] { return w.state(archer) != (unsigned)AI_ATTACK_POSITION; }, 300);
	CHECK(w.shots(archer) == 2);
}

TEST_CASE("play2 force attack ground: a busy group fires its PRIMARY weapon under a temporary lock that is released at once; an idle group is released first")
{
	ForceWorld w;
	const int alice = w.playerIndex("Alice");
	Object *cat = w.unit("Catapult", 'A', 300, 300);
	Object *foe = w.unit("Dummy", 'B', 420, 300);
	w.frames(2);
	AIGroup idle(*w.logic, { cat->getID() });
	CHECK(idle.isIdle());
	// a temporary lock left over is released before the order of an idle group
	REQUIRE(cat->getWeapons()->setWeaponLock(PRIMARY_WEAPON, LOCKED_TEMPORARILY));
	w.select(alice, { cat });
	w.groundMessage(alice, 450.0f, 300.0f);
	w.frames(1);
	CHECK_FALSE(cat->getWeapons()->isCurWeaponLocked());
	CHECK(w.state(cat) == (unsigned)AI_ATTACK_POSITION);
	// busy (attacking the foe): lock, order, release
	REQUIRE(cat->getAIUpdateInterface()->aiAttackObject(foe, CMD_FROM_PLAYER));
	AIGroup busy(*w.logic, { cat->getID() });
	CHECK_FALSE(busy.isIdle());
	w.groundMessage(alice, 440.0f, 320.0f);
	w.frames(1);
	CHECK_FALSE(cat->getWeapons()->isCurWeaponLocked());
	CHECK(w.state(cat) == (unsigned)AI_ATTACK_POSITION);
	CHECK(w.combat().counters().damageApplications == 0); // nothing landed yet on the foe
	// a permanent lock survives: the temporary lock cannot override it (RW 0x6C97F9) and the release is temporary only
	REQUIRE(cat->getWeapons()->setWeaponLock(PRIMARY_WEAPON, LOCKED_PERMANENTLY));
	w.groundMessage(alice, 430.0f, 300.0f);
	w.frames(1);
	CHECK(cat->getWeapons()->isCurWeaponLocked());
}

TEST_CASE("play2 force attack object: the player's own object is attacked by a weapon that can hurt it; a weapon whose warhead spares allies is not chosen and the attack ends (RW 0x6C8BF2)")
{
	// INTEG-2 (the DECOMP-1 x PLAY-2 merge): RotWK's weapon choice (RW 0x6C8BF2 .. 0x6C8C07, lane DECOMP-1) skips a weapon that cannot damage the victim
	// (Weapon::canDamage RW 0x6CDBF3 -> the warhead's RadiusDamageAffects, RW 0x90D77C) unless its DamageType is UNRESISTABLE; with no weapon chosen
	// AIAttackState::onEnter fails (RW 0x74CF4F -> 0x74D0C9), so the forced order on one's own object ends at once and nothing is fired
	ForceWorld w;
	const int alice = w.playerIndex("Alice");
	Object *both = w.unit("Catapult", 'A', 300, 300);
	Object *spare = w.unit("FriendlyCatapult", 'A', 300, 500);
	Object *farmA = w.unit("Dummy", 'A', 450, 300);
	Object *farmB = w.unit("Dummy", 'A', 450, 500);
	w.frames(2);
	const ObjectID idA = farmA->getID(), idB = farmB->getID();
	w.select(alice, { both });
	w.forceObjectMessage(alice, farmA);
	w.frames(1);
	w.select(alice, { spare });
	w.forceObjectMessage(alice, farmB);
	w.frames(1);
	CHECK(w.aiCommands.stats().forceAttacks == 2);
	CHECK(w.aiCommands.stats().attackOrdersAccepted == 2);
	CHECK(w.state(both) == (unsigned)AI_FORCE_ATTACK_OBJECT);
	CHECK(w.state(spare) != (unsigned)AI_FORCE_ATTACK_OBJECT);
	w.runUntil([&] { return w.shots(both) >= 2; }, 400);
	w.frames(30); // the boulders land
	CHECK(w.shots(both) >= 2);
	CHECK(w.shots(spare) == 0);
	CHECK(w.byId(idA) == nullptr); // ALLIES in RadiusDamageAffects: three boulders of 40 kill it
	REQUIRE(w.byId(idB) != nullptr);
	CHECK(w.health(w.byId(idB)) == 100.0f);
}

TEST_CASE("play2 force attack object: a plain attack order on one's own object is refused, a victim that is gone is ignored")
{
	ForceWorld w;
	const int alice = w.playerIndex("Alice");
	Object *cat = w.unit("Catapult", 'A', 300, 300);
	Object *own = w.unit("Dummy", 'A', 400, 300);
	w.frames(2);
	w.select(alice, { cat });
	w.attackMessage(alice, own);
	w.frames(1);
	CHECK(w.state(cat) == (unsigned)AI_IDLE);
	GameMessage gone(MSG_DO_FORCE_ATTACK_OBJECT, alice);
	gone.appendObjectIDArgument(99999);
	gone.appendLocationArgument(Coord3D{ 400.0f, 300.0f, 0.0f });
	w.send(gone);
	w.frames(1);
	CHECK(w.state(cat) == (unsigned)AI_IDLE);
	CHECK(w.aiCommands.stats().attackOrdersAccepted == 0);
}

TEST_CASE("play2 force attack: the S-2480 stop is reported once the ground form runs")
{
	ForceWorld w;
	const int alice = w.playerIndex("Alice");
	Object *cat = w.unit("Catapult", 'A', 300, 300);
	w.frames(2);
	w.select(alice, { cat });
	w.groundMessage(alice, 450.0f, 300.0f);
	w.frames(1);
	bool found = false;
	for (const auto &s : w.logic->notedStopHits())
	{
		found = found || s.first.rfind("[S-2480]", 0) == 0;
	}
	CHECK(found);
}

TEST_CASE("play2 force attack: two peers fed the same orders through the network encoding hash alike in every frame (both forms, own and enemy targets)")
{
	// peer 0 records the messages it sends; peer 1 receives them through NetPacket's GameMessage encoding (the lockstep wire format), on the same frames
	std::vector<std::vector<std::vector<std::uint8_t>>> wire(400);
	std::vector<std::uint32_t> hashes[2];
	for (int peer = 0; peer < 2; ++peer)
	{
		ForceWorld w;
		const int alice = w.playerIndex("Alice");
		Object *cat = w.unit("Catapult", 'A', 300, 300);
		Object *spare = w.unit("FriendlyCatapult", 'A', 300, 500);
		Object *foe = w.unit("Dummy", 'B', 480, 320);
		Object *own = w.unit("Dummy", 'A', 450, 500);
		const ObjectID foeId = foe->getID();
		w.frames(2);
		auto order = [&](int frame, const GameMessage &m) {
			if (peer == 0)
			{
				NetByteWriter bytes;
				std::string error;
				REQUIRE_MESSAGE(NetPacket::writeGameMessage(bytes, m, &error), error);
				wire[(size_t)frame].push_back(bytes.take());
			}
		};
		for (int f = 0; f < 400; ++f)
		{
			if (peer == 0)
			{
				if (f == 0)
				{
					GameMessage sel(MSG_CREATE_SELECTED_GROUP, alice);
					sel.appendBooleanArgument(true);
					sel.appendObjectIDArgument(cat->getID());
					order(f, sel);
					GameMessage g(MSG_DO_FORCE_ATTACK_GROUND, alice);
					g.appendLocationArgument(Coord3D{ 480.0f, 300.0f, w.logic->getGroundHeight(480.0f, 300.0f) });
					order(f, g);
				}
				if (f == 40)
				{
					GameMessage sel(MSG_CREATE_SELECTED_GROUP, alice);
					sel.appendBooleanArgument(true);
					sel.appendObjectIDArgument(spare->getID());
					order(f, sel);
					GameMessage o(MSG_DO_FORCE_ATTACK_OBJECT, alice);
					o.appendObjectIDArgument(own->getID());
					o.appendLocationArgument(*own->getPosition());
					order(f, o);
				}
				if (f == 120)
				{
					GameMessage sel(MSG_CREATE_SELECTED_GROUP, alice);
					sel.appendBooleanArgument(true);
					sel.appendObjectIDArgument(cat->getID());
					sel.appendObjectIDArgument(spare->getID());
					order(f, sel);
					GameMessage o(MSG_DO_FORCE_ATTACK_OBJECT, alice);
					o.appendObjectIDArgument(foe->getID());
					o.appendLocationArgument(*foe->getPosition());
					order(f, o);
				}
			}
			for (const std::vector<std::uint8_t> &b : wire[(size_t)f])
			{
				NetByteReader r(b);
				std::string error;
				std::unique_ptr<GameMessage> m = NetPacket::readGameMessage(r, &error);
				REQUIRE_MESSAGE(m, error);
				w.send(*m);
			}
			w.frames(1);
			hashes[peer].push_back(w.hash());
			if (f == 119)
			{
				CHECK(w.shots(spare) == 0); // INTEG-2: its warhead spares allies, so the forced order on its own dummy chooses no weapon (RW 0x6C8BF2)
			}
		}
		CHECK(w.shots(cat) >= 2);
		CHECK(w.byId(foeId) == nullptr); // within 20 of the spot: the ground attack's boulders kill it before frame 120, so the last order finds no victim
		CHECK(w.shots(spare) == 0);
		CHECK(w.health(own) == 100.0f);
		if (peer == 1)
		{
			CHECK(w.aiCommands.stats().forceAttackGrounds == 1);
			CHECK(w.aiCommands.stats().forceAttacks == 2);
		}
	}
	REQUIRE(hashes[0].size() == hashes[1].size());
	for (size_t i = 0; i < hashes[0].size(); ++i)
	{
		REQUIRE_MESSAGE(hashes[0][i] == hashes[1][i], "frame " << i);
	}
	CHECK(hashes[0].front() != hashes[0].back());
}

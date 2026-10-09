// OpenBFME. FX-2 tests: the logic's effect calls (GameLogic/FXEvents.h) - which events, at which frames, with which objects - on synthetic data
// (combattest::CombatWorld), the state hash not touched by a sink, and the live effect player (GameClient/LiveFX) on a retail map: a trebuchet shelling a
// orc pit plays its fire FX, the boulders' impact FX, the building's damage state FX and the fires of its damaged states, with no FXList the store lacks.
#include "CreepTestUtil.h"
#include "doctest.h"
#include "CombatTestUtil.h"
#include "HudTestUtil.h"

#include "GameClient/FXPlayback.h"
#include "GameClient/LiveFX.h"
#include "GameLogic/DamageFX.h"
#include "GameLogic/FXEvents.h"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace combattest;

namespace
{
// a recording sink: every call with the logic frame it was made in
struct Recorder : FXEventSink
{
	std::vector<FXEvent> events;
	void onFXEvent(const FXEvent &e) override { events.push_back(e); }
	std::vector<FXEvent> site(const std::string &s) const
	{
		std::vector<FXEvent> out;
		for (const FXEvent &e : events)
		{
			if (s == e.site)
			{
				out.push_back(e);
			}
		}
		return out;
	}
};

// FxSword: SwordWeapon with a FireFX; LateSword: the same with SuspendFXDelay 2000 ms (10 frames). FxSwordsman / LateSwordsman carry them and do not look for
// enemies on their own. FxCorpse: an FXListDie that orients to the killer and a SlowDeathBehavior with FX in three phases. FxTarget: armour with a DamageFX.
const char kFxObjects[] =
	"Weapon FxSword\n"
	"  AttackRange = 11.5\n"
	"  MeleeWeapon = Yes\n"
	"  DelayBetweenShots = 1000\n"
	"  FireFX = FX_TestSwing\n"
	"  DamageNugget\n"
	"    Damage = 10\n"
	"    Radius = 0.0\n"
	"    DelayTime = 0\n"
	"    DamageType = SLASH\n"
	"    DeathType = NORMAL\n"
	"  End\n"
	"End\n"
	"Weapon LateSword\n"
	"  AttackRange = 11.5\n"
	"  MeleeWeapon = Yes\n"
	"  DelayBetweenShots = 1000\n"
	"  FireFX = FX_TestSwing\n"
	"  SuspendFXDelay = 2000\n"
	"  DamageNugget\n"
	"    Damage = 10\n"
	"    Radius = 0.0\n"
	"    DelayTime = 0\n"
	"    DamageType = SLASH\n"
	"    DeathType = NORMAL\n"
	"  End\n"
	"End\n"
	"Object FxSwordsman\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK\n"
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
	"    Weapon = PRIMARY FxSword\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object LateSwordsman\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK\n"
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
	"    Weapon = PRIMARY LateSword\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object FxCorpse\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = NoArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = FXListDie ModuleTag_FXDie\n"
	"    DeathTypes = ALL\n"
	"    DeathFX = FX_TestDie\n"
	"    OrientToObject = Yes\n"
	"  End\n"
	"  Behavior = FXListDie ModuleTag_FXDie2\n"
	"    DeathTypes = ALL\n"
	"    DeathFX = None\n"
	"  End\n"
	"  Behavior = SlowDeathBehavior ModuleTag_Death\n"
	"    DeathTypes = ALL\n"
	"    DestructionDelay = 2000\n"
	"    FX = INITIAL FX_TestInitialA FX_TestInitialB\n"
	"    FX = MIDPOINT FX_TestMid\n"
	"    FX = FINAL FX_TestFinal\n"
	"    OCL = INITIAL OCL_TestA OCL_TestB\n"
	"    Sound = INITIAL TestDieSound TestDieSound2\n"
	"  End\n"
	"End\n"
	"Object FxNoneCorpse\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = NoArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = SlowDeathBehavior ModuleTag_Death\n"
	"    DeathTypes = ALL\n"
	"    DestructionDelay = 2000\n"
	"    FX = INITIAL None\n"
	"    OCL = INITIAL None None\n"
	"  End\n"
	"End\n"
	"Object FxTarget\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = NoArmor\n"
	"    DamageFX = TestDamageFX\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"  End\n"
	"End\n";

// every DamageFX type: minor below 30, major from 30, throttled for 2000 ms (10 frames)
const char kDamageFX[] =
	"DamageFX TestDamageFX\n"
	"  AmountForMajorFX = Default 30\n"
	"  MajorFX = Default FX_TestMajor\n"
	"  MinorFX = Default FX_TestMinor\n"
	"  ThrottleTime = Default 2000\n"
	"End\n";

DamageInfo hit(const Object *source, float amount)
{
	DamageInfo d;
	d.m_input.m_sourceID = source ? source->getID() : 0;
	d.m_input.m_amount = amount;
	d.m_input.m_damageType = DAMAGE_SLASH;
	return d;
}

// one FxSwordsman attacking a Dummy for `frames` frames; the hash of every frame
std::vector<std::uint32_t> swordFight(FXEventSink *sink, int frames, unsigned *shots = nullptr, size_t *logged = nullptr)
{
	CombatWorld w(kFxObjects);
	w.logic->fxEvents().setSink(sink);
	Object *a = w.unit("FxSwordsman", 'A', 300, 300);
	Object *d = w.unit("Dummy", 'B', 312, 300);
	w.frames(2);
	REQUIRE(w.aiOf(a)->aiAttackObject(d, CMD_FROM_PLAYER));
	std::vector<std::uint32_t> hashes;
	size_t count = 0;
	for (int i = 0; i < frames; ++i)
	{
		w.frames(1);
		count += w.logic->fxEvents().frameEvents().size();
		hashes.push_back(w.hash());
	}
	if (shots)
	{
		*shots = (unsigned)a->getWeapons()->stats().shotsFired;
	}
	if (logged)
	{
		*logged = count;
	}
	return hashes;
}
} // namespace

TEST_CASE("fx2 events: every shot of a weapon with a FireFX is one WEAPON_FIRE_FX event in the frame of the shot, with the shooter, the victim and the slot")
{
	CombatWorld w(kFxObjects);
	Recorder rec;
	w.logic->fxEvents().setSink(&rec);
	Object *a = w.unit("FxSwordsman", 'A', 300, 300);
	Object *d = w.unit("Dummy", 'B', 312, 300);
	w.frames(2);
	REQUIRE(w.aiOf(a)->aiAttackObject(d, CMD_FROM_PLAYER));
	std::vector<unsigned> shotFrames;
	unsigned last = 0;
	for (int i = 0; i < 40 && w.byId(d->getID()); ++i)
	{
		w.frames(1);
		const unsigned shots = (unsigned)a->getWeapons()->stats().shotsFired;
		if (shots != last)
		{
			shotFrames.push_back(w.logic->getFrame());
			last = shots;
		}
		// the frame's list holds this frame's calls only
		for (const FXEvent &e : w.logic->fxEvents().frameEvents())
		{
			CHECK(e.frame == w.logic->getFrame());
		}
	}
	const std::vector<FXEvent> fire = rec.site("FireFX");
	REQUIRE(shotFrames.size() >= 3);
	REQUIRE(fire.size() == shotFrames.size());
	for (size_t i = 0; i < fire.size(); ++i)
	{
		const FXEvent &e = fire[i];
		CHECK(e.kind == FXEvent::WEAPON_FIRE_FX);
		CHECK(e.frame == shotFrames[i]);
		CHECK(*e.fxList == "FX_TestSwing");
		CHECK(e.primary == a->getID());
		CHECK(e.secondary == d->getID());
		CHECK(e.weaponSlot == 0);
		CHECK(e.barrel == 0);
		CHECK(e.hasTransform);
		CHECK_FALSE(e.playWhenStealthed);
	}
	// DelayBetweenShots 1000 ms = 5 frames at least between the shots (the attack state's approach and pre-attack add to it)
	for (size_t i = 1; i < shotFrames.size(); ++i)
	{
		CHECK(shotFrames[i] - shotFrames[i - 1] >= 5);
	}
	CHECK(w.logic->fxEvents().perSite().at("FireFX") == fire.size());
}

TEST_CASE("fx2 events: no FireFX while the logic frame is below the weapon's SuspendFXDelay frame (RW 0x6CC915 reads Weapon + 0x30)")
{
	CombatWorld w(kFxObjects);
	Recorder rec;
	w.logic->fxEvents().setSink(&rec);
	Object *a = w.unit("LateSwordsman", 'A', 300, 300);
	Object *d = w.unit("FxTarget", 'B', 312, 300);
	const unsigned created = w.logic->getFrame();
	REQUIRE(w.aiOf(a)->aiAttackObject(d, CMD_FROM_PLAYER));
	std::vector<unsigned> shotFrames;
	unsigned last = 0;
	for (int i = 0; i < 30; ++i)
	{
		w.frames(1);
		const unsigned shots = (unsigned)a->getWeapons()->stats().shotsFired;
		if (shots != last)
		{
			shotFrames.push_back(w.logic->getFrame());
			last = shots;
		}
	}
	REQUIRE(shotFrames.size() >= 3);
	REQUIRE(shotFrames.front() < created + 10); // the first shot falls inside the suspension
	size_t expected = 0;
	for (unsigned f : shotFrames)
	{
		expected += f >= created + 10 ? 1 : 0;
	}
	CHECK(rec.site("FireFX").size() == expected);
	CHECK(expected < shotFrames.size());
}

TEST_CASE("fx2 events: a death plays FXListDie's DeathFX on the corpse with the killer, a 'None' DeathFX is no call, the slow death plays one picked FX per phase")
{
	CombatWorld w(kFxObjects);
	Recorder rec;
	w.logic->fxEvents().setSink(&rec);
	Object *a = w.unit("Swordsman", 'A', 300, 300);
	Object *c = w.unit("FxCorpse", 'B', 340, 300);
	w.frames(3);
	const ObjectID id = c->getID();
	const Coord3D at = *c->getPosition();
	DamageInfo big = hit(a, 1000.0f);
	w.logic->random().enableCallLog(true);
	c->attemptDamage(big);
	w.logic->random().enableCallLog(false);
	REQUIRE(c->isEffectivelyDead());
	// RW 0x8609A8: the FX and Sound picks are client draws (the player makes them), the OCL pick is a logic draw at SlowDeathBehavior.cpp line 0x221
	std::vector<int> sdLines;
	for (const GameLogicRandom::Call &call : w.logic->random().callLog())
	{
		if (call.file == "SlowDeathBehavior.cpp")
		{
			sdLines.push_back(call.line);
		}
	}
	// the roulette, the sink / destruction / midpoint draws of beginSlowDeath (line 0), then the OCL pick of the INITIAL phase
	REQUIRE(!sdLines.empty());
	CHECK(sdLines.back() == 0x221);
	CHECK(std::count(sdLines.begin(), sdLines.end(), 0x221) == 1);
	CHECK(std::count(sdLines.begin(), sdLines.end(), 0x238) == 0);
	CHECK(std::count(sdLines.begin(), sdLines.end(), 0x217) == 0);
	const unsigned deathFrame = w.logic->getFrame();
	const std::vector<FXEvent> die = rec.site("FXListDie");
	REQUIRE(die.size() == 1);
	CHECK(die[0].kind == FXEvent::OBJECT_FX);
	CHECK(*die[0].fxList == "FX_TestDie");
	CHECK(die[0].primary == id);
	CHECK(die[0].secondary == a->getID());
	CHECK(die[0].frame == deathFrame);
	CHECK(die[0].position.x == at.x);
	CHECK(die[0].position.y == at.y);
	const std::vector<FXEvent> initial = rec.site("SlowDeath INITIAL");
	REQUIRE(initial.size() == 1);
	REQUIRE(initial[0].choices != nullptr);
	CHECK(*initial[0].choices == std::vector<std::string>{ "FX_TestInitialA", "FX_TestInitialB" });
	CHECK(initial[0].frame == deathFrame);
	CHECK(initial[0].secondary == INVALID_ID);
	int guard = 0;
	while (w.byId(id) && guard++ < 30)
	{
		w.frames(1);
	}
	REQUIRE(w.byId(id) == nullptr);
	const std::vector<FXEvent> mid = rec.site("SlowDeath MIDPOINT");
	const std::vector<FXEvent> fin = rec.site("SlowDeath FINAL");
	REQUIRE(mid.size() == 1);
	REQUIRE(fin.size() == 1);
	CHECK(*mid[0].fxList == "FX_TestMid");
	CHECK(*fin[0].fxList == "FX_TestFinal");
	// DestructionDelay 2000 ms = 10 frames (no variance): the FINAL phase plays in the frame the object is destroyed; the midpoint lies in 35% .. 65% of it
	CHECK(fin[0].frame == deathFrame + 10);
	CHECK(mid[0].frame >= deathFrame + 3);
	CHECK(mid[0].frame <= deathFrame + 7);
	// the death sound list (every token of the Sound line: RW 0x861903) on the corpse in the death frame; the player picks one
	const std::vector<FXEvent> sound = rec.site("SlowDeath Sound INITIAL");
	REQUIRE(sound.size() == 1);
	CHECK(sound[0].kind == FXEvent::OBJECT_SOUND);
	REQUIRE(sound[0].choices != nullptr);
	CHECK(*sound[0].choices == std::vector<std::string>{ "TestDieSound", "TestDieSound2" });
	CHECK(sound[0].frame == deathFrame);
	CHECK(sound[0].primary == id);
	CHECK(rec.events.size() == 5); // FXListDie, INITIAL, the sound, MIDPOINT, FINAL: the 'None' DeathFX is no call
}

TEST_CASE("fx2 events: the slow death does nothing when its resolved-entry mask (RW byte +0x18C) is zero: no phase draw, no call")
{
	CombatWorld w(kFxObjects);
	Recorder rec;
	w.logic->fxEvents().setSink(&rec);
	Object *a = w.unit("Swordsman", 'A', 300, 300);
	Object *c = w.unit("FxNoneCorpse", 'B', 340, 300);
	w.frames(3);
	const ObjectID id = c->getID();
	w.logic->random().enableCallLog(true);
	DamageInfo big = hit(a, 1000.0f);
	c->attemptDamage(big);
	int guard = 0;
	while (w.byId(id) && guard++ < 30)
	{
		w.frames(1);
	}
	w.logic->random().enableCallLog(false);
	REQUIRE(w.byId(id) == nullptr);
	for (const GameLogicRandom::Call &call : w.logic->random().callLog())
	{
		CHECK_FALSE((call.file == "SlowDeathBehavior.cpp" && call.line != 0));
	}
	CHECK(rec.events.empty());
}

TEST_CASE("fx2 events: every shot at a victim draws the aim seed of Weapon.cpp:1749 (RW 0x6CB85A flag 1), FireFX or not, suspended or not")
{
	for (const char *shooter : { "Swordsman", "LateSwordsman" })
	{
		CombatWorld w(kFxObjects);
		Object *a = w.unit(shooter, 'A', 300, 300);
		Object *d = w.unit("FxTarget", 'B', 312, 300);
		w.frames(2);
		REQUIRE(w.aiOf(a)->aiAttackObject(d, CMD_FROM_PLAYER));
		w.logic->random().enableCallLog(true);
		w.frames(30);
		w.logic->random().enableCallLog(false);
		const unsigned shots = (unsigned)a->getWeapons()->stats().shotsAtVictim;
		size_t aimDraws = 0;
		for (const GameLogicRandom::Call &call : w.logic->random().callLog())
		{
			if (call.file == "Weapon.cpp" && call.line == 1749)
			{
				++aimDraws;
				CHECK(call.lo == 0);
				CHECK(call.hi == 12345678);
			}
		}
		MESSAGE(std::string(shooter) << ": " << shots << " shots at the victim, " << aimDraws << " aim draws");
		CHECK(shots >= 3);
		CHECK(aimDraws == shots);
	}
}

TEST_CASE("fx2 frame events: the FireWhenSkipped single-frame test follows the track's mode and direction (RW 0x4B337B)")
{
	// no step, the frame itself
	CHECK_FALSE(LiveFX::framePassed(7, 7, 7, W3D_ANIM_MODE_LOOP, 1));
	CHECK(LiveFX::framePassed(4, 5, 5, W3D_ANIM_MODE_LOOP_BACKWARDS, -1));
	// LOOP: ordinary forward, wrapped
	CHECK(LiveFX::framePassed(3, 8, 5, W3D_ANIM_MODE_LOOP, 1));
	CHECK_FALSE(LiveFX::framePassed(6, 8, 5, W3D_ANIM_MODE_LOOP, 1));
	CHECK(LiveFX::framePassed(18, 2, 19, W3D_ANIM_MODE_LOOP, 1));
	CHECK(LiveFX::framePassed(18, 2, 1, W3D_ANIM_MODE_LOOP, 1));
	CHECK_FALSE(LiveFX::framePassed(18, 2, 10, W3D_ANIM_MODE_LOOP, 1));
	// LOOP_BACKWARDS: a backward step 12 -> 11 does not pass frame 5 (the review's case), wrapped
	CHECK_FALSE(LiveFX::framePassed(12, 11, 5, W3D_ANIM_MODE_LOOP_BACKWARDS, -1));
	CHECK(LiveFX::framePassed(12, 4, 5, W3D_ANIM_MODE_LOOP_BACKWARDS, -1));
	CHECK(LiveFX::framePassed(2, 18, 1, W3D_ANIM_MODE_LOOP_BACKWARDS, -1));
	CHECK(LiveFX::framePassed(2, 18, 19, W3D_ANIM_MODE_LOOP_BACKWARDS, -1));
	CHECK_FALSE(LiveFX::framePassed(2, 18, 10, W3D_ANIM_MODE_LOOP_BACKWARDS, -1));
	// ONCE / MANUAL / ONCE_BACKWARDS follow the direction
	CHECK(LiveFX::framePassed(3, 8, 5, W3D_ANIM_MODE_ONCE, 1));
	CHECK_FALSE(LiveFX::framePassed(8, 3, 5, W3D_ANIM_MODE_ONCE, 1));
	CHECK(LiveFX::framePassed(8, 3, 5, W3D_ANIM_MODE_ONCE_BACKWARDS, -1));
	CHECK(LiveFX::framePassed(8, 3, 5, W3D_ANIM_MODE_PLAY_TO_FRAME, -1));
	// PING_PONG: forward ordinary and reflected, backward ordinary and reflected
	CHECK(LiveFX::framePassed(3, 8, 5, W3D_ANIM_MODE_LOOP_PINGPONG, 1));
	CHECK(LiveFX::framePassed(9, 7, 8, W3D_ANIM_MODE_LOOP_PINGPONG, 1));
	CHECK_FALSE(LiveFX::framePassed(9, 7, 10, W3D_ANIM_MODE_LOOP_PINGPONG, 1));
	CHECK(LiveFX::framePassed(8, 3, 5, W3D_ANIM_MODE_LOOP_PINGPONG, -1));
	CHECK(LiveFX::framePassed(1, 3, 2, W3D_ANIM_MODE_LOOP_PINGPONG, -1));
	CHECK_FALSE(LiveFX::framePassed(1, 3, 0, W3D_ANIM_MODE_LOOP_PINGPONG, -1));
	CHECK_FALSE(LiveFX::framePassed(3, 8, 5, 0, 1)); // an unknown mode never passes
}

TEST_CASE("fx2 events: ActiveBody::doDamageFX plays the armour set's DamageFX on the victim with the attacker, minor / major by the damage dealt, throttled per type (RW 0x8C2F02)")
{
	CombatWorld w(kFxObjects);
	w.w.fx.env.blocks.registerBlock("DamageFX", [](INI *ini) { DamageFXStore::parseDamageFXDefinitionGlobal(ini); });
	const std::string err = w.w.load(kDamageFX, INI_LOAD_OVERWRITE, "damagefx.ini");
	REQUIRE_MESSAGE(err.empty(), err);
	Recorder rec;
	w.logic->fxEvents().setSink(&rec);
	// the damage source must not fight on its own: a Swordsman's own sword hits (it acquires the target 40 away, lane PHYS-1's melee machine closes in within the
	// throttle window) would add DamageFX events of their own
	Object *a = w.unit("FxSwordsman", 'A', 300, 300);
	Object *t = w.unit("FxTarget", 'B', 340, 300);
	w.frames(2);
	DamageInfo small = hit(a, 10.0f); // NoArmor: 10 dealt, below 30: minor
	t->attemptDamage(small);
	REQUIRE(rec.site("DamageFX").size() == 1);
	CHECK(*rec.site("DamageFX")[0].fxList == "FX_TestMinor");
	CHECK(rec.site("DamageFX")[0].primary == t->getID());
	CHECK(rec.site("DamageFX")[0].secondary == a->getID());
	const unsigned first = w.logic->getFrame();
	// the same type within the 10 throttle frames: nothing
	w.frames(9);
	DamageInfo again = hit(a, 50.0f);
	t->attemptDamage(again);
	CHECK(rec.site("DamageFX").size() == 1);
	CHECK(w.logic->getFrame() == first + 9);
	// at the end of the throttle: the major list
	w.frames(1);
	DamageInfo big = hit(a, 50.0f);
	t->attemptDamage(big);
	REQUIRE(rec.site("DamageFX").size() == 2);
	CHECK(*rec.site("DamageFX")[1].fxList == "FX_TestMajor");
	// an object without a DamageFX in its armour set: nothing
	Object *plain = w.unit("Dummy", 'B', 400, 300);
	DamageInfo h = hit(a, 10.0f);
	plain->attemptDamage(h);
	CHECK(rec.site("DamageFX").size() == 2);
}

TEST_CASE("fx2 events: a sink changes nothing in the logic - the same fight with and without a player gives the same state hash in every frame and the same calls")
{
	Recorder rec;
	unsigned shotsA = 0, shotsB = 0;
	size_t loggedA = 0, loggedB = 0;
	const std::vector<std::uint32_t> withSink = swordFight(&rec, 30, &shotsA, &loggedA);
	const std::vector<std::uint32_t> without = swordFight(nullptr, 30, &shotsB, &loggedB);
	CHECK(withSink == without);
	CHECK(shotsA == shotsB);
	CHECK(loggedA == loggedB);
	CHECK(rec.events.size() == loggedA);
	CHECK(loggedA >= 3);
}

TEST_CASE("fx2 events: FXEventLog drops empty and 'None' names and counts per site")
{
	FXEventLog log;
	Recorder rec;
	log.setSink(&rec);
	static const std::string none = "NONE", empty, name = "FX_A";
	FXEvent e;
	e.site = "T";
	e.fxList = &none;
	log.emit(e);
	e.fxList = &empty;
	log.emit(e);
	e.fxList = nullptr;
	log.emit(e);
	e.fxList = &name;
	log.emit(e);
	log.emit(e);
	CHECK(rec.events.size() == 2);
	CHECK(log.total() == 2);
	CHECK(log.perSite().at("T") == 2);
	CHECK(log.frameEvents().size() == 2);
	log.beginFrame();
	CHECK(log.frameEvents().empty());
	CHECK(log.total() == 2);
}

// ---- retail: the live effect player on a skirmish map --------------------------------------------------------------------------------------------------
namespace
{
struct LiveRun
{
	std::vector<std::uint32_t> hashes;
	LiveFX::Stats stats;
	std::map<std::string, unsigned long long> logicSites;
	size_t peakParticles = 0, peakSystems = 0;
	std::vector<std::string> playbackErrors;
	bool targetDamaged = false;
	std::string probe;
};

// a Gondor trebuchet shelling a Mordor orc pit 450 units away (GondorTrebuchetRock: range 500, MinimumAttackRange 300) for `frames` logic frames; with `withFX` the LiveFX player and FX-1's simulator run on it
LiveRun trebuchetRun(bool withFX, int frames)
{
	hudtest::SharedWorld &s = hudtest::shared();
	LiveRun out;
	hudtest::Rig rig(s);
	std::unique_ptr<FXPlayback> playback;
	std::unique_ptr<LiveFX> fx;
	if (withFX)
	{
		playback = std::make_unique<FXPlayback>(*s.mount->fs, RandomAlgorithm::RotWK_GameDat_LCG);
		out.playbackErrors = playback->loadRetailData();
		fx = std::make_unique<LiveFX>(*rig.game, *playback);
	}
	const auto context = s.world->enterContext();
	const Coord3D base{ 1500.0f, 1500.0f, 0.0f };
	std::string err;
	creeptest::removeCreeps(rig.game->logic()); // lane MOD-4: the map's creeps use the old particle systems of S-684 (InfantryDustTrails)
	Object *treb = rig.game->createObject("GondorTrebuchet", rig.index(), base, 0.0f, &err);
	REQUIRE_MESSAGE(treb != nullptr, err);
	const int enemy = rig.game->players().findPlayerWithName("Player_2")->getPlayerIndex();
	Object *pit = rig.game->createObject("MordorOrcPit", enemy, Coord3D{ base.x + 450.0f, base.y, 0.0f }, 0.0f, &err);
	REQUIRE_MESSAGE(pit != nullptr, err);
	const ObjectID bid = pit->getID();
	const ObjectID trebId = treb->getID();
	rig.game->advance(0.2);
	REQUIRE(treb->getAIUpdateInterface() != nullptr);
	REQUIRE(treb->getAIUpdateInterface()->aiAttackObject(pit, CMD_FROM_PLAYER));
	for (int i = 0; i < frames; ++i)
	{
		if (i == 5)
		{
			// a hit that takes the pit below its damaged threshold at once (the trebuchet alone needs minutes): the damage state change runs the draw's
			// damaged state, its EnteringStateFX and ParticleSysBone fires
			if (Object *b = rig.logic().findObjectByID(bid))
			{
				DamageInfo d;
				d.m_input.m_amount = b->getBodyModule()->getMaxHealth() * 0.6f;
				d.m_input.m_damageType = DAMAGE_UNRESISTABLE;
				b->attemptDamage(d);
			}
		}
		rig.game->advance(0.2); // one logic frame, the drawables' animations and poses
		if (fx)
		{
			fx->flushPending(); // SMOOTH-1: the logic's effect calls are captured at the call and played on the render side (GameWorld does it at idle points)
			fx->updateAttachedSystems();
			for (int k = 0; k < 6; ++k)
			{
				playback->step(); // 30 client frames per second
			}
			out.peakParticles = std::max(out.peakParticles, (size_t)playback->particles().particleCount());
			out.peakSystems = std::max(out.peakSystems, (size_t)playback->particles().systemCount());
		}
		out.hashes.push_back(rig.logic().computeStateHash());
		if (Object *b = rig.logic().findObjectByID(bid))
		{
			out.targetDamaged = out.targetDamaged || b->getBodyModule()->getHealth() < b->getBodyModule()->getMaxHealth();
		}
	}
	out.logicSites = rig.logic().fxEvents().perSite();
	if (Object *t = rig.logic().findObjectByID(trebId))
	{
		out.probe = "treb shots " + std::to_string(t->getWeapons() ? t->getWeapons()->stats().shotsFired : 0) + " launched " + std::to_string(rig.logic().combat().counters().projectilesLaunched) +
			" detonated " + std::to_string(rig.logic().combat().counters().projectilesDetonated) + " ground hits " + std::to_string(rig.logic().combat().counters().projectileGroundHits) +
			" pos " + std::to_string(t->getPosition()->x) + "," + std::to_string(t->getPosition()->y);
	}
	if (fx)
	{
		out.stats = fx->stats();
	}
	return out;
}
} // namespace

TEST_CASE("fx2 retail: a trebuchet shelling an orc pit in a live game plays its fire FX, the boulders' impact FX, the pit's damage FX and fires, and the logic is the same without the player")
{
	if (!hudtest::haveWorld("fx2 retail live FX"))
	{
		return;
	}
	const int frames = 250; // 50 seconds
	const LiveRun live = trebuchetRun(true, frames);
	CHECK(live.playbackErrors.empty());
	MESSAGE("probe: " << live.probe);
	REQUIRE(live.targetDamaged);
	// the logic calls: the trebuchet's FireFX (FX_TrebuchetWeapon) and the boulder projectiles' FXListDie (FX_TrebuchetImpactHit)
	REQUIRE(live.logicSites.count("FireFX"));
	REQUIRE(live.logicSites.count("FXListDie"));
	CHECK(live.stats.played.count("FireFX"));
	CHECK(live.stats.played.at("FXListDie") == live.logicSites.at("FXListDie"));
	CHECK(live.stats.played.at("FireFX") + (live.stats.skipped.count("FireFX without a drawable") ? live.stats.skipped.at("FireFX without a drawable") : 0) == live.logicSites.at("FireFX"));
	// every FXList the retail logic and draw states named is in the retail store
	CHECK_MESSAGE(live.stats.missingFXLists.empty(), (live.stats.missingFXLists.empty() ? std::string() : *live.stats.missingFXLists.begin()));
	CHECK_MESSAGE(live.stats.unresolvedParticleSystems.empty(), (live.stats.unresolvedParticleSystems.empty() ? std::string() : *live.stats.unresolvedParticleSystems.begin()));
	// the damaged pit: its EnteringStateFX (FX_BuildingDamaged) and the ParticleSysBone fires of its damaged state; the boulders' trails
	CHECK(live.stats.played.count("EnteringStateFX"));
	CHECK(live.stats.attachedCreated > 0);
	CHECK(live.peakParticles > 0);
	CHECK(live.peakSystems > 0);
	for (const std::string &b : live.stats.missingBones)
	{
		MESSAGE("fx2 retail: bone not in the model: " << b);
	}
	MESSAGE("fx2 retail: logic FireFX " << live.logicSites.at("FireFX") << ", FXListDie " << live.logicSites.at("FXListDie") << ", attached systems created " << live.stats.attachedCreated
		<< ", peak particles " << live.peakParticles << ", peak systems " << live.peakSystems << ", fire FX at a bone " << live.stats.fireFXAtBone << " / on the object " << live.stats.fireFXOnObject);
	// the client player changes nothing the logic hashes
	const LiveRun bare = trebuchetRun(false, frames);
	CHECK(bare.hashes == live.hashes);
	CHECK(bare.logicSites == live.logicSites);
}

TEST_CASE("fx2 stops: S-680 .. S-686 are reported by the live effect player (S-684 retired by lane FX-3)")
{
	const std::vector<std::string> s = LiveFX::stops();
	REQUIRE(s.size() == 6);
	CHECK(s[0].rfind("[S-680] fire FX:", 0) == 0);
	CHECK(s[1].rfind("[S-681] damage FX:", 0) == 0);
	CHECK(s[2].rfind("[S-683] effects not played:", 0) == 0);
	CHECK(s[3].rfind("[S-685] slow death phases:", 0) == 0);
	CHECK(s[4].rfind("[S-682] live FX:", 0) == 0);
	CHECK(s[5].rfind("[S-686] TransitionDamageFX:", 0) == 0);
	for (const std::string &line : s)
	{
		CHECK(line.find("S-684") == std::string::npos);
	}
	CHECK(FXEventLog::stops().size() == 4);
}

TEST_CASE("fx2 retail: a walking mumakil fires the FXEvent footsteps of its moving animation state (RW 0x4BCE68), client side only")
{
	if (!hudtest::haveWorld("fx2 retail frame events"))
	{
		return;
	}
	hudtest::SharedWorld &s = hudtest::shared();
	hudtest::Rig rig(s);
	FXPlayback playback(*s.mount->fs, RandomAlgorithm::RotWK_GameDat_LCG);
	CHECK(playback.loadRetailData().empty());
	LiveFX fx(*rig.game, playback);
	const auto context = s.world->enterContext();
	std::string err;
	const int enemy = rig.game->players().findPlayerWithName("Player_2")->getPlayerIndex();
	Object *mum = rig.game->createObject("MordorMumakil", enemy, Coord3D{ 1500.0f, 1500.0f, 0.0f }, 0.0f, &err);
	REQUIRE_MESSAGE(mum != nullptr, err);
	rig.game->advance(0.2);
	REQUIRE(mum->getAIUpdateInterface() != nullptr);
	mum->getAIUpdateInterface()->aiMoveToPosition(Coord3D{ 1900.0f, 1500.0f, 0.0f }, CMD_FROM_PLAYER);
	const unsigned long long logicBefore = rig.logic().fxEvents().total();
	for (int i = 0; i < 60; ++i)
	{
		// the render side at 30 Hz: six draws per logic frame, each stepping the animations and the events
		for (int k = 0; k < 6; ++k)
		{
			rig.game->advance(0.2 / 6.0);
			fx.updateAttachedSystems();
			playback.step();
		}
	}
	MESSAGE("mumakil: " << fx.stats().frameEventsFired << " FXEvent entries fired; position " << mum->getPosition()->x);
	CHECK(mum->getPosition()->x > 1550.0f); // it walked
	CHECK(fx.stats().frameEventsFired > 0);
	CHECK(fx.stats().played.count("FXEvent"));
	CHECK(fx.stats().missingFXLists.empty());
	CHECK(rig.logic().fxEvents().total() == logicBefore); // the footsteps are the client's: no logic call
}

// lane INTEG-1 (S-634): XP-1's level grant in a live game. After logic frame 10 a Gondor soldier horde's 49th orc kill grants GoodLevel2 (RW 0x821319) to the
// horde and its members; each grant calls RW 0x8211EC, which the logic hands to the FXEventLog as an OBJECT_FX of site "LevelUpFx" (GoodLevel2: GenericLevelUp2FX,
// no bone: doFXObj on the object, RW 0x4B1B5A), and LiveFX plays every one of them.
TEST_CASE("fx2 retail: a level-up in a live game plays the level's LevelUpFx through LiveFX (RW 0x8213C6 -> 0x8211EC)")
{
	if (!hudtest::haveWorld("fx2 retail level-up FX"))
	{
		return;
	}
	hudtest::SharedWorld &s = hudtest::shared();
	hudtest::Rig rig(s);
	FXPlayback playback(*s.mount->fs, RandomAlgorithm::RotWK_GameDat_LCG);
	CHECK(playback.loadRetailData().empty());
	LiveFX fx(*rig.game, playback);
	const auto context = s.world->enterContext();
	std::string err;
	Object *horde = rig.game->createObject("GondorFighterHorde", rig.index(), Coord3D{ 1500.0f, 1500.0f, 0.0f }, 0.0f, &err);
	REQUIRE_MESSAGE(horde != nullptr, err);
	const int enemy = rig.game->players().findPlayerWithName("Player_2")->getPlayerIndex();
	std::vector<ObjectID> orcs;
	for (int i = 0; i < 49; ++i)
	{
		Object *o = rig.game->createObject("MordorFighter", enemy, Coord3D{ 2500.0f + 10.0f * (float)(i % 7), 2500.0f + 10.0f * (float)(i / 7), 0.0f }, 0.0f, &err);
		REQUIRE_MESSAGE(o != nullptr, err);
		orcs.push_back(o->getID());
	}
	for (int i = 0; i < 12; ++i)
	{
		rig.game->advance(0.2);
	}
	fx.flushPending(); // SMOOTH-1: the effects of the frames so far, before the count
	REQUIRE(rig.logic().getFrame() >= 10);
	const bool hasMembers = horde->getContain() && horde->getContain()->getContainedItemsList() && !horde->getContain()->getContainedItemsList()->empty();
	REQUIRE(hasMembers);
	Object *member = horde->getContain()->getContainedItemsList()->front();
	const unsigned long long before = fx.stats().played.count("LevelUpFx") ? fx.stats().played.at("LevelUpFx") : 0;
	for (ObjectID id : orcs)
	{
		if (Object *o = rig.logic().findObjectByID(id))
		{
			DamageInfo d;
			d.m_input.m_damageType = DAMAGE_UNRESISTABLE;
			d.m_input.m_sourceID = member->getID();
			d.m_input.m_amount = 100000.0f;
			d.m_input.m_kill = true;
			o->attemptDamage(d);
		}
	}
	rig.game->advance(0.2); // the delayed grants (RW 0x821567) run in this frame
	fx.flushPending(); // SMOOTH-1: played on the render side
	const std::map<std::string, unsigned long long> &sites = rig.logic().fxEvents().perSite();
	REQUIRE(sites.count("LevelUpFx"));
	REQUIRE(fx.stats().played.count("LevelUpFx"));
	CHECK(fx.stats().played.at("LevelUpFx") - before == sites.at("LevelUpFx")); // every call played
	CHECK(sites.at("LevelUpFx") >= 16);                                          // the horde and its 15 soldiers
	CHECK(fx.stats().missingFXLists.count("GenericLevelUp2FX") == 0);
	MESSAGE("fx2 retail level-up: " << sites.at("LevelUpFx") << " LevelUpFx calls played");
}

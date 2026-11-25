// OpenBFME. GPL-3.0. PORT-294: ObjectCreationUpgrade's own mux slots — the module that makes an object when an upgrade
// completes. Synthetic INI only: no retail or mod assets.
//
// TARGET FACTS (RotWK 2.01 game.dat, caveat S-001). The class's mux vtable RW 0xC6E250 is the base's (RW 0x8D26BF / 0x8D26F3 / 0x8D278A /
// 0x8D26B3 / 0x5B462D / 0x8D27E8) with four slots of its own:
//  2 wouldUpgrade RW 0x8B82AB: the activation mask must not be empty (0x8B82E0 over its 0x24 words), the conflicting mask must hold none of
//    the mask's bits (RW 0x8097D6, always "the any test": the base's slot 14, RW 0x8D2749, is not called), then the activation mask is held
//    "all" (RW 0x6AACB3) with RequiresAllTriggers (slot 13, RW 0x8B81DC) else "any" (RW 0x8097D6). The base's "already ran" test (RW 0x8D273F
//    of mux + 4) is absent, and postUpgradeCheck depends on that.
//  6 postUpgradeCheck RW 0x8B8329: the mask is the controlling player's completed upgrades (RW 0x68B678, + 0x14C) OR the object's (+ 0x28C,
//    RW 0x68CC6C) — the castle keep's mask of Object::updateUpgradeModules (RW 0x69378C) is not added. wouldUpgrade yes and nothing pending
//    (+ 0x2C): give the upgrade to itself (RW 0x855388). Otherwise the removal runs at once (slot 8, RW 0x8B87C5), pending or not.
//  7 isPermanent RW 0x9188EB answers false: the data's Permanent (the mux base's + 0x136) is parsed and never read.
// 11 / 13 / 14 (RW 0x8B81B5 / 0x8B81DC / 0x8B81E6) and 12 / 15 (RW 0x8B81F0) are the mux base's own functions under the + 8 data layout
//    (the masks at + 0x10 / + 0xA0): the same behaviour, nothing to add here.
// Object::updateUpgradeModules (RW 0x6936FE) asks attemptUpgrade on every mux that did not execute and postUpgradeCheck on every mux
// (0x6937AB), so a module is asked again on every upgrade completion event (RW 0x6AE483, from Player::addUpgrade) and never on a removal
// (RW 0x6AE60C); a test that takes an upgrade away asks the modules itself, as the next completion event does.
// What the module does with its own answer is the update RW 0x8B8464 (ported by lane FACT-C): after Delay frames, an object of ThingToSpawn is
// made for the controlling player at the owner's transform applied to Offset, turned by the owner's angle + Angle, and registered with the
// owner's SlaveWatcherBehavior (RW 0x887AF5), then GrantUpgrade / RemoveUpgrade run by name.

#include "doctest.h"

#include "EconTestUtil.h"

#include "Common/Player.h"
#include "Common/Upgrade.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/SlaveWatcherBehavior.h"
#include "GameLogic/Object/Object.h"

#include <string>

using namespace econtest;

namespace
{
// Port294SpikesBought is what a player buys (a PLAYER upgrade: Player + 0x14C), Port294OwnSpikes an OBJECT one the object carries (Object
// + 0x28C), Port294OtherBought a second player upgrade — any completion event re-asks the modules (RW 0x6AE483).
const char kPort294Objects[] =
	"Upgrade Port294SpikesBought\n"
	"  Type = PLAYER\n"
	"End\n"
	"Upgrade Port294OtherBought\n"
	"  Type = PLAYER\n"
	"End\n"
	"Upgrade Port294ConflictBought\n"
	"  Type = PLAYER\n"
	"End\n"
	"Upgrade Port294SpikesKept\n"
	"  Type = OBJECT\n"
	"End\n"
	"Upgrade Port294OwnSpikes\n"
	"  Type = OBJECT\n"
	"End\n"
	"Object Port294Fortress\n"
	"  KindOf = STRUCTURE SELECTABLE\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000.0\n"
	"  End\n"
	"  Behavior = ObjectCreationUpgrade MakeTheSpikes\n"
	"    TriggeredBy = Port294SpikesBought\n"
	"    ConflictsWith = Port294ConflictBought\n"
	"    Delay = 0.0\n"
	"    ThingToSpawn = Port294Spikes\n"
	"    Offset = X:10.0 Y:0.0 Z:5.0\n"
	"    Angle = 90.0\n"
	"    DestroyWhenSold = Yes\n"
	"    GrantUpgrade = Port294SpikesKept\n"
	"  End\n"
	"  Behavior = SlaveWatcherBehavior WatchTheSpikes\n"
	"  End\n"
	"End\n"
	// the same module with Permanent in its data: the class answers slot 7 with false (RW 0x9188EB), so the removal still runs
	"Object Port294PermanentFortress\n"
	"  KindOf = STRUCTURE SELECTABLE\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000.0\n"
	"  End\n"
	"  Behavior = ObjectCreationUpgrade MakeTheSpikes\n"
	"    TriggeredBy = Port294OwnSpikes\n"
	"    Delay = 0.0\n"
	"    ThingToSpawn = Port294Spikes\n"
	"    DestroyWhenSold = Yes\n"
	"    Permanent = Yes\n"
	"  End\n"
	"  Behavior = SlaveWatcherBehavior WatchTheSpikes\n"
	"  End\n"
	"End\n"
	"Object Port294Spikes\n"
	"  KindOf = STRUCTURE\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100.0\n"
	"  End\n"
	"End\n";

// the objects of one template the logic holds (the update makes one, a bug makes more)
int countOf(GameLogic &logic, const std::string &templateName)
{
	int n = 0;
	for (const Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getTemplate()->getName() == templateName)
		{
			++n;
		}
	}
	return n;
}

// the same, the ones the logic has not killed: a killed object stays in the list until the frame's destroy list runs
int countAliveOf(GameLogic &logic, const std::string &templateName)
{
	int n = 0;
	for (const Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getTemplate()->getName() == templateName && !o->isEffectivelyDead())
		{
			++n;
		}
	}
	return n;
}

// the object the module made: the owner's SlaveWatcherBehavior holds its id (RW 0x887AF5 from RW 0x8B86D6)
Object *spawnedBy(econtest::Fx &f, Object &owner)
{
	SlaveWatcherBehavior *watcher = SlaveWatcherBehavior::of(owner);
	REQUIRE_MESSAGE(watcher, "the owner's SlaveWatcherBehavior");
	return f.logic->findObjectByID(watcher->getSlaveID());
}
} // namespace

TEST_CASE("PORT-294: ObjectCreationUpgrade makes its object at Offset and Angle, once, and keeps the grant while the trigger stays (RW 0x8B8464 / RW 0x8B8329)")
{
	econtest::Fx f(kPort294Objects);
	Object *fortress = f.makeAt("Port294Fortress", "Alice", 100.0f, 100.0f);
	REQUIRE(fortress != nullptr);
	SlaveWatcherBehavior *watcher = SlaveWatcherBehavior::of(*fortress);
	REQUIRE(watcher != nullptr);
	REQUIRE(watcher->getSlaveID() == INVALID_ID);
	Player *alice = f.player("Alice");
	REQUIRE(alice != nullptr);

	// the player buys the trigger: RW 0x6AE483 asks updateUpgradeModules on every object of the player, the mux gives itself the upgrade
	// (RW 0x8D26BF -> its own wouldUpgrade RW 0x8B82AB -> RW 0x855388), upgradeImplementation sets the wait frame (RW 0x8B8281)
	alice->addCompletedUpgrade("Port294SpikesBought");
	f.frames(2); // RW 0x8B847D: the frame must be past Delay (0), so the first frame only arms the wait

	Object *spikes = spawnedBy(f, *fortress);
	REQUIRE_MESSAGE(spikes != nullptr, "one ObjectCreationUpgrade run makes one object");
	CHECK(spikes->getTemplate()->getName() == "Port294Spikes");
	CHECK(spikes->getControllingPlayer() == alice);
	// RW 0x8B8515 .. 0x8B8642: the owner's transform applied to Offset (the identity basis here), then the owner's position: exact sums
	CHECK(spikes->getPosition()->x == 110.0f);
	CHECK(spikes->getPosition()->y == 100.0f);
	CHECK(spikes->getPosition()->z == 5.0f);
	// RW 0x8B8683: the owner's angle (0) + Angle. The field is degrees scaled by the INI parser (RW 0x42EE15 -> RW 0x73A40E, the
	// 0.017453292f of INI::parseAngleReal) and the transform keeps it as given, so the check is the quarter turn.
	// tolerance: the parsed float of "90.0" is the parser's rounding of 90 * 0.017453292f, not 90 * pi / 180 in double
	CHECK(spikes->getOrientation() == doctest::Approx(1.5707964f));
	// RW 0x8B8713: GrantUpgrade by name on the owner
	CHECK(fortress->hasUpgrade("Port294SpikesKept"));
	CHECK(watcher->getSlaveID() == spikes->getID());

	// RW 0x8B82AB has no "already ran" test, so the object answers RW 0x694914 with yes for the upgrade that made the module run: the
	// control bar's upgrade button (RW 0x942733's affectedByUpgrade branch) treats it as applied on this object, where the mux base's
	// RW 0x8D26F3 would have said no
	CHECK(fortress->affectedByUpgrade(Player::resolveUpgrade("Port294SpikesBought", false)));

	// a second completion event while the trigger is still there: postUpgradeCheck RW 0x8B8329 answers yes and nothing is pending, so
	// nothing is made twice and nothing is taken away
	alice->addCompletedUpgrade("Port294OtherBought");
	f.frames(4);
	CHECK(countOf(*f.logic, "Port294Spikes") == 1);
	CHECK_FALSE(spikes->isEffectivelyDead());
	CHECK(fortress->hasUpgrade("Port294SpikesKept"));
	CHECK(watcher->getSlaveID() == spikes->getID());
}

TEST_CASE("PORT-294: a conflicting upgrade — the next upgrade event takes the module's upgrade back and DestroyWhenSold kills the object it made (RW 0x8B8329 -> RW 0x8B87C5)")
{
	econtest::Fx f(kPort294Objects);
	Object *fortress = f.makeAt("Port294Fortress", "Alice", 200.0f, 200.0f);
	REQUIRE(fortress != nullptr);
	Player *alice = f.player("Alice");
	REQUIRE(alice != nullptr);
	alice->addCompletedUpgrade("Port294SpikesBought");
	f.frames(2);
	Object *spikes = spawnedBy(f, *fortress);
	REQUIRE(spikes != nullptr);
	REQUIRE_FALSE(spikes->isEffectivelyDead());
	const ObjectID spikesId = spikes->getID();

	// the trigger is still in the player's mask: 0x8B8329's wouldUpgrade answer stays yes even though the module has run (RW 0x8B82AB does
	// not ask the executed flag of the mux base's RW 0x8D273F), so this event must leave the made object and the grant alone
	alice->addCompletedUpgrade("Port294OtherBought");
	f.frames(2);
	CHECK(fortress->hasUpgrade("Port294SpikesKept"));
	CHECK(countOf(*f.logic, "Port294Spikes") == 1);
	REQUIRE_FALSE(spikes->isEffectivelyDead());

	// an upgrade the module conflicts with arrives: the module has run, so updateUpgradeModules (RW 0x6936FE) no longer asks attemptUpgrade
	// (0x69375F) and only postUpgradeCheck (0x6937AF) sees it. RW 0x8B82AB answers no (RW 0x8B8300 over the conflicting mask), so 0x8B8387
	// runs the removal slot 8 at once (RW 0x8B87C5), pending or not: not executed, not pending, and DestroyWhenSold kills the watcher's
	// slave (RW 0x887AF5 id, RW 0xA0605D read, RW 0x698EC3 with damage type 8)
	alice->addCompletedUpgrade("Port294ConflictBought");
	CHECK(spikes->isEffectivelyDead());

	// and it is armed again: with the conflict gone the same event runs the module a second time (0x8B837A: nothing pending -> RW 0x855388)
	f.frames(1); // the slave's death reaches the SlaveWatcher (RW 0x887A35)
	alice->removeCompletedUpgrade("Port294ConflictBought");
	CHECK_FALSE(alice->hasUpgradeComplete("Port294ConflictBought"));
	fortress->updateUpgradeModules(); // what RW 0x6AE483 does on every object of the player
	f.frames(2);
	Object *again = spawnedBy(f, *fortress);
	REQUIRE(again != nullptr);
	CHECK(again->getID() != spikesId);
	CHECK_FALSE(again->isEffectivelyDead());
	CHECK(countAliveOf(*f.logic, "Port294Spikes") == 1); // the killed one is gone, the new one is the only live one
}

TEST_CASE("PORT-294: the data's Permanent does not hold the removal back — slot 7 answers false (RW 0x9188EB, RW 0x8D2688)")
{
	econtest::Fx f(kPort294Objects);
	Object *fortress = f.makeAt("Port294PermanentFortress", "Alice", 300.0f, 300.0f);
	REQUIRE(fortress != nullptr);
	// an OBJECT upgrade: it lands in the object's own mask (+ 0x28C), which 0x8B835D ORs in
	fortress->giveUpgrade("Port294OwnSpikes");
	f.frames(2);
	Object *spikes = spawnedBy(f, *fortress);
	REQUIRE(spikes != nullptr);
	REQUIRE_FALSE(spikes->isEffectivelyDead());

	// RW 0x691438 -> slot 3 (RW 0x8D278A) -> RW 0x8D2688: the removal runs unless slot 7 answers permanent; the class answers false
	// (RW 0x9188EB) although the data says Permanent = Yes, so DestroyWhenSold still kills the object
	fortress->removeUpgrade(Player::resolveUpgrade("Port294OwnSpikes", true));
	CHECK_FALSE(fortress->hasUpgrade("Port294OwnSpikes"));
	CHECK(spikes->isEffectivelyDead());
}

// OpenBFME retail tests for STEALTH-2 (the abilities that hide objects, in a live arena): the elven cloak of a Mirkwood archer horde (ToggleHiddenSpecialAbility-
// Update: HIDDEN, its countdown paused, the cloak's InvisibilityUpdate on; moving leaves STEALTH and the UNTOGGLE_HIDDEN_WHEN_LEAVING_STEALTH option untoggles it),
// Wormtongue's escape (EffectDuration 15 s = 75 frames, PreventActivationConditions HIDDEN) and Thranduil's Move Unseen (InvisibilitySpecialPower: the allies within
// BroadcastRadius of the location, not the caster nor an enemy, camouflaged for Duration). They share the HUD tests' retail world (the file name sorts with the
// test_hud_* files) and SKIP when ROTWK_INSTALL / BFME2_INSTALL are unset.

#include "doctest.h"
#include "HudTestUtil.h"
#include "StealthArena.h"

#include "Common/Player.h"
#include "Common/SpecialPower.h"
#include "Common/Upgrade.h"
#include "GameClient/ClientEvents.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/GameMessage.h"
#include "Common/StateHash.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/InvisibilityModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Module/StealthAbilityModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/System/InvisibilityManager.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace
{
using hudtest::shared;
using stealthtest::Arena;
using stealthtest::membersOf;

constexpr unsigned kUngated = 0x40000; // the do* option that skips the pause / disabled gate (RW 0x8980A3)

bool haveWorld()
{
	return hudtest::haveWorld("stealth abilities retail");
}

SpecialPowerModule *powerOf(Object &o, const char *templateName)
{
	for (const std::unique_ptr<BehaviorModule> &m : o.modules())
	{
		SpecialPowerModule *sp = dynamic_cast<SpecialPowerModule *>(m.get());
		if (sp && sp->getSpecialPowerTemplate() && sp->getSpecialPowerTemplate()->getName() == templateName)
		{
			return sp;
		}
	}
	return nullptr;
}

InvisibilityUpdate *firstInvisibility(Object &o)
{
	return dynamic_cast<InvisibilityUpdate *>(o.findModule("InvisibilityUpdate"));
}

bool weaponHidden(const Object &o)
{
	const int bit = CombatNames::weaponSetBit("HIDDEN");
	const WeaponSetFlags &f = o.getWeapons()->weaponSetFlags();
	return ((f[(size_t)bit >> 5] >> (bit & 31)) & 1u) != 0;
}

bool ringWeaponSet(const Object &o)
{
	const int bit = CombatNames::weaponSetBit("WEAPONSET_ONE_RING_MODE");
	const WeaponSetFlags &f = o.getWeapons()->weaponSetFlags();
	return ((f[(size_t)bit >> 5] >> (bit & 31)) & 1u) != 0;
}

bool hidden(const Object &o)
{
	return o.testStatus((unsigned)CombatNames::status("HIDDEN"));
}

int runUntil(Arena &a, int limit, const std::function<bool()> &done)
{
	int frames = 0;
	for (; frames < limit && !done(); ++frames)
	{
		a.run(1);
	}
	return frames;
}

unsigned hashOf(Arena &a)
{
	return a.logic.computeStateHash();
}
} // namespace

TEST_CASE("stealth abilities retail: the templates, the binary's bit numbers and the module data")
{
	if (!haveWorld())
	{
		return;
	}
	auto scope = shared().world->enterContext();
	CHECK(CombatNames::status("HIDDEN") == 0x10);
	CHECK(CombatNames::modelCondition("HIDDEN") == 0x103);   // Object + 0x12C bit 3 (RW 0x8B1B1A)
	CHECK(CombatNames::weaponSetBit("HIDDEN") == 0x3D);      // RW 0x691059(0x3D)
	std::vector<std::string> toggles, powers;
	for (const ThingTemplate *t : shared().world->things().templates())
	{
		if (t->getFinalOverride() != t)
		{
			continue;
		}
		for (const ThingTemplate::Nugget &n : t->behaviorModules().nuggets())
		{
			if (dynamic_cast<const ToggleHiddenSpecialAbilityUpdateModuleData *>(n.data.get()))
			{
				toggles.push_back(t->getName());
			}
			if (dynamic_cast<const InvisibilitySpecialPowerModuleData *>(n.data.get()))
			{
				powers.push_back(t->getName());
			}
		}
	}
	std::string names;
	for (const std::string &n : toggles)
	{
		names += " " + n;
	}
	MESSAGE("ToggleHiddenSpecialAbilityUpdate: " << toggles.size() << " templates:" << names);
	names.clear();
	for (const std::string &n : powers)
	{
		names += " " + n;
	}
	MESSAGE("InvisibilitySpecialPower: " << powers.size() << " templates:" << names);
	auto has = [](const std::vector<std::string> &v, const char *n) { return std::find(v.begin(), v.end(), n) != v.end(); };
	CHECK(has(toggles, "ElvenMirkwoodArcherHorde"));
	CHECK(has(toggles, "RohanFrodo"));
	CHECK(has(toggles, "IsengardWormTongue"));
	CHECK(has(toggles, "ElvenThranduil"));
	CHECK(has(powers, "ElvenThranduil"));
	// Thranduil's Move Unseen: BroadcastRadius THRANDUIL_MOVEUNSEEN_EFFECT_RADIUS (50), Duration 30 s, a CAMOUFLAGE nugget that firing forbids
	for (const ThingTemplate::Nugget &n : shared().world->things().findTemplate("ElvenThranduil")->getFinalOverride()->behaviorModules().nuggets())
	{
		if (const auto *d = dynamic_cast<const InvisibilitySpecialPowerModuleData *>(n.data.get()))
		{
			CHECK(d->m_broadcastRadius == 50.0f);
			CHECK(d->m_duration == 150u);
			CHECK(d->m_nugget.invisibilityType == InvisibilityNugget::CAMOUFLAGE);
			CHECK(d->m_nugget.detectionRange == 100.0f);
			CHECK(d->m_nugget.forbiddenConditions == (unsigned)InvisibilityNugget::FIRING_ANY);
		}
	}
	for (const ThingTemplate::Nugget &n : shared().world->things().findTemplate("IsengardWormTongue")->getFinalOverride()->behaviorModules().nuggets())
	{
		if (const auto *d = dynamic_cast<const ToggleHiddenSpecialAbilityUpdateModuleData *>(n.data.get()))
		{
			CHECK(d->m_effectDuration == 75u); // 15000 ms at 5 logic frames per second
			CHECK(d->m_showPalantirTimer);
		}
	}
}

TEST_CASE("stealth abilities retail: the elven cloak hides a Mirkwood archer horde until it moves")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared());
	Object *elves = a.place("ElvenMirkwoodArcherHorde", 0, 600.0f, 600.0f);
	a.run(30);
	ToggleHiddenSpecialAbilityUpdate *th = ToggleHiddenSpecialAbilityUpdate::of(*elves);
	REQUIRE(th != nullptr);
	InvisibilityUpdate *cloak = firstInvisibility(*elves);
	REQUIRE(cloak != nullptr);
	CHECK_FALSE(cloak->isActive()); // StartsActive = No: off until the toggle
	SpecialPowerModule *sp = powerOf(*elves, "SpecialAbilityElfCloak");
	REQUIRE(sp != nullptr);
	CHECK(InvisibilityManager::invisibilityType(*elves) != InvisibilityNugget::STEALTH);
	const unsigned hashBefore = hashOf(a);
	sp->doSpecialPower(0);
	const int toHide = runUntil(a, 60, [&] { return hidden(*elves); });
	MESSAGE("hidden " << toHide << " frames after the cloak");
	REQUIRE(hidden(*elves));
	CHECK(th->hides() == 1u);
	CHECK(th->hideFrame() != 0u);
	CHECK(elves->testModelCondition(CombatNames::modelCondition("HIDDEN")));
	REQUIRE(elves->getWeapons() != nullptr);
	CHECK(weaponHidden(*elves));
	CHECK(cloak->isActive());
	CHECK(sp->pauseCount() == 1); // the button's countdown is paused while hidden
	CHECK(hashOf(a) != hashBefore);
	const int toStealth = runUntil(a, 120, [&] { return InvisibilityManager::invisibilityType(*elves) == InvisibilityNugget::STEALTH; });
	MESSAGE("STEALTH " << toStealth << " frames after the hide");
	REQUIRE(InvisibilityManager::invisibilityType(*elves) == InvisibilityNugget::STEALTH);
	for (Object *m : membersOf(*elves))
	{
		CHECK(InvisibilityManager::invisibilityType(*m) == InvisibilityNugget::STEALTH);
	}
	CHECK(InvisibilityManager::isStealthedAndUndetected(*elves, a.player(1)));
	CHECK(InvisibilityManager::clientLook(*elves, a.player(1)) == 5);
	a.run(90); // standing still keeps it
	CHECK(hidden(*elves));
	CHECK(InvisibilityManager::invisibilityType(*elves) == InvisibilityNugget::STEALTH);
	// moving (a forbidden condition) leaves STEALTH, and UNTOGGLE_HIDDEN_WHEN_LEAVING_STEALTH untoggles the cloak (RW 0x81B3D3)
	REQUIRE(elves->getAIUpdateInterface() != nullptr);
	elves->getAIUpdateInterface()->aiMoveToPosition(Coord3D{ 900.0f, 600.0f, 0.0f }, CMD_FROM_PLAYER);
	const int toReveal = runUntil(a, 120, [&] { return !hidden(*elves); });
	MESSAGE("unhidden " << toReveal << " frames after the move order");
	CHECK_FALSE(hidden(*elves));
	CHECK(th->unhides() == 1u);
	CHECK(InvisibilityManager::invisibilityType(*elves) != InvisibilityNugget::STEALTH);
	CHECK_FALSE(elves->testModelCondition(CombatNames::modelCondition("HIDDEN")));
	CHECK_FALSE(weaponHidden(*elves));
	CHECK_FALSE(cloak->isActive());
	CHECK(sp->pauseCount() == 0);
	CHECK_FALSE(InvisibilityManager::isStealthedAndUndetected(*elves, a.player(1)));
}

TEST_CASE("stealth abilities retail: Wormtongue's escape lasts EffectDuration and cannot be used while hidden")
{
	if (!haveWorld())
	{
		return;
	}
	auto run = [](std::vector<unsigned> &hashes) {
		Arena a(shared());
		Object *w = a.place("IsengardWormTongue", 1, 600.0f, 600.0f);
		a.run(30);
		const SpecialPowerTemplate *escape = TheSpecialPowerStore->findSpecialPowerTemplate("SpecialAbilityWormtongueEscape");
		REQUIRE(escape != nullptr);
		ToggleHiddenSpecialAbilityUpdate *th = ToggleHiddenSpecialAbilityUpdate::of(*w);
		REQUIRE(th != nullptr);
		CHECK(SpecialPowerModules::canUseSpecialPower(*w, escape));
		REQUIRE(SpecialPowerModules::doSpecialPower(*w, escape, 0, false));
		runUntil(a, 60, [&] { return hidden(*w); });
		REQUIRE(hidden(*w));
		const unsigned hideFrame = th->hideFrame();
		CHECK_FALSE(SpecialPowerModules::canUseSpecialPower(*w, escape)); // PreventActivationConditions = HIDDEN
		runUntil(a, 120, [&] { return InvisibilityManager::invisibilityType(*w) == InvisibilityNugget::STEALTH; });
		CHECK(InvisibilityManager::invisibilityType(*w) == InvisibilityNugget::STEALTH);
		CHECK(InvisibilityManager::isStealthedAndUndetected(*w, a.player(0)));
		// moving is allowed (ForbiddenConditions = USING_ABILITY only)
		w->getAIUpdateInterface()->aiMoveToPosition(Coord3D{ 800.0f, 600.0f, 0.0f }, CMD_FROM_PLAYER);
		a.run(30);
		CHECK(hidden(*w));
		CHECK(InvisibilityManager::invisibilityType(*w) == InvisibilityNugget::STEALTH);
		runUntil(a, 500, [&] { return !hidden(*w); });
		const unsigned unhidAt = a.logic.getFrame();
		MESSAGE("hidden at frame " << hideFrame << ", unhidden at " << unhidAt);
		CHECK_FALSE(hidden(*w));
		CHECK(unhidAt >= hideFrame + 75u);
		CHECK(unhidAt <= hideFrame + 77u);
		CHECK_FALSE(firstInvisibility(*w)->isActive());
		// the STEALTH nugget (UpdatePeriod 2 s) lapses after the module stops renewing it
		runUntil(a, 200, [&] { return InvisibilityManager::invisibilityType(*w) != InvisibilityNugget::STEALTH; });
		CHECK(InvisibilityManager::invisibilityType(*w) != InvisibilityNugget::STEALTH);
		CHECK(SpecialPowerModules::canUseSpecialPower(*w, escape));
		hashes.push_back(hashOf(a));
	};
	std::vector<unsigned> hashes;
	run(hashes);
	run(hashes);
	REQUIRE(hashes.size() == 2u);
	CHECK(hashes[0] == hashes[1]);
}

TEST_CASE("stealth abilities retail: Move Unseen camouflages the allies around the location for its Duration, not the caster nor enemies")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared());
	Object *thranduil = a.place("ElvenThranduil", 0, 450.0f, 600.0f);
	Object *friendNear = a.place("GondorFighterHorde", 0, 650.0f, 600.0f);
	Object *friendFar = a.place("GondorFighterHorde", 0, 650.0f, 800.0f);
	a.run(30);
	// the location: the friendly horde's settled position (BroadcastRadius 50 reaches the horde objects by their centre); an enemy horde 30 from it
	const Coord3D loc = *friendNear->getPosition();
	Object *enemyNear = a.place("MordorFighterHorde", 1, loc.x, loc.y + 30.0f);
	a.run(1);
	InvisibilitySpecialPower *mu = dynamic_cast<InvisibilitySpecialPower *>(powerOf(*thranduil, "SpecialAbilityMoveUnseen"));
	REQUIRE(mu != nullptr);
	mu->doSpecialPowerAtLocation(loc, kUngated);
	std::vector<ObjectID> hit = mu->lastAffected();
	MESSAGE("Move Unseen reached " << hit.size() << " objects");
	auto reached = [&](const Object *o) { return std::find(hit.begin(), hit.end(), o->getID()) != hit.end(); };
	CHECK(reached(friendNear));
	CHECK_FALSE(reached(friendFar));
	CHECK_FALSE(reached(enemyNear));
	CHECK_FALSE(reached(thranduil));
	// the enemy horde stood within the nugget's DetectionRange (100): its camouflage detector would reveal the allies; it leaves the field after the cast
	a.logic.destroyObject(enemyNear);
	const int toCamo = runUntil(a, 60, [&] { return InvisibilityManager::invisibilityType(*friendNear) == InvisibilityNugget::CAMOUFLAGE; });
	MESSAGE("camouflaged " << toCamo << " frames after the cast");
	CHECK(InvisibilityManager::invisibilityType(*friendNear) == InvisibilityNugget::CAMOUFLAGE);
	CHECK(InvisibilityManager::invisibilityType(*friendFar) != InvisibilityNugget::CAMOUFLAGE);
	CHECK(InvisibilityManager::isStealthedAndUndetected(*friendNear, a.player(1)));
	// the camouflage lasts Duration (150 frames) after the cast, then lapses
	a.run(120);
	CHECK(InvisibilityManager::invisibilityType(*friendNear) == InvisibilityNugget::CAMOUFLAGE);
	runUntil(a, 100, [&] { return InvisibilityManager::invisibilityType(*friendNear) != InvisibilityNugget::CAMOUFLAGE; });
	CHECK(InvisibilityManager::invisibilityType(*friendNear) != InvisibilityNugget::CAMOUFLAGE);
	MESSAGE("the camouflage lapsed at frame " << a.logic.getFrame());
}

TEST_CASE("stealth abilities retail: the Corrupted Man's disguise (StealthUpdate DisguisesAsTeam): what the enemy sees, and a detector ends it")
{
	if (!haveWorld())
	{
		return;
	}
	auto run = [](std::vector<unsigned> &hashes) {
		std::unique_ptr<ClientEventRecorder> recorder; // declared first: it outlives the arena's objects, which keep a pointer to it
		Arena a(shared());
		recorder = std::make_unique<ClientEventRecorder>(a.logic); // the drawable events (RW 0x776F03's swap goes to the client as REPLACED)
		ClientEventRecorder &client = *recorder;
		a.logic.setClientHooks(&client);
		Object *cah = a.place("CreateAHero", 0, 600.0f, 600.0f);
		const UpgradeTemplate *corrupted = Player::resolveUpgrade("Upgrade_CreateAHero_ClassCorruptedMan", false);
		REQUIRE(corrupted != nullptr);
		cah->giveUpgrade(corrupted); // the class (the StealthUpdate's RequiredUpgradeNames, the class weapons)
		Object *orcs = a.place("MordorFighterHorde", 1, 900.0f, 600.0f);
		a.run(30);
		std::vector<Object *> members = membersOf(*orcs);
		REQUIRE_FALSE(members.empty());
		Object *target = members[0];
		StealthUpdate *st = StealthUpdate::of(*cah);
		REQUIRE(st != nullptr);
		CHECK_FALSE(st->isEnabled()); // DisguisesAsTeam: off until a disguise (RW 0x776EC9)
		CHECK(st->disguisePlayerIndex() == -1);
		SpecialPowerModule *sp = powerOf(*cah, "SpecialAbilityCreateAHeroDisguise");
		REQUIRE(sp != nullptr);
		REQUIRE(sp->getSpecialPowerTemplate()->getSpecialPowerType() == 0x20); // SPECIAL_DISGUISE_AS_VEHICLE
		sp->doSpecialPowerAtObject(target, kUngated);
		const int toStart = runUntil(a, 30, [&] { return st->disguiseTemplate() != nullptr; });
		MESSAGE("the disguise started " << toStart << " frames after the order");
		REQUIRE(st->disguiseTemplate() == target->getTemplate());
		CHECK(st->disguisePlayerIndex() == a.player(1)->getPlayerIndex());
		CHECK(st->isEnabled());
		CHECK(st->disguising());
		// DisguiseTransitionTime 2 s = 10 frames: the look changes halfway (RW 0x777606)
		const unsigned left = st->disguiseTransitionLeft();
		const int toShown = runUntil(a, 20, [&] { return st->disguiseShown(); });
		MESSAGE("the disguise is shown " << toShown << " frames later (" << left << " transition frames were left)");
		CHECK(st->disguiseShown());
		CHECK(2u * st->disguiseTransitionLeft() <= 10u);
		auto lastReplaced = [&]() -> ClientEvent {
			ClientEvent last;
			last.tmpl = nullptr;
			for (const ClientEvent &e : client.take())
			{
				if (e.kind == ClientEvent::REPLACED && e.object == cah->getID())
				{
					last = e;
				}
			}
			return last;
		};
		const ClientEvent look = lastReplaced();
		REQUIRE(look.tmpl == target->getTemplate()); // everyone now draws a Mordor fighter ...
		CHECK(look.hasHouseColor == a.player(1)->hasTeamColor());
		CHECK(look.houseColor == a.player(1)->getPlayerColor()); // ... in Mordor's colour
		runUntil(a, 20, [&] { return cah->testStatus((unsigned)CombatNames::status("STEALTHED")); });
		REQUIRE(cah->testStatus((unsigned)CombatNames::status("STEALTHED")));
		// what the enemy sees: a unit of its own; it neither acquires nor may be ordered at it
		CHECK(InvisibilityManager::isStealthedAndUndetected(*cah, a.player(1)));
		REQUIRE(target->getWeapons() != nullptr);
		CHECK_FALSE(target->getWeapons()->canAttackObject(*cah, CMD_FROM_PLAYER, false));
		a.run(10);
		CHECK(st->disguiseTemplate() != nullptr);
		const unsigned disguisedHash = a.logic.computeStateHash();
		// what breaks it: detection. An enemy skull totem (StealthDetectorUpdate) marks it detected (RW 0x8A66C0 -> 0x7767A9) and the disguise ends (RW 0x7768AF);
		// (RevealDistanceFromTarget 100 does the same when the hero's AI goal is that close: the bare template carries no class weapon to attack with)
		a.place("WildSkullTotem", 1, 650.0f, 600.0f);
		const int toReveal = runUntil(a, 60, [&] { return st->disguiseTemplate() == nullptr; });
		MESSAGE("the reveal began " << toReveal << " frames after the totem was placed");
		REQUIRE(st->disguiseTemplate() == nullptr);
		CHECK(a.logic.computeStateHash() != disguisedHash);
		runUntil(a, 20, [&] { return !st->isEnabled(); });
		CHECK_FALSE(st->disguiseShown());
		CHECK(st->disguisePlayerIndex() == -1);
		const ClientEvent back = lastReplaced();
		CHECK(back.tmpl == cah->getTemplate());
		CHECK(back.houseColor == a.player(0)->getPlayerColor());
		CHECK_FALSE(st->isEnabled());
		CHECK_FALSE(cah->testStatus((unsigned)CombatNames::status("STEALTHED")));
		CHECK_FALSE(InvisibilityManager::isStealthedAndUndetected(*cah, a.player(1)));
		CHECK(target->getWeapons()->canAttackObject(*cah, CMD_FROM_PLAYER, false));
		hashes.push_back(a.logic.computeStateHash());
	};
	std::vector<unsigned> hashes;
	run(hashes);
	run(hashes);
	REQUIRE(hashes.size() == 2u);
	CHECK(hashes[0] == hashes[1]);
}

TEST_CASE("stealth abilities: the One Ring (StealthUpdate ring mode) is a no-op with retail data; with ring times set, MSG_ONE_RING and CancelOneRingEffect")
{
	if (!haveWorld())
	{
		return;
	}
	Arena a(shared());
	a.logic.invisibility().addTree(Coord3D{ 615.0f, 600.0f, 0.0f }); // the Noldor's stealth is forbidden away from trees: the ring's STEALTHED needs it too
	Object *cah = a.place("NoldorWarriorHorde", 0, 600.0f, 600.0f); // an enabled StealthUpdate (a DisguisesAsTeam one sleeps until disguised)
	a.run(5);
	StealthUpdate *st = StealthUpdate::of(*cah);
	REQUIRE(st != nullptr);
	// retail: no StealthUpdate sets RingAnimTimeOn or RingDelayAfterRemoving, so RW 0x776B50 returns at once
	CHECK(st->data()->m_ringAnimTimeOn == 0u);
	CHECK(st->data()->m_ringDelayAfterRemoving == 0u);
	st->toggleRing();
	CHECK_FALSE(st->ringWorn());
	// the binary's ring mode on a copy of the module data with ring times (a code test: no retail object reaches it)
	StealthUpdateModuleData *d = const_cast<StealthUpdateModuleData *>(st->data());
	const StealthUpdateModuleData saved = *d;
	struct Restore
	{
		StealthUpdateModuleData *d;
		StealthUpdateModuleData s;
		~Restore() { *d = s; }
	} restore{ d, saved };
	d->m_ringAnimTimeOn = 5;
	d->m_ringAnimTimeOff = 5;
	d->m_ringDelayAfterRemoving = 20;
	a.player(0)->selection() = { cah->getID() };
	GameLogicDispatch dispatch(a.logic);
	InvisibilityModules::registerHandlers(dispatch);
	dispatch.dispatch(GameMessage(MSG_ONE_RING, a.player(0)->getPlayerIndex()));
	REQUIRE(st->ringWorn());
	CHECK(cah->testModelCondition(CombatNames::modelCondition("ONE_RING")));
	CHECK(cah->getDisabledMask() != DISABLEDMASK_NONE); // held until OneRingDelayOn passes
	CHECK(ringWeaponSet(*cah));
	const int toStealth = runUntil(a, 30, [&] { return cah->testStatus((unsigned)CombatNames::status("STEALTHED")); });
	MESSAGE("STEALTHED " << toStealth << " frames after the ring went on (OneRingDelayOn " << d->m_oneRingDelayOn << "; the horde's forest stealth may already hold)");
	CHECK(InvisibilityManager::isStealthedAndUndetected(*cah, a.player(1)));
	// an enemy battle wagon with the Hearth (StealthDetectorUpdate CancelOneRingEffect) detects it and takes the ring off (RW 0x8A66D4)
	Object *wagon = a.place("DwarvenBattleWagon", 1, 700.0f, 600.0f);
	const UpgradeTemplate *hearth = Player::resolveUpgrade("Upgrade_BattleWagonHearth", false);
	REQUIRE(hearth != nullptr);
	wagon->giveUpgrade(hearth);
	// each detection tick marks it (RW 0x8A66C0: the detection expiry + 0x24 extended) and then takes the ring off (RW 0x776B50: + 0x24 = now + OneRingDelayOff), so
	// while the wagon keeps detecting it the take-off is re-armed; it comes off OneRingDelayOff frames after the last tick
	runUntil(a, 30, [&] { return cah->testStatus((unsigned)CombatNames::status("DETECTED")); });
	CHECK(cah->testStatus((unsigned)CombatNames::status("DETECTED")));
	a.logic.destroyObject(wagon);
	const int toOff = runUntil(a, 60, [&] { return !st->ringWorn(); });
	MESSAGE("the ring came off " << toOff << " frames after the wagon left");
	CHECK_FALSE(st->ringWorn());
	CHECK_FALSE(cah->testModelCondition(CombatNames::modelCondition("ONE_RING")));
	CHECK_FALSE(ringWeaponSet(*cah));
	// RingDelayAfterRemoving: the ring cannot go on again at once
	st->toggleRing();
	CHECK_FALSE(st->ringWorn());
}

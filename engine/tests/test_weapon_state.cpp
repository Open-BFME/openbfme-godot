// OpenBFME unit tests: the live Weapon timing state machine, range checks, FiringTracker and AI wait decisions (GameLogic/WeaponState.h).
// GPL-3.0. Lane WEAPON-1. Expected values: the golden sequences G1..G5 and the golden vectors of tools/weapon/dump_timing_facts.py and
// damage_golden.py (a reference model of the decoded RW routines: no retail oracle runs on this machine, stop S-186), the PLAN rule 2
// ms -> frame conversions, and the retail weapons' real parameters.

#include "doctest.h"
#include "IniTestUtil.h"
#include "RetailTestMount.h"

#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponState.h"
#include "GameLogic/WeaponStores.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

using namespace initest;

namespace
{
std::uint32_t bitsOf(float f)
{
	std::uint32_t u;
	std::memcpy(&u, &f, 4);
	return u;
}

struct StateWorld
{
	Fixture fx;
	WeaponStores stores;
	StateWorld()
	{
		stores.install();
		fx.env.blocks.registerBlock("Weapon", [](INI *ini) { WeaponStore::parseWeaponTemplateDefinitionGlobal(ini); });
	}
	const WeaponTemplate *make(const std::string &body)
	{
		const std::string err = loadError(fx.env, "w.ini", "Weapon W" + std::to_string(counter) + "\n" + body + "End\n");
		REQUIRE_MESSAGE(err.empty(), err);
		return stores.weapons().findWeaponTemplate("W" + std::to_string(counter++));
	}
	int counter = 0;
};

struct TestHost : WeaponHost
{
	std::uint32_t frame = 0;
	GameLogicRandom rng{ RandomAlgorithm::RotWK_GameDat_LCG };
	std::uint32_t bonusMask = 0;
	const WeaponBonusSet *globalSet = nullptr;
	float rofAttr = 1.0f;
	bool hasContainAmmo = false;
	unsigned containCount = 0;
	int barrels = 4;
	int trackerShots = 0;
	bool positionMatches = false;
	std::vector<std::uint32_t> disabledUntil;

	TestHost() { rng.seedRandom(1234); rng.enableCallLog(true); }
	std::uint32_t currentFrame() override { return frame; }
	GameLogicRandom &logicRandom() override { return rng; }
	std::uint32_t weaponBonusConditionMask() override { return bonusMask; }
	const WeaponBonusSet *globalWeaponBonusSet() override { return globalSet; }
	float rateOfFireAttributeProduct() override { return rofAttr; }
	bool containedAmmo(const ObjectFilter &, unsigned &count) override
	{
		count = containCount;
		return hasContainAmmo;
	}
	bool ownerHasContainAmmo(const ObjectFilter &) override { return hasContainAmmo && containCount > 0; }
	int barrelCount(int) override { return barrels; }
	int firingTrackerShotsAtTarget(bool, unsigned, const Coord3D &) override { return trackerShots; }
	bool ownerPositionMatchesLastShot() override { return positionMatches; }
	void setOwnerDisabledUntil(std::uint32_t f) override { disabledUntil.push_back(f); }
	float groundHeightAt(float, float) override { return 5.0f; }
	Coord3D linearTargetPosition(float x, float y) override { return Coord3D{ x + 100.0f, y + 200.0f, 7.0f }; }
};

struct Shots : WeaponDeliverer
{
	struct Shot
	{
		int barrel;
		WeaponShotTarget target;
		bool scattered;
	};
	std::vector<Shot> shots;
	int detonations = 0;
	int assists = 0;
	void fireWeaponTemplate(const WeaponBonus &, int barrel, const WeaponShotTarget &t, bool scattered, const WeaponFireGate &) override { shots.push_back({ barrel, t, scattered }); }
	void fireProjectileDetonation(const WeaponBonus &, const WeaponShotTarget &) override { ++detonations; }
	void requestAssistance(const WeaponShotTarget &) override { ++assists; }
};

Weapon::FireArgs victimShot()
{
	Weapon::FireArgs a;
	a.target.hasVictim = true;
	a.target.victimID = 5;
	a.target.victimPosition = Coord3D{ 10, 0, 0 };
	return a;
}

struct Run
{
	std::vector<std::uint32_t> shotFrames;
	std::vector<int> statusAfter; ///< status after the commands of each frame
};

// the golden-sequence driver: from `from` to `to` inclusive, fire whenever the write-back status is READY
Run simulate(Weapon &w, TestHost &host, Shots &out, std::uint32_t from, std::uint32_t to)
{
	Run r;
	for (std::uint32_t f = from; f <= to; ++f)
	{
		host.frame = f;
		if (w.getStatusWriteBack(host) == WEAPON_READY_TO_FIRE)
		{
			w.privateFireWeapon(host, out, victimShot());
			r.shotFrames.push_back(f);
		}
		r.statusAfter.push_back(w.getStatus(host));
	}
	return r;
}
}

// =============================================================================================================================
// the golden timing sequences (tools/weapon/dump_timing_facts.py --golden)
// =============================================================================================================================
TEST_CASE("Weapon timing G1: ClipSize 3, delay 5 frames, reload 25 -> 24 frames: shots at 0, 5, 10, 34, 39")
{
	StateWorld w;
	const WeaponTemplate *t = w.make("  ClipSize = 3\n  DelayBetweenShots = 1000\n  ClipReloadTime = 5000\n  AutoReloadsClip = YES\n");
	CHECK(t->m_delayBetweenShotsMin == 5);
	CHECK(t->m_clipReloadMin == 25);
	TestHost host;
	Weapon weapon(t, 0, 0);
	Shots out;
	CHECK(weapon.storedStatus() == WEAPON_OUT_OF_AMMO);
	weapon.loadAmmoNow(host);
	const Run r = simulate(weapon, host, out, 0, 40);
	CHECK(r.shotFrames == std::vector<std::uint32_t>({ 0, 5, 10, 34, 39 }));
	for (int f = 0; f <= 9; ++f)
	{
		CHECK(r.statusAfter[(size_t)f] == WEAPON_BETWEEN_FIRING_SHOTS);
	}
	CHECK(r.statusAfter[10] == WEAPON_RELOADING_CLIP); // the clip is empty: the reload starts the same frame
	CHECK(r.statusAfter[33] == WEAPON_RELOADING_CLIP);
	CHECK(r.statusAfter[34] == WEAPON_BETWEEN_FIRING_SHOTS);
	CHECK(out.shots.size() == 5);
	CHECK(host.rng.callLog().empty()); // min == max everywhere: no draw
	// the barrels advance one shot at a time and wrap at the drawable's barrel count
	CHECK(out.shots[0].barrel == 0);
	CHECK(out.shots[1].barrel == 1);
	CHECK(out.shots[3].barrel == 3);
	CHECK(out.shots[4].barrel == 0);
}

TEST_CASE("Weapon timing G2: PER_ATTACK pre-attack once, then shots every 5 frames; FIRING lasts FiringDuration")
{
	StateWorld w;
	const WeaponTemplate *t = w.make("  PreAttackType = PER_ATTACK\n  PreAttackDelay = 500\n  FiringDuration = 400\n  DelayBetweenShots = 1000\n");
	CHECK(t->m_preAttackDelay == 3);
	CHECK(t->m_firingDuration == 2);
	TestHost host;
	Weapon weapon(t, 0, 0);
	Shots out;
	weapon.loadAmmoNow(host);
	CHECK(weapon.ammoInClip() == 0x7FFFFFFFu); // ClipSize 0 = unlimited
	host.frame = 0;
	WeaponShotTarget target;
	target.hasVictim = true;
	target.victimID = 5;
	CHECK(weapon.preFireWeapon(host, target) == 3);
	CHECK(weapon.whenPreAttackFinished() == 3);
	CHECK(weapon.followThruEnd() == 5); // frame + FiringDuration + delay
	host.trackerShots = 1; // later preFire calls at the same target do nothing
	std::vector<int> status;
	std::vector<std::uint32_t> shots;
	for (std::uint32_t f = 0; f <= 13; ++f)
	{
		host.frame = f;
		if (weapon.getStatusWriteBack(host) == WEAPON_READY_TO_FIRE)
		{
			weapon.privateFireWeapon(host, out, victimShot());
			shots.push_back(f);
		}
		status.push_back(weapon.getStatus(host));
	}
	CHECK(shots == std::vector<std::uint32_t>({ 3, 8, 13 }));
	CHECK(std::vector<int>(status.begin(), status.begin() + 3) == std::vector<int>({ 4, 4, 4 }));
	CHECK(status[3] == WEAPON_FIRING);
	CHECK(status[4] == WEAPON_FIRING);
	CHECK(status[5] == WEAPON_BETWEEN_FIRING_SHOTS);
	CHECK(status[7] == WEAPON_BETWEEN_FIRING_SHOTS);
	CHECK(status[8] == WEAPON_FIRING);
	CHECK(status[9] == WEAPON_FIRING);
	CHECK(status[10] == WEAPON_BETWEEN_FIRING_SHOTS);
	// a second preFire at the same target changes nothing but draws no random number either (PreAttackRandomAmount is 0)
	host.frame = 20;
	const std::uint32_t before = weapon.whenPreAttackFinished();
	CHECK(weapon.preFireWeapon(host, target) == 0);
	CHECK(weapon.whenPreAttackFinished() == before);
	CHECK(host.rng.callLog().empty());
}

TEST_CASE("Weapon timing G3: PER_SHOT with a random pre-attack part and a Min/Max reload; the RNG draws are in retail order")
{
	StateWorld w;
	const WeaponTemplate *t = w.make("  PreAttackType = PER_SHOT\n  PreAttackDelay = 500\n  PreAttackRandomAmount = 200\n  ClipSize = 1\n  ClipReloadTime = Min:5000 Max:6000\n");
	CHECK(t->m_preAttackRandomAmount == 1);
	TestHost host;
	Weapon weapon(t, 0, 0);
	Shots out;
	weapon.loadAmmoNow(host);
	host.frame = 0;
	WeaponShotTarget target;
	target.hasVictim = true;
	const int delay = weapon.preFireWeapon(host, target);
	REQUIRE(host.rng.callLog().size() == 1);
	CHECK(host.rng.callLog()[0].line == 3644);
	CHECK(host.rng.callLog()[0].lo == 0);
	CHECK(host.rng.callLog()[0].hi == 1);
	const int jitter = host.rng.callLog()[0].result;
	CHECK(jitter >= 0);
	CHECK(jitter <= 1);
	CHECK(weapon.preAttackJitter() == jitter);
	CHECK(delay == 3 + jitter); // the jitter is added unscaled
	std::uint32_t shotFrame = 0;
	for (std::uint32_t f = 0; f < 20; ++f)
	{
		host.frame = f;
		const int st = weapon.getStatusWriteBack(host);
		if (f < (std::uint32_t)delay)
		{
			CHECK(st == WEAPON_PRE_ATTACK);
		}
		else if (st == WEAPON_READY_TO_FIRE)
		{
			shotFrame = f;
			weapon.privateFireWeapon(host, out, victimShot());
			break;
		}
	}
	CHECK(shotFrame == (std::uint32_t)delay);
	// the second draw is the reload: line 1006, (25, 30), after the shot's own (absent) delay draw
	REQUIRE(host.rng.callLog().size() == 2);
	CHECK(host.rng.callLog()[1].line == 1006);
	CHECK(host.rng.callLog()[1].lo == 25);
	CHECK(host.rng.callLog()[1].hi == 30);
	int reload = host.rng.callLog()[1].result;
	reload -= reload % 3;
	CHECK(weapon.whenWeCanFireAgain() == shotFrame + (std::uint32_t)reload);
	CHECK(weapon.storedStatus() == WEAPON_RELOADING_CLIP);
	CHECK(weapon.ammoInClip() == 1);
}

TEST_CASE("Weapon timing G4: EagleClawAttack-like: ClipSize 2, delay 250 ms, reload 2000 ms -> 9: shots at 0, 2, 11, 13")
{
	StateWorld w;
	const WeaponTemplate *t = w.make("  ClipSize = 2\n  DelayBetweenShots = 250\n  ClipReloadTime = 2000\n  AutoReloadsClip = Yes\n");
	TestHost host;
	Weapon weapon(t, 0, 0);
	Shots out;
	weapon.loadAmmoNow(host);
	const Run r = simulate(weapon, host, out, 0, 14);
	CHECK(r.shotFrames == std::vector<std::uint32_t>({ 0, 2, 11, 13 }));
	CHECK(r.statusAfter[0] == WEAPON_BETWEEN_FIRING_SHOTS);
	CHECK(r.statusAfter[2] == WEAPON_RELOADING_CLIP);
	CHECK(weapon.whenWeCanFireAgain() == 22); // the shot at 13 emptied the clip again: 13 + 9
}

TEST_CASE("Weapon timing G5: getPercentReadyToFire after the first shot of a clip of 3 with a 5 frame delay")
{
	StateWorld w;
	const WeaponTemplate *t = w.make("  ClipSize = 3\n  DelayBetweenShots = 1000\n  ClipReloadTime = 5000\n");
	TestHost host;
	Weapon weapon(t, 0, 0);
	Shots out;
	weapon.loadAmmoNow(host);
	host.frame = 0;
	REQUIRE(weapon.getStatusWriteBack(host) == WEAPON_READY_TO_FIRE);
	CHECK(weapon.getPercentReadyToFire(host) == 1.0f);
	weapon.privateFireWeapon(host, out, victimShot());
	CHECK(weapon.timerStart() == 0);
	CHECK(weapon.whenWeCanFireAgain() == 5);
	const float expected[7] = { 0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f, 1.0f };
	for (std::uint32_t f = 0; f <= 6; ++f)
	{
		host.frame = f;
		const float got = weapon.getPercentReadyToFire(host);
		INFO("frame " << f);
		CHECK(std::fabs(got - expected[f]) < 1e-6f);
	}
	host.frame = 0;
	CHECK(bitsOf(weapon.getPercentReadyToFire(host)) == 0u);
}

// =============================================================================================================================
// the arithmetic
// =============================================================================================================================
TEST_CASE("Weapon timing: bonus-scaled delays (HORDE + NATIONALISM rate of fire 1.75): delay 30 -> 17, clip reload 100 -> 56, pre-attack 15 x 0.9 -> 13")
{
	StateWorld w;
	const WeaponTemplate *t = w.make("  DelayBetweenShots = 150\n  ClipReloadTime = 500\n  PreAttackDelay = 75\n");
	CHECK(t->m_delayBetweenShotsMin == 1);
	// the retail numbers directly on a template
	WeaponTemplate raw;
	raw.m_delayBetweenShotsMin = raw.m_delayBetweenShotsMax = 30;
	raw.m_clipReloadMin = raw.m_clipReloadMax = 100;
	raw.m_preAttackDelay = 15;
	WeaponBonus b;
	b.m_field[WEAPONBONUS_RATE_OF_FIRE] = 1.0f + 0.5f + 0.25f;
	b.m_field[WEAPONBONUS_PRE_ATTACK] = 0.9f;
	GameLogicRandom rng(RandomAlgorithm::RotWK_GameDat_LCG);
	rng.seedRandom(1);
	CHECK(Weapon::getDelayBetweenShots(raw, b, 1.0f, rng) == 17);
	CHECK(Weapon::getDelayBetweenShots(raw, b, 2.0f, rng) == 8); // the attribute product multiplies the rate: 30 / 3.5
	CHECK(Weapon::getClipReloadTime(raw, b, rng) == 56); // 100 - 100 % 3 = 99; 99 / 1.75
	CHECK(Weapon::getPreAttackDelayScaled(raw, b) == 13); // 15 * 0.9f = 13.5 truncated
	WeaponBonus unit;
	CHECK(Weapon::getDelayBetweenShots(raw, unit, 1.0f, rng) == 30);
	CHECK(Weapon::getClipReloadTime(raw, unit, rng) == 99);
	CHECK(rng.callLog().empty());
	rng.enableCallLog(true);
	raw.m_delayBetweenShotsMax = 40;
	raw.m_clipReloadMax = 120;
	const unsigned d = Weapon::getDelayBetweenShots(raw, unit, 1.0f, rng);
	CHECK(d >= 30);
	CHECK(d <= 40);
	const unsigned c = Weapon::getClipReloadTime(raw, unit, rng);
	CHECK(c >= 99);
	CHECK(c <= 120);
	REQUIRE(rng.callLog().size() == 2);
	CHECK(rng.callLog()[0].line == 988);
	CHECK(rng.callLog()[1].line == 1006);
	CHECK(c % 3 == 0);
}

TEST_CASE("Weapon timing: getStatus branches (IdleAfterFiringDelay, stored status, container ammo)")
{
	StateWorld w;
	const WeaponTemplate *plain = w.make("  ClipSize = 1\n  DelayBetweenShots = 1000\n  AutoReloadsClip = NO\n");
	TestHost host;
	Weapon weapon(plain, 0, 0);
	Shots out;
	weapon.loadAmmoNow(host);
	host.frame = 0;
	REQUIRE(weapon.getStatusWriteBack(host) == WEAPON_READY_TO_FIRE);
	CHECK_FALSE(weapon.privateFireWeapon(host, out, victimShot())); // the clip emptied: no auto reload
	CHECK(weapon.storedStatus() == WEAPON_OUT_OF_AMMO);
	CHECK(weapon.whenWeCanFireAgain() == 0x7FFFFFFFu);
	host.frame = 1000;
	CHECK(weapon.getStatus(host) == WEAPON_OUT_OF_AMMO); // stays 1 until something reloads
	weapon.reloadAmmo(host); // no ClipReloadTime: loaded at once
	host.frame = 1000;
	CHECK(weapon.getStatus(host) == WEAPON_READY_TO_FIRE);
	// a fresh weapon reports OUT_OF_AMMO until it is loaded
	Weapon fresh(plain, 0, 0);
	host.frame = 0;
	CHECK(fresh.getStatus(host) == WEAPON_OUT_OF_AMMO);
	// the write-back refuses PRE_ATTACK / FIRING
	const WeaponTemplate *pre = w.make("  PreAttackDelay = 1000\n");
	Weapon pw(pre, 0, 0);
	pw.loadAmmoNow(host);
	host.frame = 0;
	WeaponShotTarget tgt;
	CHECK(pw.preFireWeapon(host, tgt) == 5);
	bool valid = true;
	CHECK(pw.getStatus(host, &valid) == WEAPON_PRE_ATTACK);
	CHECK_FALSE(valid);
	CHECK(pw.getStatusWriteBack(host) == WEAPON_PRE_ATTACK);
	CHECK(pw.storedStatus() == WEAPON_PRE_ATTACK); // stored by preFire itself
	// IdleAfterFiringDelay >= 0 takes the other getStatus branch: the same answers in the same situations here
	const WeaponTemplate *idle = w.make("  ClipSize = 2\n  IdleAfterFiringDelay = 1000\n  DelayBetweenShots = 1000\n");
	Weapon iw(idle, 0, 0);
	iw.loadAmmoNow(host);
	host.frame = 0;
	REQUIRE(iw.getStatusWriteBack(host) == WEAPON_READY_TO_FIRE);
	iw.privateFireWeapon(host, out, victimShot());
	host.frame = 2;
	CHECK(iw.getStatus(host) == WEAPON_BETWEEN_FIRING_SHOTS);
	host.frame = 5;
	CHECK(iw.getStatus(host) == WEAPON_READY_TO_FIRE);
	// ProjectileFilterInContainer: the ammo is the contained count
	const WeaponTemplate *rock = w.make("  ProjectileFilterInContainer = ANY +INFANTRY\n  DelayBetweenShots = 1000\n");
	CHECK(rock->m_projectileFilterInContainer.flag);
	Weapon rw(rock, 0, 0);
	host.hasContainAmmo = true;
	host.containCount = 3;
	host.frame = 0;
	CHECK(rw.getRemainingAmmo(host, false) == 3);
	CHECK(rw.getStatus(host) == WEAPON_READY_TO_FIRE);
	host.containCount = 0;
	CHECK(rw.getRemainingAmmo(host, false) == 0);
	CHECK(rw.getStatus(host) == WEAPON_OUT_OF_AMMO);
}

TEST_CASE("Weapon timing: pre-attack types (PER_CLIP, PER_POSITION), leech range and the follow-through window")
{
	StateWorld w;
	TestHost host;
	WeaponShotTarget tgt;
	{
		const WeaponTemplate *t = w.make("  PreAttackType = PER_CLIP\n  PreAttackDelay = 1000\n  ClipSize = 3\n  DelayBetweenShots = 200\n");
		Weapon wp(t, 0, 0);
		Shots out;
		wp.loadAmmoNow(host);
		host.frame = 0;
		CHECK(wp.preFireWeapon(host, tgt) == 5); // the clip is full
		host.frame = 5;
		REQUIRE(wp.getStatusWriteBack(host) == WEAPON_READY_TO_FIRE);
		wp.privateFireWeapon(host, out, victimShot());
		host.frame = 6;
		CHECK(wp.preFireWeapon(host, tgt) == 0); // no longer full
	}
	{
		const WeaponTemplate *t = w.make("  PreAttackType = PER_POSITION\n  PreAttackDelay = 1000\n");
		Weapon wp(t, 0, 0);
		wp.loadAmmoNow(host);
		host.frame = 0;
		host.positionMatches = true;
		CHECK(wp.preFireWeapon(host, tgt) == 0);
		host.positionMatches = false;
		CHECK(wp.preFireWeapon(host, tgt) == 5);
	}
	{
		const WeaponTemplate *t = w.make("  LeechRangeWeapon = Yes\n  PreAttackDelay = 1000\n  FiringDuration = 400\n");
		Weapon wp(t, 0, 0);
		wp.loadAmmoNow(host);
		host.frame = 10;
		CHECK(wp.preFireWeapon(host, tgt) == 5);
		CHECK(wp.leechRangeDeadline() == 5 + 10 + 2); // (int)(PreAttackDelay * bonus) + frame + FiringDuration
		CHECK(wp.followThruEnd() == 10 + 2 + 5);
	}
	{
		// the rate-of-fire bonus never alters a delay that is already running; the pre-attack bonus scales the delay
		const WeaponTemplate *t = w.make("  PreAttackDelay = 1000\n  WeaponBonus = HORDE PRE_ATTACK 50%\n");
		Weapon wp(t, 0, 0);
		wp.loadAmmoNow(host);
		host.frame = 0;
		host.bonusMask = 1u << 1;
		CHECK(wp.preFireWeapon(host, tgt) == 2); // 5 * 0.5 = 2.5 truncated
		host.bonusMask = 0;
	}
}

TEST_CASE("Weapon timing: scatter targets are drawn without replacement, linear targets cycle, projectile detonation skips both")
{
	StateWorld w;
	TestHost host;
	{
		const WeaponTemplate *t = w.make("  ClipSize = 2\n  DelayBetweenShots = 200\n  ScatterTarget = X:1 Y:2\n  ScatterTarget = X:3 Y:4\n  ScatterTargetScalar = 10\n");
		Weapon wp(t, 0, 0);
		Shots out;
		wp.loadAmmoNow(host);
		REQUIRE(wp.scatterTargetIndices().size() == 2);
		host.frame = 0;
		REQUIRE(wp.getStatusWriteBack(host) == WEAPON_READY_TO_FIRE);
		wp.privateFireWeapon(host, out, victimShot());
		REQUIRE(out.shots.size() == 1);
		CHECK(out.shots[0].scattered);
		CHECK_FALSE(out.shots[0].target.hasVictim);
		const Coord3D p = out.shots[0].target.victimPosition;
		const bool first = p.x == 20.0f && p.y == 20.0f; // victim (10,0) + (1,2) * 10
		const bool second = p.x == 40.0f && p.y == 40.0f;
		CHECK((first || second));
		CHECK(p.z == 5.0f);
		CHECK(wp.scatterTargetIndices().size() == 1);
		REQUIRE(host.rng.callLog().size() >= 1);
		CHECK(host.rng.callLog().back().line == 3261);
		CHECK(host.rng.callLog().back().hi == 1);
		host.frame = 2;
		REQUIRE(wp.getStatusWriteBack(host) == WEAPON_READY_TO_FIRE);
		wp.privateFireWeapon(host, out, victimShot());
		CHECK(wp.scatterTargetIndices().size() == 2); // emptied by the second shot, then rebuilt by the clip reload
	}
	{
		const WeaponTemplate *t = w.make("  LinearTarget = X:1 Y:2 T:0\n  LinearTarget = X:3 Y:4 T:1\n  IgnoreLinearFirstTarget = Yes\n");
		Weapon wp(t, 0, 0);
		Shots out;
		host.frame = 0;
		wp.loadAmmoNow(host);
		REQUIRE(wp.getStatusWriteBack(host) == WEAPON_READY_TO_FIRE);
		wp.privateFireWeapon(host, out, victimShot());
		// entry 0 has T 0 (continue) and is skipped by IgnoreLinearFirstTarget; entry 1 (T 1) fires and stops the loop
		REQUIRE(out.shots.size() == 1);
		CHECK(out.shots[0].target.victimPosition.x == 103.0f);
		CHECK(out.shots[0].target.victimPosition.z == 7.0f);
	}
	{
		const WeaponTemplate *t = w.make("  ScatterTarget = X:1 Y:2\n  RequestAssistRange = 50\n");
		Weapon wp(t, 0, 0);
		Shots out;
		host.frame = 0;
		wp.loadAmmoNow(host);
		Weapon::FireArgs a = victimShot();
		a.isProjectileDetonation = true;
		REQUIRE(wp.getStatusWriteBack(host) == WEAPON_READY_TO_FIRE);
		wp.privateFireWeapon(host, out, a);
		CHECK(out.detonations == 1);
		CHECK(out.shots.empty());
		CHECK(out.assists == 1);
	}
}

TEST_CASE("Weapon timing: reload, loadAmmoNow, setClipPercentFull, HoldDuringReload and the copy rules")
{
	StateWorld w;
	TestHost host;
	const WeaponTemplate *t = w.make("  ClipSize = 10\n  ClipReloadTime = 2000\n  HoldDuringReload = Yes\n  SuspendFXDelay = 1000\n  ShotsPerBarrel = 3\n");
	host.frame = 100;
	Weapon wp(t, 2, host.frame);
	CHECK(wp.suspendFXFrame() == 105);
	CHECK(wp.numShotsForCurBarrel() == 3);
	wp.reloadAmmo(host);
	CHECK(wp.ammoInClip() == 10);
	CHECK(wp.storedStatus() == WEAPON_RELOADING_CLIP);
	CHECK(wp.whenWeCanFireAgain() == 100 + 9); // 10 frames, rounded down to a multiple of 3
	CHECK(host.disabledUntil == std::vector<std::uint32_t>({ 109 }));
	wp.loadAmmoNow(host);
	CHECK(wp.whenWeCanFireAgain() == 100);
	CHECK(host.disabledUntil.size() == 1); // an instant load disables nobody
	// setClipPercentFull: floor(ClipSize * pct)
	wp.setAmmoInClip(2);
	wp.setClipPercentFull(host, 0.55f, false);
	CHECK(wp.ammoInClip() == 5);
	CHECK(wp.timerStart() == 100);
	wp.setClipPercentFull(host, 0.25f, false); // lowering needs allowReduction
	CHECK(wp.ammoInClip() == 5);
	wp.setClipPercentFull(host, 0.25f, true);
	CHECK(wp.ammoInClip() == 2);
	// copy: the template, owner, slot and the suspend-FX frame are copied, the timers and ammo are not
	wp.setOwnerID(77);
	Weapon copy(wp);
	CHECK(copy.getTemplate() == t);
	CHECK(copy.getOwnerID() == 77);
	CHECK(copy.getSlot() == 2);
	CHECK(copy.suspendFXFrame() == 105);
	CHECK(copy.ammoInClip() == 0);
	CHECK(copy.storedStatus() == WEAPON_OUT_OF_AMMO);
	Weapon assigned(t, 0, 0);
	assigned = wp;
	CHECK(assigned.getSlot() == 2);
	CHECK(assigned.ammoInClip() == 0);
	// the pitch limit is derived from the template
	const WeaponTemplate *pitch = w.make("  MinTargetPitch = -45\n");
	CHECK(Weapon(pitch, 0, 0).pitchLimited());
	CHECK_FALSE(Weapon(t, 0, 0).pitchLimited());
}

// =============================================================================================================================
// range and geometry
// =============================================================================================================================
namespace
{
struct RangeHostDouble : WeaponRangeHost
{
	bool sumFound = false;
	float sum = 0.0f;
	float garrisonScale = -1.0f;
	float cap = 1.0e9f;
	bool reach = true;
	float runDown = 0.0f;
	bool flag11A = false;
	int srcLayer = 0, vicLayer = 0;
	bool layerException = false;
	bool rangeAttributeSum(float &s) override
	{
		s = sum;
		return sumFound;
	}
	float garrisonRangeScale() override { return garrisonScale; }
	float garrisonRangeCap(const RangeSubject &) override { return cap; }
	float boxDistanceSquared(const RangeSubject &, const RangeSubject &) override { return 1234.0f; }
	bool meleeReach(const RangeSubject &, const RangeSubject &) override { return reach; }
	float meleeRunDownLimit() override { return runDown; }
	bool victimFlag11A_40() override { return flag11A; }
	int layerOf(bool victim) override { return victim ? vicLayer : srcLayer; }
	bool victimLayerException() override { return layerException; }
};
}

TEST_CASE("Weapon range: the template range, the height bonus and the minimum range (RW 0x6C9F5B / 0x6CA03E)")
{
	WeaponTemplate t;
	t.m_attackRange = 100.0f;
	WeaponBonus b;
	CHECK(WeaponTemplateRangeBase(t, b, 0.0f) == 97.5f); // 100 - the 2.5 undersize
	t.m_rangeBonus = 20.0f;
	t.m_rangeBonusMinHeight = 10.0f;
	t.m_rangeBonusPerFoot = 0.5f;
	CHECK(WeaponTemplateRangeBase(t, b, 0.0f) == 97.5f);       // not high enough above the target
	CHECK(WeaponTemplateRangeBase(t, b, -30.0f) == 127.5f);    // (20 - (10 + -30) * 0.5) + 97.5
	t.m_restrictedHeightRange = 5.0f;
	CHECK(WeaponTemplateRangeBase(t, b, -30.0f) == 0.0f);
	CHECK(WeaponTemplateRangeBase(t, b, 3.0f) == 97.5f);
	b.m_field[WEAPONBONUS_RANGE] = 1.2f;
	t.m_restrictedHeightRange = 0.0f;
	t.m_rangeBonus = 0.0f;
	CHECK(WeaponTemplateRangeBase(t, b, 0.0f) == 1.2f * 100.0f - 2.5f);
	WeaponTemplate m;
	m.m_minimumAttackRange = 20.0f;
	CHECK(WeaponTemplateMinimumRange(m) == 17.5f);
	m.m_minimumAttackRange = 2.0f;
	CHECK(WeaponTemplateMinimumRange(m) == 0.0f);
	// the attribute and garrison scale
	RangeHostDouble host;
	RangeSubject s;
	WeaponTemplate r;
	r.m_attackRange = 102.5f;
	WeaponBonus unit;
	CHECK(WeaponGetAttackRange(r, unit, host, s, 0.0f) == 100.0f);
	host.sumFound = true;
	host.sum = 0.5f;
	CHECK(WeaponGetAttackRange(r, unit, host, s, 0.0f) == 150.0f);
	CHECK(WeaponGetAttackRangeNoTarget(r, unit, host, s) == 150.0f * 1.5f); // the scale is applied twice
	s.insideGarrison = true;
	host.garrisonScale = 2.0f;
	host.cap = 200.0f;
	CHECK(WeaponGetAttackRange(r, unit, host, s, 0.0f) == 200.0f); // 3.0 * 100 capped
	CHECK(WeaponRangeScale(host, s) == 3.0f);
	// the attack distance adds the bounding spheres
	RangeSubject src, vic;
	src.boundingSphereRadius = 3.0f;
	vic.boundingSphereRadius = 4.0f;
	host.sumFound = false;
	host.garrisonScale = -1.0f;
	s.insideGarrison = false;
	CHECK(WeaponGetAttackDistance(r, unit, host, src, &vic, nullptr) == 107.0f);
	Coord3D p{ 0, 0, 0 };
	CHECK(WeaponGetAttackDistance(r, unit, host, src, nullptr, &p) == 100.0f);
	CHECK(WeaponGetAttackDistance(r, unit, host, src, nullptr, nullptr) == 100.0f);
}

TEST_CASE("Weapon range: distances, isTooClose, isWithinAttackRange (RW 0x6634BF, 0x6CA83B, 0x6CC07C)")
{
	CHECK(CircleDistanceSquared(Coord3D{ 0, 0, 0 }, 5.0f, Coord3D{ 30, 0, 0 }, 5.0f) == 400.0f);
	CHECK(CircleDistanceSquared(Coord3D{ 0, 0, 0 }, 20.0f, Coord3D{ 30, 0, 0 }, 20.0f) == 0.0f); // overlapping: zero, not negative
	CHECK(CircleDistanceSquaredToPoint(Coord3D{ 0, 0, 0 }, 5.0f, Coord3D{ 3, 4, 100 }) == 0.0f);
	CHECK(CircleDistanceSquaredToPoint(Coord3D{ 0, 0, 0 }, 1.0f, Coord3D{ 3, 4, 100 }) == 16.0f); // z is ignored
	RangeHostDouble host;
	WeaponTemplate t;
	t.m_attackRange = 100.0f;
	t.m_minimumAttackRange = 20.0f;
	WeaponBonus bonus;
	RangeSubject src, vic;
	vic.position = Coord3D{ 50, 0, 0 };
	CHECK_FALSE(WeaponIsTooClose(t, host, src, vic));
	vic.position = Coord3D{ 10, 0, 0 };
	CHECK(WeaponIsTooClose(t, host, src, vic));
	CHECK(WeaponIsTooCloseToPoint(t, src, Coord3D{ 10, 0, 0 }));
	CHECK_FALSE(WeaponIsTooCloseToPoint(t, src, Coord3D{ 40, 0, 0 }));
	WeaponTemplate noMin;
	noMin.m_attackRange = 100.0f;
	CHECK_FALSE(WeaponIsTooClose(noMin, host, src, vic));
	// within range: the 97.5 undersized range squared against the squared distance
	vic.position = Coord3D{ 97, 0, 0 };
	CHECK(WeaponIsWithinAttackRange(t, bonus, host, src, src.position, &vic, vic.position, 0.0f, true));
	vic.position = Coord3D{ 98, 0, 0 };
	CHECK_FALSE(WeaponIsWithinAttackRange(t, bonus, host, src, src.position, &vic, vic.position, 0.0f, true));
	// too close: false with the minimum range check, true without
	vic.position = Coord3D{ 10, 0, 0 };
	CHECK_FALSE(WeaponIsWithinAttackRange(t, bonus, host, src, src.position, &vic, vic.position, 0.0f, true));
	CHECK(WeaponIsWithinAttackRange(t, bonus, host, src, src.position, &vic, vic.position, 0.0f, false));
	// `extra` enters squared and is added to the squared distance
	vic.position = Coord3D{ 90, 0, 0 };
	CHECK(WeaponIsWithinAttackRange(t, bonus, host, src, src.position, &vic, vic.position, 0.0f, true));
	CHECK_FALSE(WeaponIsWithinAttackRange(t, bonus, host, src, src.position, &vic, vic.position, 40.0f, true)); // 1600 + 8100 > 9506.25
	// a position target: the range loses another 2.5
	CHECK(WeaponIsWithinAttackRange(t, bonus, host, src, src.position, nullptr, Coord3D{ 95, 0, 0 }, 0.0f, true));
	CHECK_FALSE(WeaponIsWithinAttackRange(t, bonus, host, src, src.position, nullptr, Coord3D{ 96, 0, 0 }, 0.0f, true));
	// the goal-position variant: min <= distance and distance <= range
	CHECK(WeaponIsSourceWithGoalPositionWithinAttackRange(t, bonus, host, src, Coord3D{ 0, 0, 0 }, nullptr, Coord3D{ 50, 0, 0 }));
	CHECK_FALSE(WeaponIsSourceWithGoalPositionWithinAttackRange(t, bonus, host, src, Coord3D{ 0, 0, 0 }, nullptr, Coord3D{ 10, 0, 0 }));
	// a BOX pair uses the host's box distance
	RangeSubject box = vic;
	box.isBox = true;
	vic.position = Coord3D{ 1000, 0, 0 };
	box.position = vic.position;
	CHECK(WeaponIsWithinAttackRange(t, bonus, host, src, src.position, &box, box.position, 0.0f, false)); // 1234 <= 9506.25 although far away
}

TEST_CASE("Weapon range: melee weapons (contesting buildings, structures, run-down, layers) and the pitch window")
{
	RangeHostDouble host;
	WeaponTemplate melee;
	melee.m_meleeWeapon = true;
	melee.m_attackRange = 10.0f;
	WeaponBonus bonus;
	RangeSubject src, vic;
	vic.position = Coord3D{ 500, 0, 0 };
	CHECK_FALSE(WeaponIsWithinAttackRange(melee, bonus, host, src, src.position, nullptr, vic.position, 0.0f, true)); // no victim
	src.contestingBuilding = true;
	vic.contestingBuilding = true;
	CHECK(WeaponIsWithinAttackRange(melee, bonus, host, src, src.position, &vic, vic.position, 0.0f, true));
	src.contestingBuilding = false;
	vic.contestingBuilding = false;
	// a plain victim takes the normal distance test
	CHECK_FALSE(WeaponIsWithinAttackRange(melee, bonus, host, src, src.position, &vic, vic.position, 0.0f, true));
	// a structure goes through the partition's melee reach test
	vic.isStructure = true;
	host.reach = true;
	CHECK(WeaponIsWithinAttackRange(melee, bonus, host, src, src.position, &vic, vic.position, 0.0f, true));
	host.reach = false;
	CHECK_FALSE(WeaponIsWithinAttackRange(melee, bonus, host, src, src.position, &vic, vic.position, 0.0f, true));
	host.reach = true;
	host.srcLayer = 1;
	host.vicLayer = 0x11;
	CHECK_FALSE(WeaponIsWithinAttackRange(melee, bonus, host, src, src.position, &vic, vic.position, 0.0f, true));
	host.srcLayer = 0x11;
	host.vicLayer = 1;
	CHECK_FALSE(WeaponIsWithinAttackRange(melee, bonus, host, src, src.position, &vic, vic.position, 0.0f, true));
	host.layerException = true;
	CHECK(WeaponIsWithinAttackRange(melee, bonus, host, src, src.position, &vic, vic.position, 0.0f, true));
	host.reach = false;
	src.runningDownFromBehind = true;
	host.runDown = 600.0f; // within the run-down limit: reach is not even asked
	CHECK(WeaponIsWithinAttackRange(melee, bonus, host, src, src.position, &vic, vic.position, 0.0f, true));
	// the pitch window
	WeaponTemplate pt;
	pt.m_minTargetPitch = -0.5f;
	pt.m_maxTargetPitch = 0.5f;
	CHECK(WeaponIsWithinTargetPitch(pt, false, 0.0f, 100.0f, 9.0f, 9.0f));
	CHECK(WeaponIsWithinTargetPitch(pt, true, 0.0f, 5.0f, 9.0f, 9.0f)); // less than 10 apart
	CHECK_FALSE(WeaponIsWithinTargetPitch(pt, true, 0.0f, 50.0f, 0.9f, 1.2f));
	CHECK(WeaponIsWithinTargetPitch(pt, true, 0.0f, 50.0f, 0.4f, 1.2f));
	CHECK(WeaponIsWithinTargetPitch(pt, true, 0.0f, 50.0f, -1.0f, 1.0f)); // the window straddles the weapon's
}

// =============================================================================================================================
// FiringTracker and the AI decisions
// =============================================================================================================================
namespace
{
struct TrackerHost : FiringTrackerHost
{
	std::uint32_t frame = 0;
	Coord3D pos{};
	std::uint32_t mask = 0;
	int reloads = 0;
	std::uint32_t currentFrame() override { return frame; }
	Coord3D ownerPosition() override { return pos; }
	std::uint32_t weaponBonusConditionMask() override { return mask; }
	void setWeaponBonusConditionMask(std::uint32_t m) override { mask = m; }
	void reloadAllWeapons() override { ++reloads; }
};
}

TEST_CASE("FiringTracker: continuous fire speeds up past ContinuousFireOne / Two and resets; AutoReloadWhenIdle reloads once idle")
{
	StateWorld w;
	const WeaponTemplate *t = w.make("  ContinuousFireOne = 2\n  ContinuousFireTwo = 4\n  ContinuousFireCoast = 1000\n  AutoReloadWhenIdle = 1000\n  DelayBetweenShots = 200\n");
	CHECK(t->m_continuousFireCoastFrames == 5);
	CHECK(t->m_autoReloadWhenIdle == 5);
	TestHost wh;
	Weapon weapon(t, 0, 0);
	weapon.setWhenWeCanFireAgain(3);
	TrackerHost host;
	FiringTracker tracker;
	const std::uint32_t mean = 1u << FiringTracker::CONDITION_CONTINUOUS_FIRE_MEAN;
	const std::uint32_t fast = 1u << FiringTracker::CONDITION_CONTINUOUS_FIRE_FAST;
	for (int shot = 1; shot <= 6; ++shot)
	{
		host.frame = (std::uint32_t)shot;
		tracker.shotFired(host, weapon, 5, Coord3D{ 1, 2, 3 }, false);
		CHECK(tracker.shotCount() == shot);
		if (shot <= 2)
		{
			CHECK(host.mask == 0u);
		}
		else if (shot <= 4)
		{
			CHECK(host.mask == mean); // count > One
		}
		else
		{
			CHECK(host.mask == fast); // count > Two
		}
	}
	CHECK(tracker.getShotsAtTarget(true, 5, Coord3D{}) == 6);
	CHECK(tracker.getShotsAtTarget(true, 6, Coord3D{}) == 0);
	CHECK(tracker.getShotsAtTarget(false, 0, Coord3D{ 1, 2, 3 }) == 0); // the last shot was at an object
	CHECK(tracker.autoReloadDeadline() == 6 + 5);
	CHECK(tracker.coastEnd() == 3 + 5);
	// a position shot switches the mode and restarts the count
	host.frame = 7;
	host.mask = 0;
	tracker.shotFired(host, weapon, 0, Coord3D{ 9, 9, 9 }, false);
	CHECK(tracker.shotCount() == 1);
	CHECK(tracker.getShotsAtTarget(false, 0, Coord3D{ 9, 9, 9 }) == 1);
	// the owner standing still after the coast window: update() resets the continuous fire and reloads when idle
	host.pos = tracker.lastOwnerPosition();
	host.frame = 100;
	host.mask = fast;
	const std::uint32_t sleep = tracker.update(host);
	CHECK(host.mask == 0u);
	CHECK(host.reloads == 1);
	CHECK(tracker.autoReloadDeadline() == 0);
	CHECK(sleep == 0x3FFFFFFFu);
	// a moving owner hard-resets
	host.pos = Coord3D{ 50, 50, 50 };
	tracker.update(host);
	CHECK(tracker.shotCount() == 0);
	(void)wh;
}

TEST_CASE("AI wait decisions: HoldAfterFiringDelay / IdleAfterFiringDelay and the status condition table")
{
	StateWorld w;
	const WeaponTemplate *t = w.make("  HoldAfterFiringDelay = 1000\n  IdleAfterFiringDelay = 600\n");
	WaitEnterResult r = WeaponWaitUntilFinishedFiringEnter(*t, WEAPON_READY_TO_FIRE, false, 10);
	CHECK(r.state == STATE_SUCCESS);
	r = WeaponWaitUntilFinishedFiringEnter(*t, WEAPON_READY_TO_FIRE, true, 10);
	CHECK(r.state == STATE_CONTINUE);
	r = WeaponWaitUntilFinishedFiringEnter(*t, WEAPON_BETWEEN_FIRING_SHOTS, false, 10);
	CHECK(r.state == STATE_CONTINUE);
	CHECK(r.lockWeapon);
	CHECK(r.setDisabled);
	CHECK(r.disabledUntil == 15);
	const WeaponTemplate *plain = w.make("  DelayBetweenShots = 200\n");
	r = WeaponWaitUntilFinishedFiringEnter(*plain, WEAPON_RELOADING_CLIP, false, 10);
	CHECK_FALSE(r.lockWeapon);
	CHECK_FALSE(r.setDisabled);
	// update: still inside the idle window -> continue; FIRING -> continue; afterwards success (no idle delay) or failure (an idle delay)
	CHECK(WeaponWaitUntilFinishedFiringUpdate(*t, WEAPON_READY_TO_FIRE, 10, 12) == STATE_CONTINUE);
	CHECK(WeaponWaitUntilFinishedFiringUpdate(*t, WEAPON_READY_TO_FIRE, 10, 13) == STATE_FAILURE);
	CHECK(WeaponWaitUntilFinishedFiringUpdate(*t, WEAPON_FIRING, 0, 99) == STATE_CONTINUE);
	CHECK(WeaponWaitUntilFinishedFiringUpdate(*plain, WEAPON_READY_TO_FIRE, 10, 99) == STATE_SUCCESS);
	// the status -> condition table {0, 0, 2, 3, 4, 1} and the follow-through rules
	TestHost host;
	Weapon weapon(plain, 0, 0);
	CHECK(WeaponStatusCondition(*plain, weapon, WEAPON_BETWEEN_FIRING_SHOTS, 10, true, false, false) == 2);
	CHECK(WeaponStatusCondition(*plain, weapon, WEAPON_RELOADING_CLIP, 10, true, false, false) == 3);
	CHECK(WeaponStatusCondition(*plain, weapon, WEAPON_PRE_ATTACK, 10, true, false, false) == 4);
	CHECK(WeaponStatusCondition(*plain, weapon, WEAPON_FIRING, 10, true, false, false) == 1);
	CHECK(WeaponStatusCondition(*plain, weapon, WEAPON_OUT_OF_AMMO, 10, true, false, false) == 0);
	CHECK(WeaponStatusCondition(*plain, weapon, WEAPON_BETWEEN_FIRING_SHOTS, 10, false, false, false) == 0); // not attacking
	CHECK(WeaponStatusCondition(*plain, weapon, WEAPON_READY_TO_FIRE, 10, true, true, false) == 2);        // ready, attacking and firing
	CHECK(WeaponStatusCondition(*plain, weapon, WEAPON_READY_TO_FIRE, 10, true, false, false) == 0);
	const WeaponTemplate *hold = w.make("  HoldDuringReload = Yes\n");
	CHECK(WeaponStatusCondition(*hold, weapon, WEAPON_RELOADING_CLIP, 10, false, false, false) == 3);
	(void)host;
}

// =============================================================================================================================
// retail weapons (SKIP when ROTWK_INSTALL / BFME2_INSTALL are unset)
// =============================================================================================================================
TEST_CASE("Weapon timing retail: the timing sequence of the real EagleClawAttack follows from its parsed frames")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("Weapon timing retail");
		return;
	}
	REQUIRE_MESSAGE(mount->error.empty(), mount->error);
	RetailObjectWorld world(*mount->fs);
	std::string error;
	REQUIRE_MESSAGE(world.load(&error), error);
	const WeaponTemplate *t = world.weaponStores().weapons().findWeaponTemplate("EagleClawAttack");
	REQUIRE(t != nullptr);
	CHECK(t->m_clipSize == 2);
	CHECK(t->m_delayBetweenShotsMin == 2);
	CHECK(t->m_delayBetweenShotsMax == 2);
	CHECK(t->m_clipReloadMin == 10);
	CHECK(t->m_autoReloadsClip == AUTO_RELOAD);
	TestHost host;
	Weapon weapon(t, 0, 0);
	Shots out;
	weapon.loadAmmoNow(host);
	const Run r = simulate(weapon, host, out, 0, 14);
	CHECK(r.shotFrames == std::vector<std::uint32_t>({ 0, 2, 11, 13 }));
	// every retail weapon runs through the machine without a draw outside the documented sites
	size_t drawn = 0;
	for (const auto &wt : world.weaponStores().weapons().templates())
	{
		TestHost h;
		Weapon wp(wt.get(), 0, 0);
		Shots o;
		wp.loadAmmoNow(h);
		for (std::uint32_t f = 0; f < 60; ++f)
		{
			h.frame = f;
			if (wp.getStatusWriteBack(h) == WEAPON_READY_TO_FIRE)
			{
				wp.privateFireWeapon(h, o, victimShot());
			}
		}
		for (const auto &c : h.rng.callLog())
		{
			CHECK((c.line == 988 || c.line == 1006 || c.line == 3261));
			++drawn;
		}
	}
	CHECK(drawn > 0);
}

// =============================================================================================================================
// the numeric contract (Sol review): _ftol2 low words, invalid conversions, wide x87 results
// =============================================================================================================================
TEST_CASE("Weapon numerics P1: frame arithmetic wraps like the retail low word of _ftol2 (instant reload at frame 0xC0000000)")
{
	StateWorld w;
	const WeaponTemplate *t = w.make("  ClipSize = 3\n  ClipReloadTime = 5000\n");
	TestHost host;
	Weapon weapon(t, 0, 0);
	host.frame = 0xC0000000u;
	weapon.loadAmmoNow(host); // instant: whenWeCanFireAgain = frame, which no int32 holds
	CHECK(weapon.whenWeCanFireAgain() == 0xC0000000u);
	CHECK(weapon.getStatus(host) == WEAPON_READY_TO_FIRE);
	host.frame = 0xC0000000u;
	weapon.reloadAmmo(host); // 25 -> 24 frames: the fadd rounds the sum to 24 bits (an ulp is 256 up here), so the deadline stays 0xC0000000
	CHECK(weapon.whenWeCanFireAgain() == 0xC0000000u);
	host.frame = 0xC0000000u - 256u;
	weapon.reloadAmmo(host);
	CHECK(weapon.whenWeCanFireAgain() == 0xBFFFFF00u); // exact below 2^31 * 1.5: 0xBFFFFF00 + 24 rounds to 0xBFFFFF00
	// the pre-attack deadline and the follow-through window wrap in unsigned arithmetic
	const WeaponTemplate *pre = w.make("  PreAttackDelay = 1000\n  FiringDuration = 400\n  LeechRangeWeapon = Yes\n");
	Weapon pw(pre, 0, 0);
	host.frame = 0xFFFFFFFEu;
	pw.loadAmmoNow(host);
	WeaponShotTarget tgt;
	CHECK(pw.preFireWeapon(host, tgt) == 5);
	CHECK(pw.whenPreAttackFinished() == 3u); // 0xFFFFFFFE + 5 wraps
	CHECK(pw.followThruEnd() == 0xFFFFFFFEu + 2u + 5u);
}

TEST_CASE("Weapon numerics P1: a zero or invalid rate of fire gives the x87 integer indefinite, not undefined behaviour")
{
	WeaponTemplate raw;
	raw.m_delayBetweenShotsMin = raw.m_delayBetweenShotsMax = 30;
	raw.m_clipReloadMin = raw.m_clipReloadMax = 99;
	GameLogicRandom rng(RandomAlgorithm::RotWK_GameDat_LCG);
	rng.seedRandom(1);
	WeaponBonus zero;
	zero.m_field[WEAPONBONUS_RATE_OF_FIRE] = 0.0f;
	CHECK(Weapon::getDelayBetweenShots(raw, zero, 1.0f, rng) == 0x80000000u); // 30 / 0 = inf: fistp stores 0x80000000
	CHECK(Weapon::getClipReloadTime(raw, zero, rng) == 0x80000000u);
	WeaponBonus nan;
	nan.m_field[WEAPONBONUS_RATE_OF_FIRE] = std::numeric_limits<float>::quiet_NaN();
	CHECK(Weapon::getDelayBetweenShots(raw, nan, 1.0f, rng) == 0x80000000u);
	WeaponBonus unit;
	CHECK(Weapon::getDelayBetweenShots(raw, unit, 0.0f, rng) == 0x80000000u);
	WeaponBonus tiny;
	tiny.m_field[WEAPONBONUS_RATE_OF_FIRE] = 1.0e-30f;
	CHECK(Weapon::getDelayBetweenShots(raw, tiny, 1.0f, rng) == 0x80000000u); // 3e31 does not fit an int32
	// pre-attack: cvttss2si of an out of range product is 0x80000000
	raw.m_preAttackDelay = 15;
	WeaponBonus huge;
	huge.m_field[WEAPONBONUS_PRE_ATTACK] = 1.0e30f;
	CHECK(Weapon::getPreAttackDelayScaled(raw, huge) == INT32_MIN);
	WeaponBonus bad;
	bad.m_field[WEAPONBONUS_PRE_ATTACK] = std::numeric_limits<float>::quiet_NaN();
	CHECK(Weapon::getPreAttackDelayScaled(raw, bad) == INT32_MIN);
	// a barrel counter at the int limits wraps instead of overflowing
	StateWorld w;
	const WeaponTemplate *t = w.make("  ClipSize = 3\n  ShotsPerBarrel = -2147483648\n  DelayBetweenShots = 200\n");
	TestHost host;
	Weapon weapon(t, 0, 0);
	Shots out;
	weapon.loadAmmoNow(host);
	host.frame = 0;
	REQUIRE(weapon.getStatusWriteBack(host) == WEAPON_READY_TO_FIRE);
	weapon.privateFireWeapon(host, out, victimShot()); // --numShots from INT_MIN wraps to INT_MAX
	CHECK(weapon.numShotsForCurBarrel() == 2147483647);
}

TEST_CASE("Weapon numerics P2: the distance chain keeps the x87 exponent (distance and radius both 2^64 give 0, not inf)")
{
	const float big = std::ldexp(1.0f, 64);
	// dx * dx = 2^128 is wider than binary32 but stays in the register; sqrt = 2^64; minus the radius 2^64 = 0; d = 0
	CHECK(CircleDistanceSquared(Coord3D{ big, 0, 0 }, big, Coord3D{ 0, 0, 0 }, 0.0f) == 0.0f);
	CHECK(CircleDistanceSquared(Coord3D{ big, 0, 0 }, 0.0f, Coord3D{ 0, 0, 0 }, big) == 0.0f);
	CHECK(CircleDistanceSquaredToPoint(Coord3D{ big, big, 0 }, 0.0f, Coord3D{ 0, 0, 0 }) == std::numeric_limits<float>::infinity()); // 2^64.5 squared overflows binary32 only at the final mulss
	CHECK(CircleDistanceSquaredToPoint(Coord3D{ big, 0, 0 }, std::ldexp(1.0f, 63), Coord3D{ 0, 0, 0 }) == std::ldexp(1.0f, 126)); // d = 2^63, d * d = 2^126 fits
}

// OpenBFME. GPL-3.0.
//
// CombatState (lane COMBAT-1): the per game state of the combat code that does not belong to an object: the projectile launcher (PROJ-1: the real one makes projectile objects), the
// target finder's cell index, the counters of the run report. GameLogic owns one (GameLogic::combat()).
//
// Determinism: nothing here depends on pointer values or unordered containers; the index keeps insertion / object-list order. The projectiles are objects: their state is hashed
// with the objects.

#pragma once

#include "GameLogic/Combat/TargetFinder.h"
#include "GameLogic/Combat/WeaponDelivery.h"

#include <memory>
#include <string>
#include <cstdint>
#include <functional>
#include <vector>

class ObjectProjectileLauncher;

class GameLogic;
class Object;
class StateHasher;
class Xfer;

class CombatState
{
public:
	explicit CombatState(GameLogic &logic);
	~CombatState();
	CombatState(const CombatState &) = delete;
	CombatState &operator=(const CombatState &) = delete;

	// PROJ-1: a replacement for the real launcher (null restores it). The launcher must outlive the state.
	void setProjectileLauncher(ProjectileLauncher *launcher) { m_launcher = launcher; }
	ProjectileLauncher &launcher();
	bool usesRealLauncher() const { return m_launcher == nullptr; }
	// PROJ-1 / RENDER-2: where the launch bones are (RW 0x6756A1, a query of the launcher's drawable: null = no drawable, the identity bone of RW 0x6CACDA). Not owned,
	// not hashed: it holds no state of its own beyond a cache of values that depend only on the draw data (DrawableLaunchBones).
	void setLaunchOffsets(ProjectileLaunchOffsets *offsets) { m_launchOffsets = offsets; }
	ProjectileLaunchOffsets *launchOffsets() const { return m_launchOffsets; }

	void reset();

	struct Counters
	{
		unsigned long long damageApplications = 0;
		unsigned long long kills = 0;
		unsigned long long bountyPaid = 0;
		unsigned long long projectilesLaunched = 0;     ///< BezierProjectileBehavior launches (PROJ-1)
		unsigned long long launchBonesFound = 0;        ///< launches whose launcher's drawable named a launch bone (RW 0x6756A1 true; RENDER-2)
		unsigned long long garrisonLaunchUnported = 0;  ///< launches from an INSIDE_GARRISON launcher (the container branch of RW 0x6CAB85, stop S-360)
		unsigned long long garrisonLaunches = 0;        ///< lane GARRISON-1: launches that took the ENCLOSED container's drawable and transform (RW 0x6CAC62)
		unsigned long long launchesWithoutBones = 0;    ///< launches with no launch bone provider installed (a bare GameLogic without the W3D assets: the identity bone, S-460)
		unsigned long long projectilesDetonated = 0;
		unsigned long long projectilesLanded = 0;       ///< CrushStyle projectiles that came to rest
		unsigned long long projectileGroundHits = 0;    ///< the end of a path (handleCollision(null))
		unsigned long long projectileBounces = 0;
		unsigned long long projectileFxUnplayed = 0;    ///< GroundHitFX / GroundBounceFX events (client)
		unsigned long long projectileEmotionsUnported = 0;
		unsigned long long unportedNuggets = 0;
		unsigned long long flankIgnored = 0;
		unsigned long long rubbleEntered = 0; // lane COMBAT-2: structures that reached the RUBBLE damage state
		unsigned long long collapsesBegun = 0, collapsesDone = 0;   // StructureCollapseUpdate
		unsigned long long castleMemberHits = 0, castleBreaches = 0; // CastleMemberBehavior's DAMAGE interface (every breach, whoever owns the member)
		unsigned long long respawnWithoutUpdate = 0, delayedDeaths = 0; // RespawnBody / DelayedDeathBody
		unsigned long long collapseEffectsUnported = 0;              // FX lists / OCLs a collapse phase would have fired (the lists are the client's / the OCL lane's)
		// lane HORDE-2 (S-580 .. S-589)
		unsigned long long crushes = 0;              ///< SquishCollide crushes (RW 0x8BFBAE step 9)
		unsigned long long crushWeaponShots = 0;     ///< CrushWeapon / CrushRevengeWeapon fired
		unsigned long long crushBumps = 0;           ///< contacts where canCrush failed (RW 0x696800, lane COMBAT-3)
		unsigned long long ramHits = 0;              ///< lane COMBAT-4: RamPower's 0-damage shockwave hits (RW 0x8BFF08 .. 0x8BFF68 -> the handler RW 0x6968BC)
		unsigned long long crushKnockbacks = 0;      ///< lane COMBAT-3: CrushKnockback flings (RW 0x692223) that threw the victim
		unsigned long long crushBumpAttacksNotPorted = 0; ///< lane COMBAT-3: bumps of an enemy whose contact attack RW 0x6962DB is not ported (S-1600)
		unsigned long long crushDecelerations = 0;   ///< onCrush slowed the crusher's (or its horde's) locomotor
		unsigned long long flanks = 0;               ///< flank tests that answered yes (RW 0x876FC4)
		unsigned long long flankTests = 0;           ///< flank tests asked (RW 0x68FB63)
		// lane COMBAT-4 (S-1600, S-1790)
		unsigned long long metaImpactHits = 0;        ///< MetaImpactNugget shockwave hits handed to Object::attemptDamage (RW 0x91062F -> 0x698E7D)
		unsigned long long metaImpactKills = 0;       ///< victims the nugget's KillObjectFilter killed (RW 0x910695)
		unsigned long long metaImpactNestedHorde = 0; ///< hits on a member of a horde inside another container (RW 0x910764 .. 0x9107AF): NOT ported (S-1790)
		unsigned long long shockwaveFlings = 0;       ///< the shockwave handler threw its object (RW 0x696D95 fling / RW 0x696BB7 flyTo)
		unsigned long long shockwaveStandUps = 0;     ///< the handler's stand-up of a resisting object (RW 0x696A24)
		unsigned long long shockwaveRampageKills = 0; ///< flung objects in AI state 0x2D killed (RW 0x696E17)
		// lane FLANK-1 (S-363): the contacts of flying projectiles (the collide interface of BezierProjectileBehavior, RW vtable 0xC585C8)
		unsigned long long projectileContacts = 0;     ///< contacts met (a candidate whose geometry overlaps the projectile's, RW 0xAD2CE0)
		unsigned long long projectileContactHits = 0;  ///< contacts canCollideWith accepted (RW 0x85EF6A), handed to handleCollision (RW 0x85FDB7)
		unsigned long long projectilePreLandingEmotions = 0; ///< PreLandingEmotion requests (RW 0x85F2DD .. 0x85F3F0 -> RW 0x68F383)
	};
	Counters &counters() { return m_counters; }
	const Counters &counters() const { return m_counters; }
	// lane SHIP-1: an observer of every damaging hit a body applies (ActiveBody::attemptDamage after internalChangeHealth: the victim, the source id, the amount
	// dealt and the health it took). For tests and tools only: no simulation state, never hashed, empty in a game
	using DamageTrace = std::function<void(const Object &victim, std::uint32_t sourceId, float dealt, float taken)>;
	void setDamageTrace(DamageTrace trace) { m_damageTrace = std::move(trace); }
	const DamageTrace &damageTrace() const { return m_damageTrace; }

	// the spatial index of the attackable objects
	TargetFinder &targets() { return m_targets; }

	// scenario switch (default on): the idle mood scan that makes units look for enemies. A movement sweep that crosses enemy lines turns it off; the setting is part of the state hash
	bool autoAcquireEnabled() const { return m_autoAcquire; }
	void setAutoAcquireEnabled(bool on) { m_autoAcquire = on; }

	// the projectile objects that fly: live BezierProjectileBehavior modules with a path, not detonated, on an object that is neither dead nor destroyed (PROJ-1)
	size_t inFlight() const;

	// lane FLANK-1 (S-363 / S-780): RotWK registers every object without KindOf NO_COLLIDE with its collision manager (RW 0x68E31F -> 0xB6C470), and a flying
	// BezierProjectileBehavior answers that manager's contacts through its collide interface (RW vtable 0xC585C8: onCollide RW 0x85E52B = canCollideWith
	// RW 0x85EF6A, then handleCollision RW 0x85FDB7). The port's contact pairs come from the AI units' overlap pass (AIWorld::processCollisions, S-780), which
	// has no projectile, so the projectiles get their own pass in the same row (RW 0x62E93B .. 0x62E94E), after it. A projectile enters the list when it is
	// fired along a path (projectileFireAtObjectOrPosition) and leaves it when its path is gone; the list is derived (not saved, rebuilt after a load from the
	// objects, see processProjectileContacts) and walked in id order.
	void noteFlyingProjectile(std::uint32_t objectId);
	void processProjectileContacts();
	size_t flyingProjectileCount() const { return m_flying.size(); }

	// lane COMBAT-2: a castle member that reached RUBBLE and counts for the EVA "castle breached" (CastleMemberBehavior RW 0x79A0ED). The event exists for EVERY owner (it is simulation
	// state and hashed); delivering the EVA is the client's: it plays it when `ownerPlayerIndex` is its local player
	struct CastleBreach
	{
		unsigned frame = 0;
		std::uint32_t objectId = 0;
		int ownerPlayerIndex = -1;
	};
	void recordCastleBreach(unsigned frame, std::uint32_t objectId, int ownerPlayerIndex)
	{
		++m_counters.castleBreaches;
		m_breaches.push_back(CastleBreach{ frame, objectId, ownerPlayerIndex });
	}
	const std::vector<CastleBreach> &castleBreaches() const { return m_breaches; }
	void crc(StateHasher &hasher) const;
	void xfer(Xfer &x); // lane SAVE-4b: obfme:Combat (OpenBFME native, S-4190)

	// the stop lines that apply to a game (docs/STOPS.md S-320 .. S-339)
	static std::vector<std::string> stops();
	// lane HORDE-2's stop lines (S-580, S-582, S-583, S-584), appended to report()
	static std::vector<std::string> horde2Stops();
	// the lines of the stops this run touched (counted)
	std::vector<std::string> report() const;

private:
	GameLogic &m_logic;
	ProjectileLauncher *m_launcher = nullptr;
	ProjectileLaunchOffsets *m_launchOffsets = nullptr;
	TargetFinder m_targets;
	Counters m_counters;
	DamageTrace m_damageTrace;
	std::vector<CastleBreach> m_breaches;
	bool m_autoAcquire = true;
	std::unique_ptr<ObjectProjectileLauncher> m_real;
	std::vector<std::uint32_t> m_flying;  ///< lane FLANK-1: flying projectile ids, ascending (derived state: not hashed, not saved)
	bool m_flyingValid = true;            ///< false after a load: the next pass rebuilds m_flying from the objects
	std::vector<std::uint32_t> m_flyingScratch, m_contactScratch; ///< the candidates of one projectile (kept between frames: no allocation per frame)
};

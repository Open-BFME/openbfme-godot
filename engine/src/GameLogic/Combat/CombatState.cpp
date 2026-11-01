// OpenBFME. GPL-3.0.
// See GameLogic/Combat/CombatState.h.

#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/ObjectWeapons.h"

#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Combat/ProjectileLauncher.h"
#include "GameLogic/Module/NotifyCrushModules.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/Module/StancesBehavior.h"
#include "GameLogic/Module/ProjectileModules.h"
#include "GameLogic/Module/HordeAIUpdate.h"
#include "GameLogic/Module/BannerCarrierUpdate.h"
#include "GameLogic/Module/SquishCollide.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Contain/HordeFlank.h"
#include "Common/StateHash.h"
#include "GameLogic/SnapshotXfer.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/Weapon.h"

#include <algorithm>

CombatState::CombatState(GameLogic &logic)
	: m_logic(logic)
	, m_targets(logic)
	, m_real(std::make_unique<ObjectProjectileLauncher>())
{
}

CombatState::~CombatState() = default;

ProjectileLauncher &CombatState::launcher()
{
	return m_launcher ? *m_launcher : *m_real;
}

size_t CombatState::inFlight() const
{
	size_t n = 0;
	for (const Object *o = m_logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->isDestroyed() || o->isEffectivelyDead())
		{
			continue;
		}
		for (const std::unique_ptr<BehaviorModule> &m : o->modules())
		{
			const BezierProjectileBehavior *b = dynamic_cast<const BezierProjectileBehavior *>(m.get());
			if (b && !b->flightPath().empty() && !b->hasDetonated())
			{
				++n;
				break;
			}
		}
	}
	return n;
}

// lane FLANK-1: the module of an object whose BezierProjectileBehavior flies (a path, not detonated); null otherwise
namespace
{
BezierProjectileBehavior *flyingBezier(Object &o)
{
	for (const std::unique_ptr<BehaviorModule> &m : o.modules())
	{
		if (BezierProjectileBehavior *b = dynamic_cast<BezierProjectileBehavior *>(m.get()))
		{
			return !b->flightPath().empty() && !b->hasDetonated() ? b : nullptr;
		}
	}
	return nullptr;
}

} // namespace

void CombatState::noteFlyingProjectile(std::uint32_t objectId)
{
	const std::vector<std::uint32_t>::iterator it = std::lower_bound(m_flying.begin(), m_flying.end(), objectId);
	if (it == m_flying.end() || *it != objectId)
	{
		m_flying.insert(it, objectId);
	}
}

// lane FLANK-1 (S-363 / S-780). Retail: RW 0x68E31F registers an object without KindOf NO_COLLIDE (bit 30) with the collision manager (RW 0xB6C470); the manager's
// update (row RW 0x62E93B .. 0x62E94E, after ThePartitionManager's) finds the overlapping pairs (broad phase RW 0xB6D8A0, the test vslot 0x20) and calls onCollide
// both ways (RW 0xB6D060). A BezierProjectileBehavior's collide interface (RW vtable 0xC585C8, slot 0 RW 0x85E52B) asks canCollideWith (RW 0x85EF6A) and hands an
// accepted contact to handleCollision (RW 0x85FDB7; BFME2 decomp BezierProjectileBehaviorSlots.cpp rva0045B4C0 / rva0045BF06, Collide.cpp rva0045CD6E).
// INFERENCE (stop S-780): the pairs of a flying projectile are the objects ThePartitionManager returns within its bounding sphere (distance type 3) whose shapes
// overlap (RW 0xAD2CE0, NotifyCrushModules::geometriesOverlap), met in id order; retail's broad phase and its contact table order were not ported. Lane SIEGE-1 read
// the contact test (vslot 0x20 = RW 0x68C7B2): it runs from the pair's lower ObjectID's side with each object's own GeometryInfo (see the test below). Only the
// projectile's side of a contact is dispatched here: a flying unit (a thrown or leaping infantry template) already meets the AI units through
// AIWorld's pass, and the other object's collide modules are not called for a projectile (a ported module reacting to one: none that changes a 2.01 fight is known).
// HORDE objects (KindOf 109) are not candidates: retail's manager leaves them out and canCollideWith refuses them. The geometry is the pathfinder's (an AIWorld).
void CombatState::processProjectileContacts()
{
	if (!m_logic.aiWorld())
	{
		return;
	}
	if (!m_flyingValid)
	{
		// after a load: the list is what the launches built, i.e. every object whose BezierProjectileBehavior flies (sorted by id, as the launches keep it)
		m_flying.clear();
		for (Object *o = m_logic.getFirstObject(); o; o = o->getNextObject())
		{
			if (!o->isDestroyed() && flyingBezier(*o))
			{
				m_flying.push_back(o->getID());
			}
		}
		std::sort(m_flying.begin(), m_flying.end());
		m_flyingValid = true;
	}
	if (m_flying.empty())
	{
		return;
	}
	static const unsigned kNoCollide = (unsigned)CombatNames::kindOf("NO_COLLIDE"), kHorde = (unsigned)CombatNames::kindOf("HORDE");
	static const unsigned kNoCollisions = (unsigned)CombatNames::statuses().noCollisions;
	m_flyingScratch.assign(m_flying.begin(), m_flying.end()); // a contact can launch or destroy projectiles: walk a copy
	for (std::uint32_t id : m_flyingScratch)
	{
		Object *p = m_logic.findObjectByID(id);
		BezierProjectileBehavior *b = p && !p->isDestroyed() ? flyingBezier(*p) : nullptr;
		if (!b || p->isKindOf(kNoCollide))
		{
			m_flying.erase(std::lower_bound(m_flying.begin(), m_flying.end(), id));
			continue;
		}
		if (p->testStatus(kNoCollisions) || !p->isInWorld())
		{
			continue;
		}
		const std::vector<ObjectGeometry::Shape> &own = p->geometryShapes(); // lane MODCOMPAT-MOD1: the object's own GeometryInfo
		if (own.empty())
		{
			continue; // no shape overlaps anything (RW 0xAD2CE0 walks the shapes)
		}
		PartitionFilterFn candidate([&](Object &o) {
			return &o != p && !o.isDestroyed() && o.isInWorld() && !o.isKindOf(kNoCollide) && !o.isKindOf(kHorde) && !o.testStatus(kNoCollisions) &&
				!o.isOutOfCollisionManager(); // lane MODCOMPAT-MOD1: RW 0xB6C480 removed its record
		});
		const PartitionHits hits =
			m_logic.partition().iterateObjectsInRange(*p->getPosition(), CombatQueries::boundingSphereRadius(*p), FROM_BOUNDINGSPHERE_3D, { &candidate }, ITER_FASTEST);
		m_contactScratch.clear();
		for (const PartitionHit &h : hits)
		{
			m_contactScratch.push_back(h.object->getID());
		}
		std::sort(m_contactScratch.begin(), m_contactScratch.end());
		for (std::uint32_t otherId : m_contactScratch)
		{
			if (p->isDestroyed() || p->testStatus(kNoCollisions) || !flyingBezier(*p))
			{
				break; // detonated, landed or killed by an earlier contact
			}
			Object *o = m_logic.findObjectByID(otherId);
			if (!o || o->isDestroyed())
			{
				continue;
			}
			// lane SIEGE-1: the pair's narrow test runs from its lower ObjectID's side, each object with its own GeometryInfo (RW 0xB6D060: first->test(second's
			// object), the test RW 0x68C7B2 -> 0x68BA2A = RW 0xAD2CE0(first's geometry Object + 0xA8, position, orientation, second's ...); RW 0xB6D730 orders a
			// pair by the id window slot 4 returns, RW 0x670216 = Object + 0x74). RW 0xAD2CE0's height rules are not symmetric (the own shape's centre against
			// the other's top, the own top against the other's base z): a structure made before the stone tests its own shapes against the stone, so the
			// lintel of Helm's Deep's gate arch (offset z 110) does not reach down onto a stone falling on the gate
			const std::vector<ObjectGeometry::Shape> &projShapes = p->geometryShapes(); // the objects' own GeometryInfo (Object + 0xA8, Object::geometryShapes)
			const std::vector<ObjectGeometry::Shape> &otherShapes = o->geometryShapes();
			const bool overlap = p->getID() < o->getID()
				? NotifyCrushModules::geometriesOverlap(projShapes, *p->getPosition(), p->getOrientation(), otherShapes, *o->getPosition(), o->getOrientation())
				: NotifyCrushModules::geometriesOverlap(otherShapes, *o->getPosition(), o->getOrientation(), projShapes, *p->getPosition(), p->getOrientation());
			if (!overlap)
			{
				continue;
			}
			++m_counters.projectileContacts;
			if (b->projectileCanCollideWith(o)) // RW 0x85E52B
			{
				++m_counters.projectileContactHits;
				b->projectileHandleCollision(o);
			}
		}
		if (p->isDestroyed() || !flyingBezier(*p))
		{
			const std::vector<std::uint32_t>::iterator it = std::lower_bound(m_flying.begin(), m_flying.end(), id);
			if (it != m_flying.end() && *it == id)
			{
				m_flying.erase(it);
			}
		}
	}
}

void CombatState::reset()
{
	m_targets.reset();
	m_counters = Counters();
	m_breaches.clear();
	m_autoAcquire = true;
	m_flying.clear();
	m_flyingValid = true;
}

void CombatState::crc(StateHasher &h) const
{
	h.addBool(m_autoAcquire);
	h.addU64(m_counters.damageApplications);
	h.addU64(m_counters.kills);
	h.addU64(m_counters.bountyPaid);
	h.addU64(m_counters.projectilesLaunched);
	h.addU64(m_counters.launchBonesFound);
	h.addU64(m_counters.garrisonLaunchUnported);
	h.addU64(m_counters.garrisonLaunches);
	h.addU64(m_counters.launchesWithoutBones);
	h.addU64(m_counters.projectilesDetonated);
	h.addU64(m_counters.projectilesLanded);
	h.addU64(m_counters.projectileGroundHits);
	h.addU64(m_counters.projectileBounces);
	h.addU64(m_counters.projectileFxUnplayed);
	h.addU64(m_counters.projectileEmotionsUnported);
	h.addU64(m_counters.unportedNuggets);
	h.addU64(m_counters.flankIgnored);
	h.addU64(m_counters.rubbleEntered);
	h.addU64(m_counters.collapsesBegun);
	h.addU64(m_counters.collapsesDone);
	h.addU64(m_counters.collapseEffectsUnported);
	h.addU64(m_counters.respawnWithoutUpdate);
	h.addU64(m_counters.delayedDeaths);
	h.addU64(m_counters.castleMemberHits);
	h.addU64(m_counters.castleBreaches);
	h.addU64(m_counters.crushes);
	h.addU64(m_counters.crushWeaponShots);
	h.addU64(m_counters.crushBumps);
	h.addU64(m_counters.ramHits);
	h.addU64(m_counters.crushKnockbacks);
	h.addU64(m_counters.crushBumpAttacksNotPorted);
	h.addU64(m_counters.crushDecelerations);
	h.addU64(m_counters.flanks);
	h.addU64(m_counters.flankTests);
	h.addU64(m_counters.metaImpactHits); // lane COMBAT-4
	h.addU64(m_counters.metaImpactKills);
	h.addU64(m_counters.metaImpactNestedHorde);
	h.addU64(m_counters.shockwaveFlings);
	h.addU64(m_counters.shockwaveStandUps);
	h.addU64(m_counters.shockwaveRampageKills);
	h.addU64(m_counters.projectileContacts); // lane FLANK-1
	h.addU64(m_counters.projectileContactHits);
	h.addU64(m_counters.projectilePreLandingEmotions);
	h.addU32((std::uint32_t)m_breaches.size());
	for (const CastleBreach &b : m_breaches)
	{
		h.addU32(b.frame);
		h.addU32(b.objectId);
		h.addI32(b.ownerPlayerIndex);
	}
}

std::vector<std::string> CombatState::stops()
{
	return {
		"[S-320] weapon sets: the object's weapon set is the best WeaponSet block for its WeaponSet conditions (RW 0x73D89F) with a Weapon per slot (WEAPON-1 timing code); the RATE_OF_FIRE / RANGE attribute "
		"modifiers, GameData's global WeaponBonus set, the drawable's barrel count (1 barrel), the garrison range scale, the box-to-box distance of RW 0x68F430 (a box counts as its bounding circle), the "
		"target pitch test and the FiringTrackerHelper shell (the tracker runs from the AI update; the WeaponStatusHelper is ported, lane PROJ-2) are not available; a weapon that needs them answers as "
		"if they were neutral",
		"[S-321] delivery: DamageNugget is delivered (radius damage scans the object list in list order, RW's partition order is implementation defined); a weapon with a ProjectileNugget launches a projectile "
		"object through ProjectileLauncher (lane PROJ-1, stops S-360..S-364); MetaImpactNugget throws through the shockwave handler (lane COMBAT-4, S-1600 / S-1790); DOTNugget hits and registers its damage over time with the DOTManager (lane DECOMP-1, S-1959); AttributeModifierNugget adds its ModifierList, disables its AntiCategories and plays its AntiFX (lane DECOMP-1: RW 0x90EAF9, the arc's fcos is S-167's cosd); ParalyzeNugget disables its victims (lane DECOMP-1: RW 0x90F177); WeaponOCLNugget runs its ObjectCreationList (lane FACT-A: RW 0x90F858 / 0x90F887); lane NUGGET-1 delivers StealMoney (RW 0x9118B1), OpenGate (RW 0x9115BB / 0x91161F; a GateProxyBehavior victim is not opened: S-1940), EmotionWeapon (RW 0x911770 / 0x9117A7), LuaEvent (RW 0x911EDC, through the live game's script engine; none attached: counted, S-1220), SpawnAndFade (RW 0x911C46), SpecialModelCondition (RW 0x90EF46), Grab (RW 0x910C8A; the passenger's fling is S-863), DamageField (RW 0x90F5F3; the victim form needs the weapon's PreferredTargetBone, retail reads a null source otherwise: reported) and DamageContained (RW 0x91117C); SlaveAttack orders the expansion's slave to shoot (lane MODCOMPAT-MOD1: RW 0x910DCF / 0x910EA1 / 0x910F78); FireLogic (TheFireLogicSystem is not ported, S-191) and the passenger hit roll are counted and not executed",
		"[S-322] ActiveBody: armour is the ArmorSet of the object's armor flags looked up per hit (RW 0x5D893C; the flank test RW 0x68FB63 is lane HORDE-2's, S-582); the ARMOR / INVULNERABLE attribute modifiers, the burning death "
		"fire cap, doDamageFX and the damage modules' onDamage are not ported (the damage state model conditions are COMBAT-2's, S-342); ImmortalBody / HighlanderBody / ActiveBody share the data table 0xC71D68, "
		"StructureBody (an extra EMPTY table 0xC84858), InactiveBody, RespawnBody, DelayedDeathBody and SymbioticStructuresBody run since COMBAT-2 (S-340, S-343)",
		"[S-323] kill credit: Object::scoreTheKill (RW 0x6955BC) skips a victim with KindOf IGNORED_IN_GUI (0x695661), requires an enemy of another owner (0x6956E8..0x6956F7) and then pays the bounty through Economy::awardBounty (call 0x695743 -> 0x6AC06F); the victim's playable-side test (0x69574F) comes after the bounty in RW and only gates skill points, so it is not applied to the bounty; the score keeper, the skill points, the experience tracker, the academy statistics and the EVA of a lost unit are other lanes'",
		"[S-324] death: SlowDeathBehavior is RotWK's (lane COMBAT-4): the roulette RW 0x861712 (GameLogicRandomValue(0, total - 1) at line 0x32F, the overkill probability RW 0x860608), beginSlowDeath RW 0x860E93 (DeathFlags' statuses and model conditions with DYING, the sink / destruction / midpoint draws at lines 0x1A5 / 0x1A6 / 0x1AB, DoNotRandomizeMidpoint, DecayBeginTime, the FadeDelay frame, FlingForce: the body thrown by RW 0x860664, EXPLODED_FLAILING, its timers held while it flies) and the update RW 0x860B39 (EXPLODED_BOUNCING on landing, the HIT_GROUND phase, SINKING, DISABLED_HELD, z - SinkRate and 5.7 more above the terrain, MIDPOINT, FINAL and the destruction, DECAY); the phase OCL runs (lane SPELL-2: RW 0x860A46, S-530); the Weapon phase effect fires the temporary weapon (lane DECOMP-1, RW 0x860A8C); NOT ported: the drawable's fade and shadow (client), the LOD death scale (1.0 at every 2.01 GameLOD level) and its rescale, the HULK quick death (needs the script hulk lifetime override, never set), a slaved update's notice on a fling; DestroyDie and KeepObjectDie act; FXListDie, CreateObjectDie and every other die module have no runtime",
		"[S-325] attack machine: AIAttackState and its sub machine (Pursue 0x64, Approach 0x65, Aim 0x66, Fire 0x67, WaitUntilFinishedFiring 0x68) are the B1 ids and transitions (RW's own ids were not read); "
		"the transition conditions (weapon range test, victim death) are inference; no turrets, a stealthed and undetected victim ends the attack state (lane STEALTH-1, inference of the RW sub-state), no garrison fire points, no combo locomotors, no attack position / area / squad, no retaliation"
		"; lane SPELL-3 (review r3): the Aim and Fire states of an attack on an object carry RotWK's cannotPossiblyAttackObject (RW 0x741798, the B1 condition lists): an owner Object::isAbleToAttack refuses (NO_ATTACK, the condition 0x81, UNDER_CONSTRUCTION, SOLD) leaves the machine with failure, an ordered attack included; its getAbleToAttackSpecificObject half (RW 0x68D6A6 with the AI's last command source) and the B1 conditions to Pursue are not ported, and the horde and melee machines have no conditions",
		"[S-326] acquisition (lane TARGET-1): RotWK's mood scan getNextMoodTarget (RW 0x66844A) with its cadence (AI + 0x21C, the one-off random offset AI + 0x3BC, "
		"resetNextMoodCheckTime RW 0x662DD9 on idle entry) and the target search AI::findClosestEnemy (RW 0x7006D7: the partition filters, the AttackPriority table "
		"score with the combat chain, pathability, the horde adjustment RW 0x68D30E, the line of sight filter RW 0x6616AC: lane LOS-1) run for the idle, attack-move, guard, "
		"hunt, turret and giant bird scans; not ported: the friendly-fire crowd query (distance type 4, S-1020), the player's stop flag AI + 0x3CC "
		"and its retaliation branch, the firing tracker's take-once target (Object + 0x248 + 0x38), the contained structure's apparent player, the attack-moving "
		"skirmish AI filter (state 0x21, S-328), the script attack priority actions and the team prototype's priority override (S-1180); the melee re-acquire (state 0xE5) still uses the port's TargetFinder",
		"[S-327] horde combat: the horde object runs the melee approach and wait states (B1 ids 0xC8..0xCC) and its members attack through their own attack machine; HordeMeleeAmoeba's cell search is "
		"approximated by every member attacking the nearest enemy member in reach or walking toward it; the readiness rule, RanksToReleaseWhenAttacking, back-up and the banner carrier are not ported (the crush is lane HORDE-2's: S-580, S-583)",
		"[S-328] attack commands: MSG_DO_ATTACK_OBJECT, MSG_DO_FORCE_ATTACK_OBJECT and MSG_DO_ATTACKMOVETO (as attack-move: the group attacks what it meets, then continues) reach the group of the "
		"selection; attack ground, guard, hunt and the weapon fire commands are not executed",
	};
}

std::vector<std::string> CombatState::horde2Stops()
{
	return { ObjectCrush::stopLine(), HordeFlank::stopLine(), HordeAIUpdate::squishStopLine(), HordeContain::reformStopLine(), BannerCarrierUpdate::stopLine(), HordeAIUpdate::amoebaStopLine(), HordeContain::formationStopLine(), HordeAIUpdate::approachStopLine(),
		StancesBehavior::stopLines()[0], ObjectCrush::combat3StopLines()[0], ObjectCrush::combat3StopLines()[1] }; // lane INTEG-1: S-585; lane COMBAT-3: S-1600 / S-1601
}

std::vector<std::string> CombatState::report() const
{
	std::vector<std::string> out = stops();
	out.push_back(ObjectWeapons::choiceStopLine()); // lane DECOMP-1 (S-1582)
	for (const std::string &s : ProjectileModules::stops())
	{
		out.push_back(s); // lane PROJ-1 (S-360 ..)
	}
	for (const std::string &s : horde2Stops())
	{
		out.push_back(s); // lane HORDE-2 (S-580 ..)
	}
	out.push_back(HordeContain::attackStopLine()); // lane ARCHER-1 (S-2610)
	out.push_back("[S-320..S-328 counters] damage applications " + std::to_string(m_counters.damageApplications) + ", kills " + std::to_string(m_counters.kills) + ", bounty paid " +
		std::to_string(m_counters.bountyPaid) + ", projectiles launched " + std::to_string(m_counters.projectilesLaunched) + " detonated " + std::to_string(m_counters.projectilesDetonated) + " landed " +
		std::to_string(m_counters.projectilesLanded) + " ground hits " + std::to_string(m_counters.projectileGroundHits) + " bounces " + std::to_string(m_counters.projectileBounces) +
		" fx unplayed " + std::to_string(m_counters.projectileFxUnplayed) + " launch bones " + std::to_string(m_counters.launchBonesFound) +
		" garrison launches unported " + std::to_string(m_counters.garrisonLaunchUnported) + " garrison launches " + std::to_string(m_counters.garrisonLaunches) + " launches without bones " + std::to_string(m_counters.launchesWithoutBones) + " emotions unported " + std::to_string(m_counters.projectileEmotionsUnported) +
		" pre-landing emotions " + std::to_string(m_counters.projectilePreLandingEmotions) + " contacts " + std::to_string(m_counters.projectileContacts) + " contact hits " +
		std::to_string(m_counters.projectileContactHits) +
		", unported nuggets " + std::to_string(m_counters.unportedNuggets) + ", flank tests ignored " + std::to_string(m_counters.flankIgnored));
	out.push_back("[S-580..S-584 counters] crushes " + std::to_string(m_counters.crushes) + ", crush weapon shots " + std::to_string(m_counters.crushWeaponShots) + ", bumps " +
		std::to_string(m_counters.crushBumps) + " (contact attacks not ported " + std::to_string(m_counters.crushBumpAttacksNotPorted) + "), knockbacks " +
		std::to_string(m_counters.crushKnockbacks) + ", ram hits " + std::to_string(m_counters.ramHits) + ", decelerations " + std::to_string(m_counters.crushDecelerations) +
		", flank tests " + std::to_string(m_counters.flankTests) + " flanked " + std::to_string(m_counters.flanks));
	out.push_back("[S-1600 / S-1790 counters] meta impact hits " + std::to_string(m_counters.metaImpactHits) + ", kill filter kills " + std::to_string(m_counters.metaImpactKills) +
		", nested horde hits not ported " + std::to_string(m_counters.metaImpactNestedHorde) + ", shockwave flings " + std::to_string(m_counters.shockwaveFlings) + ", stand-ups " +
		std::to_string(m_counters.shockwaveStandUps) + ", rampage kills " + std::to_string(m_counters.shockwaveRampageKills));
	return out;
}

// ---- lane SAVE-4b: obfme:Combat (OpenBFME native, S-4190: no retail counterpart; the counters are hashed state of the port) --------------------------
// The target finder's cell index is a cache: rebuilt from the objects at its next query (TargetFinder::reset), never saved.
void CombatState::xfer(Xfer &x)
{
	// version 2 (lane FLANK-1): the projectile contact and pre-landing emotion counters
	XferVersion v{ 1, 2 };
	x.xferVersion(v);
	const int version = v.current;
	x.xferBool(&m_autoAcquire);
	for (unsigned long long *c : { &m_counters.damageApplications, &m_counters.kills, &m_counters.bountyPaid, &m_counters.projectilesLaunched,
			 &m_counters.launchBonesFound, &m_counters.garrisonLaunchUnported, &m_counters.garrisonLaunches, &m_counters.launchesWithoutBones,
			 &m_counters.projectilesDetonated, &m_counters.projectilesLanded, &m_counters.projectileGroundHits, &m_counters.projectileBounces,
			 &m_counters.projectileFxUnplayed, &m_counters.projectileEmotionsUnported, &m_counters.unportedNuggets, &m_counters.flankIgnored,
			 &m_counters.rubbleEntered, &m_counters.collapsesBegun, &m_counters.collapsesDone, &m_counters.collapseEffectsUnported,
			 &m_counters.respawnWithoutUpdate, &m_counters.delayedDeaths, &m_counters.castleMemberHits, &m_counters.castleBreaches, &m_counters.crushes,
			 &m_counters.crushWeaponShots, &m_counters.crushBumps, &m_counters.ramHits, &m_counters.crushKnockbacks, &m_counters.crushBumpAttacksNotPorted,
			 &m_counters.crushDecelerations, &m_counters.flanks, &m_counters.flankTests, &m_counters.metaImpactHits, &m_counters.metaImpactKills,
			 &m_counters.metaImpactNestedHorde, &m_counters.shockwaveFlings, &m_counters.shockwaveStandUps, &m_counters.shockwaveRampageKills })
	{
		SnapshotXfer::u64(x, *c);
	}
	for (unsigned long long *c : { &m_counters.projectileContacts, &m_counters.projectileContactHits, &m_counters.projectilePreLandingEmotions })
	{
		if (version >= 2)
		{
			SnapshotXfer::u64(x, *c);
		}
		else
		{
			*c = 0; // a version 1 save has none of them: the game starts them at 0 (Sol r1)
		}
	}
	SnapshotXfer::each(x, m_breaches, 12, [&](CastleBreach &b) {
		x.xferUnsignedInt(&b.frame);
		x.xferObjectID(&b.objectId); // an object id (the same u32)
		x.xferInt(&b.ownerPlayerIndex);
		SnapshotXfer::checkRange(b.ownerPlayerIndex, -1, m_logic.players().getPlayerCount() - 1, "a castle breach's owner player");
	});
	if (x.isLoading())
	{
		m_targets.reset();
		m_flying.clear();
		m_flyingValid = false; // lane FLANK-1: rebuilt from the loaded objects by the next contact pass
	}
}

// OpenBFME. GPL-3.0.
// See GameLogic/Combat/CombatState.h.

#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/ObjectWeapons.h"

#include "GameLogic/Combat/ProjectileLauncher.h"
#include "GameLogic/Module/StancesBehavior.h"
#include "GameLogic/Module/ProjectileModules.h"
#include "GameLogic/Module/HordeAIUpdate.h"
#include "GameLogic/Module/BannerCarrierUpdate.h"
#include "GameLogic/Module/SquishCollide.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Contain/HordeFlank.h"
#include "Common/StateHash.h"
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

void CombatState::reset()
{
	m_targets.reset();
	m_counters = Counters();
	m_breaches.clear();
	m_autoAcquire = true;
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
		"object through ProjectileLauncher (lane PROJ-1, stops S-360..S-364); MetaImpactNugget throws through the shockwave handler (lane COMBAT-4, S-1600 / S-1790); DOTNugget hits and registers its damage over time with the DOTManager (lane DECOMP-1, S-1959); AttributeModifierNugget adds its ModifierList, disables its AntiCategories and plays its AntiFX (lane DECOMP-1: RW 0x90EAF9, the arc's fcos is S-167's cosd); ParalyzeNugget disables its victims (lane DECOMP-1: RW 0x90F177); every other nugget kind (FireLogic, WeaponOCL, ...) and the passenger hit roll are counted and not executed",
		"[S-322] ActiveBody: armour is the ArmorSet of the object's armor flags looked up per hit (RW 0x5D893C; the flank test RW 0x68FB63 is lane HORDE-2's, S-582); the ARMOR / INVULNERABLE attribute modifiers, the burning death "
		"fire cap, doDamageFX and the damage modules' onDamage are not ported (the damage state model conditions are COMBAT-2's, S-342); ImmortalBody / HighlanderBody / ActiveBody share the data table 0xC71D68, "
		"StructureBody (an extra EMPTY table 0xC84858), InactiveBody, RespawnBody, DelayedDeathBody and SymbioticStructuresBody run since COMBAT-2 (S-340, S-343)",
		"[S-323] kill credit: Object::scoreTheKill (RW 0x6955BC) skips a victim with KindOf IGNORED_IN_GUI (0x695661), requires an enemy of another owner (0x6956E8..0x6956F7) and then pays the bounty through Economy::awardBounty (call 0x695743 -> 0x6AC06F); the victim's playable-side test (0x69574F) comes after the bounty in RW and only gates skill points, so it is not applied to the bounty; the score keeper, the skill points, the experience tracker, the academy statistics and the EVA of a lost unit are other lanes'",
		"[S-324] death: SlowDeathBehavior is RotWK's (lane COMBAT-4): the roulette RW 0x861712 (GameLogicRandomValue(0, total - 1) at line 0x32F, the overkill probability RW 0x860608), beginSlowDeath RW 0x860E93 (DeathFlags' statuses and model conditions with DYING, the sink / destruction / midpoint draws at lines 0x1A5 / 0x1A6 / 0x1AB, DoNotRandomizeMidpoint, DecayBeginTime, the FadeDelay frame, FlingForce: the body thrown by RW 0x860664, EXPLODED_FLAILING, its timers held while it flies) and the update RW 0x860B39 (EXPLODED_BOUNCING on landing, the HIT_GROUND phase, SINKING, DISABLED_HELD, z - SinkRate and 5.7 more above the terrain, MIDPOINT, FINAL and the destruction, DECAY); the phase OCL runs (lane SPELL-2: RW 0x860A46, S-530); the Weapon phase effect fires the temporary weapon (lane DECOMP-1, RW 0x860A8C); NOT ported: the drawable's fade and shadow (client), the LOD death scale (1.0 at every 2.01 GameLOD level) and its rescale, the HULK quick death (needs the script hulk lifetime override, never set), a slaved update's notice on a fling; DestroyDie and KeepObjectDie act; FXListDie, CreateObjectDie and every other die module have no runtime",
		"[S-325] attack machine: AIAttackState and its sub machine (Pursue 0x64, Approach 0x65, Aim 0x66, Fire 0x67, WaitUntilFinishedFiring 0x68) are the B1 ids and transitions (RW's own ids were not read); "
		"the transition conditions (weapon range test, victim death) are inference; no turrets, a stealthed and undetected victim ends the attack state (lane STEALTH-1, inference of the RW sub-state), no garrison fire points, no combo locomotors, no attack position / area / squad, no retaliation",
		"[S-326] acquisition: an idle unit scans for an enemy every MoodAttackCheckRate frames within its vision range (the template's VisionRange), nearest first, ties by object id; AttackPriority tables, "
		"the stances' acquisition side (StancesBehavior's RW 0x662A21, S-585), guard and hunt states, NOTWHILEATTACKING and the retail partition manager's filters are not read (stealth: a stealthed and undetected enemy is not attackable, RW 0x6C9147, and a stealthed unit scans only with STEALTHED, RW 0x66844A: lane STEALTH-1)",
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

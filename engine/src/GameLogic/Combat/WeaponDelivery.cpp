// OpenBFME. GPL-3.0.
// See GameLogic/Combat/WeaponDelivery.h for the target facts.

#include "GameLogic/Combat/WeaponDelivery.h"

#include "Common/Player.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Damage.h"
#include "GameLogic/FXEvents.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/PhysicsBehavior.h"
#include "GameLogic/Module/ProjectileModules.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/Module/HordeAIUpdate.h"
#include "GameLogic/Module/ProjectileModules.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Contain/HordeFlank.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponNugget.h"
#include "GameLogic/WeaponStores.h"
#include "GameLogic/System/DOTManager.h"
#include "GameLogic/AttributeModifiers.h"
#include "GameLogic/Object/AttributeModifierPool.h"

#include <cmath>

namespace
{
const char *const kWeaponCpp = "Weapon.cpp"; // the retail file string (RW 0xC18320): the call log keys on the line numbers

// the NuggetDamageHost of one (source, victim) pair
class DamageHost : public NuggetDamageHost
{
public:
	DamageHost(GameLogic &logic, ObjectID sourceId, Object *victim)
		: m_logic(logic)
		, m_sourceId(sourceId)
		, m_source(sourceId != INVALID_ID ? logic.findObjectByID(sourceId) : nullptr)
		, m_victim(victim)
	{
	}
	bool hasVictim() override { return m_victim != nullptr; }
	float victimDistanceSqr2D(const Coord3D &center) override { return CombatQueries::centerDistanceSquared2D(*m_victim->getPosition(), center); }
	Coord3D victimPosition() override { return *m_victim->getPosition(); }
	Coord3D sourcePosition() override { return m_source ? *m_source->getPosition() : Coord3D{ 0.0f, 0.0f, 0.0f }; }
	bool sourceHasContain() override { return m_source && m_source->getContain() != nullptr; }
	int sourcePassengerCount() override { return m_source && m_source->getContain() ? (int)m_source->getContain()->getContainCount() : 0; }
	bool filterAllowsVictim(const ObjectFilter &filter) override
	{
		const Player *p = m_source ? m_source->getControllingPlayer() : nullptr;
		return m_victim && ObjectFilterMatch::allows(m_logic, filter, *m_victim, p);
	}
	// lane XP-1: the source's AttributeModifierPoolUpdate (RW 0x68C818 sum / RW 0x68C82D product, name null)
	bool sourceAdditive(int type, float &out) override { return m_source && m_source->attributeModifierSum(type, nullptr, out); }
	bool sourceMultiplicative(int type, bool innate, float &out) override { return m_source && m_source->attributeModifierProduct(type, nullptr, innate, out); }
	// RW 0x90E57D / 0x90E5A6: Object::isFlankedBy (RW 0x68FB63), lane HORDE-2 (S-582)
	bool victimFlankedBySource() override { return m_source && m_victim && HordeFlank::isFlankedBy(*m_victim, *m_source); }
	bool sourceFlankedByVictim() override { return m_source && m_victim && HordeFlank::isFlankedBy(*m_source, *m_victim); }
	bool weaponSourceIsVictim() override { return m_source && m_source == m_victim; }
	std::uint32_t sourcePlayerMask() override
	{
		const Player *p = m_source ? m_source->getControllingPlayer() : nullptr;
		return p && p->getPlayerIndex() >= 0 && p->getPlayerIndex() < 32 ? (1u << p->getPlayerIndex()) : 0u;
	}
	unsigned weaponSourceID() override { return m_sourceId; }

private:
	GameLogic &m_logic;
	ObjectID m_sourceId;
	Object *m_source;
	Object *m_victim;
};

// RW 0x90D6F5 (the upgrade test of a nugget): no forbidden upgrade and every required upgrade, on the object or its player (inference on "all": the single-name case is
// what retail data uses)
bool upgradeTest(const WeaponNugget &n, const Object *source)
{
	if (n.m_requiredUpgradeNames.empty() && n.m_forbiddenUpgradeNames.empty())
	{
		return true;
	}
	if (!source)
	{
		return n.m_requiredUpgradeNames.empty();
	}
	const Player *p = source->getControllingPlayer();
	auto has = [&](const std::string &u) { return source->hasUpgrade(u) || (p && p->hasUpgradeComplete(u)); };
	for (const std::string &u : n.m_forbiddenUpgradeNames)
	{
		if (has(u))
		{
			return false;
		}
	}
	for (const std::string &u : n.m_requiredUpgradeNames)
	{
		if (!has(u))
		{
			return false;
		}
	}
	return true;
}

// RW 0x90E855 / 0x90D77C shouldDeliver (the parts that do not need the attribute pool)
bool shouldDeliver(const WeaponNugget &n, const WeaponTemplate &w, const Object *source, const Object &victim, GameLogic &logic)
{
	if (!upgradeTest(n, source))
	{
		return false;
	}
	if (!source)
	{
		return true;
	}
	const unsigned affects = w.m_affectsMask;
	if (!(affects & WEAPON_AFFECTS_SELF) && &victim == source)
	{
		return false;
	}
	const CombatNames::Kind &k = CombatNames::kinds();
	if ((affects & WEAPON_DOESNT_AFFECT_SIMILAR) && victim.getTemplate() == source->getTemplate() && source->getRelationship(victim) == ALLIES)
	{
		return false;
	}
	if (victim.isKindOf((unsigned)k.projectile) && !(affects & WEAPON_AFFECTS_PROJECTILES))
	{
		return false;
	}
	// the relationship mask (RW 0x68D7AB): ENEMIES -> 4, NEUTRAL -> 8, ALLIES -> 2
	const Relationship r = source->getRelationship(victim);
	const unsigned mask = r == ENEMIES ? WEAPON_AFFECTS_ENEMIES : r == NEUTRAL ? WEAPON_AFFECTS_NEUTRALS : WEAPON_AFFECTS_ALLIES;
	if ((affects & mask) == 0)
	{
		return false;
	}
	if (ObjectFilterMatch::isValid(&n.m_specialObjectFilter) && !ObjectFilterMatch::allows(logic, n.m_specialObjectFilter, victim, source->getControllingPlayer()))
	{
		return false;
	}
	return true;
}

bool WeaponDeliveryShouldDeliver(const WeaponNugget &n, const WeaponTemplate &w, const Object *source, const Object &victim, GameLogic &logic)
{
	return shouldDeliver(n, w, source, victim, logic);
}

const char *const kNestedHordeStop =
	"[S-1790] shockwaves (COMBAT-4): NOT ported: MetaImpactNugget's hit on a member of a horde that is itself inside a container (RW 0x910764 .. 0x9107AF: the source scores the member, the outer "
	"container's contain slots 4 / 0x3C / 0x7C act on it; counted, nothing applied); the handler's audio event (RW 0x696947 .. 0x696A0E, TheAudio slot 0x64) and the drawable's "
	"projectile type (client); FlipDirection is read by no 2.01 nugget code found; INFERENCE: a horde's member list is its contain list (horde interface slot 0x108 not read), the "
	"clear-radius landing height is the ground (layer heights S-161), the AI state of RW 0x662D70 is the port's current state";

// RW 0x68D91A Object::estimateDamage: a TREE (template + 0x113 bit 6) answers 1.0 for FLAME (6) and 0 for anything else; else the body's estimate (body vslot 2);
// no body: 0 (lane DECOMP-1)
float objectEstimateDamage(const Object &victim, const DamageInfoInput &in)
{
	static const int kTree = ObjectTemplateInfoBuilder::kindOfIndex("TREE");
	if (kTree >= 0 && victim.isKindOf((unsigned)kTree))
	{
		return in.m_damageType == DAMAGE_FLAME ? 1.0f : 0.0f;
	}
	const BodyModuleInterface *body = victim.getBodyModule();
	return body ? body->estimateDamage(in) : 0.0f;
}

// RW 0x90E855, DamageNugget's (and DOTNugget's) slot 1, isApplicable(weapon, victim) (lane DECOMP-1; BFME2 decomp tier B same-shape): the base test RW 0x90D77C
// (shouldDeliver above); the nugget's DamageInfo filled with noFlank (RW 0x90E28C(.., 1, 0)), its answer unread; false without a victim or when the weapon's
// owner (Weapon + 8) is not in the logic; false when LostLeadershipUselessAgainst (+ 0x198) is not empty, the victim has all of its KindOf bits (RW 0x70C4FE)
// and the owner's pool has LEADERSHIP (category 1) disabled at this frame (RW 0x804D94: frame < + 0x30 + 4); then the victim's estimate of the hit (RW 0x68D91A),
// scaled for PassengerProportionalAttack like the fill (x87 count / MaxAttackPassengers, capped at 1.0, mulss): applicable when it is above 0 or the hit kills
bool damageApplicable(GameLogic &logic, ObjectID ownerId, const DamageNugget &n, const WeaponTemplate &w, Object &victim)
{
	Object *owner = ownerId != INVALID_ID ? logic.findObjectByID(ownerId) : nullptr;
	if (!shouldDeliver(n, w, owner, victim, logic))
	{
		return false;
	}
	DamageHost host(logic, ownerId, &victim);
	DamageInfo info;
	(void)FillDamageInfo(n, w, host, true, nullptr, info);
	if (!owner)
	{
		return false;
	}
	bool anyUseless = false;
	for (std::uint32_t word : n.m_lostLeadershipUselessAgainst)
	{
		anyUseless = anyUseless || word != 0;
	}
	if (anyUseless)
	{
		bool all = true;
		const KindOfMaskType &k = victim.getKindOf();
		for (size_t i = 0; i < k.size(); ++i)
		{
			all = all && (k[i] & n.m_lostLeadershipUselessAgainst[i]) == n.m_lostLeadershipUselessAgainst[i];
		}
		if (all)
		{
			const AttributeModifierPool *pool = static_cast<const AttributeModifierPool *>(owner->findModule("AttributeModifierPoolUpdate"));
			if (pool && logic.getFrame() < pool->categoryDisabledUntil(1))
			{
				return false;
			}
		}
	}
	float estimate = objectEstimateDamage(victim, info.m_input);
	if (w.m_passengerProportionalAttack && owner->getContain())
	{
		const int maxPassengers = (int)w.m_maxAttackPassengers;
		if (maxPassengers > 0)
		{
			const std::uint32_t count = (std::uint32_t)owner->getContain()->getContainCount();
			float q = SimMath::fstpDword(SimMath::pc24DivW(SimMath::fildU32(count), (double)maxPassengers));
			if (q > 1.0f)
			{
				q = 1.0f;
			}
			estimate = SimMath::sseMul(estimate, q);
		}
	}
	return 0.0f < estimate || info.m_input.m_kill;
}

// RW 0x911407, DOTNugget's slot 14 before the plain hit (lane DECOMP-1): with a victim, a record filled by the same fillDamageInfo (RW 0x90E28C) goes to
// TheGameLogic + 0x174 (RW 0x821073) with interval + 0x1C4, end = frame + DamageDuration (+ 0x1C8), next = frame + DamageInterval
void registerDamageOverTime(GameLogic &logic, ObjectID sourceId, const DOTNugget &nugget, const WeaponTemplate &weapon, Object &victim, const Coord3D *center)
{
	DamageHost host(logic, sourceId, &victim);
	DOTManager::Record r;
	if (!FillDamageInfo(nugget, weapon, host, false, center, r.info))
	{
		return;
	}
	const std::uint32_t now = logic.getFrame();
	r.interval = nugget.m_damageInterval;
	r.nextFrame = nugget.m_damageInterval + now;
	r.endFrame = nugget.m_damageDuration + now;
	logic.dot().add(victim.getID(), r);
}

// RW 0x90E683 applyToVictim (DamageNugget's slot 14): true when the hit took health (actualDamageClipped > 0). A DOTNugget (slot 14 RW 0x911407) registers its
// damage over time first, then deals this plain hit and answers with it
bool applyToVictim(GameLogic &logic, ObjectID sourceId, const DamageNugget &nugget, const WeaponTemplate &weapon, Object &victim, const Coord3D *center)
{
	if (nugget.kind() == NUGGET_DOT)
	{
		registerDamageOverTime(logic, sourceId, static_cast<const DOTNugget &>(nugget), weapon, victim, center);
	}
	DamageHost host(logic, sourceId, &victim);
	DamageInfo info;
	if (!FillDamageInfo(nugget, weapon, host, false, center, info))
	{
		return false;
	}
	Object *source = sourceId != INVALID_ID ? logic.findObjectByID(sourceId) : nullptr;
	if (info.m_input.m_damageType == DAMAGE_HEALING)
	{
		victim.attemptHealing(info.m_input.m_amount, &victim);
		return false;
	}
	victim.attemptDamage(info);
	if (nugget.m_drainLife && source && !source->isKindOf((unsigned)CombatNames::kinds().structure))
	{
		const float drained = SimMath::mulf32(nugget.m_drainLifeMultiplier, info.m_output.m_actualDamageDealt);
		if (drained != 0.0f)
		{
			source->attemptHealing(drained, nullptr);
		}
	}
	return info.m_output.m_actualDamageClipped > 0.0f;
}

// RW 0x90DEF0 radiusDamage (the shape of the partition query is 3D bounding sphere: S-321 the 2D bounding circle stands in)
void radiusDamage(GameLogic &logic, ObjectID sourceId, const DamageNugget &nugget, const WeaponTemplate &weapon, const Coord3D &center, unsigned long long *unported)
{
	const float radius = nugget.m_radius > 1.0f ? nugget.m_radius : 1.0f;
	const CombatNames::Kind &k = CombatNames::kinds();
	Object *source = sourceId != INVALID_ID ? logic.findObjectByID(sourceId) : nullptr;
	// RW 0x90DEF0 (the arc block before shouldDeliver; fcos call RW 0x90E16E): a DamageArc under pi (RW 0xBDD388) keeps a cone: its axis runs from the source through the centre (the source's X axis, matrix +8 / +0x18 / +0x28,
	// when the two coincide); an object is inside when the unit vector centre -> object has a dot product of at least cos(DamageArc) with the axis.
	// A zero vector stays unnormalised (the squared length is tested against 0.0, RW 0xC1B594), so the object at the centre (the victim) has dot 0.
	// DamageArcInverted keeps the outside instead. The cos is fcos (RW 0x42F4E0, stop S-167: the deterministic cosd stands in); the inverse square root is RW 0x441C56.
	const bool arc = nugget.m_damageArc < 3.14159274f && source;
	float axisX = 0.0f, axisY = 0.0f, axisZ = 0.0f;
	double cosArc = 0.0;
	if (arc)
	{
		const Coord3D *sp = source->getPosition();
		axisX = SimMath::subf32(center.x, sp->x);
		axisY = SimMath::subf32(center.y, sp->y);
		axisZ = SimMath::subf32(center.z, sp->z);
		if (SimMath::length3d(axisX, axisY, axisZ) == 0.0)
		{
			const float *basis = source->getBasis();
			axisX = basis[0];
			axisY = basis[3];
			axisZ = basis[6];
		}
		const float len2 = SimMath::sumSquares3(axisZ, axisY, axisX);
		if (len2 != 0.0f)
		{
			const float inv = ProjectileInvSqrt(len2);
			axisX = SimMath::mulf32(axisX, inv);
			axisY = SimMath::mulf32(axisY, inv);
			axisZ = SimMath::mulf32(axisZ, inv);
		}
		cosArc = SimMath::cosd(nugget.m_damageArc); // fcos: the result stays wide in ST0 for the fcomip
	}
	else if (nugget.m_damageArc < 3.14159274f && unported)
	{
		++*unported; // the cone reads the source's position (+0x38): a sourceless arc is not ported
	}
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->isKindOf((unsigned)k.inert) || o->isKindOf((unsigned)k.unattackable) || o->isDestroyed() || o->isEffectivelyDead() || !o->isInWorld())
		{
			continue; // lane GARRISON-1: a contain's rider out of the world (RW 0x68C18F) is not in the partition
		}
		const float d2 = CombatQueries::centerDistanceSquared2D(*o->getPosition(), center);
		const float reach = SimMath::addf32(radius, CombatQueries::boundingCircleRadius(*o));
		if (d2 > SimMath::mulf32(reach, reach))
		{
			continue;
		}
		if (arc)
		{
			float vx = SimMath::subf32(o->getPosition()->x, center.x);
			float vy = SimMath::subf32(o->getPosition()->y, center.y);
			float vz = SimMath::subf32(o->getPosition()->z, center.z);
			const float v2 = SimMath::sumSquares3(vx, vy, vz); // RW 0x90E0F4 sums the victim's squared length in x / y / z order (MOVE-2 r4 review)
			if (v2 != 0.0f)
			{
				const float inv = ProjectileInvSqrt(v2);
				vx = SimMath::mulf32(vx, inv);
				vy = SimMath::mulf32(vy, inv);
				vz = SimMath::mulf32(vz, inv);
			}
			const float dot = SimMath::addf32(SimMath::addf32(SimMath::mulf32(vz, axisZ), SimMath::mulf32(vy, axisY)), SimMath::mulf32(vx, axisX)); // z, y, x (RW 0x90E14B)
			if ((cosArc <= (double)dot) == nugget.m_damageArcInverted) // RW 0x90E177
			{
				continue;
			}
		}
		if (!damageApplicable(logic, sourceId, nugget, weapon, *o)) // slot 1 (RW 0x90E1A1 -> 0x90E855; lane DECOMP-1)
		{
			continue;
		}
		if (nugget.m_minRadius > 0.0f && d2 < SimMath::mulf32(nugget.m_minRadius, nugget.m_minRadius))
		{
			continue;
		}
		const bool took = applyToVictim(logic, sourceId, nugget, weapon, *o, &center);
		if (took && 4.0f >= radius) // GameData DamageRadiusMinimumForSplash (RW +0xB40, retail 4.0): a small radius hurts one object
		{
			break;
		}
	}
}

// ---- MetaImpactNugget (lane COMBAT-4; RW vtable 0xC7B848: slot 1 RW 0x910070, slot 5 RW 0x910025, slot 6 RW 0x9108EE, slot 12 RW 0x9188EB (false), slot 14 RW 0x910380,
// slot 15 RW 0x91062F, slot 16 RW 0x910179). `sourceId` stands for RW's source argument (its + 8, the source object's id, is all the nugget reads of it). ----
namespace MetaImpact
{
const char *const kFile = "MetaImpactNugget.cpp"; // RW 0xC7B7D8 (...\Object\WeaponEffects\MetaImpactNugget.cpp)

int kindIndex(const char *name)
{
	return ObjectTemplateInfoBuilder::kindOfIndex(name);
}

// RW 0x910070 shouldDeliver(source, victim): no TREE (template + 0x113 bit 6) or STRUCTURE (+ 0x108 bit 7); a HORDE (+ 0x114 bit 13) only with AffectHordes; not
// shockwave-proof (RW 0x68E16A(cyclonic)); ShockwaveResistance * 0.2 (RW 0xD9F61C, SSE) below ShockWaveAmount, or cyclonic and SHIP; not contained, unless by a HORDE
// and without AffectHordes; alive: not OnlyWhenJustDied, then the base test RW 0x90D77C; dead: the body (+ 0x25C) slot 0x44 (ActiveBody's last damage frame + 0xAC) at
// least TheGameLogic's frame (+ 0x40), then the base test
bool shouldDeliver(GameLogic &logic, const MetaImpactNugget &n, const WeaponTemplate &w, const Object *source, Object *victim)
{
	static const int kTree = kindIndex("TREE"), kStructure = kindIndex("STRUCTURE"), kHorde = kindIndex("HORDE"), kShip = kindIndex("SHIP");
	if (!victim)
	{
		return false;
	}
	if (victim->isKindOf((unsigned)kTree) || victim->isKindOf((unsigned)kStructure) || (!n.m_affectHordes && victim->isKindOf((unsigned)kHorde)))
	{
		return false;
	}
	const bool cyclonic = n.m_cyclonicFactor != 0.0f;
	const float resistance = ObjectKnockback::shockwaveResistance(*victim);
	if (resistance >= 100.0f && !(cyclonic && victim->isKindOf((unsigned)kShip))) // RW 0x68E16A
	{
		return false;
	}
	if (!(SimMath::mulf32(resistance, 0.200000003f) < n.m_shockWaveAmount) && !(cyclonic && victim->isKindOf((unsigned)kShip)))
	{
		return false;
	}
	if (const Object *c = victim->getContainedBy())
	{
		if (!c->isKindOf((unsigned)kHorde) || n.m_affectHordes)
		{
			return false;
		}
	}
	if (!victim->isEffectivelyDead())
	{
		if (n.m_onlyWhenJustDied)
		{
			return false;
		}
		return WeaponDeliveryShouldDeliver(n, w, source, *victim, logic);
	}
	const ActiveBody *body = dynamic_cast<const ActiveBody *>(victim->getBodyModule());
	if (body && logic.getFrame() <= body->lastDamageFrame())
	{
		return WeaponDeliveryShouldDeliver(n, w, source, *victim, logic);
	}
	return false;
}

// RW 0x910380 (slot 14): the DamageInfo's shockwave half; false without the source object
bool fill(GameLogic &logic, const MetaImpactNugget &n, ObjectID sourceId, Object &victim, DamageInfo &out)
{
	Object *source = logic.findObjectByID(sourceId);
	if (!source)
	{
		return false;
	}
	DamageInfoInput &d = out.m_input;
	d.m_shockWaveAmount = n.m_shockWaveAmount; // fld; fstp
	const Coord3D &sp = *source->getPosition();
	const Coord3D &vp = *victim.getPosition();
	Coord3D v;
	if (!n.m_shockWaveClearRadius && n.m_invertShockWave)
	{
		v = Coord3D{ SimMath::subf32(sp.x, vp.x), SimMath::subf32(sp.y, vp.y), SimMath::subf32(sp.z, vp.z) }; // source - victim
	}
	else
	{
		v = Coord3D{ SimMath::subf32(vp.x, sp.x), SimMath::subf32(vp.y, sp.y), SimMath::subf32(vp.z, sp.z) }; // victim - source
	}
	if (n.m_cyclonicFactor != 0.0f)
	{
		// RW 0x910442 .. 0x910492 (SSE): c = min(CyclonicFactor, 1); x' = (y * c) * c + x * (1 - c); y' = y * (1 - c) + (0 - x * c) * c
		const float c = n.m_cyclonicFactor > 1.0f ? 1.0f : n.m_cyclonicFactor;
		const float k = SimMath::subf32(1.0f, c);
		const float x = SimMath::addf32(SimMath::mulf32(SimMath::mulf32(v.y, c), c), SimMath::mulf32(v.x, k));
		const float y = SimMath::addf32(SimMath::mulf32(v.y, k), SimMath::mulf32(SimMath::subf32(0.0f, SimMath::mulf32(v.x, c)), c));
		v.x = x;
		v.y = y;
	}
	// RW 0x910497 .. 0x910540: every component's MSVCR71 fabs below 0.0001 (RW 0xBD19E0): ClearRadius turns the vector to a random planar direction
	// (GameLogicRandomValueReal(0, 2 pi) at line 0x170, MSVCR71 cos / sin: the deterministic pair, S-167), otherwise z = 1.0
	if (SimMath::absD((double)v.x) < (double)0.0001f && SimMath::absD((double)v.y) < (double)0.0001f && SimMath::absD((double)v.z) < (double)0.0001f)
	{
		if (n.m_shockWaveClearRadius)
		{
			const float a = logic.random().getValueReal(0.0f, 6.28318548f, kFile, 0x170);
			v.x = SimMath::fstpDword(SimMath::cosd(a));
			v.y = SimMath::fstpDword(SimMath::sind(a));
		}
		else
		{
			v.z = 1.0f;
		}
	}
	d.m_shockWaveVector = v;
	d.m_shockWaveRadius = n.m_shockWaveRadius;
	d.m_shockWaveTaperOff = n.m_shockWaveTaperOff;
	d.m_shockWaveZMult = n.m_shockWaveZMult;
	d.m_shockWaveClearRadius = n.m_shockWaveClearRadius;
	d.m_shockWaveClearMult = n.m_shockWaveClearMult;
	d.m_shockWaveClearFlingHeight = n.m_shockWaveClearFlingHeight;
	if (n.m_shockWaveClearRadius)
	{
		d.m_shockWaveClearCenter = sp;
	}
	// the delay: fild DelayTime (+ 2^32 when negative), fstp; ShockWaveSpeed > 0 adds |v| (RW 0x403111) / speed on the x87
	d.m_delay = SimMath::fstpDword(SimMath::fildU32(n.m_delayTime));
	d.m_cyclonicFactor = n.m_cyclonicFactor;
	if (n.m_shockWaveSpeed > 0.0f)
	{
		const double len = SimMath::sqrtPC24((double)SimMath::sumSquares3(v.x, v.y, v.z));
		d.m_delay = SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24DivW(len, (double)n.m_shockWaveSpeed), (double)d.m_delay));
	}
	d.m_shockWaveSourceID = source->getID();
	const Player *p = source->getControllingPlayer();
	d.m_sourcePlayerMask = p && p->getPlayerIndex() >= 0 && p->getPlayerIndex() < 32 ? (1u << p->getPlayerIndex()) : 0u;
	d.m_sourceID = sourceId;
	return true;
}

// RW 0x91062F (slot 15): the KillObjectFilter kills (Object::kill(UNRESISTABLE, NORMAL) RW 0x698EC3); RESIST_KNOCKBACK above 0 resists when
// GameLogicRandomValueReal(0, 0.999) (RW 0xC7B844, line 0x1B0) <= it; a HERO (template + 0x113 bit 2) resists when HeroResist > GameLogicRandomValueReal(0, 0.999)
// (line 0x1B9); a HORDE_MEMBER whose horde is itself contained (RW 0x910764 .. 0x9107AF: the outer container's contain slots 4 / 0x3C / 0x7C, NOT ported: S-1790);
// otherwise slot 14 fills a fresh DamageInfo (RW 0x66365E) and Object::attemptDamage (RW 0x698E7D) takes it
void apply(GameLogic &logic, const MetaImpactNugget &n, ObjectID sourceId, Object &victim)
{
	static const int kHero = kindIndex("HERO"), kHorde = kindIndex("HORDE");
	CombatState::Counters &counters = logic.combat().counters();
	Object *source = logic.findObjectByID(sourceId);
	if (source && ObjectFilterMatch::isValid(&n.m_killObjectFilter) &&
		ObjectFilterMatch::allows(logic, n.m_killObjectFilter, victim, source->getControllingPlayer()))
	{
		victim.kill(DEATH_NORMAL);
		++counters.metaImpactKills;
		return;
	}
	float resist = 0.0f;
	victim.attributeModifierSum(10, nullptr, resist); // RESIST_KNOCKBACK (RW 0x68C818)
	if (resist > 0.0f && !(logic.random().getValueReal(0.0f, 0.999000013f, kFile, 0x1B0) > resist))
	{
		return;
	}
	if (victim.isKindOf((unsigned)kHero) && n.m_heroResist > logic.random().getValueReal(0.0f, 0.999000013f, kFile, 0x1B9))
	{
		return;
	}
	const Object *horde = victim.getContainedBy();
	if (victim.testStatus((unsigned)CombatNames::statuses().hordeMember) && horde && horde->getContainedBy() && horde->isKindOf((unsigned)kHorde))
	{
		++counters.metaImpactNestedHorde; // S-1790
		logic.noteStop(kNestedHordeStop);
		return;
	}
	DamageInfo info;
	if (fill(logic, n, sourceId, victim, info))
	{
		victim.attemptDamage(info);
		++counters.metaImpactHits;
	}
}

// RW 0x910179 (slot 16): ShockWaveArc below pi (RW 0xBDD388) keeps the victims whose direction from the shock point lies within the arc around the source's direction to
// the point (the source's x axis when it stands on it); ShockWaveArcInverted keeps the others. No source object: nothing
void arcApply(GameLogic &logic, const MetaImpactNugget &n, ObjectID sourceId, Object &victim, const Coord3D &pos)
{
	if (3.14159274f > n.m_shockWaveArc)
	{
		const Coord3D &vp = *victim.getPosition();
		const float dvx = SimMath::subf32(vp.x, pos.x), dvy = SimMath::subf32(vp.y, pos.y), dvz = SimMath::subf32(vp.z, pos.z);
		Object *source = logic.findObjectByID(sourceId);
		if (!source)
		{
			return;
		}
		const Coord3D &sp = *source->getPosition();
		float sx = SimMath::subf32(pos.x, sp.x), sy = SimMath::subf32(pos.y, sp.y), sz = SimMath::subf32(pos.z, sp.z);
		if (SimMath::sqrtPC24((double)SimMath::sumSquares3(sx, sy, sz)) == 0.0) // RW 0x403111; fldz; fucompi
		{
			const float *b = source->getBasis(); // + 8 / + 0x18 / + 0x28: the x axis
			sx = b[0];
			sy = b[3];
			sz = b[6];
		}
		// the source direction: (z*z + y*y) + x*x (SSE), scaled by RW 0x441C56 on the x87
		const float ss = SimMath::addf32(SimMath::addf32(SimMath::mulf32(sz, sz), SimMath::mulf32(sy, sy)), SimMath::mulf32(sx, sx));
		if (ss != 0.0f)
		{
			const float inv = ProjectileInvSqrt(ss);
			sx = SimMath::pc24Mul(sx, inv);
			sy = SimMath::pc24Mul(sy, inv);
			sz = SimMath::pc24Mul(sz, inv);
		}
		// the victim direction: (x*x + y*y) + z*z
		float ux = dvx, uy = dvy, uz = dvz;
		const float sv = SimMath::addf32(SimMath::addf32(SimMath::mulf32(dvx, dvx), SimMath::mulf32(dvy, dvy)), SimMath::mulf32(dvz, dvz));
		if (sv != 0.0f)
		{
			const float inv = ProjectileInvSqrt(sv);
			ux = SimMath::pc24Mul(dvx, inv);
			uy = SimMath::pc24Mul(dvy, inv);
			uz = SimMath::pc24Mul(inv, dvz);
		}
		// x87: (uz*sz + uy*sy) + ux*sx, stored; against fcos(arc) (RW 0x42F4E0, kept wide)
		const double dotW = SimMath::pc24AddW(SimMath::pc24AddW(SimMath::pc24MulW((double)uz, (double)sz), SimMath::pc24MulW((double)uy, (double)sy)),
			SimMath::pc24MulW((double)ux, (double)sx));
		const float dot = SimMath::fstpDword(dotW);
		const bool inside = (double)dot >= SimMath::cosd(n.m_shockWaveArc);
		if (inside == n.m_shockWaveArcInverted)
		{
			return;
		}
	}
	apply(logic, n, sourceId, victim);
}

// RW 0x9108EE (slot 6): the objects within max(ShockWaveRadius, 1.0) of `pos` (ThePartitionManager RW 0xA39340: FROM_BOUNDINGSPHERE_3D, a cyclone FROM_CENTER_3D; the
// filter RW 0x797EAC rejects INERT; ITER_FASTEST), each through slot 1; a HORDE's members (its horde interface's list, RW 0x68C866 -> slot 0x108) first, then the horde
void deliverAt(GameLogic &logic, const MetaImpactNugget &n, const WeaponTemplate &w, ObjectID sourceId, const Coord3D &pos)
{
	static const int kInert = kindIndex("INERT"), kHorde = kindIndex("HORDE");
	const float radius = 1.0f > n.m_shockWaveRadius ? 1.0f : n.m_shockWaveRadius;
	PartitionFilterFn notInert([](Object &o) { return !o.isKindOf((unsigned)kInert); });
	const DistanceCalculationType dc = n.m_cyclonicFactor != 0.0f ? FROM_CENTER_3D : FROM_BOUNDINGSPHERE_3D;
	std::vector<ObjectID> ids;
	for (const PartitionHit &hit : logic.partition().iterateObjectsInRange(pos, radius, dc, { &notInert }, ITER_FASTEST))
	{
		ids.push_back(hit.object->getID());
	}
	for (ObjectID id : ids)
	{
		Object *o = logic.findObjectByID(id);
		if (!o || !shouldDeliver(logic, n, w, logic.findObjectByID(sourceId), o))
		{
			continue;
		}
		if (o->isKindOf((unsigned)kHorde) && o->getContain())
		{
			std::vector<ObjectID> members;
			for (Object *m : *o->getContain()->getContainedItemsList())
			{
				members.push_back(m->getID());
			}
			for (ObjectID mid : members)
			{
				if (Object *m = logic.findObjectByID(mid))
				{
					arcApply(logic, n, sourceId, *m, pos);
				}
			}
		}
		arcApply(logic, n, sourceId, *o, pos);
	}
}

// RW 0x910025 (slot 5): the victim itself through slot 15 when slot 1 allows (no arc test), then a positive ShockWaveRadius shocks around it (slot 6)
void deliverTo(GameLogic &logic, const MetaImpactNugget &n, const WeaponTemplate &w, ObjectID sourceId, Object &victim)
{
	if (shouldDeliver(logic, n, w, logic.findObjectByID(sourceId), &victim))
	{
		apply(logic, n, sourceId, victim);
	}
	if (n.m_shockWaveRadius > 0.0f)
	{
		const Coord3D at = *victim.getPosition();
		deliverAt(logic, n, w, sourceId, at);
	}
}

// fireWeaponTemplate's nugget dispatch for a MetaImpactNugget (RW 0x6CCECC .. 0x6CCF17: slot 12 is false): a victim gets slot 5 when slot 1 allows it, no victim the
// position through slot 2 (the upgrade test RW 0x90D8F0) and slot 6
void deliver(GameLogic &logic, const MetaImpactNugget &n, const WeaponTemplate &w, ObjectID sourceId, Object *victim, const Coord3D &pos)
{
	if (victim)
	{
		if (shouldDeliver(logic, n, w, logic.findObjectByID(sourceId), victim))
		{
			deliverTo(logic, n, w, sourceId, *victim);
		}
	}
	else if (upgradeTest(n, logic.findObjectByID(sourceId)))
	{
		deliverAt(logic, n, w, sourceId, pos);
	}
}
} // namespace MetaImpact

// RW 0x90EB69 .. 0x90ECC4 (AttributeModifierNugget) and RW 0x90F1E7 .. 0x90F348 (ParalyzeNugget), the same code (lane DECOMP-1): an arc below pi (RW 0xBDD388) with
// a source keeps the victim whose direction from the source lies within the arc around the source's x axis (matrix + 8 / + 0x18 / + 0x28): the offset (SSE) and
// the axis each normalised by RW 0x441C56 on the x87 when their squared length ((z*z + y*y) + x*x for the axis, (x*x + y*y) + z*z for the offset, SSE) is not
// 0; the dot (uz*az + uy*ay) + ux*ax on the x87, stored; fcos(arc) (RW 0x42F4E0) kept wide; `ja`: outside when the cosine is above the dot
bool outsideSourceArc(float arc, const Object *source, const Object &victim)
{
	if (!(3.14159274f > arc) || !source)
	{
		return false;
	}
	const Coord3D &vp = *victim.getPosition();
	const Coord3D &sp = *source->getPosition();
	const float dx = SimMath::subf32(vp.x, sp.x), dy = SimMath::subf32(vp.y, sp.y), dz = SimMath::subf32(vp.z, sp.z);
	const float *b = source->getBasis();
	float ax = b[0], ay = b[3], az = b[6];
	const float aa = SimMath::addf32(SimMath::addf32(SimMath::mulf32(az, az), SimMath::mulf32(ay, ay)), SimMath::mulf32(ax, ax));
	if (aa != 0.0f)
	{
		const float inv = ProjectileInvSqrt(aa);
		ax = SimMath::pc24Mul(ax, inv);
		ay = SimMath::pc24Mul(ay, inv);
		az = SimMath::pc24Mul(az, inv);
	}
	float ux = dx, uy = dy, uz = dz;
	const float dd = SimMath::addf32(SimMath::addf32(SimMath::mulf32(dx, dx), SimMath::mulf32(dy, dy)), SimMath::mulf32(dz, dz));
	if (dd != 0.0f)
	{
		const float inv = ProjectileInvSqrt(dd);
		ux = SimMath::pc24Mul(dx, inv);
		uy = SimMath::pc24Mul(dy, inv);
		uz = SimMath::pc24Mul(inv, dz);
	}
	const double dotW = SimMath::pc24AddW(SimMath::pc24AddW(SimMath::pc24MulW((double)uz, (double)az), SimMath::pc24MulW((double)uy, (double)ay)),
		SimMath::pc24MulW((double)ux, (double)ax));
	const float dot = SimMath::fstpDword(dotW);
	return SimMath::cosd(arc) > (double)dot;
}

// ---- AttributeModifierNugget (lane DECOMP-1; RW vtable 0xC7B1C8: slot 1 RW 0x9114B4 (the base test RW 0x90D77C), slot 2 RW 0x911BFD, slot 5 RW 0x90ED61,
// slot 6 RW 0x90EE10, the apply RW 0x90EAF9). `sourceId` stands for RW's source argument (its + 8 is all the nugget reads of it). ----
namespace AttributeModifier
{
// RW 0x90EAF9 (source, victim): with AffectHordeMembers (+ 0x160) a HORDE (template + 0x115 bit 5) with a contain first passes every member (its contain's list,
// slot 0x118) through this same function; then DamageArc (+ 0x154) below pi (RW 0xBDD388) with a source keeps a victim whose direction from the source lies
// within the arc around the source's x axis (matrix + 8 / + 0x18 / + 0x28); then a named list (+ 0x148) is added for its own duration (RW 0x68F1A8(name, -1)) and,
// when the victim has its AttributeModifierPoolUpdate (RW 0x68C4A6) and the list a positive Duration (TheAttributeModifierStore RW 0x614470 / 0x614495),
// AntiCategories (+ 0x158) are disabled until frame + Duration (RW 0x804FCC) and AntiFX (+ 0x15C) plays on the victim (RW 0x4B1B5A)
void apply(GameLogic &logic, const AttributeModifierNugget &n, ObjectID sourceId, Object &victim)
{
	static const int kHorde = MetaImpact::kindIndex("HORDE");
	Object *source = sourceId != INVALID_ID ? logic.findObjectByID(sourceId) : nullptr;
	if (n.m_affectHordeMembers && victim.isKindOf((unsigned)kHorde) && victim.getContain())
	{
		std::vector<ObjectID> members;
		for (Object *m : *victim.getContain()->getContainedItemsList())
		{
			members.push_back(m->getID());
		}
		for (ObjectID mid : members)
		{
			if (Object *m = logic.findObjectByID(mid))
			{
				apply(logic, n, sourceId, *m);
			}
		}
	}
	if (outsideSourceArc(n.m_damageArc, source, victim)) // RW 0x90EB69 .. 0x90ECC4
	{
		return;
	}
	if (n.m_attributeModifier.empty())
	{
		return;
	}
	victim.addAttributeModifier(n.m_attributeModifier, -1); // RW 0x68F1A8
	AttributeModifierPool *pool = static_cast<AttributeModifierPool *>(victim.findModule("AttributeModifierPoolUpdate")); // RW 0x68C4A6
	if (!pool)
	{
		return;
	}
	const ModifierListTemplate *list = TheAttributeModifierStore ? TheAttributeModifierStore->find(n.m_attributeModifier) : nullptr;
	const int duration = list ? (int)list->m_duration : 0; // RW 0x614495: 0 for an unknown list; `jle`: a signed test
	if (duration <= 0)
	{
		return;
	}
	pool->disableCategories(n.m_antiCategories, logic.getFrame() + (UnsignedInt)duration); // RW 0x804FCC
	if (FXEventLog::isFXName(n.m_antiFX))
	{
		logic.fxEvents().emit(FXEventLog::objectEvent(FXEvent::OBJECT_FX, "AttributeModifierNugget AntiFX", logic.getFrame(), n.m_antiFX, victim)); // RW 0x4B1B5A
	}
}

// RW 0x90EE10 (slot 6): the objects within max(Radius, 1.0) of `pos` (ThePartitionManager RW 0xA39300: FROM_BOUNDINGSPHERE_3D, no filter, ITER_FASTEST), each
// through slot 1 then the apply
void deliverAt(GameLogic &logic, const AttributeModifierNugget &n, const WeaponTemplate &w, ObjectID sourceId, const Coord3D &pos)
{
	const float radius = n.m_radius > 1.0f ? n.m_radius : 1.0f;
	std::vector<ObjectID> ids;
	for (const PartitionHit &hit : logic.partition().iterateObjectsInRange(pos, radius, FROM_BOUNDINGSPHERE_3D, {}, ITER_FASTEST))
	{
		ids.push_back(hit.object->getID());
	}
	for (ObjectID id : ids)
	{
		Object *o = logic.findObjectByID(id);
		if (o && shouldDeliver(n, w, sourceId != INVALID_ID ? logic.findObjectByID(sourceId) : nullptr, *o, logic))
		{
			apply(logic, n, sourceId, *o);
		}
	}
}

// RW 0x90ED61 (slot 5): the victim through slot 1 then the apply; a positive Radius then works around the victim's position (slot 6)
void deliverTo(GameLogic &logic, const AttributeModifierNugget &n, const WeaponTemplate &w, ObjectID sourceId, Object &victim)
{
	if (shouldDeliver(n, w, sourceId != INVALID_ID ? logic.findObjectByID(sourceId) : nullptr, victim, logic))
	{
		apply(logic, n, sourceId, victim);
	}
	if (n.m_radius > 0.0f)
	{
		const Coord3D at = *victim.getPosition();
		deliverAt(logic, n, w, sourceId, at);
	}
}

// fireWeaponTemplate's dispatch (RW 0x6CCECC .. 0x6CCF17, slot 12 false): a victim through slot 1 then slot 5, no victim the upgrade test (slot 2) then slot 6
void deliver(GameLogic &logic, const AttributeModifierNugget &n, const WeaponTemplate &w, ObjectID sourceId, Object *victim, const Coord3D &pos)
{
	Object *source = sourceId != INVALID_ID ? logic.findObjectByID(sourceId) : nullptr;
	if (victim)
	{
		if (shouldDeliver(n, w, source, *victim, logic))
		{
			deliverTo(logic, n, w, sourceId, *victim);
		}
	}
	else if (upgradeTest(n, source))
	{
		deliverAt(logic, n, w, sourceId, pos);
	}
}
} // namespace AttributeModifier

// ---- ParalyzeNugget (lane DECOMP-1; RW vtable 0xC7B328: slot 1 RW 0x90F0E8, slot 5 RW 0x90F403, slot 6 RW 0x90F44A, the apply RW 0x90F177; the same frame as
// AttributeModifierNugget's, read in RotWK) ----
namespace Paralyze
{
// RW 0x90F0E8: a victim with status IGNORE_PARALYZE_NUGGET (101, RW 0x44DDEC) no; the base test RW 0x90D77C; a victim with model condition BURNINGDEATH
// (544, RW 0x46E918) no
bool applicable(GameLogic &logic, const ParalyzeNugget &n, const WeaponTemplate &w, const Object *source, Object &victim)
{
	static const int kIgnore = CombatNames::status("IGNORE_PARALYZE_NUGGET");
	static const int kBurningDeath = CombatNames::modelCondition("BURNINGDEATH");
	if (kIgnore >= 0 && victim.testStatus((unsigned)kIgnore))
	{
		return false;
	}
	if (!shouldDeliver(n, w, source, victim, logic))
	{
		return false;
	}
	return !victim.testModelCondition(kBurningDeath);
}

// RW 0x90F177 (source, victim): with AffectHordeMembers (+ 0x159) a HORDE with a contain first passes every member here; the source-axis arc (+ 0x150); then
// setDisabledUntil(FreezeAnimation (+ 0x158) ? USER_FROZEN : USER_PARALYZED, now + Duration (+ 0x14C)) (RW 0x6907F1), ParalyzeFX (+ 0x154) on the victim
// (RW 0x4B1B5A); a garrisonable contain (slot 0x10) orders every occupant with an AI out: a HORDE aiHordeExit(victim, 0) (RW 0x775AFB), anything not held in
// a horde (RW 0x6939DF) aiExit(victim, 0) (RW 0x7716C1)
void apply(GameLogic &logic, const ParalyzeNugget &n, ObjectID sourceId, Object &victim)
{
	static const int kHorde = MetaImpact::kindIndex("HORDE");
	Object *source = sourceId != INVALID_ID ? logic.findObjectByID(sourceId) : nullptr;
	if (n.m_affectHordeMembers && victim.isKindOf((unsigned)kHorde) && victim.getContain())
	{
		std::vector<ObjectID> members;
		for (Object *m : *victim.getContain()->getContainedItemsList())
		{
			members.push_back(m->getID());
		}
		for (ObjectID mid : members)
		{
			if (Object *m = logic.findObjectByID(mid))
			{
				apply(logic, n, sourceId, *m);
			}
		}
	}
	if (outsideSourceArc(n.m_damageArc, source, victim))
	{
		return;
	}
	victim.setDisabled(n.m_freezeAnimation ? DISABLED_USER_FROZEN : DISABLED_USER_PARALYZED, logic.getFrame() + n.m_duration);
	if (FXEventLog::isFXName(n.m_paralyzeFX))
	{
		logic.fxEvents().emit(FXEventLog::objectEvent(FXEvent::OBJECT_FX, "ParalyzeNugget ParalyzeFX", logic.getFrame(), n.m_paralyzeFX, victim));
	}
	ContainModuleInterface *c = victim.getContain();
	if (!c || !c->isGarrisonable())
	{
		return;
	}
	std::vector<ObjectID> occupants;
	if (const ContainModuleInterface::ContainedItemsList *items = c->getContainedItemsList())
	{
		for (Object *o : *items)
		{
			occupants.push_back(o->getID());
		}
	}
	for (ObjectID id : occupants)
	{
		Object *o = logic.findObjectByID(id);
		AIUpdateInterface *ai = o ? o->getAIUpdateInterface() : nullptr;
		if (!ai)
		{
			continue;
		}
		if (o->isKindOf((unsigned)kHorde))
		{
			ai->aiHordeExit(&victim, CMD_FROM_PLAYER);
		}
		else if (!ai->isContained())
		{
			ai->aiExit(&victim, CMD_FROM_PLAYER);
		}
	}
}

// RW 0x90F44A (slot 6): max(Radius, 1.0) around `pos` (RW 0xA39300: FROM_BOUNDINGSPHERE_3D, no filter), each through slot 1 then the apply
void deliverAt(GameLogic &logic, const ParalyzeNugget &n, const WeaponTemplate &w, ObjectID sourceId, const Coord3D &pos)
{
	const float radius = n.m_radius > 1.0f ? n.m_radius : 1.0f;
	std::vector<ObjectID> ids;
	for (const PartitionHit &hit : logic.partition().iterateObjectsInRange(pos, radius, FROM_BOUNDINGSPHERE_3D, {}, ITER_FASTEST))
	{
		ids.push_back(hit.object->getID());
	}
	for (ObjectID id : ids)
	{
		Object *o = logic.findObjectByID(id);
		if (o && applicable(logic, n, w, sourceId != INVALID_ID ? logic.findObjectByID(sourceId) : nullptr, *o))
		{
			apply(logic, n, sourceId, *o);
		}
	}
}

// RW 0x90F403 (slot 5): the victim through slot 1 then the apply; a positive Radius then works around the victim (slot 6)
void deliverTo(GameLogic &logic, const ParalyzeNugget &n, const WeaponTemplate &w, ObjectID sourceId, Object &victim)
{
	if (applicable(logic, n, w, sourceId != INVALID_ID ? logic.findObjectByID(sourceId) : nullptr, victim))
	{
		apply(logic, n, sourceId, victim);
	}
	if (n.m_radius > 0.0f)
	{
		const Coord3D at = *victim.getPosition();
		deliverAt(logic, n, w, sourceId, at);
	}
}

// fireWeaponTemplate's dispatch (RW 0x6CCECC .. 0x6CCF17): a victim through slot 1 then slot 5, no victim the upgrade test (slot 2) then slot 6
void deliver(GameLogic &logic, const ParalyzeNugget &n, const WeaponTemplate &w, ObjectID sourceId, Object *victim, const Coord3D &pos)
{
	Object *source = sourceId != INVALID_ID ? logic.findObjectByID(sourceId) : nullptr;
	if (victim)
	{
		if (applicable(logic, n, w, source, *victim))
		{
			deliverTo(logic, n, w, sourceId, *victim);
		}
	}
	else if (upgradeTest(n, source))
	{
		deliverAt(logic, n, w, sourceId, pos);
	}
}
} // namespace Paralyze
} // namespace

// RW 0x6CB779 (lane DECOMP-1): any nugget of the template whose slot 1 (isApplicable(weapon, victim)) answers true; false without a victim. The slot 1 bodies
// (RotWK vtables, read with capstone): DamageNugget / DOTNugget RW 0x90E855 (damageApplicable); ProjectileNugget RW 0x90F954 (the base test RW 0x90D77C, a
// warhead (+ 0x148) and RW 0x6CB779 on the warhead); MetaImpactNugget RW 0x910070; AttributeModifier / WeaponOCL / OpenGate / SpecialModelCondition RW 0x9114B4
// and EmotionWeapon / StealMoney RW 0x90D77C (the base test); LuaEventNugget RW 0x8470FA (true); FireLogicNugget RW 0x8F8014 (false); HordeAttackNugget
// RW 0x9119CB (the base test, then with an owner that has a horde interface: a member's current weapon RW 0x6CDBF3 against the victim, else true; the member the
// interface slots 0x50 / 0x114(0xA4) pick is INFERENCE: the first living member); Paralyze RW 0x90F0E8, DamageField RW 0x90F501, Grab RW 0x910C27, DamageContained
// RW 0x911086 and SpawnAndFade RW 0x911BCF are ported (lane DECOMP-1 r2). SlaveAttack RW 0x910DCF asks the owner's SlaveWatcherBehavior for its master's current
// weapon (RW 0x6CDBF3): that module is not ported, so the base test stands in (S-1582)
bool WeaponTemplateAnyNuggetApplicable(GameLogic &logic, const WeaponTemplate &t, ObjectID ownerId, Object *victim, int depth)
{
	if (!victim || depth > 4)
	{
		return false;
	}
	Object *owner = ownerId != INVALID_ID ? logic.findObjectByID(ownerId) : nullptr;
	for (const std::shared_ptr<WeaponNugget> &np : t.m_nuggets)
	{
		const WeaponNugget &n = *np;
		bool ok = false;
		switch (n.kind())
		{
		case NUGGET_DAMAGE:
		case NUGGET_DOT:
			ok = damageApplicable(logic, ownerId, static_cast<const DamageNugget &>(n), t, *victim);
			break;
		case NUGGET_PROJECTILE:
		{
			const ProjectileNugget &pn = static_cast<const ProjectileNugget &>(n);
			const WeaponTemplate *warhead = TheWeaponStore ? TheWeaponStore->findWeaponTemplate(pn.m_warheadTemplateName) : nullptr;
			ok = shouldDeliver(n, t, owner, *victim, logic) && warhead && WeaponTemplateAnyNuggetApplicable(logic, *warhead, ownerId, victim, depth + 1);
			break;
		}
		case NUGGET_META_IMPACT:
			ok = MetaImpact::shouldDeliver(logic, static_cast<const MetaImpactNugget &>(n), t, owner, victim);
			break;
		case NUGGET_PARALYZE:
			ok = Paralyze::applicable(logic, static_cast<const ParalyzeNugget &>(n), t, owner, *victim); // RW 0x90F0E8
			break;
		case NUGGET_DAMAGE_FIELD:
		{
			// RW 0x90F501: the base test, a resolved WeaponTemplateName (+ 0x148) and RW 0x6CB779 on that weapon
			const DamageFieldNugget &dn = static_cast<const DamageFieldNugget &>(n);
			const WeaponTemplate *wt = TheWeaponStore ? TheWeaponStore->findWeaponTemplate(dn.m_weaponTemplateName) : nullptr;
			ok = shouldDeliver(n, t, owner, *victim, logic) && wt && WeaponTemplateAnyNuggetApplicable(logic, *wt, ownerId, victim, depth + 1);
			break;
		}
		case NUGGET_GRAB:
			// RW 0x910C27: the weapon's owner, the base test, and the owner's contain accepting the victim (slot 0x98(victim, 1, 0) == 1); no contain: false
			ok = owner && shouldDeliver(n, t, owner, *victim, logic) && owner->getContain() && owner->getContain()->isValidContainerFor(*victim, true, false);
			break;
		case NUGGET_SPAWN_AND_FADE:
			// RW 0x911BCF: the base test and ObjectTargetFilter (+ 0x148) for the victim with no player (RW 0x7640C1(victim, 0))
			ok = shouldDeliver(n, t, owner, *victim, logic) &&
			     ObjectFilterMatch::allows(logic, static_cast<const SpawnAndFadeNugget &>(n).m_objectTargetFilter, *victim, nullptr);
			break;
		case NUGGET_DAMAGE_CONTAINED:
		{
			// RW 0x911086: the base test, the owner present, a victim contain holding something (slot 0x114), garrisonable (slot 0x10) and not slot 0x1C (INFERENCE:
			// unidentified, false for every ported contain), and a contained object not effectively dead (+ 0x458 bit 0) with every KillKindof bit (+ 0x14C)
			// and none of KillKindofNot (+ 0x168) (RW 0x70C4FE)
			const DamageContainedNugget &dc = static_cast<const DamageContainedNugget &>(n);
			ok = false;
			ContainModuleInterface *c = victim->getContain();
			if (shouldDeliver(n, t, owner, *victim, logic) && owner && c && c->getContainCount() > 0 && c->isGarrisonable())
			{
				if (const ContainModuleInterface::ContainedItemsList *items = c->getContainedItemsList())
				{
					for (const Object *o : *items)
					{
						if (o->isEffectivelyDead())
						{
							continue;
						}
						bool all = true, none = true;
						const KindOfMaskType &k = o->getKindOf();
						for (size_t i = 0; i < k.size(); ++i)
						{
							all = all && (k[i] & dc.m_killKindof[i]) == dc.m_killKindof[i];
							none = none && (k[i] & dc.m_killKindofNot[i]) == 0;
						}
						if (all && none)
						{
							ok = true;
							break;
						}
					}
				}
			}
			break;
		}
		case NUGGET_LUA_EVENT:
			ok = true;
			break;
		case NUGGET_FIRE_LOGIC:
			ok = false;
			break;
		case NUGGET_HORDE_ATTACK:
		{
			ok = shouldDeliver(n, t, owner, *victim, logic);
			static const int kHorde = ObjectTemplateInfoBuilder::kindOfIndex("HORDE");
			if (ok && owner && owner->getContain() && owner->isKindOf((unsigned)kHorde))
			{
				const Object *member = nullptr;
				if (const ContainModuleInterface::ContainedItemsList *items = owner->getContain()->getContainedItemsList())
				{
					for (const Object *m : *items)
					{
						if (m && !m->isEffectivelyDead())
						{
							member = m;
							break;
						}
					}
				}
				const ObjectWeapons *mw = member ? const_cast<Object *>(member)->getWeapons() : nullptr;
				const Weapon *cw = mw ? mw->currentWeapon() : nullptr;
				if (cw && cw->getTemplate())
				{
					ok = WeaponTemplateAnyNuggetApplicable(logic, *cw->getTemplate(), member->getID(), victim, depth + 1);
				}
			}
			break;
		}
		default:
			ok = shouldDeliver(n, t, owner, *victim, logic);
			break;
		}
		if (ok)
		{
			return true;
		}
	}
	return false;
}

unsigned DeliverNuggets(GameLogic &logic, ObjectID sourceId, const WeaponTemplate &weapon, const WeaponBonus &, Object *victim, const Coord3D *pos, bool, unsigned long long *unported)
{
	unsigned applied = 0;
	Object *source = sourceId != INVALID_ID ? logic.findObjectByID(sourceId) : nullptr;
	for (const std::shared_ptr<WeaponNugget> &np : weapon.m_nuggets)
	{
		const WeaponNugget &n = *np;
		if (n.kind() == NUGGET_META_IMPACT)
		{
			// lane COMBAT-4: the temporary weapon's fireWeaponTemplate (RW 0x6CF590 -> 0x6CC915) dispatches it as below
			const Coord3D at = victim ? *victim->getPosition() : (pos ? *pos : Coord3D{ 0.0f, 0.0f, 0.0f });
			MetaImpact::deliver(logic, static_cast<const MetaImpactNugget &>(n), weapon, sourceId, victim, at);
			continue;
		}
		if (n.kind() == NUGGET_PARALYZE)
		{
			const Coord3D at = victim ? *victim->getPosition() : (pos ? *pos : Coord3D{ 0.0f, 0.0f, 0.0f });
			Paralyze::deliver(logic, static_cast<const ParalyzeNugget &>(n), weapon, sourceId, victim, at); // lane DECOMP-1
			continue;
		}
		if (n.kind() == NUGGET_ATTRIBUTE_MODIFIER)
		{
			// lane DECOMP-1: the warhead path dispatches it as fireWeaponTemplate does
			const Coord3D at = victim ? *victim->getPosition() : (pos ? *pos : Coord3D{ 0.0f, 0.0f, 0.0f });
			AttributeModifier::deliver(logic, static_cast<const AttributeModifierNugget &>(n), weapon, sourceId, victim, at);
			continue;
		}
		if (n.kind() != NUGGET_DAMAGE && n.kind() != NUGGET_DOT) // DOTNugget: DamageNugget's vtable but slot 14 (lane DECOMP-1)
		{
			if (unported)
			{
				++*unported;
			}
			continue;
		}
		const DamageNugget &dn = static_cast<const DamageNugget &>(n);
		if (victim)
		{
			// RW 0x6CB7DD .. 0x6CB806 (the victim form, lane DECOMP-1 r3): slot 1 (RW 0x90E855) gates the whole nugget, then slot 5 (RW 0x90DAFD): Radius == 0 the
			// victim (slot 1 again, then slot 14), a positive Radius the radius damage around the victim (slot 6); an immune victim protects its neighbours
			if (!CombatQueries::isAlive(*victim) || !damageApplicable(logic, sourceId, dn, weapon, *victim))
			{
				continue;
			}
			if (dn.m_radius == 0.0f)
			{
				applied += applyToVictim(logic, sourceId, dn, weapon, *victim, nullptr) ? 1u : 0u;
			}
			else if (dn.m_radius > 0.0f)
			{
				radiusDamage(logic, sourceId, dn, weapon, *victim->getPosition(), unported);
			}
		}
		else if (upgradeTest(n, source))
		{
			// no victim (lane DECOMP-1 r4): the upgrade test, then slot 6 at the position (RW 0x90DEF0: max(Radius, 1.0)), so a Radius 0 warhead that lands
			// (a projectile without HitStoredTarget) still hurts what stands where it lands
			const Coord3D center = pos ? *pos : Coord3D{ 0.0f, 0.0f, 0.0f };
			radiusDamage(logic, sourceId, dn, weapon, center, unported);
		}
	}
	return applied;
}

ObjectWeaponDelivery::ObjectWeaponDelivery(Object &source, ObjectWeapons &weapons, const WeaponTemplate &weapon, const Weapon &firing)
	: m_source(&source)
	, m_weapons(&weapons)
	, m_weapon(&weapon)
	, m_firing(&firing)
{
}

// RW 0x6CC915 fireWeaponTemplate
void ObjectWeaponDelivery::fireWeaponTemplate(const WeaponBonus &bonus, int curBarrel, const WeaponShotTarget &target, bool scattered, const WeaponFireGate &gate)
{
	GameLogic &logic = m_source->logic();
	const WeaponTemplate &t = *m_weapon;
	GameLogicRandom &rng = logic.random();
	Object *victim = target.hasVictim ? logic.findObjectByID(target.victimID) : nullptr;
	Coord3D pos = target.victimPosition;
	ObjectWeapons::Stats &stats = m_weapons->statsMutable();
	// RW 0x6CCA2C .. 0x6CCAA1, the range gate: without argument 6 and past the leech-range deadline (Weapon + 0x50 > frame keeps it open) a target beyond the
	// maximum range that RW 0x6CC07C does not reach either, or inside the minimum range, returns before the fire FX, the projectile and the damage. The shot
	// is still spent: privateFireWeapon's tail (ammo, barrel, delay, reload) runs after this call whatever it did
	if (!gate.ignoreRanges && !(gate.leechRangeDeadline > logic.getFrame()) &&
		!m_weapons->deliveryRangeAllows(t, bonus, victim, victim ? *victim->getPosition() : target.victimPosition))
	{
		++stats.shotsOutOfRange;
		return;
	}
	++stats.shotsFired;
	if (victim)
	{
		++stats.shotsAtVictim;
		pos = *victim->getPosition();
	}
	emitFireFX(curBarrel, victim, pos);
	// RW 0x6CCBF9 .. 0x6CCC51 (lane COMBAT-4): DamageDealtAtSelfPosition (+ 0x11C) delivers at the source's position with no victim (so no miss roll either); the z of
	// the original target is kept when the source's AI answers its slot 0x184, which only the AI class of vtable RW 0xC2E1C8 does (RW 0x8CEF91; the others RW 0x851E97
	// return null): no ported AI is that class
	if (t.m_damageDealtAtSelfPosition)
	{
		pos = *m_source->getPosition();
		victim = nullptr;
	}
	// the miss rolls (scratch/weapon1/damage.md 2.7, in this order)
	bool miss = false;
	if (victim && !scattered)
	{
		if (t.m_infantryInaccuracyDist > 0.0f && victim->isKindOf((unsigned)CombatNames::kinds().infantry) && !t.m_disableScatterForTargetsOnWall)
		{
			miss = true; // (1) no roll
		}
	}
	// (2) RW 0x6CCCA7: HitPercentage rolls whatever the target is: with no victim (a position shot, or DamageDealtAtSelfPosition's cleared one) it still draws (lane COMBAT-4 r2)
	if (t.m_hitPercentage < 1.0f)
	{
		const float r = rng.getValueReal(0.0f, 1.0f, kWeaponCpp, 1489);
		if (r >= t.m_hitPercentage)
		{
			miss = true;
		}
	}
	if (victim && !scattered)
	{
		if (t.m_canBeDodged && victim->getBodyModule())
		{
			const float dodge = victim->getBodyModule()->getDodgePercent();
			if (dodge > 0.0f)
			{
				const float r = rng.getValueReal(0.0f, 1.0f, kWeaponCpp, 1495); // (3)
				if (dodge >= r)
				{
					miss = true;
				}
			}
		}
		// (4) HitPassengerPercentage: the victim's contain module (garrison) is not a passenger target in this lane (S-321)
	}
	if (miss)
	{
		// RW 0x6CC399 computeScatter: the position path, no victim
		float radius = t.m_scatterRadius;
		if (t.m_infantryInaccuracyDist > 0.0f && victim && victim->isKindOf((unsigned)CombatNames::kinds().infantry))
		{
			radius = SimMath::addf32(radius, t.m_infantryInaccuracyDist);
		}
		const float targetRadius = victim ? SimMath::addf32(CombatQueries::boundingCircleRadius(*victim), 5.0f) : 0.0f;
		if (radius > targetRadius)
		{
			radius = rng.getValueReal(targetRadius, radius, kWeaponCpp, 1625);
		}
		const Coord3D src = *m_source->getPosition();
		float angle;
		if (victim)
		{
			const float base = (float)SimMath::atan2d(SimMath::subf32(src.y, pos.y), SimMath::subf32(src.x, pos.x));
			const float half = 1.5707964f;
			angle = rng.getValueReal(SimMath::subf32(base, half), SimMath::addf32(base, half), kWeaponCpp, 1639);
		}
		else
		{
			angle = rng.getValueReal(0.0f, 6.28318548f, kWeaponCpp, 1643);
		}
		pos.x = SimMath::addf32(pos.x, SimMath::mulf32((float)SimMath::cosd(angle), radius));
		pos.y = SimMath::addf32(pos.y, SimMath::mulf32((float)SimMath::sind(angle), radius));
		pos.z = logic.getGroundHeight(pos.x, pos.y);
		victim = nullptr;
	}
	// the nugget loop in parse order
	for (const std::shared_ptr<WeaponNugget> &np : t.m_nuggets)
	{
		const WeaponNugget &n = *np;
		switch (n.kind())
		{
		case NUGGET_DAMAGE:
		case NUGGET_DOT: // lane DECOMP-1: DamageNugget's vtable except slot 14 (applyToVictim registers the damage over time)
		{
			const DamageNugget &dn = static_cast<const DamageNugget &>(n);
			if (victim)
			{
				// RW 0x6CCED3 .. 0x6CCEF5 (lane DECOMP-1 r3): slot 1 (RW 0x90E855) gates the whole nugget before slot 5 (RW 0x90DAFD): Radius == 0 the victim, a
				// positive Radius the radius damage around the victim (slot 6); an immune victim protects its neighbours
				if (!damageApplicable(logic, m_source->getID(), dn, t, *victim))
				{
					break;
				}
				if (dn.m_radius == 0.0f)
				{
					applyToVictim(logic, m_source->getID(), dn, t, *victim, nullptr);
				}
				else if (dn.m_radius > 0.0f)
				{
					radiusDamage(logic, m_source->getID(), dn, t, *victim->getPosition(), &logic.combat().counters().unportedNuggets);
				}
			}
			else if (upgradeTest(n, m_source))
			{
				// RW 0x6CCEFA .. 0x6CCF17: no victim: slot 2 (RW 0x911BFD -> the upgrade test RW 0x90D8F0) then slot 6 at the position (RW 0x90DEF0: max(Radius, 1.0),
				// so a Radius 0 nugget of a shot with no victim (a miss, a position shot) still reaches what stands at the point)
				radiusDamage(logic, m_source->getID(), dn, t, pos, &logic.combat().counters().unportedNuggets);
			}
			break;
		}
		case NUGGET_PROJECTILE:
		{
			const ProjectileNugget &pn = static_cast<const ProjectileNugget &>(n);
			if (!upgradeTest(n, m_source))
			{
				break;
			}
			ProjectileShot shot;
			shot.source = m_source->getID();
			shot.victim = victim ? victim->getID() : (ObjectID)INVALID_ID;
			shot.sourcePosition = *m_source->getPosition();
			shot.targetPosition = pos;
			shot.weapon = &t;
			shot.warhead = TheWeaponStore ? TheWeaponStore->findWeaponTemplate(pn.m_warheadTemplateName) : nullptr;
			shot.bonus = bonus;
			shot.barrel = curBarrel;
			shot.slot = m_firing->getSlot(); // the firing weapon's slot (lane DECOMP-1 r3); the nugget's WeaponLaunchBoneSlotOverride still wins in the launcher
			shot.nugget = &pn;
			if (pn.m_useAlwaysAttackOffset)
			{
				// RW 0x90FC99 (slot 6): the target is the AlwaysAttackHereOffset in the source's frame, whatever the victim is: ((m2*oz + m1*oy) + m0*ox) + t per row
				const float *b = m_source->getBasis();
				const Coord3D &o = pn.m_alwaysAttackHereOffset;
				const Coord3D &sp = *m_source->getPosition();
				auto row = [&](int r, float t0) {
					const float s = SimMath::addf32(SimMath::mulf32(b[r * 3 + 2], o.z), SimMath::mulf32(b[r * 3 + 1], o.y));
					return SimMath::addf32(SimMath::addf32(s, SimMath::mulf32(b[r * 3], o.x)), t0);
				};
				shot.targetPosition = Coord3D{ row(0, sp.x), row(1, sp.y), row(2, sp.z) };
				shot.victim = INVALID_ID;
			}
			if (!shot.warhead)
			{
				logic.reportError("weapon " + t.getName() + ": the ProjectileNugget's warhead '" + pn.m_warheadTemplateName + "' is not in the WeaponStore");
			}
			++stats.projectilesLaunched;
			logic.combat().launcher().launch(logic, shot);
			break;
		}
		case NUGGET_META_IMPACT:
			MetaImpact::deliver(logic, static_cast<const MetaImpactNugget &>(n), t, m_source->getID(), victim, pos); // lane COMBAT-4
			break;
		case NUGGET_PARALYZE:
			Paralyze::deliver(logic, static_cast<const ParalyzeNugget &>(n), t, m_source->getID(), victim, pos); // lane DECOMP-1
			break;
		case NUGGET_ATTRIBUTE_MODIFIER:
			AttributeModifier::deliver(logic, static_cast<const AttributeModifierNugget &>(n), t, m_source->getID(), victim, pos); // lane DECOMP-1
			break;
		case NUGGET_HORDE_ATTACK:
		{
			// lane ARCHER-1: the nugget's slots 5 / 6 (RW 0x911A58 at the victim, RW 0x911AC9 at the position): the source's contain (Object + 0x258) gives its horde
			// interface (slot 0x7C); none: nothing. Unless LockWeaponSlot is 5 the source's slot is locked temporarily (RW 0x69121A), then the interface's slot 4
			// (RW 0x875221: attackTargetNow(victim, ClosestMemberOnly)) or slot 0 (RW 0x875550: attackPositionNow) releases the ranks (HordeMemberPass.cpp)
			if (!upgradeTest(n, m_source))
			{
				break;
			}
			ContainModuleInterface *contain = m_source->getContain();
			HordeContainInterface *hi = contain ? contain->getHordeContainInterface() : nullptr;
			if (hi)
			{
				const HordeAttackNugget &hn = static_cast<const HordeAttackNugget &>(n);
				if (hn.m_lockWeaponSlot != 5)
				{
					// RW 0x69121A: the members first (contain slot 0x168), then the horde (lane ARCHER-1 r2: the horde alone left its members free to choose)
					ObjectWeapons::setObjectWeaponLock(*m_source, hn.m_lockWeaponSlot, LOCKED_TEMPORARILY);
				}
				if (victim)
				{
					hi->attackTargetNow(victim, hn.m_closestMemberOnly);
				}
				else
				{
					hi->attackPositionNow(pos);
				}
			}
			break;
		}
		default:
			++logic.combat().counters().unportedNuggets;
			break;
		}
	}
}

// RW 0x6CB85A(out, source, victim, flag 1), the aim query of the fire FX block (lane FX-2 review). TARGET FACTS: RW 0x6CB81A first asks the victim for the
// weapon's PreferredTargetBone (+0x90): a non-empty name found exactly once among the victim drawable's pristine bones (RW 0x68C5AF -> 0x672A73, then
// 0x70BCE7 to world) is the answer with no draw; otherwise GameLogicRandomValue(0, 0xBC614E = 12345678) is drawn (Weapon.cpp:1749, RW 0x6CB8C2..0x6CB8D3)
// and handed as the seed to RW 0x690BD2, the victim geometry's contact point. The contact point is stop S-362's approximation (the geometry centre, the seed
// unused) exactly as BezierProjectileBehavior::aimPosition has it. The bone answer comes from the launch bone provider (template data and logic state);
// without one (a GameLogic without the W3D assets) no bone is found.
Coord3D ObjectWeaponDelivery::fireFXAimPosition(const Object &victim)
{
	GameLogic &logic = m_source->logic();
	const std::string &boneName = m_weapon->m_preferredTargetBone;
	if (!boneName.empty())
	{
		float bone[12];
		ProjectileLaunchOffsets *provider = logic.combat().launchOffsets();
		if (provider && provider->singleLogicalBone(victim, boneName, bone))
		{
			// RW 0x70BCE7 convertBonePosToWorldPos: the victim's transform applied to the bone's translation, retail's SSE order
			const float *t = victim.getBasis();
			const Coord3D &p = *victim.getPosition();
			const float tt[3] = { p.x, p.y, p.z };
			float w[3];
			for (int r = 0; r < 3; ++r)
			{
				const float v = SimMath::addf32(SimMath::mulf32(t[r * 3 + 2], bone[11]), SimMath::mulf32(t[r * 3 + 1], bone[7]));
				w[r] = SimMath::addf32(SimMath::addf32(v, SimMath::mulf32(t[r * 3 + 0], bone[3])), tt[r]);
			}
			return Coord3D{ w[0], w[1], w[2] };
		}
	}
	const int seed = logic.random().getValue(0, 12345678, kWeaponCpp, 1749);
	(void)seed; // RW 0x690BD2's aim point choice (S-362)
	Coord3D aim = *victim.getPosition();
	aim.z = SimMath::addf32(aim.z, SimMath::mulf32(CombatQueries::geometry(victim).height, 0.5f));
	return aim;
}

// RW 0x6CC915, the fire FX block (0x6CCAA7..0x6CCBF6; lane FX-2). It runs when the source has a drawable (RW 0x70E013): every object has one in retail
// (RW 0x628882 makes it at creation; DrawableManager likewise), so the block is taken for every shot whatever the client shows; eligibility is never the
// player's. First the victim aim query (above: the logic draw of Weapon.cpp:1749 unless the preferred bone answers), BEFORE anything can return. Then the FX:
// FireFlankFX (+0xB4) when the victim is flanked by the source (RW 0x68FB63; the flank test is not ported, S-322: never) else FireFX (+0xA4, the REGULAR
// entry: RW reads no veterancy entry here), none while the logic frame is below the weapon's suspendFXFrame (+0x30). The local-player / stealth tests
// (RW 0x68B749, 0x694C0D, +0x135) belong to the viewing client: the event carries PlayFXWhenStealthed.
void ObjectWeaponDelivery::emitFireFX(int curBarrel, const Object *victim, const Coord3D &victimPos)
{
	GameLogic &logic = m_source->logic();
	const Coord3D aim = victim ? fireFXAimPosition(*victim) : victimPos;
	const WeaponTemplate &t = *m_weapon;
	const Weapon *w = m_firing; // the firing weapon's own suspend-FX frame (+ 0x30)
	if (w && w->getTemplate() == &t && logic.getFrame() < w->suspendFXFrame())
	{
		return;
	}
	if (!FXEventLog::isFXName(t.m_fireFXs[0]))
	{
		return;
	}
	FXEvent e = FXEventLog::objectEvent(FXEvent::WEAPON_FIRE_FX, "FireFX", logic.getFrame(), t.m_fireFXs[0], *m_source);
	e.secondary = victim ? victim->getID() : (ObjectID)INVALID_ID;
	e.secondaryPosition = aim;
	e.weaponSlot = m_firing->getSlot();
	e.barrel = curBarrel;
	e.weaponSpeed = t.m_weaponSpeed;
	e.playWhenStealthed = t.m_playFXWhenStealthed;
	logic.fxEvents().emit(e);
}

// RW 0x6CB7BD: a projectile detonation delivers the nuggets of the template directly (the warhead path of DeliverNuggets)
void ObjectWeaponDelivery::fireProjectileDetonation(const WeaponBonus &bonus, const WeaponShotTarget &target)
{
	GameLogic &logic = m_source->logic();
	Object *victim = target.hasVictim ? logic.findObjectByID(target.victimID) : nullptr;
	DeliverNuggets(logic, m_source->getID(), *m_weapon, bonus, victim, &target.victimPosition, true, &logic.combat().counters().unportedNuggets);
}

// RW 0x6CAB32 processRequestAssistance: RequestAssistRange (retail data: a few weapons) asks allies to help; the AI lane's reaction is not ported (S-325)
void ObjectWeaponDelivery::requestAssistance(const WeaponShotTarget &)
{
}

// OpenBFME. GPL-3.0.
// See GameLogic/Combat/ObjectWeapons.h.

#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Module/SquishCollide.h"

#include "Common/Audio/AudioRequests.h"
#include "Common/Player.h"
#include "Common/GameCommon.h"
#include "Common/PlayerList.h"
#include "Common/StateHash.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/InvisibilityModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/System/InvisibilityManager.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/System/ShroudManager.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponNugget.h"
#include "GameLogic/WeaponSet.h"

#include <cstring>
#include <stdexcept>

namespace
{
const char *const kWeaponSetCpp = "WeaponSet.cpp"; // RW 0xC16C08

int popcount128(const std::array<std::uint32_t, 4> &a)
{
	int n = 0;
	for (std::uint32_t w : a)
	{
		while (w)
		{
			w &= w - 1;
			++n;
		}
	}
	return n;
}

// the model conditions of one weapon slot (RW 0x6C84C9 / 0x6C83FD: slots A .. E)
struct SlotConditions
{
	int preattack, firing, firingOrPreattack, firingOrReloading, betweenShots, reloading, usingWeapon, lockBit;
};

const SlotConditions &slotConditions(int slot)
{
	static const SlotConditions table[5] = {
		{ CombatNames::modelCondition("PREATTACK_A"), CombatNames::modelCondition("FIRING_A"), CombatNames::modelCondition("FIRING_OR_PREATTACK_A"),
			CombatNames::modelCondition("FIRING_OR_RELOADING_A"), CombatNames::modelCondition("BETWEEN_FIRING_SHOTS_A"), CombatNames::modelCondition("RELOADING_A"),
			CombatNames::modelCondition("USING_WEAPON_A"), CombatNames::modelCondition("WEAPONLOCK_PRIMARY") },
		{ CombatNames::modelCondition("PREATTACK_B"), CombatNames::modelCondition("FIRING_B"), CombatNames::modelCondition("FIRING_OR_PREATTACK_B"),
			CombatNames::modelCondition("FIRING_OR_RELOADING_B"), CombatNames::modelCondition("BETWEEN_FIRING_SHOTS_B"), CombatNames::modelCondition("RELOADING_B"),
			CombatNames::modelCondition("USING_WEAPON_B"), CombatNames::modelCondition("WEAPONLOCK_SECONDARY") },
		{ CombatNames::modelCondition("PREATTACK_C"), CombatNames::modelCondition("FIRING_C"), CombatNames::modelCondition("FIRING_OR_PREATTACK_C"),
			CombatNames::modelCondition("FIRING_OR_RELOADING_C"), CombatNames::modelCondition("BETWEEN_FIRING_SHOTS_C"), CombatNames::modelCondition("RELOADING_C"),
			CombatNames::modelCondition("USING_WEAPON_C"), CombatNames::modelCondition("WEAPONLOCK_TERTIARY") },
		{ CombatNames::modelCondition("PREATTACK_D"), CombatNames::modelCondition("FIRING_D"), CombatNames::modelCondition("FIRING_OR_PREATTACK_D"),
			CombatNames::modelCondition("FIRING_OR_RELOADING_D"), CombatNames::modelCondition("BETWEEN_FIRING_SHOTS_D"), CombatNames::modelCondition("RELOADING_D"),
			CombatNames::modelCondition("USING_WEAPON_D"), CombatNames::modelCondition("WEAPONLOCK_QUATERNARY") },
		{ CombatNames::modelCondition("PREATTACK_E"), CombatNames::modelCondition("FIRING_E"), CombatNames::modelCondition("FIRING_OR_PREATTACK_E"),
			CombatNames::modelCondition("FIRING_OR_RELOADING_E"), CombatNames::modelCondition("BETWEEN_FIRING_SHOTS_E"), CombatNames::modelCondition("RELOADING_E"),
			CombatNames::modelCondition("USING_WEAPON_E"), CombatNames::modelCondition("WEAPONLOCK_QUINARY") },
	};
	return table[slot];
}

void addBit(Object::ModelConditionBits &bits, int bit)
{
	bits[(size_t)bit >> 5] |= 1u << (bit & 31);
}
} // namespace

// RW 0x73D89F / 0x73D917 (the same best match as FindArmorTemplateSet over the 128 bit set conditions): the most conditions met first, then the fewest unmet
const WeaponTemplateSet *FindWeaponTemplateSet(const std::vector<WeaponTemplateSet> &sets, const WeaponSetFlags &flags)
{
	const WeaponTemplateSet *best = nullptr;
	int bestYes = 0;
	int bestExtra = 999;
	for (const WeaponTemplateSet &s : sets)
	{
		WeaponSetFlags yes{}, extra{};
		for (size_t i = 0; i < 4; ++i)
		{
			yes[i] = s.m_types[i] & flags[i];
			extra[i] = s.m_types[i] & ~flags[i];
		}
		const int y = popcount128(yes);
		const int x = popcount128(extra);
		if (y > bestYes || (y == bestYes && x < bestExtra))
		{
			best = &s;
			bestYes = y;
			bestExtra = x;
		}
	}
	return best;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// the hosts
// ---------------------------------------------------------------------------------------------------------------------------------
class ObjectWeapons::Host : public WeaponHost
{
public:
	explicit Host(ObjectWeapons &w)
		: m_w(w)
	{
	}
	std::uint32_t currentFrame() override { return m_w.owner().logic().getFrame(); }
	GameLogicRandom &logicRandom() override { return m_w.owner().logic().random(); }
	std::uint32_t weaponBonusConditionMask() override { return m_w.owner().weaponBonusConditionMask(); }
	const WeaponBonusSet *globalWeaponBonusSet() override { return nullptr; } // GameData's WeaponBonus set is not loaded (S-320): the bonus of no condition is 1.0
	float rateOfFireAttributeProduct() override { return 1.0f; }              // the attribute modifiers (S-320)
	bool containedAmmo(const ObjectFilter &, unsigned &count) override
	{
		count = 0;
		return false;
	}
	bool ownerHasContainAmmo(const ObjectFilter &) override { return false; }
	int barrelCount(int) override { return 1; } // the drawable's barrel count (S-320)
	int firingTrackerShotsAtTarget(bool hasVictim, unsigned victimID, const Coord3D &pos) override { return m_w.m_tracker.getShotsAtTarget(hasVictim, victimID, pos); }
	bool ownerPositionMatchesLastShot() override
	{
		const Coord3D &a = *m_w.owner().getPosition();
		const Coord3D &b = m_w.m_tracker.lastOwnerPosition();
		return a.x == b.x && a.y == b.y && a.z == b.z; // RW 0x403270: exact float compare
	}
	void setOwnerDisabledUntil(std::uint32_t frame) override { m_w.owner().setDisabled(8, frame); } // DISABLED_TEMPORARILY_BUSY (type 8)
	float groundHeightAt(float x, float y) override { return m_w.owner().logic().getGroundHeight(x, y); }
	Coord3D linearTargetPosition(float offsetX, float offsetY) override
	{
		const Object &o = m_w.owner();
		const float a = o.getOrientation();
		const float c = (float)SimMath::cosd(a), s = (float)SimMath::sind(a);
		Coord3D p = *o.getPosition();
		const float x = SimMath::subf32(SimMath::mulf32(offsetX, c), SimMath::mulf32(offsetY, s));
		const float y = SimMath::addf32(SimMath::mulf32(offsetX, s), SimMath::mulf32(offsetY, c));
		p.x = SimMath::addf32(p.x, x);
		p.y = SimMath::addf32(p.y, y);
		p.z = m_w.owner().logic().getGroundHeight(p.x, p.y);
		return p;
	}

private:
	ObjectWeapons &m_w;
};

class ObjectWeapons::RangeHost : public WeaponRangeHost
{
public:
	explicit RangeHost(ObjectWeapons &w) : m_w(w) {}
	ObjectWeapons &m_w;
	bool rangeAttributeSum(float &) override { return false; }
	// lane GARRISON-1: GameData GarrisonedRangeMultiplier (RW + 0x1224; RW 0x6CA61F applies it when >= 0 and the source is INSIDE_GARRISON)
	float garrisonRangeScale() override { return m_w.owner().logic().settings().garrisonedRangeMultiplier; }
	// lane GARRISON-1: RW 0x6FF412(source, 3): with an AI, the bounding radius plus the vision range (RW 0x68E43B: Object + 0x1B0, the template's VisionRange; its
	// VISION attribute modifier and height bonus terms RW 0x68E459 .. 0x68E4DC are not ported, as STEALTH-1's detector range); the AI mood's AIData multipliers
	// (RW 0x6FF44A .. 0x6FF47E: + 0x4C / + 0x50 for the guard moods) are not ported (S-1103)
	float garrisonRangeCap(const RangeSubject &source) override
	{
		const Object &o = m_w.owner();
		if (!o.getAIUpdateInterface())
		{
			return 0.0f; // RW 0x6FF431: [0xC1B594] = 0
		}
		float vision = 0.0f;
		if (const FieldValue *v = static_cast<const ThingTemplate *>(o.getTemplate())->getFinalOverride()->findField("VisionRange"))
		{
			if (const float *f = std::get_if<float>(v))
			{
				vision = *f;
			}
		}
		return SimMath::addf32(source.boundingCircleRadius, vision);
	}
	float boxDistanceSquared(const RangeSubject &a, const RangeSubject &b) override
	{
		return CircleDistanceSquared(a.position, a.boundingCircleRadius, b.position, b.boundingCircleRadius); // a box counts as its bounding circle (S-320)
	}
	// RW 0x6ED843 (the partition's melee reach test): the edge-to-edge distance within the weapon's reach (set by the caller, S-320)
	bool meleeReach(const RangeSubject &source, const RangeSubject &victim) override
	{
		return CircleDistanceSquared(source.position, source.boundingCircleRadius, victim.position, victim.boundingCircleRadius) <= SimMath::mulf32(m_meleeReach, m_meleeReach);
	}
	float meleeRunDownLimit() override { return 0.0f; }
	bool victimFlag11A_40() override { return false; }
	int layerOf(bool) override { return 0; }
	bool victimLayerException() override { return false; }
	float m_meleeReach = 0.0f;
};

class ObjectWeapons::TrackerHost : public FiringTrackerHost
{
public:
	explicit TrackerHost(ObjectWeapons &w)
		: m_w(w)
	{
	}
	std::uint32_t currentFrame() override { return m_w.owner().logic().getFrame(); }
	Coord3D ownerPosition() override { return *m_w.owner().getPosition(); }
	std::uint32_t weaponBonusConditionMask() override { return m_w.owner().weaponBonusConditionMask(); }
	void setWeaponBonusConditionMask(std::uint32_t mask) override
	{
		// RW 0x8E3174 sets / clears the CONTINUOUS_FIRE_MEAN / FAST bits (2, 3) of the owner's weapon bonus mask
		for (int bit : { (int)FiringTracker::CONDITION_CONTINUOUS_FIRE_MEAN, (int)FiringTracker::CONDITION_CONTINUOUS_FIRE_FAST })
		{
			m_w.owner().setWeaponBonusCondition(bit, (mask >> bit) & 1u);
		}
	}
	void reloadAllWeapons() override { m_w.reloadAllAmmo(false); }

private:
	ObjectWeapons &m_w;
};

ObjectWeapons::ObjectWeapons(Object &owner)
	: m_owner(&owner)
	, m_host(std::make_unique<Host>(*this))
	, m_rangeHost(std::make_unique<RangeHost>(*this))
	, m_trackerHost(std::make_unique<TrackerHost>(*this))
{
	updateWeaponSet();
	makeCrushWeapons();
}

// lane HORDE-2 (RW 0x693E19-0x693E79): the template's CrushWeapon (+0x610) and CrushRevengeWeapon (+0x614) become weapons of slot 0 owned by the object, loaded at once
void ObjectWeapons::makeCrushWeapons()
{
	const CrushTemplateInfo &info = CrushTemplateInfo::cached(*m_owner);
	const std::uint32_t now = m_owner->logic().getFrame();
	auto make = [&](const std::string &name, std::unique_ptr<Weapon> &out) {
		const WeaponTemplate *tmpl = (!name.empty() && TheWeaponStore) ? TheWeaponStore->findWeaponTemplate(name) : nullptr;
		if (!tmpl)
		{
			return;
		}
		out = std::make_unique<Weapon>(tmpl, PRIMARY_WEAPON, now);
		out->setOwnerID(m_owner->getID());
		out->loadAmmoNow(host());
	};
	make(info.crushWeapon, m_crushWeapon);
	make(info.crushRevengeWeapon, m_crushRevengeWeapon);
}

// RW 0x6CF328 -> privateFireWeapon RW 0x6CEF6D with the victim as the target
void ObjectWeapons::fireExtraWeapon(Weapon &w, Object &victim)
{
	if (!w.getTemplate())
	{
		return;
	}
	Weapon::FireArgs args;
	args.target.hasVictim = true;
	args.target.victimID = victim.getID();
	args.target.victimPosition = *victim.getPosition();
	ObjectWeaponDelivery delivery(*m_owner, *this, *w.getTemplate());
	w.privateFireWeapon(host(), delivery, args);
}

// RW 0x6CF3D2 -> privateFireWeapon RW 0x6CEF6D with the position as the target (its sixth argument 1: ZH forceFireWeapon's ignoreRanges)
void ObjectWeapons::fireExtraWeaponAt(Weapon &w, const Coord3D &pos)
{
	if (!w.getTemplate())
	{
		return;
	}
	Weapon::FireArgs args;
	args.target.hasVictim = false;
	args.target.victimPosition = pos;
	args.ignoreRanges = true; // RW 0x6CF3D2 pushes argument 6 = 1
	ObjectWeaponDelivery delivery(*m_owner, *this, *w.getTemplate());
	w.privateFireWeapon(host(), delivery, args);
}

std::unique_ptr<Weapon> ObjectWeapons::makeExtraWeapon(const WeaponTemplate *t)
{
	if (!t)
	{
		return nullptr;
	}
	std::unique_ptr<Weapon> w = std::make_unique<Weapon>(t, PRIMARY_WEAPON, m_owner->logic().getFrame());
	w->setOwnerID(0);
	w->loadAmmoNow(host());
	return w;
}

bool ObjectWeapons::hasWeaponSetFor(int bit) const
{
	for (const WeaponTemplateSet &s : m_owner->getTemplate()->weaponTemplateSets())
	{
		if (s.testWeaponSetFlag(bit))
		{
			return true;
		}
	}
	return false;
}

int ObjectWeapons::extraWeaponStatus(Weapon &w)
{
	return w.getStatusWriteBack(host());
}

ObjectWeapons::~ObjectWeapons() = default;

WeaponHost &ObjectWeapons::host()
{
	return *m_host;
}

WeaponRangeHost &ObjectWeapons::rangeHost()
{
	return *m_rangeHost;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// the set
// ---------------------------------------------------------------------------------------------------------------------------------
void ObjectWeapons::setWeaponSetFlags(const WeaponSetFlags &mask, bool on)
{
	for (size_t i = 0; i < m_flags.size(); ++i)
	{
		m_flags[i] = on ? (m_flags[i] | mask[i]) : (m_flags[i] & ~mask[i]);
	}
	updateWeaponSet();
}

// lane ANIM-1: RW 0x691059 (set) / 0x691106 (clear) Object::setWeaponSetFlag / clearWeaponSetFlag: the flag, the set update (RW 0x6C99E2), then the model
// condition the table RW 0xC16958 names for the flag (-1: none) is set / cleared when it differs (RW 0x68B53C tells the drawable), and a change of
// WEAPONSET_TOGGLE_1 / 2 / 3 (model conditions 0x12D .. 0x12F) starts the special model condition SWAPPING_TO_WEAPONSET_1 / 2 / 3 (0x1BD .. 0x1BF) for
// [RW 0xD9F608] = 5 frames through the object's SMC helper (RW 0x8E2C0F on Object + 0x238), on and off alike. Retail runs every step on every call (no
// early return for an unchanged flag). The Rohirrim's bow mode used to keep the spear model and none of the bow states matched (FEEDBACK-2 G8): the
// draw states ask for the MODEL condition WEAPONSET_TOGGLE_1, which only this mirror sets.
void ObjectWeapons::setWeaponSetFlag(int bit, bool on)
{
	if (bit < 0 || bit >= 128)
	{
		throw std::logic_error("ObjectWeapons::setWeaponSetFlag: bit out of range");
	}
	if (on)
	{
		m_flags[(size_t)bit >> 5] |= 1u << (bit & 31);
	}
	else
	{
		m_flags[(size_t)bit >> 5] &= ~(1u << (bit & 31));
	}
	updateWeaponSet();
	const int condition = WeaponSetModelCondition(bit);
	if (condition >= 0)
	{
		m_owner->setModelConditionState(condition, on); // a no-op when the bit already has that value (RW 0x6910A5 / 0x691151 test it first)
	}
	static const int kToggle1 = CombatNames::modelCondition("WEAPONSET_TOGGLE_1");
	static const int kSwapping1 = CombatNames::modelCondition("SWAPPING_TO_WEAPONSET_1");
	if (condition >= 0 && condition >= kToggle1 && condition <= kToggle1 + 2)
	{
		m_owner->setSpecialModelConditionState(kSwapping1 + (condition - kToggle1), (UnsignedInt)LOGICFRAMES_PER_SECOND); // RW 0x6910EB: push [0xD9F608]
	}
}

// RW 0xC16958: the model condition of each WeaponSetFlags bit (104 entries, the WeaponSetFlags name list's length), -1 for none. Engine data read from the
// binary (tools/anim/anim1_binary_facts.py pins it against game.dat); the indices are the RotWK model condition registry's (Common/ModelConditionNames.inc).
int WeaponSetModelCondition(int weaponSetBit)
{
	static const short kTable[104] = {
		12, 13, 14, 17, 15, 16, 143, 159, 161, 192, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 241, 247, 250, 251, 301, 302, 303, 304, 253,
		409, 410, 411, 412, 413, 414, 415, 416, 417, 418, 419, 420, 421, 422, 423, 424, 425, 426, 427, 428, 429, 430, 431, 432, 433, 434, 435, 436,
		437, 438, 439, 440, 259, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 553, 554, 555, 556, 557, 558, 559, 560, 561, 562, 563, 564, 565, 566, 567,
		568, 569, 570, 571, 572, 573, 574, 575, 576, 577, 578, 579, 580, 581, 582, 583, 584 };
	return weaponSetBit >= 0 && weaponSetBit < 104 ? kTable[weaponSetBit] : -1;
}

// RW 0x6C99E2
void ObjectWeapons::updateWeaponSet()
{
	const ThingTemplate *tt = m_owner->getTemplate();
	const WeaponTemplateSet *set = FindWeaponTemplateSet(tt->weaponTemplateSets(), m_flags);
	if (set == m_set && m_set)
	{
		return;
	}
	rebuildWeapons(set);
}

void ObjectWeapons::rebuildWeapons(const WeaponTemplateSet *set)
{
	const WeaponTemplateSet *old = m_set;
	// RW 0x6CA178 / 0x6CEE89: the old weapons' timing records carry over only when BOTH sets share the reload time
	struct Record
	{
		const WeaponTemplate *tmpl = nullptr;
		unsigned ammo = 0;
		int status = 0;
		std::uint32_t timerStart = 0, when = 0;
	};
	Record saved[WEAPONSLOT_COUNT];
	const bool share = old && set && old->m_isReloadTimeShared && set->m_isReloadTimeShared;
	if (share)
	{
		for (int i = 0; i < WEAPONSLOT_COUNT; ++i)
		{
			if (const Weapon *w = m_weapons[(size_t)i].get())
			{
				saved[i] = Record{ w->getTemplate(), w->ammoInClip(), w->storedStatus(), w->timerStart(), w->whenWeCanFireAgain() };
			}
		}
	}
	clearSlotConditions();
	for (std::unique_ptr<Weapon> &w : m_weapons)
	{
		w.reset();
	}
	m_set = set;
	if (!(old && set && set->m_isWeaponLockSharedAcrossSets))
	{
		m_lockType = NOT_LOCKED;
		m_curSlot = PRIMARY_WEAPON;
	}
	if (!set)
	{
		return;
	}
	const std::uint32_t now = m_owner->logic().getFrame();
	for (int i = 0; i < WEAPONSLOT_COUNT; ++i)
	{
		const WeaponTemplate *tmpl = set->m_template[i];
		if (!tmpl)
		{
			continue;
		}
		std::unique_ptr<Weapon> w = std::make_unique<Weapon>(tmpl, i, now);
		w->setOwnerID(m_owner->getID());
		bool restored = false;
		for (int j = 0; j < WEAPONSLOT_COUNT && share; ++j)
		{
			if (saved[j].tmpl == tmpl)
			{
				w->setAmmoInClip(saved[j].ammo);
				w->setStatus(saved[j].status);
				w->setWhenWeCanFireAgain(saved[j].when);
				restored = true;
				break;
			}
		}
		if (!restored)
		{
			if (tmpl->m_instantLoadClipOnActivate)
			{
				w->loadAmmoNow(host());
			}
			else
			{
				w->reloadAmmo(host());
			}
		}
		m_weapons[(size_t)i] = std::move(w);
	}
}

void ObjectWeapons::clearSlotConditions()
{
	// the model conditions of the weapons that go (the new set's weapons set their own on the next status update)
	Object::ModelConditionBits clear{};
	for (int i = 0; i < 5; ++i)
	{
		const SlotConditions &c = slotConditions(i);
		for (int bit : { c.preattack, c.firing, c.firingOrPreattack, c.firingOrReloading, c.betweenShots, c.reloading, c.usingWeapon })
		{
			addBit(clear, bit);
		}
	}
	m_owner->clearAndSetModelConditionFlags(clear, Object::ModelConditionBits{});
	m_slotCondition.fill(0);
}

bool ObjectWeapons::hasAnyWeapon() const
{
	for (const std::unique_ptr<Weapon> &w : m_weapons)
	{
		if (w)
		{
			return true;
		}
	}
	return false;
}

bool ObjectWeapons::hasAnyDamageWeapon() const
{
	for (const std::unique_ptr<Weapon> &w : m_weapons)
	{
		if (!w || !w->getTemplate())
		{
			continue;
		}
		for (const std::shared_ptr<WeaponNugget> &n : w->getTemplate()->m_nuggets)
		{
			if (n->kind() == NUGGET_DAMAGE || n->kind() == NUGGET_PROJECTILE)
			{
				return true;
			}
		}
	}
	return false;
}

bool ObjectWeapons::isOutOfAmmo() const
{
	bool any = false;
	for (const std::unique_ptr<Weapon> &w : m_weapons)
	{
		if (!w)
		{
			continue;
		}
		any = true;
		if (w->getStatus(const_cast<ObjectWeapons *>(this)->host()) != WEAPON_OUT_OF_AMMO)
		{
			return false;
		}
	}
	return any;
}

// RW 0x6C97F9
bool ObjectWeapons::setWeaponLock(int slot, WeaponLockType type)
{
	if (type == NOT_LOCKED || !weaponInSlot(slot))
	{
		return false;
	}
	if (m_lockType == LOCKED_PERMANENTLY && type == LOCKED_TEMPORARILY)
	{
		return false; // a temporary lock never overrides a permanent one
	}
	m_curSlot = slot;
	m_lockedSlot = slot;
	m_lockType = type;
	if (slot < 5)
	{
		m_owner->setModelConditionState(slotConditions(slot).lockBit, true);
	}
	return true;
}

// RW 0x6C98E6
void ObjectWeapons::releaseWeaponLock(WeaponLockType type)
{
	if (m_lockType == NOT_LOCKED)
	{
		return;
	}
	if (type == LOCKED_PERMANENTLY || (type == LOCKED_TEMPORARILY && m_lockType == LOCKED_TEMPORARILY))
	{
		if (m_lockedSlot < 5)
		{
			m_owner->setModelConditionState(slotConditions(m_lockedSlot).lockBit, false);
		}
		m_lockType = NOT_LOCKED;
	}
}

// RW 0x6C80E5
void ObjectWeapons::reloadAllAmmo(bool now)
{
	for (std::unique_ptr<Weapon> &w : m_weapons)
	{
		if (w)
		{
			if (now)
			{
				w->loadAmmoNow(host());
			}
			else
			{
				w->reloadAmmo(host());
			}
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// choosing
// ---------------------------------------------------------------------------------------------------------------------------------
unsigned ObjectWeapons::victimAntiMask(const Object &victim)
{
	const CombatNames::Kind &k = CombatNames::kinds();
	if (victim.isKindOf((unsigned)k.aircraft))
	{
		return WEAPON_ANTI_AIRBORNE_VEHICLE;
	}
	if (victim.isKindOf((unsigned)k.projectile))
	{
		return WEAPON_ANTI_PROJECTILE;
	}
	if (victim.isKindOf((unsigned)k.mine))
	{
		return WEAPON_ANTI_MINE;
	}
	if (victim.isKindOf((unsigned)k.structure))
	{
		return WEAPON_ANTI_GROUND | WEAPON_ANTI_STRUCTURE; // a weapon with either class can hit a building (inference: a sword with the default AntiGround hits one)
	}
	return WEAPON_ANTI_GROUND;
}

float ObjectWeapons::estimateWeaponDamage(const Weapon &weapon, const Object &victim) const
{
	const WeaponTemplate *t = weapon.getTemplate();
	if (!t)
	{
		return 0.0f;
	}
	const WeaponTemplate *source = t;
	const DamageNugget *dn = nullptr;
	for (int pass = 0; pass < 2 && !dn; ++pass)
	{
		for (const std::shared_ptr<WeaponNugget> &n : source->m_nuggets)
		{
			if (n->kind() == NUGGET_DAMAGE)
			{
				dn = static_cast<const DamageNugget *>(n.get());
				break;
			}
			if (pass == 0 && n->kind() == NUGGET_PROJECTILE && TheWeaponStore)
			{
				const WeaponTemplate *wh = TheWeaponStore->findWeaponTemplate(static_cast<const ProjectileNugget *>(n.get())->m_warheadTemplateName);
				if (wh)
				{
					source = wh;
					break; // look in the warhead on the next pass
				}
			}
		}
		if (source == t)
		{
			break; // no warhead found: the first pass was the only one
		}
	}
	if (!dn)
	{
		return 0.0f;
	}
	if (!victim.getBodyModule())
	{
		return dn->m_damage; // a victim without a body takes the nugget's damage (retail: no health to lose; the shot is still pointless but possible)
	}
	DamageInfoInput in;
	in.m_amount = dn->m_damage;
	in.m_damageType = dn->m_damageType;
	return victim.getBodyModule()->estimateDamage(in);
}

// ZH WeaponSet::chooseBestWeaponForTarget, RotWK RW 0x6C8A4E (one real draw per candidate: WeaponSet.cpp:1478)
bool ObjectWeapons::chooseBestWeaponForTarget(const Object *victim, WeaponChoiceCriteria criteria, CommandSourceType source)
{
	if (isCurWeaponLocked())
	{
		return true;
	}
	if (!victim)
	{
		m_curSlot = PRIMARY_WEAPON;
		return true;
	}
	if (!m_set)
	{
		return false;
	}
	if (m_set->m_template[1] || m_set->m_template[2] || m_set->m_template[3] || m_set->m_template[4])
	{
		// lane ANIM-1: a choice between weapons depends on the scoring of RW 0x6C8A4E that this port does not reproduce
		static const std::string kStopChoice =
			"[S-1582] weapon choice (lane ANIM-1): RW 0x6C8A4E is ported for PRE_ATTACK counting as ready (RW 0x6C8C72), OnlyAgainst (RW 0x6C8D21) and "
			"OnlyInCondition (RW 0x6C8E39); not ported: its score is 1 / 0 (the weapon can damage the victim, RW 0x6CDBF3) where this port estimates the damage "
			"(ZH), the range and distance-outside-range compares of the criteria 1 .. 4, the minimum range tests of the ready flag, the "
			"template byte + 0x170 and RW 0x662398 filters, ReadyStatusSharedWithinSet (set + 0x364), the victimless branch (RW 0x6C8AA7: the first slot that is not out of ammo)";
		m_owner->logic().noteStop(kStopChoice);
	}
	bool found = false, foundBackup = false;
	float longestRange = 0.0f, bestDamage = 0.0f, longestRangeBackup = 0.0f, bestDamageBackup = 0.0f;
	int decision = PRIMARY_WEAPON, decisionBackup = PRIMARY_WEAPON;
	const unsigned victimMask = victimAntiMask(*victim);
	for (int i = WEAPONSLOT_COUNT - 1; i >= PRIMARY_WEAPON; --i)
	{
		Weapon *w = weaponInSlot(i);
		if (!w || !w->getTemplate())
		{
			continue;
		}
		const std::uint32_t okSources = m_set->m_autoChooseMask[i];
		if ((okSources & (1u << (unsigned)source)) == 0)
		{
			continue;
		}
		const int status = w->getStatus(host());
		const WeaponTemplate &t = *w->getTemplate();
		if (status == WEAPON_OUT_OF_AMMO && t.m_autoReloadsClip != AUTO_RELOAD)
		{
			continue;
		}
		if (!(t.m_antiMask & victimMask))
		{
			continue;
		}
		float damage = estimateWeaponDamage(*w, *victim);
		BonusRange br = bonusRangeOf(*w, *victim);
		float attackRange = br.range;
		// lane ANIM-1, TARGET RW 0x6C8C72 .. 0x6C8C82 (`status == 0 || status == 4`): a weapon in its PRE_ATTACK counts as ready, so the unit keeps the weapon
		// whose wind-up it started. The port took READY_TO_FIRE alone (the ZH rule): a mountain troll began its punch (slot 0), the punch was no longer "ready"
		// and the next frame chose its ready shoulder bash and struck at once, before the swing (FEEDBACK-2 G5). The rest of RW 0x6C8A4E's scoring is not
		// this port's (S-1582)
		bool ready = status == WEAPON_READY_TO_FIRE || status == WEAPON_PRE_ATTACK;
		bool hordeWeapon = t.m_meleeWeapon;
		for (const std::shared_ptr<WeaponNugget> &n : t.m_nuggets)
		{
			hordeWeapon = hordeWeapon || n->kind() == NUGGET_HORDE_ATTACK;
		}
		if (damage <= 0.0f && t.m_damageType != DAMAGE_UNRESISTABLE && !hordeWeapon)
		{
			continue; // a weapon that does no damage cannot be chosen; a horde's rangefinder (a HordeAttackNugget or a MeleeWeapon without nuggets) is chosen for the range it gives (S-327)
		}
		(void)m_owner->logic().random().getValueReal(0.0f, 1.0f, kWeaponSetCpp, 1478); // RW 0x6C8CE2: the tiebreak draw (its use is not read: inference S-320)
		bool preferred = false;
		const KindOfMaskType &pref = m_set->m_preferredAgainst[i];
		bool anyPref = false;
		for (std::uint32_t word : pref)
		{
			anyPref = anyPref || word != 0;
		}
		if (anyPref)
		{
			for (size_t wd = 0; wd < pref.size(); ++wd)
			{
				if (pref[wd] & victim->getKindOf()[wd])
				{
					preferred = true;
				}
			}
		}
		// lane ANIM-1, TARGET RW 0x6C8D21 .. 0x6C8DC3: OnlyAgainst (set + 0xEC + slot * 0x1C; any bit set, RW 0x6C824C): a victim of none of its KindOf
		// (RW 0x70C548) scores -1 and is not ready, so the slot is never chosen (both bests start at 0); a victim it names scores 100000 (RW 0xBDCF20).
		// The mountain troll's shoulder bash (OnlyAgainst = SECONDARY STRUCTURE BLOCKING_GATE) used to be chosen against infantry
		const KindOfMaskType &only = m_set->m_onlyAgainst[i];
		bool anyOnly = false;
		for (std::uint32_t word : only)
		{
			anyOnly = anyOnly || word != 0;
		}
		if (anyOnly)
		{
			bool match = false;
			for (size_t wd = 0; wd < only.size(); ++wd)
			{
				match = match || (only[wd] & victim->getKindOf()[wd]) != 0;
			}
			damage = match ? 100000.0f : -1.0f;
			attackRange = damage;
			ready = match && status != WEAPON_OUT_OF_AMMO;
		}
		if (preferred)
		{
			damage = 1e10f;
			attackRange = 1e10f;
			ready = status != WEAPON_OUT_OF_AMMO;
		}
		// lane ANIM-1, TARGET RW 0x6C8E39 .. 0x6C8EB0: OnlyInCondition (set + 0x194 + slot * 0x4C; any bit set, RW 0x4B3783): the owner's model conditions
		// must hold every bit of it (RW 0x4CE8CA); otherwise -1 and not ready, else 1e10 (RW 0xBF7328). The troll's tertiary punch is OnlyInCondition MOVING
		const ModelConditionMask &cond = m_set->m_onlyInCondition[i];
		bool anyCond = false, allCond = true;
		for (size_t wd = 0; wd < cond.size(); ++wd)
		{
			anyCond = anyCond || cond[wd] != 0;
			allCond = allCond && (m_owner->getModelConditionBits()[wd] & cond[wd]) == cond[wd];
		}
		if (anyCond)
		{
			damage = allCond ? 1e10f : -1.0f;
			attackRange = damage;
			ready = allCond && status != WEAPON_OUT_OF_AMMO;
		}
		if (criteria == PREFER_MOST_DAMAGE)
		{
			if (!ready)
			{
				if (damage >= bestDamageBackup)
				{
					bestDamageBackup = damage;
					decisionBackup = i;
					foundBackup = true;
				}
			}
			else if (damage >= bestDamage)
			{
				bestDamage = damage;
				decision = i;
				found = true;
			}
		}
		else
		{
			if (!ready)
			{
				if (attackRange > longestRangeBackup)
				{
					longestRangeBackup = attackRange;
					decisionBackup = i;
					foundBackup = true;
				}
			}
			else if (attackRange > longestRange)
			{
				longestRange = attackRange;
				decision = i;
				found = true;
			}
		}
	}
	if (found)
	{
		m_curSlot = decision;
	}
	else if (foundBackup)
	{
		m_curSlot = decisionBackup;
		found = true;
	}
	else
	{
		m_curSlot = PRIMARY_WEAPON;
	}
	return found;
}

bool ObjectWeapons::canAttackObject(const Object &victim, CommandSourceType source, bool forced) const
{
	const CombatNames::Status &st = CombatNames::statuses();
	if (&victim == m_owner || !CombatQueries::isAlive(*m_owner) || !CombatQueries::isAlive(victim))
	{
		return false;
	}
	if (victim.isKindOf((unsigned)CombatNames::kinds().unattackable) || (victim.testStatus((unsigned)st.noAttackFromAI) && source == CMD_FROM_AI))
	{
		return false;
	}
	if (!forced && m_owner->getRelationship(victim) != ENEMIES)
	{
		return false;
	}
	// VIS-1 (RW 0x82C167, the action helper; stop S-565): a human player's order (any source but a script) cannot target an object that is fogged or shrouded
	// for that player
	if (source != CMD_FROM_SCRIPT && ShroudManager::isShroudedForAction(*m_owner, victim))
	{
		return false;
	}
	// lane STEALTH-1 (RW 0x6C9147 WeaponSet::getAbleToAttackSpecificObject, RW 0x6C9260 .. 0x6C929B): a victim stealthed and undetected for the attacker's player
	// (RW 0x694C0D) cannot be attacked, by an order either, unless the attacker is IGNORING_STEALTH (status 0x1B) or it is a forced attack on its own player's object;
	// a DISGUISER victim (KindOf 88, template + 0x113 bit 0) is left to its StealthUpdate's disguise test (RW 0x6C92A3 .. 0x6C92E2, lane STEALTH-2): disguised
	// (+ 0x3C), it cannot be attacked by a player that does not hold the disguise player's team (RW 0x6ADBEB) as ENEMIES; a forced attack on a disguised
	// DISGUISER skips the stealth test (RW 0x6C924A .. 0x6C9272)
	static const int kIgnoringStealth = CombatNames::status("IGNORING_STEALTH");
	static const int kDisguiser = CombatNames::kindOf("DISGUISER");
	const StealthUpdate *disguise = victim.isKindOf((unsigned)kDisguiser) ? StealthUpdate::of(victim) : nullptr;
	if (disguise && !disguise->disguiseTemplate())
	{
		disguise = nullptr;
	}
	const bool ignoreStealth = m_owner->testStatus((unsigned)kIgnoringStealth) || (forced && m_owner->getControllingPlayer() == victim.getControllingPlayer()) ||
	                           (forced && disguise);
	if (!ignoreStealth && InvisibilityManager::isStealthedAndUndetected(victim, m_owner->getControllingPlayer()))
	{
		if (!victim.isKindOf((unsigned)kDisguiser))
		{
			return false;
		}
		if (disguise)
		{
			const Player *attacker = m_owner->getControllingPlayer();
			const Player *as = victim.logic().players().getNthPlayer(disguise->disguisePlayerIndex());
			if (attacker && as && attacker->getRelationship(as->getDefaultTeam()) != ENEMIES)
			{
				return false;
			}
		}
	}
	const unsigned mask = victimAntiMask(victim);
	for (const std::unique_ptr<Weapon> &w : m_weapons)
	{
		if (w && w->getTemplate() && (w->getTemplate()->m_antiMask & mask) != 0)
		{
			return true;
		}
	}
	return false;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// range
// ---------------------------------------------------------------------------------------------------------------------------------
RangeSubject ObjectWeapons::subjectOf(const Object &obj) const
{
	const CombatNames::Kind &k = CombatNames::kinds();
	const CombatNames::Status &st = CombatNames::statuses();
	RangeSubject s;
	s.position = *obj.getPosition();
	s.boundingCircleRadius = CombatQueries::boundingCircleRadius(obj);
	s.boundingSphereRadius = CombatQueries::boundingSphereRadius(obj);
	s.isBox = CombatQueries::isBox(obj);
	s.runningDownFromBehind = obj.testStatus((unsigned)st.runningDownFromBehind);
	s.isStructure = obj.isKindOf((unsigned)k.structure);
	s.insideGarrison = obj.testStatus((unsigned)st.insideGarrison); // lane GARRISON-1: RW status 0x3A
	static const int contesting = CombatNames::status("CONTESTING_BUILDING");
	s.contestingBuilding = obj.testStatus((unsigned)contesting);
	return s;
}

ObjectWeapons::BonusRange ObjectWeapons::bonusRangeOf(const Weapon &w, const Object &victim) const
{
	BonusRange r;
	const WeaponTemplate &t = *w.getTemplate();
	WeaponBonus bonus;
	w.computeBonus(*const_cast<ObjectWeapons *>(this)->m_host, 0, bonus);
	const RangeSubject src = subjectOf(*m_owner);
	const float dz = SimMath::subf32(victim.getPosition()->z, m_owner->getPosition()->z);
	r.range = WeaponGetAttackRange(t, bonus, *const_cast<ObjectWeapons *>(this)->m_rangeHost, src, dz);
	return r;
}

// RW 0x6CC915's gate. The distance d (ebp + 0x18): a victim: RW 0x66352C(source, victim), the squared nonnegative footprint edge distance; a position:
// RW 0x6CA525(source, source position, point). Then d > getAttackRange(dz)^2 (RW 0x6CA8BD, the x87 square) and not RW 0x6CC07C(source, its position, victim,
// victim position, 0, checkMin 1) -> rejected; minimumRange^2 > d (RW 0x6CA03E) -> rejected.
// INFERENCE (S-325): the AI vslot 0x200 attack offset branch and the BRIDGE two-point distance (RW 0x67D7EC) are not ported (the edge distance is used).
bool ObjectWeapons::deliveryRangeAllows(const WeaponTemplate &t, const WeaponBonus &bonus, const Object *victim, const Coord3D &victimPos) const
{
	ObjectWeapons *self = const_cast<ObjectWeapons *>(this);
	const RangeSubject src = subjectOf(*m_owner);
	float d;
	if (victim)
	{
		d = CircleDistanceSquared(src.position, src.boundingCircleRadius, victimPos, CombatQueries::boundingCircleRadius(*victim)); // RW 0x66352C in PC24 (review r9: not the binary64 sqrt)
	}
	else
	{
		d = CircleDistanceSquaredToPoint(src.position, src.boundingCircleRadius, victimPos);
	}
	const float dz = SimMath::subf32(victimPos.z, src.position.z);
	const float range = WeaponGetAttackRange(t, bonus, *self->m_rangeHost, src, dz);
	if ((double)d > SimMath::pc24MulW((double)range, (double)range))
	{
		bool reach;
		if (victim)
		{
			const RangeSubject vic = subjectOf(*victim);
			self->m_rangeHost->m_meleeReach = range;
			reach = WeaponIsWithinAttackRange(t, bonus, *self->m_rangeHost, src, src.position, &vic, vic.position, 0.0f, true);
		}
		else
		{
			reach = WeaponIsWithinAttackRange(t, bonus, *self->m_rangeHost, src, src.position, nullptr, victimPos, 0.0f, true);
		}
		if (!reach)
		{
			return false;
		}
	}
	const float minRange = WeaponTemplateMinimumRange(t);
	return !(SimMath::pc24MulW((double)minRange, (double)minRange) > (double)d);
}

bool ObjectWeapons::isWithinAttackRange(const Object &victim, float extra, bool checkMin) const
{
	return isSlotWithinAttackRange(m_curSlot, victim, extra, checkMin);
}

bool ObjectWeapons::isSlotWithinAttackRange(int slot, const Object &victim, float extra, bool checkMin) const
{
	const Weapon *w = weaponInSlot(slot);
	if (!w || !w->getTemplate())
	{
		return false;
	}
	const WeaponTemplate &t = *w->getTemplate();
	ObjectWeapons *self = const_cast<ObjectWeapons *>(this);
	WeaponBonus bonus;
	w->computeBonus(*self->m_host, 0, bonus);
	const RangeSubject src = subjectOf(*m_owner);
	const RangeSubject vic = subjectOf(victim);
	const float dz = SimMath::subf32(vic.position.z, src.position.z);
	self->m_rangeHost->m_meleeReach = WeaponGetAttackRange(t, bonus, *self->m_rangeHost, src, dz);
	return WeaponIsWithinAttackRange(t, bonus, *self->m_rangeHost, src, src.position, &vic, vic.position, extra, checkMin);
}

bool ObjectWeapons::isWithinAttackRange(const Coord3D &pos) const
{
	const Weapon *w = weaponInSlot(m_curSlot);
	if (!w || !w->getTemplate())
	{
		return false;
	}
	ObjectWeapons *self = const_cast<ObjectWeapons *>(this);
	WeaponBonus bonus;
	w->computeBonus(*self->m_host, 0, bonus);
	const RangeSubject src = subjectOf(*m_owner);
	return WeaponIsWithinAttackRange(*w->getTemplate(), bonus, *self->m_rangeHost, src, src.position, nullptr, pos, 0.0f, true);
}

bool ObjectWeapons::isWithinAttackRangeFrom(const Coord3D &sourcePos, const Object *victim, const Coord3D &victimPos, float extra, bool checkMin) const
{
	const Weapon *w = weaponInSlot(m_curSlot);
	if (!w || !w->getTemplate())
	{
		return false;
	}
	const WeaponTemplate &t = *w->getTemplate();
	ObjectWeapons *self = const_cast<ObjectWeapons *>(this);
	WeaponBonus bonus;
	w->computeBonus(*self->m_host, 0, bonus);
	RangeSubject src = subjectOf(*m_owner);
	src.position = sourcePos;
	if (victim)
	{
		const RangeSubject vic = subjectOf(*victim);
		const float dz = SimMath::subf32(vic.position.z, sourcePos.z);
		self->m_rangeHost->m_meleeReach = WeaponGetAttackRange(t, bonus, *self->m_rangeHost, src, dz);
		return WeaponIsWithinAttackRange(t, bonus, *self->m_rangeHost, src, sourcePos, &vic, vic.position, extra, checkMin);
	}
	return WeaponIsWithinAttackRange(t, bonus, *self->m_rangeHost, src, sourcePos, nullptr, victimPos, extra, checkMin);
}

bool ObjectWeapons::isWithinAttackRangeFromTo(const Coord3D &sourcePos, const Object &victim, const Coord3D &victimPos, float extra, bool checkMin) const
{
	const Weapon *w = weaponInSlot(m_curSlot);
	if (!w || !w->getTemplate())
	{
		return false;
	}
	const WeaponTemplate &t = *w->getTemplate();
	ObjectWeapons *self = const_cast<ObjectWeapons *>(this);
	WeaponBonus bonus;
	w->computeBonus(*self->m_host, 0, bonus);
	RangeSubject src = subjectOf(*m_owner);
	src.position = sourcePos;
	RangeSubject vic = subjectOf(victim);
	vic.position = victimPos;
	const float dz = SimMath::subf32(vic.position.z, sourcePos.z);
	self->m_rangeHost->m_meleeReach = WeaponGetAttackRange(t, bonus, *self->m_rangeHost, src, dz);
	return WeaponIsWithinAttackRange(t, bonus, *self->m_rangeHost, src, sourcePos, &vic, vic.position, extra, checkMin);
}

float ObjectWeapons::attackDistance(const Object &victim) const
{
	const Weapon *w = weaponInSlot(m_curSlot);
	if (!w || !w->getTemplate())
	{
		return 0.0f;
	}
	ObjectWeapons *self = const_cast<ObjectWeapons *>(this);
	WeaponBonus bonus;
	w->computeBonus(*self->m_host, 0, bonus);
	const RangeSubject src = subjectOf(*m_owner);
	const RangeSubject vic = subjectOf(victim);
	return WeaponGetAttackDistance(*w->getTemplate(), bonus, *self->m_rangeHost, src, &vic, nullptr);
}

float ObjectWeapons::currentAttackRange() const
{
	const Weapon *w = weaponInSlot(m_curSlot);
	if (!w || !w->getTemplate())
	{
		return 0.0f;
	}
	ObjectWeapons *self = const_cast<ObjectWeapons *>(this);
	WeaponBonus bonus;
	w->computeBonus(*self->m_host, 0, bonus);
	const RangeSubject src = subjectOf(*m_owner);
	return WeaponGetAttackRange(*w->getTemplate(), bonus, *self->m_rangeHost, src, 0.0f);
}

bool ObjectWeapons::isTooClose(const Object &victim) const
{
	const Weapon *w = weaponInSlot(m_curSlot);
	if (!w || !w->getTemplate())
	{
		return false;
	}
	ObjectWeapons *self = const_cast<ObjectWeapons *>(this);
	return WeaponIsTooClose(*w->getTemplate(), *self->m_rangeHost, subjectOf(*m_owner), subjectOf(victim));
}

float ObjectWeapons::aimDelta() const
{
	const Weapon *w = weaponInSlot(m_curSlot);
	return w && w->getTemplate() ? w->getTemplate()->m_aimDelta : 0.0f;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// firing
// ---------------------------------------------------------------------------------------------------------------------------------
int ObjectWeapons::currentStatus() const
{
	const Weapon *w = weaponInSlot(m_curSlot);
	return w ? w->getStatus(*const_cast<ObjectWeapons *>(this)->m_host) : WEAPON_OUT_OF_AMMO;
}

// RW 0x4BEE31 .. 0x4BEE91 (lane ANIM-1)
bool ObjectWeapons::drawWeaponTimingFrames(int &frames) const
{
	const Weapon *w = weaponInSlot(m_curSlot);
	if (!w || !w->getTemplate())
	{
		return false;
	}
	WeaponHost &h = *const_cast<ObjectWeapons *>(this)->m_host;
	if (w->getStatus(h) == WEAPON_RELOADING_CLIP)
	{
		frames = (int)(w->whenWeCanFireAgain() - w->timerStart()); // RW 0x6CA241: +0x18 - +0x28
	}
	else
	{
		frames = (int)((std::uint32_t)w->getPreAttackDelay(h, nullptr) + w->getTemplate()->m_firingDuration); // RW 0x4BEE8F: add ecx, eax
	}
	return true;
}

// RW 0x69213E
void ObjectWeapons::preFireCurrentWeapon(const Object *victim, const Coord3D *pos)
{
	// lane FX-3: the target record (RW 0x692174 .. 0x6921AB), written before the weapon test: a victim's position and id, else a given position and id 0
	if (victim)
	{
		m_drawTargetPos = *victim->getPosition();
		m_drawTargetID = victim->getID();
	}
	else if (pos)
	{
		m_drawTargetPos = *pos;
		m_drawTargetID = INVALID_ID;
	}
	Weapon *w = weaponInSlot(m_curSlot);
	if (!w || !w->getTemplate())
	{
		return;
	}
	const WeaponTemplate &t = *w->getTemplate();
	if (t.m_lockWhenUsing)
	{
		setWeaponLock(m_curSlot, LOCKED_TEMPORARILY);
	}
	WeaponShotTarget target;
	if (victim)
	{
		target.hasVictim = true;
		target.victimID = victim->getID();
		target.victimPosition = *victim->getPosition();
	}
	else if (pos)
	{
		target.victimPosition = *pos;
	}
	if (m_owner->logic().getFrame() + 1u >= w->whenWeCanFireAgain())
	{
		// RW 0x69213E: FIRING_OR_PREATTACK_A..E cleared first (unless RUNNING_DOWN_FROM_BEHIND), then the weapon's preFire
		Object::ModelConditionBits clear{};
		for (int i = 0; i < 5; ++i)
		{
			addBit(clear, slotConditions(i).firingOrPreattack);
		}
		if (!m_owner->testStatus((unsigned)CombatNames::statuses().runningDownFromBehind))
		{
			m_owner->clearAndSetModelConditionFlags(clear, Object::ModelConditionBits{});
			m_owner->flushDrawableModelConditions(); // lane ANIM-1: RW 0x6921FB .. 0x692203, the drawable's flush RW 0x67449C(0) right after the clear
		}
		w->preFireWeapon(host(), target);
	}
}

// RW 0x69200A / 0x6920AD
bool ObjectWeapons::fireCurrentWeapon(Object *victim, const Coord3D *pos)
{
	Weapon *w = weaponInSlot(m_curSlot);
	if (!w || !w->getTemplate())
	{
		return false;
	}
	const WeaponTemplate &t = *w->getTemplate();
	if (t.m_lockWhenUsing)
	{
		setWeaponLock(m_curSlot, LOCKED_TEMPORARILY);
	}
	Weapon::FireArgs args;
	if (victim)
	{
		args.target.hasVictim = true;
		args.target.victimID = victim->getID();
		args.target.victimPosition = *victim->getPosition();
	}
	else if (pos)
	{
		args.target.victimPosition = *pos;
	}
	ObjectWeaponDelivery delivery(*m_owner, *this, t);
	const bool reloaded = w->privateFireWeapon(host(), delivery, args);
	if (reloaded || t.m_idleAfterFiringDelay == 0xFFFFFFFFu)
	{
		releaseWeaponLock(LOCKED_TEMPORARILY); // ZH: unlock when loaded; RW: IdleAfterFiringDelay == -1 releases it after every shot
	}
	m_tracker.shotFired(*m_trackerHost, *w, victim ? victim->getID() : 0u, args.target.victimPosition, false);
	// RW 0x8E3411 .. 0x8E34DD (inside FiringTracker::shotFired): the weapon's FireSound (template + 0xC8) attached to the shooter, looped while it keeps
	// firing within FireSoundLoopTime (+ 0xCC). Client audio, fire-and-forget (AUDIO-2): the loop's stop frame and handle live in the audio side
	// (AudioApi::postWeaponFireSound), not on the tracker (retail keeps them at tracker + 0x54 / + 0x58: S-712)
	if (!t.m_fireSound.empty())
	{
		AudioApi::postWeaponFireSound(t.m_fireSound, m_owner->getID(), t.m_fireSoundLoopTime, m_owner->logic().getFrame());
	}
	return reloaded;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// every frame
// ---------------------------------------------------------------------------------------------------------------------------------
// RW 0x68E197
void ObjectWeapons::updateWeaponStatusConditions()
{
	const CombatNames::Status &st = CombatNames::statuses();
	const std::uint32_t now = m_owner->logic().getFrame();
	const bool attacking = m_owner->testStatus((unsigned)st.isAttacking);
	const bool firing = m_owner->testStatus((unsigned)st.isFiringWeapon);
	const bool aiming = m_owner->testStatus((unsigned)st.isAimingWeapon);
	for (int slot = 0; slot < 5; ++slot)
	{
		const Weapon *w = weaponInSlot(slot);
		if (!w || !w->getTemplate())
		{
			continue;
		}
		const int status = w->getStatus(host());
		const int cond = WeaponStatusCondition(*w->getTemplate(), *w, status, now, attacking, firing, aiming);
		if (cond == m_slotCondition[(size_t)slot])
		{
			continue;
		}
		m_slotCondition[(size_t)slot] = cond;
		applySlotCondition(slot, cond);
	}
}

// RW 0x6C83FD (the set bits of a condition) and RW 0x6C84C9 (the slot's seven weapon bits cleared), applied by RW 0x68D607
void ObjectWeapons::applySlotCondition(int slot, int cond)
{
	{
		const SlotConditions &c = slotConditions(slot);
		Object::ModelConditionBits clear{}, set{};
		for (int bit : { c.preattack, c.firing, c.firingOrPreattack, c.firingOrReloading, c.betweenShots, c.reloading, c.usingWeapon })
		{
			addBit(clear, bit);
		}
		switch (cond)
		{
		case 1:
			addBit(set, c.firing);
			addBit(set, c.firingOrPreattack);
			addBit(set, c.firingOrReloading);
			addBit(set, c.usingWeapon);
			break;
		case 2:
			addBit(set, c.betweenShots);
			addBit(set, c.usingWeapon);
			break;
		case 3:
			addBit(set, c.reloading);
			addBit(set, c.firingOrReloading);
			addBit(set, c.usingWeapon);
			break;
		case 4:
		case 5:
			addBit(set, c.preattack);
			addBit(set, c.firingOrPreattack);
			addBit(set, c.usingWeapon);
			break;
		default:
			break;
		}
		m_owner->clearAndSetModelConditionFlags(clear, set);
	}
}

// RW 0x69036C (lane PROJ-2): the current slot (+0x36C) gets condition 1; the cached condition (+0x3A0) is not changed, so the WeaponStatusHelper re-applies its own
// answer at the end of the frame. Retail does this only for an object with a drawable (+0x84), like RW 0x68E197 (see WeaponStatusHelper.h)
void ObjectWeapons::setFiringConditionForCurrentWeapon()
{
	if (m_curSlot >= 0 && m_curSlot < 5)
	{
		applySlotCondition(m_curSlot, 1);
	}
}

void ObjectWeapons::resetFiringTracker(bool hard)
{
	m_tracker.reset(*m_trackerHost, hard); // RW 0x8E302A
}

std::uint32_t ObjectWeapons::updateFiringTracker()
{
	return m_tracker.update(*m_trackerHost);
}

void ObjectWeapons::crc(StateHasher &h) const
{
	for (std::uint32_t w : m_flags)
	{
		h.addU32(w);
	}
	h.addBool(m_set != nullptr);
	h.addI32(m_curSlot);
	h.addI32((int)m_lockType);
	h.addI32(m_lockedSlot);
	for (int c : m_slotCondition)
	{
		h.addI32(c);
	}
	for (const std::unique_ptr<Weapon> &w : m_weapons)
	{
		h.addBool(w != nullptr);
		if (w)
		{
			w->crc(h);
		}
	}
	m_tracker.crc(h);
	for (const std::unique_ptr<Weapon> *w : { &m_crushWeapon, &m_crushRevengeWeapon })
	{
		h.addBool(*w != nullptr);
		if (*w)
		{
			(*w)->crc(h);
		}
	}
	h.addU64(m_stats.shotsFired);
	h.addU64(m_stats.shotsAtVictim);
	h.addU64(m_stats.shotsOutOfRange);
	h.addU64(m_stats.projectilesLaunched);
	h.addU64(m_stats.unportedNuggets);
}

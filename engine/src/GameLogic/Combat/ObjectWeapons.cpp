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
#include "GameLogic/AI/TurretAI.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/WeaponDelivery.h"
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
	ObjectWeaponDelivery delivery(*m_owner, *this, *w.getTemplate(), w);
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
	ObjectWeaponDelivery delivery(*m_owner, *this, *w.getTemplate(), w);
	w.privateFireWeapon(host(), delivery, args);
}

// RW 0x6CF530 / 0x6CF590 (see ObjectWeapons.h)
void ObjectWeapons::fireTempWeapon(const WeaponTemplate *t, Object *source, const WeaponShotTarget &target)
{
	if (!t || !source)
	{
		return;
	}
	std::unique_ptr<ObjectWeapons> ownHost;
	ObjectWeapons *host = source->getWeapons();
	if (!host)
	{
		ownHost = std::make_unique<ObjectWeapons>(*source);
		host = ownHost.get();
	}
	const std::uint32_t now = source->logic().getFrame();
	Weapon w(t, PRIMARY_WEAPON, now); // RW 0x68AA81 allocateNewWeapon(t, PRIMARY)
	w.setOwnerID(source->getID());
	w.loadAmmoNow(host->host());      // RW 0x6CE1AC
	w.setLeechRangeDeadline(now + 1u); // RW 0x6CF56F
	Weapon::FireArgs args;
	args.target = target;
	ObjectWeaponDelivery delivery(*source, *host, *t, w); // the temporary weapon is PRIMARY
	w.privateFireWeapon(host->host(), delivery, args); // RW 0x6CE6E8 / 0x6CE6C5 -> RW 0x6CEF6D
}

void ObjectWeapons::createAndFireTempWeapon(const WeaponTemplate *t, Object *source, const Coord3D &pos)
{
	WeaponShotTarget target;
	target.hasVictim = false;
	target.victimPosition = pos;
	fireTempWeapon(t, source, target);
}

void ObjectWeapons::createAndFireTempWeaponAt(const WeaponTemplate *t, Object *source, Object &victim)
{
	WeaponShotTarget target;
	target.hasVictim = true;
	target.victimID = victim.getID();
	target.victimPosition = *victim.getPosition();
	fireTempWeapon(t, source, target);
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
			if (n->kind() == NUGGET_DAMAGE || n->kind() == NUGGET_DOT || n->kind() == NUGGET_PROJECTILE) // DOT: asDamageNugget (slot 10 RW 0x8CEF91) answers `this` as for DamageNugget
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
// RW 0x6C85CA getVictimAntiMask (lane DECOMP-1; BFME2 decomp WeaponSetSetWeaponLock.cpp getVictimAntiMask, tier A): the victim template's KindOf words decide:
// MINE 0x12, SMALL_MISSILE 8, BALLISTIC_MISSILE 0x40, PROJECTILE 4; an AIRBORNE_TARGET (status 6): CAVALRY 1, INFANTRY 0x20, MONSTER 0x200, PARACHUTE 0x80, else 0;
// on the ground: STRUCTURE 0x102 (GROUND | STRUCTURE), else 2 (GROUND)
unsigned ObjectWeapons::victimAntiMask(const Object &victim)
{
	static const int kMine = CombatNames::kindOf("MINE"), kSmallMissile = CombatNames::kindOf("SMALL_MISSILE"), kBallistic = CombatNames::kindOf("BALLISTIC_MISSILE"),
	                 kProjectile = CombatNames::kindOf("PROJECTILE"), kCavalry = CombatNames::kindOf("CAVALRY"), kInfantry = CombatNames::kindOf("INFANTRY"),
	                 kMonster = CombatNames::kindOf("MONSTER"), kParachute = CombatNames::kindOf("PARACHUTE"), kStructure = CombatNames::kindOf("STRUCTURE");
	static const int kAirborne = CombatNames::status("AIRBORNE_TARGET");
	auto is = [&](int k) { return k >= 0 && victim.isKindOf((unsigned)k); };
	if (is(kMine))
	{
		return WEAPON_ANTI_MINE | WEAPON_ANTI_GROUND;
	}
	if (is(kSmallMissile))
	{
		return WEAPON_ANTI_SMALL_MISSILE;
	}
	if (is(kBallistic))
	{
		return WEAPON_ANTI_BALLISTIC_MISSILE;
	}
	if (is(kProjectile))
	{
		return WEAPON_ANTI_PROJECTILE;
	}
	if (kAirborne >= 0 && victim.testStatus((unsigned)kAirborne))
	{
		if (is(kCavalry))
		{
			return WEAPON_ANTI_AIRBORNE_VEHICLE;
		}
		if (is(kInfantry))
		{
			return WEAPON_ANTI_AIRBORNE_INFANTRY;
		}
		if (is(kMonster))
		{
			return WEAPON_ANTI_AIRBORNE_MONSTER;
		}
		if (is(kParachute))
		{
			return WEAPON_ANTI_PARACHUTE;
		}
		return 0;
	}
	return is(kStructure) ? (WEAPON_ANTI_GROUND | WEAPON_ANTI_STRUCTURE) : WEAPON_ANTI_GROUND;
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
			if (n->kind() == NUGGET_DAMAGE || n->kind() == NUGGET_DOT) // DOTNugget is a DamageNugget (slot 10 RW 0x8CEF91 answers `this`; lane DECOMP-1)
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

const char *ObjectWeapons::choiceStopLine()
{
	return "[S-1582] weapon choice: RW 0x6C8A4E is ported (lane DECOMP-1: criteria 0 .. 5, ReadyStatusSharedWithinSet, the victimless pick, the 1 / 0 score of canDamage RW 0x6CDBF3 "
	       "through each nugget's isApplicable, the range error, OnlyAgainst / PreferredAgainst / OnlyInCondition, the turret aim gate RW 0x662398 on GARRISON-2's TurretAI); "
	       "NOT ported: calcPitches RW 0xAD2620 for a pitch-limited weapon (taken as within; no retail weapon limits its pitch), CannotTargetCastleVictims' test (no retail weapon "
	       "sets it), SlaveAttackNugget's isApplicable (RW 0x910DCF asks the unported SlaveWatcherBehavior: "
	       "the base test RW 0x90D77C stands in), DamageContainedNugget's contain slot 0x1C (taken as false); INFERENCE: HordeAttackNugget's member (RW 0x9119CB, horde slots 0x50 / 0x114) is the first living one";
}

// RW 0x6CDBF3 Weapon::canDamage (lane DECOMP-1, tier A): no template, or OUT_OF_AMMO with AutoReloadsClip set: false; CannotTargetCastleVictims (+ 0x168: a victim
// with 16 attackers or more, then a castle partition test) is not ported (no retail weapon sets it: noted); then RW 0x6CB779 on the template's nuggets
bool ObjectWeapons::canDamage(const Weapon &w, Object &victim) const
{
	const WeaponTemplate *t = w.getTemplate();
	if (!t)
	{
		return false;
	}
	if (w.getStatus(const_cast<ObjectWeapons *>(this)->host()) == WEAPON_OUT_OF_AMMO && t->m_autoReloadsClip != AUTO_RELOAD)
	{
		return false;
	}
	if (t->m_cannotTargetCastleVictims)
	{
		m_owner->logic().noteStop("[S-1582] weapon choice: CannotTargetCastleVictims (RW 0x6CDC28 .. 0x6CDCCA: the victim's attacker count and the castle partition test) is not ported");
	}
	return WeaponTemplateAnyNuggetApplicable(m_owner->logic(), *t, m_owner->getID(), &victim);
}

// RW 0x6C8A4E WeaponSet::chooseBestWeaponForTarget (lane DECOMP-1; BFME2 decomp reverse/attempts/0x002c7d03.cpp, tier B same-shape: the RotWK body is BFME2's with the AI
// at Object + 0x260; every step below was read from RotWK). Criteria 5 takes the set's DefaultWeaponChoiceCritera (+ 0x360). A locked weapon stays. With
// ReadyStatusSharedWithinSet (+ 0x364) any weapon not READY_TO_FIRE keeps the current one. No victim: the first slot (0 .. 5) whose weapon is not OUT_OF_AMMO with
// AutoReloadsClip, is not a MeleeWeapon (+ 0x125) and has NoVictimNeeded (+ 0x16B), else PRIMARY and false. Else slots 5 .. 0: the slot's auto-choose test
// (RW 0x6C831C: WeaponSet + 0x36 + slot, only ever 0, or the set's AutoChooseSources bit), not BombardType (+ 0x170), OUT_OF_AMMO only with AutoReloadsClip
// AUTO and IdleAfterFiringDelay (+ 0x78) negative, the victim anti mask (RW 0x6C85CA), the target pitch (RW 0x6CA9BF), canDamage (RW 0x6CDBF3) or DamageType
// UNRESISTABLE; the score is 1 / 0 (canDamage), the range RW 0x6CA921 (getAttackRange at the victim's height), the range error (the distance for a MeleeWeapon,
// else how far the 3D centre distance (RW 0x403111) lies outside [MinimumAttackRange, range]), the logic draw WeaponSet.cpp:1478 (RW 0x6C8CE2); ready =
// READY_TO_FIRE or PRE_ATTACK, not below the minimum range and not a turret slot aiming (RW 0x662398: no turrets are ported, S-325); OnlyAgainst (+ 0xEC),
// PreferredAgainst (+ 0x44), OnlyInCondition (+ 0x194) override them (-1 / 1e5 / 1e10 RW 0xBD19DC / 0xBDCF20 / 0xBF7328, the draw +- 1); then per criterion
// (0 most damage >=, 1 longest range >, 2 PREFER_GRAB_OVER_DAMAGE: RW 0x6CA3DA wins at once else 0, 3 least movement <=, 4 random >) the ready best, else the
// best that is not ready; none: PRIMARY and false
bool ObjectWeapons::chooseBestWeaponForTarget(const Object *victimIn, WeaponChoiceCriteria criteria, CommandSourceType source)
{
	if (criteria == PREFER_TEMPLATE_DEFAULT)
	{
		criteria = m_set ? (WeaponChoiceCriteria)m_set->m_defaultWeaponChoiceCriteria : PREFER_MOST_DAMAGE; // RW 0x6C8A63
	}
	if (isCurWeaponLocked())
	{
		return true;
	}
	if (!m_set)
	{
		return false;
	}
	WeaponHost &h = host();
	if (m_set->m_isReadyStatusSharedWithinSet)
	{
		for (int i = 0; i < WEAPONSLOT_COUNT; ++i)
		{
			Weapon *w = weaponInSlot(i);
			if (w && w->getStatus(h) != WEAPON_READY_TO_FIRE)
			{
				return true;
			}
		}
	}
	if (!victimIn)
	{
		for (int i = 0; i < WEAPONSLOT_COUNT; ++i)
		{
			Weapon *w = weaponInSlot(i);
			if (!w || !w->getTemplate())
			{
				continue;
			}
			const WeaponTemplate &t = *w->getTemplate();
			if ((w->getStatus(h) != WEAPON_OUT_OF_AMMO || t.m_autoReloadsClip == AUTO_RELOAD) && !t.m_meleeWeapon && t.m_noVictimNeeded)
			{
				m_curSlot = i;
				return true;
			}
		}
		m_curSlot = PRIMARY_WEAPON;
		return false;
	}
	Object &victim = *const_cast<Object *>(victimIn);
	GameLogic &logic = m_owner->logic();
	const float kBig = 1.0e10f;
	struct Best
	{
		float damage = 0.0f, range = 0.0f, error = 1.0e10f, random = 0.0f;
		int slot = PRIMARY_WEAPON;
		bool found = false;
	} ready, backup;
	for (int i = WEAPONSLOT_COUNT - 1; i >= PRIMARY_WEAPON; --i)
	{
		Weapon *w = weaponInSlot(i);
		if (!w || !w->getTemplate())
		{
			continue;
		}
		if ((m_set->m_autoChooseMask[i] & (1u << (unsigned)source)) == 0) // RW 0x6C831C (WeaponSet + 0x36 is never set: only the ctor writes it)
		{
			continue;
		}
		const WeaponTemplate &t = *w->getTemplate();
		if (t.m_bombardType)
		{
			continue;
		}
		const int status = w->getStatus(h);
		if (status == WEAPON_OUT_OF_AMMO && (t.m_autoReloadsClip != AUTO_RELOAD || (std::int32_t)t.m_idleAfterFiringDelay >= 0))
		{
			continue;
		}
		if ((t.m_antiMask & victimAntiMask(victim)) == 0)
		{
			continue;
		}
		if (w->pitchLimited() && !(SimMath::absD((double)SimMath::sseSub(victim.getPosition()->z, m_owner->getPosition()->z)) < 10.0))
		{
			logic.noteStop("[S-1582] weapon choice: the target pitch of a pitch-limited weapon (RW 0x6CA9BF -> calcPitches RW 0xAD2620) is not ported: taken as within");
		}
		const bool affects = canDamage(*w, victim);
		if (!affects && t.m_damageType != DAMAGE_UNRESISTABLE)
		{
			continue;
		}
		const Coord3D &sp = *m_owner->getPosition();
		const Coord3D &vp = *victim.getPosition();
		const float dist = SimMath::fstpDword(SimMath::length3d(SimMath::sseSub(sp.x, vp.x), SimMath::sseSub(sp.y, vp.y), SimMath::sseSub(sp.z, vp.z)));
		float damage = affects ? 1.0f : 0.0f;
		float range = bonusRangeOf(*w, victim).range;  // RW 0x6CA921 -> 0x6CA8BD
		const float minRange = WeaponTemplateMinimumRange(t); // RW 0x6CA2BB -> 0x6CA03E
		bool isReady = status == WEAPON_READY_TO_FIRE || status == WEAPON_PRE_ATTACK;
		float error = 0.0f;
		if (t.m_meleeWeapon)
		{
			error = dist;
		}
		else if (minRange > dist)
		{
			error = SimMath::sseSub(minRange, dist);
		}
		else if (dist > range)
		{
			error = SimMath::sseSub(dist, range);
		}
		float random = logic.random().getValueReal(0.0f, 1.0f, kWeaponSetCpp, 1478); // RW 0x6C8CE2
		// RW 0x6C8CF7: a slot on the owner's turret that is aiming at this victim (RW 0x662398: the AI's one turret, AI + 0x20C, isWeaponSlotOnTurret RW 0x8DC4ED
		// then isTryingToAimAtTarget RW 0x8DCA19; lane DECOMP-1 r3, the turrets run since GARRISON-2), or a victim inside the minimum range, is not ready
		AIUpdateInterface *ownerAI = m_owner->getAIUpdateInterface();
		TurretAI *turret = ownerAI ? ownerAI->turret() : nullptr;
		if ((turret && turret->isWeaponSlotOnTurret(i) && turret->isTryingToAimAtTarget(&victim)) || dist < minRange)
		{
			isReady = false;
		}
		auto anyBits = [](const auto &mask) {
			for (std::uint32_t word : mask)
			{
				if (word != 0)
				{
					return true;
				}
			}
			return false;
		};
		auto victimHasAny = [&](const KindOfMaskType &mask) { // RW 0x70C548
			for (size_t wd = 0; wd < mask.size(); ++wd)
			{
				if (mask[wd] & victim.getKindOf()[wd])
				{
					return true;
				}
			}
			return false;
		};
		const auto win = [&](float score) {
			damage = score;
			range = score;
			error = 0.0f;
			random = SimMath::sseAdd(random, 1.0f);
			isReady = w->getStatus(h) != WEAPON_OUT_OF_AMMO && !(dist < minRange);
		};
		const auto lose = [&]() {
			damage = -1.0f;
			range = -1.0f;
			error = kBig;
			random = SimMath::sseSub(random, 1.0f);
			isReady = false;
		};
		if (anyBits(m_set->m_onlyAgainst[i])) // RW 0x6C8D21
		{
			if (victimHasAny(m_set->m_onlyAgainst[i]))
			{
				win(100000.0f);
			}
			else
			{
				lose();
			}
		}
		if (anyBits(m_set->m_preferredAgainst[i]) && victimHasAny(m_set->m_preferredAgainst[i])) // RW 0x6C8DC3
		{
			win(kBig);
		}
		const ModelConditionMask &cond = m_set->m_onlyInCondition[i];
		if (anyBits(cond)) // RW 0x6C8E39 .. 0x6C8EB0 (RW 0x4CE8CA: every bit held)
		{
			bool all = true;
			for (size_t wd = 0; wd < cond.size(); ++wd)
			{
				all = all && (m_owner->getModelConditionBits()[wd] & cond[wd]) == cond[wd];
			}
			if (all)
			{
				win(kBig);
			}
			else
			{
				lose();
			}
		}
		Best &b = isReady ? ready : backup;
		bool take = false;
		switch (criteria)
		{
		case PREFER_GRAB_OVER_DAMAGE:
			// RW 0x6CA3DA (lane DECOMP-1 r2): a weapon with a GrabNugget (+ 0x157) whose owner's contain accepts the victim (contain slot 0x98(victim, 1, 0)) wins at once:
			// it becomes the choice and the loop ends (RW 0x6C8F33 .. 0x6C8F45: local_24 = -1); else the slot scores as PREFER_MOST_DAMAGE
			if (t.m_hasGrabNugget && m_owner->getContain() && m_owner->getContain()->isValidContainerFor(victim, true, false))
			{
				ready.damage = damage; // RW 0x6C8FC7: the ready best, whatever the readiness
				ready.slot = i;
				ready.found = true;
				i = PRIMARY_WEAPON - 1; // the loop ends
				continue;
			}
			take = b.damage <= damage;
			break;
		case PREFER_LONGEST_RANGE:
			take = b.range < range;
			break;
		case PREFER_LEAST_MOVEMENT:
			take = error <= b.error;
			break;
		case SELECT_AT_RANDOM:
			take = b.random < random;
			break;
		default:
			take = b.damage <= damage;
			break;
		}
		if (take)
		{
			switch (criteria)
			{
			case PREFER_LONGEST_RANGE: b.range = range; break;
			case PREFER_LEAST_MOVEMENT: b.error = error; break;
			case SELECT_AT_RANDOM: b.random = random; break;
			default: b.damage = damage; break;
			}
			b.slot = i;
			b.found = true;
		}
	}
	if (ready.found)
	{
		m_curSlot = ready.slot;
		return true;
	}
	if (backup.found)
	{
		m_curSlot = backup.slot;
		return true;
	}
	m_curSlot = PRIMARY_WEAPON;
	return false;
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
	ObjectWeaponDelivery delivery(*m_owner, *this, t, *w);
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

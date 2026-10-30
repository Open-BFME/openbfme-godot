// OpenBFME. GPL-3.0. BFME2 CrateCollide family; see docs/CRATE_PICKUPS.md.
#include "GameLogic/Module/CrateCollide.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Science.h"
#include "Common/StateHash.h"
#include "Common/Upgrade.h"
#include "GameLogic/AI/AIGarrisonStates.h"
#include "GameLogic/Economy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SnapshotXfer.h"
#include <algorithm>
#include <cstddef>
#include <stdexcept>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

void VeterancyCrateCollideModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	CrateCollideModuleData::buildFieldParse(p);
#define OFF(x) (int)offsetof(VeterancyCrateCollideModuleData, x)
	static const FieldParse fields[] = {
		{ "EffectRange", INI::parseUnsignedInt, nullptr, OFF(m_effectRange) },
		{ "AddsOwnerVeterancy", INI::parseBool, nullptr, OFF(m_addsOwnerVeterancy) },
		{ "IsPilot", INI::parseBool, nullptr, OFF(m_isPilot) },
		{ "AffectsUpToLevel", INI::parseInt, nullptr, OFF(m_affectsUpToLevel) },
		{ nullptr, nullptr, nullptr, 0 }
	};
#undef OFF
	p.add(fields);
}

const char *CrateCollide::contactStop()
{
	return "[S-5900] crate pickups use ground bounding-circle overlaps in the partition/collision phase, in crate and picker ID order; retail's full contact geometry and pair order are not reproduced (S-581, S-780)";
}

const char *CrateCollide::limitationsStop()
{
	return "[S-5901] crate pickups do not reproduce the CREEP-controller and undetected-defector gates, significant-height tolerance, GUI rank label, ExecuteAnimation or pilot script-name transfer; CREEP collectors and pilot crates are refused";
}

bool CrateCollide::valid(const Object &other) const
{
	Object *crate = getObject();
	Player *owner = other.getControllingPlayer();
	if (m_executed || crate->isDestroyed() || other.isDestroyed() || other.isEffectivelyDead() || !other.isInWorld() ||
		!owner || owner == crate->logic().players().getNeutralPlayer()) return false;
	if (other.isKindOfName("CREEP")) { crate->logic().noteStop(limitationsStop()); return false; }
	const bool building = m_data->m_buildingPickup && other.isKindOfName("STRUCTURE");
	if (!other.getAIUpdateInterface() && !building) return false;
	if (GarrisonRules::isAboveTerrain(*crate) && !building) return false;
	for (size_t i = 0; i < m_data->m_requiredKindOf.size() * 32; ++i)
	{
		if ((m_data->m_requiredKindOf[i / 32] & (1u << (i % 32))) && !other.isKindOf((unsigned)i)) return false;
		if ((m_data->m_forbiddenKindOf[i / 32] & (1u << (i % 32))) && other.isKindOf((unsigned)i)) return false;
	}
	if (m_data->m_forbidOwnerPlayer && owner == crate->getControllingPlayer()) return false;
	if (m_data->m_humanOnly && owner->getPlayerType() != PLAYER_HUMAN) return false;
	if (!m_data->m_pickupScience.empty() && m_data->m_pickupScience != "None")
	{
		const ScienceType science = TheScienceStore ? TheScienceStore->getScienceFromInternalName(m_data->m_pickupScience) : SCIENCE_INVALID;
		if (science == SCIENCE_INVALID) throw std::logic_error("CrateCollide: unknown PickupScience " + m_data->m_pickupScience);
		if (!owner->hasScience(science)) return false;
	}
	return true;
}

bool CrateCollide::wouldLikeToCollideWith(const Object &other) const { return valid(other); }

void CrateCollide::onCollide(Object *other, const Coord3D *, const Coord3D *)
{
	if (!other || !valid(*other) || !execute(*other)) return;
	Object &crate = *getObject();
	GameLogic &logic = crate.logic();
	logic.fxEvents().emit(FXEventLog::objectEvent(FXEvent::OBJECT_FX, "CrateCollide", logic.getFrame(), m_data->m_executeFX, *other));
	if (!m_data->m_executeAnimation.empty())
		logic.noteStop(limitationsStop());
	m_executed = true;
	logic.destroyObject(&crate);
}

bool SalvageCrateCollide::valid(const Object &other) const
{
	return CrateCollide::valid(other) && other.getAIUpdateInterface() && !other.isKindOfName("IMMOBILE");
}

bool SalvageCrateCollide::execute(Object &other)
{
	Player *owner = other.getControllingPlayer();
	if (!m_salvage->m_allowAIPickup && owner->getPlayerType() == PLAYER_COMPUTER) return false;
	GameLogic &logic = other.logic();
	// BFME2 SalvageCrateCollide::rva004BD281: one roll, even for a 0%/100% chance.
	if (logic.random().getValueReal(0.0f, 1.0f, "SalvageCrateCollide.cpp", 0x45) < m_salvage->m_levelUpChance)
	{
		if (ExperienceTracker *xp = other.getExperienceTracker()) xp->gainExpForLevel(1, true, false);
	}
	else if (!m_salvage->m_upgrade.empty())
	{
		const UpgradeTemplate *upgrade = owner->resolveUpgrade(m_salvage->m_upgrade, true);
		if (upgrade->getUpgradeType() == UPGRADE_TYPE_PLAYER) owner->addUpgrade(upgrade, Player::UPGRADE_STATUS_COMPLETE);
	}
	else
	{
		int money = m_salvage->m_minResource;
		if (money != m_salvage->m_maxResource) money = logic.random().getValue(money, m_salvage->m_maxResource, "SalvageCrateCollide.cpp", 0xDB);
		if (money > 0)
		{
			money = logic.economy().applyIncomeMultipliers(*owner, (float)money);
			if (money > 0) owner->depositMoney((unsigned)money, true);
			logic.economy().noteIncome(other, *owner, money);
		}
	}
	return true;
}

bool VeterancyCrateCollide::valid(const Object &other) const
{
	const ExperienceTracker *xp = other.getExperienceTracker();
	return CrateCollide::valid(other) && !GarrisonRules::isAboveTerrain(other) &&
		!m_veterancy->m_addsOwnerVeterancy && !m_veterancy->m_isPilot && xp && xp->isTrainable() && xp->isAcceptingExperiencePoints() &&
		xp->getRank() <= m_veterancy->m_affectsUpToLevel &&
		(!m_veterancy->m_isPilot || other.getControllingPlayer() == getObject()->getControllingPlayer());
}

bool VeterancyCrateCollide::execute(Object &other)
{
	auto promote = [&](Object &obj) {
		if (ExperienceTracker *xp = obj.getExperienceTracker()) xp->gainExpForLevel(1, !m_veterancy->m_isPilot, false);
	};
	if (m_veterancy->m_effectRange == 0) promote(other);
	else
	{
		PartitionFilterFn owned([&](Object &obj) { return obj.getControllingPlayer() == other.getControllingPlayer() && &obj != &other; });
		auto hits = other.logic().partition().iterateObjectsInRange(*other.getPosition(), (float)m_veterancy->m_effectRange, FROM_CENTER_2D, { &owned }, ITER_FASTEST);
		for (const auto &hit : hits) promote(*hit.object);
	}
	if (m_veterancy->m_isPilot) other.logic().noteStop(limitationsStop());
	return true;
}

void CrateCollide::processContacts(GameLogic &logic)
{
	std::vector<Object *> crates;
	for (Object *obj = logic.getFirstObject(); obj; obj = obj->getNextObject())
		if (obj->hasCrateCollide()) crates.push_back(obj);
	std::sort(crates.begin(), crates.end(), [](Object *a, Object *b) { return a->getID() < b->getID(); });
	for (Object *crate : crates)
	{
		if (crate->isDestroyed() || !crate->isInWorld()) continue;
		logic.noteStop(contactStop());
		logic.noteStop(limitationsStop());
		PathfindGeometry geometry;
		ObjectGeometry::fillPathfindGeometry(*crate->getTemplate(), geometry);
		PartitionFilterFn live([&](Object &obj) { return &obj != crate && obj.isInWorld() && !obj.isDestroyed() && !obj.isEffectivelyDead(); });
		auto hits = logic.partition().iterateObjectsInRange(*crate->getPosition(), geometry.boundingCircleRadius(), FROM_BOUNDINGSPHERE_2D, { &live }, ITER_FASTEST);
		std::vector<Object *> order;
		for (const auto &hit : hits) order.push_back(hit.object);
		std::sort(order.begin(), order.end(), [](Object *a, Object *b) { return a->getID() < b->getID(); });
		for (Object *other : order)
		{
			if (crate->isDestroyed()) break;
			ObjectCollide::onCollide(*crate, *other);
		}
	}
}

void CrateCollide::crc(StateHasher &h) const { h.addBool(m_executed); }
void CrateCollide::xferCrate(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferBehaviorModule(x);
	x.xferBool(&m_executed);
}
void SalvageCrateCollide::xfer(Xfer &x) { SnapshotXfer::version(x, 1, 1); xferCrate(x); }
void VeterancyCrateCollide::xfer(Xfer &x) { SnapshotXfer::version(x, 1, 1); xferCrate(x); }

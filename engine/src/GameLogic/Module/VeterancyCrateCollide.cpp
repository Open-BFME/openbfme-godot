// OpenBFME. GPL-3.0. See GameLogic/Module/VeterancyCrateCollide.h and docs/CRATE_PICKUPS.md.
#include "GameLogic/Module/VeterancyCrateCollide.h"

#include "Common/Player.h"
#include "Common/Xfer.h"
#include "GameLogic/AI/AIGarrisonStates.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/SnapshotXfer.h"

#include <cstddef>

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

const char *VeterancyCrateCollide::limitationsStop()
{
	return "[S-5901] veterancy crates do not reproduce the significant-height tolerance, GUI rank label, ExecuteAnimation or pilot script-name transfer; pilot crates are refused";
}

bool VeterancyCrateCollide::isValidToExecute(const Object *other) const
{
	if (!CrateCollide::isValidToExecute(other))
	{
		return false;
	}
	getObject()->logic().noteStop(limitationsStop());
	const ExperienceTracker *xp = other->getExperienceTracker();
	if (m_veterancy->m_isPilot)
	{
		getObject()->logic().noteStop(limitationsStop());
		return false;
	}
	// RW 0x8C1076: getLevelsToGain returns zero for AddsOwnerVeterancy, so native validity refuses it.
	return !GarrisonRules::isAboveTerrain(*other) && !m_veterancy->m_addsOwnerVeterancy && xp && xp->isTrainable() && xp->isAcceptingExperiencePoints() &&
		xp->getRank() <= m_veterancy->m_affectsUpToLevel;
}

bool VeterancyCrateCollide::executeCrateBehavior(Object &other)
{
	auto promote = [&](Object &obj) {
		if (ExperienceTracker *xp = obj.getExperienceTracker()) xp->gainExpForLevel(1, true, false);
	};
	if (m_veterancy->m_effectRange == 0)
	{
		promote(other);
	}
	else
	{
		PartitionFilterFn owned([&](Object &obj) { return obj.getControllingPlayer() == other.getControllingPlayer() && &obj != &other; });
		auto hits = other.logic().partition().iterateObjectsInRange(*other.getPosition(), (float)m_veterancy->m_effectRange, FROM_CENTER_2D, { &owned }, ITER_FASTEST);
		for (const auto &hit : hits) promote(*hit.object);
	}
	return true;
}

void VeterancyCrateCollide::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferCrateCollide(x);
}

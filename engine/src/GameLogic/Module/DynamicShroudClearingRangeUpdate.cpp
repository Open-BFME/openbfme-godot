// OpenBFME. GPL-3.0. See GameLogic/Module/DynamicShroudClearingRangeUpdate.h (fork issue 285, LOCAL-35).
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif
#include "GameLogic/Module/DynamicShroudClearingRangeUpdate.h"

#include "Common/INI.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SnapshotXfer.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/System/ShroudManager.h"

#include <cstddef>
#include <memory>
#include <stdexcept>

namespace
{
using Data = DynamicShroudClearingRangeUpdateModuleData;
#define DSCRU_OFF(m) (int)offsetof(Data, m)
// RW 0xC62648 (the rows in the binary's order; the parse uses the offsets)
const FieldParse kFields[] = {
	{ "ChangeInterval", INI::parseDurationUnsignedInt, nullptr, DSCRU_OFF(m_changeInterval) },
	{ "GrowInterval", INI::parseDurationUnsignedInt, nullptr, DSCRU_OFF(m_growInterval) },
	{ "ShrinkDelay", INI::parseDurationUnsignedInt, nullptr, DSCRU_OFF(m_shrinkDelay) },
	{ "ShrinkTime", INI::parseDurationUnsignedInt, nullptr, DSCRU_OFF(m_shrinkTime) },
	{ "GrowDelay", INI::parseDurationUnsignedInt, nullptr, DSCRU_OFF(m_growDelay) },
	{ "GrowTime", INI::parseDurationUnsignedInt, nullptr, DSCRU_OFF(m_growTime) },
	{ "FinalVision", INI::parseReal, nullptr, DSCRU_OFF(m_finalVision) },
	{ "GridDecalTemplate", parseRadiusDecalTemplate, nullptr, DSCRU_OFF(m_gridDecalTemplate) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef DSCRU_OFF
} // namespace

void DynamicShroudClearingRangeUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kFields);
}

DynamicShroudClearingRangeUpdate::DynamicShroudClearingRangeUpdate(Thing *thing, const Data *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	// RW 0x88E6D8: the deadlines come off ShrinkDelay + ShrinkTime (the total countdown); 200.0f is the retail default range.
	m_changeIntervalCountdown = 0;
	m_visionChangePerInterval = 0.0f;
	const std::int32_t stateCountDown = (std::int32_t)(m_data->m_shrinkDelay + m_data->m_shrinkTime);
	m_stateCountDown = stateCountDown;
	m_totalFrames = stateCountDown < 1 ? 1 : stateCountDown; // RW `cmp edx,1; jl` max(1, stateCountDown)
	m_shrinkStartDeadline = (std::uint32_t)(stateCountDown - (std::int32_t)m_data->m_shrinkDelay);
	m_growStartDeadline = (std::uint32_t)(stateCountDown - (std::int32_t)m_data->m_growDelay);
	m_sustainDeadline = (std::uint32_t)((std::int32_t)m_growStartDeadline - (std::int32_t)m_data->m_growTime);
	m_doneForeverFrame = getObject() ? getObject()->logic().getFrame() + (UnsignedInt)stateCountDown : (UnsignedInt)stateCountDown;
	m_decalsCreated = false;
	m_nativeClearingRange = 200.0f;
	m_currentClearingRange = 0.0f;
	m_state = NOT_STARTED_YET;

	Object *obj = getObject();
	if (!obj || !obj->getControllingPlayer())
	{
		return;
	}
	// RW 0x88E79A -> 0x88E7A5: Object::getShroudClearingRange (RW 0x68E4E2), read only with a controlling player.
	if (ShroudManager *sm = obj->logic().shroud())
	{
		m_nativeClearingRange = sm->visionRange(*obj);
	}
}

UpdateSleepTime DynamicShroudClearingRangeUpdate::update()
{
	// RW 0x88E9B0 (the update interface vtable sits at +0x10, so retail's `this` is the +0x10 sub-object).
	if (m_state == SLEEPING)
	{
		return UPDATE_SLEEP_NONE;
	}
	Object *me = getObject();
	if (!me)
	{
		return UPDATE_SLEEP_NONE;
	}
	const UnsignedInt currentFrame = me->logic().getFrame();

	if (!m_decalsCreated)
	{
		createGridDecals(m_data->m_gridDecalTemplate, 100.0f, *me->getPosition()); // the decal visual is not drawn (S-7100)
		m_decalsCreated = true;
	}

	if (m_stateCountDown <= 0 || currentFrame > m_doneForeverFrame)
	{
		m_state = DONE_FOREVER;
	}
	else if (m_stateCountDown <= (std::int32_t)m_shrinkStartDeadline)
	{
		m_state = SHRINKING;
	}
	else if (m_stateCountDown <= (std::int32_t)m_sustainDeadline)
	{
		m_state = SUSTAINING;
	}
	else if (m_stateCountDown <= (std::int32_t)m_growStartDeadline)
	{
		m_state = GROWING;
	}

	switch (m_state)
	{
	case NOT_STARTED_YET:
		animateGridDecals();
		break;
	case GROWING:
	{
		animateGridDecals();
		const float growTime = SimMath::sseFromInt32((std::int32_t)m_data->m_growTime);
		const float denom = growTime > 1.0f ? growTime : 1.0f; // RW `fcompi`: max(1.0f, growTime)
		const float step = SimMath::sseDiv(m_nativeClearingRange, denom);
		m_currentClearingRange = SimMath::sseAdd(m_currentClearingRange, step);
		if (m_currentClearingRange >= m_nativeClearingRange) // RW `comiss; jb`: not below
		{
			m_state = SUSTAINING;
		}
		break;
	}
	case SUSTAINING:
		m_currentClearingRange = m_nativeClearingRange;
		killGridDecals();
		break;
	case SHRINKING:
	{
		const float shrinkTime = SimMath::sseFromInt32((std::int32_t)m_data->m_shrinkTime);
		const float denom = shrinkTime > 1.0f ? shrinkTime : 1.0f;
		const float step = SimMath::sseDiv(SimMath::sseSub(m_nativeClearingRange, m_data->m_finalVision), denom);
		m_currentClearingRange = SimMath::sseSub(m_currentClearingRange, step);
		break;
	}
	case DONE_FOREVER:
		killGridDecals();
		m_currentClearingRange = m_data->m_finalVision;
		break;
	case SLEEPING:
		break;
	}

	if (m_stateCountDown > 0)
	{
		--m_stateCountDown;
	}

	if (m_changeIntervalCountdown > 0)
	{
		--m_changeIntervalCountdown;
	}
	else
	{
		// RW 0x88EB34 .. 0x88EB67
		m_changeIntervalCountdown = (m_state == GROWING ? m_data->m_growInterval : m_data->m_changeInterval);
		me->setShroudClearingRange(m_currentClearingRange);
		if (m_state == DONE_FOREVER)
		{
			m_state = SLEEPING;
		}
	}

	return UPDATE_SLEEP_NONE;
}

void DynamicShroudClearingRangeUpdate::xfer(Xfer &x)
{
	// RW 0x88EB76 (11 fields; m_state is recomputed every frame and not saved)
	SnapshotXfer::version(x, 1, 1);
	xferUpdateModule(x);
	x.xferInt(&m_stateCountDown);
	x.xferInt(&m_totalFrames);
	SnapshotXfer::checkRange(m_totalFrames, 1, 0x7FFFFFFF, "DynamicShroudClearingRangeUpdate m_totalFrames");
	x.xferUnsignedInt(&m_growStartDeadline);
	x.xferUnsignedInt(&m_sustainDeadline);
	x.xferUnsignedInt(&m_shrinkStartDeadline);
	x.xferUnsignedInt(&m_doneForeverFrame);
	x.xferUnsignedInt(&m_changeIntervalCountdown);
	x.xferBool(&m_decalsCreated);
	x.xferReal(&m_visionChangePerInterval);
	x.xferReal(&m_nativeClearingRange);
	x.xferReal(&m_currentClearingRange);
}

void DynamicShroudClearingRangeUpdate::crc(StateHasher &hasher) const
{
	UpdateModule::crc(hasher);
	hasher.addI32(m_stateCountDown);
	hasher.addI32(m_totalFrames);
	hasher.addU32(m_growStartDeadline);
	hasher.addU32(m_sustainDeadline);
	hasher.addU32(m_shrinkStartDeadline);
	hasher.addU32(m_doneForeverFrame);
	hasher.addU32(m_changeIntervalCountdown);
	hasher.addBool(m_decalsCreated);
	hasher.addFloat(m_visionChangePerInterval);
	hasher.addFloat(m_nativeClearingRange);
	hasher.addFloat(m_currentClearingRange);
}

void DynamicShroudClearingRangeUpdate::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<Data>("DynamicShroudClearingRangeUpdate", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("DynamicShroudClearingRangeUpdate", MODULETYPE_BEHAVIOR,
		[](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
			const auto *typed = dynamic_cast<const Data *>(data);
			if (!typed)
			{
				throw std::logic_error("DynamicShroudClearingRangeUpdate: the module data is not typed");
			}
			return std::make_unique<DynamicShroudClearingRangeUpdate>(thing, typed);
		});
}

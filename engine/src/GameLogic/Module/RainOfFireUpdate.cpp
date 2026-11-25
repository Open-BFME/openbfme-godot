// OpenBFME. GPL-3.0.
// See GameLogic/Module/RainOfFireUpdate.h (lane MODGAP-2, Local 169; editions: both RotWK 2.01 and BFME2 1.06).

// Local 174: RW 0x8AF799 (RainOfFireUpdate::getModuleName)
#include "GameLogic/Module/RainOfFireUpdate.h"

#include "Common/INI.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SnapshotXfer.h"

#include <cstddef>
#include <memory>
#include <stdexcept>

namespace
{
#define ROFU_OFF(m) (int)offsetof(RainOfFireUpdateModuleData, m)
// RW 0xC6B618 (9 rows)
const FieldParse kRainOfFireUpdateFieldParse[] = {
	{ "StartRainTime", INI::parseDurationUnsignedInt, nullptr, ROFU_OFF(m_startRainTime) },
	{ "DarknessFadeTime", INI::parseDurationUnsignedInt, nullptr, ROFU_OFF(m_darknessFadeTime) },
	{ "RainEmitterHeight", INI::parseReal, nullptr, ROFU_OFF(m_rainEmitterHeight) },
	{ "DarknessLevel", INI::parseReal, nullptr, ROFU_OFF(m_darknessLevel) },
	{ "JitterRadius", INI::parseReal, nullptr, ROFU_OFF(m_jitterRadius) },
	{ "DPSMin", INI::parseReal, nullptr, ROFU_OFF(m_dpsMin) },
	{ "DPSMax", INI::parseReal, nullptr, ROFU_OFF(m_dpsMax) },
	{ "DPSRampupTime", INI::parseDurationReal, nullptr, ROFU_OFF(m_dpsRampupTime) },
	{ "RainOffset", INI::parseCoord2D, nullptr, ROFU_OFF(m_rainOffset) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef ROFU_OFF
} // namespace

// RW 0x8AF6A7 (ctor)
// RW 0x8AF701
void RainOfFireUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kRainOfFireUpdateFieldParse);
}

// RW 0x8AF712
RainOfFireUpdate::RainOfFireUpdate(Thing *thing, const RainOfFireUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	if (m_data)
	{
		if (getObject())
		{
			m_frame = getObject()->logic().getFrame();
		}
		if (m_data->m_dpsRampupTime > 0.0f)
		{
			m_dpsIncrement = SimMath::divf32(SimMath::subf32(m_data->m_dpsMax, m_data->m_dpsMin), m_data->m_dpsRampupTime);
		}
	}
}

// RW 0x8AF8B7 (deleting dtor RW 0x8AFC43)
RainOfFireUpdate::~RainOfFireUpdate()
{
	setDarkness(0.0f);
}

// RW 0x8AF7F7: stores m_darkness = amount and computes 1.0 - (m_darknessLevel * m_scale * amount)
void RainOfFireUpdate::setDarkness(float amount)
{
	m_darkness = amount;
	if (!m_data)
	{
		return;
	}
	m_effectiveDarkness = SimMath::subf32(1.0f, SimMath::mulf32(SimMath::mulf32(m_data->m_darknessLevel, m_scale), amount));
}

// RW 0x5015D4 / BFME2 slot 0
void RainOfFireUpdate::release(float amount)
{
	m_scale = amount;
}

// RW 0x8AF79F / BFME2 0x4AC18D
void RainOfFireUpdate::advance(float amount)
{
	m_darkness = SimMath::addf32(m_darkness, amount);
}

UpdateSleepTime RainOfFireUpdate::update()
{
	if (!m_data)
	{
		return UPDATE_SLEEP_FOREVER;
	}
	return UPDATE_SLEEP_NONE;
}

// RW 0x8AF835
void RainOfFireUpdate::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferUpdateModule(x);
	x.xferUnsignedInt(&m_frame);
	x.xferReal(&m_darkness);
	x.xferReal(&m_scale);
	x.xferReal(&m_dps);
	x.xferReal(&m_dpsIncrement);
}

// RW 0x8AF891
void RainOfFireUpdate::loadPostProcess()
{
	release(m_scale);
	setDarkness(m_darkness);
}

// RW 0x64F09F (friend_newModuleInstance), RW 0x8AF7B2 (getModuleNameKey)
void RainOfFireUpdate::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<RainOfFireUpdateModuleData>("RainOfFireUpdate", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("RainOfFireUpdate", MODULETYPE_BEHAVIOR,
		[](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
			const auto *typed = dynamic_cast<const RainOfFireUpdateModuleData *>(data);
			if (!typed)
			{
				throw std::logic_error("RainOfFireUpdate: module data is not typed");
			}
			return std::make_unique<RainOfFireUpdate>(thing, typed);
		});
}

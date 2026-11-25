#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif
// OpenBFME. GPL-3.0.
// See GameLogic/Module/PartTheHeavensUpdate.h (lane MODGAP-2; editions: both RotWK 2.01 and BFME2 1.06).

#include "GameLogic/Module/PartTheHeavensUpdate.h"

#include "Common/INI.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SnapshotXfer.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <stdexcept>

namespace
{
constexpr float kRadsPerDegree = 0.0174532925f;

#define PTH_OFF(m) (int)offsetof(PartTheHeavensUpdateModuleData, m)
// RW 0xC6B8F8 / BFME2 0xC54DC8
const FieldParse kPartTheHeavensUpdateFieldParse[] = {
	{ "Texture", INI::parseAsciiString, nullptr, PTH_OFF(m_texture) },
	{ "Radius", FunctionCurve::parseFunctionCurve, nullptr, PTH_OFF(m_radius) },
	{ "Color", INI::parseColorInt, nullptr, PTH_OFF(m_color) },
	{ "Opacity", FunctionCurve::parseFunctionCurve, nullptr, PTH_OFF(m_opacity) },
	{ "Angle", FunctionCurve::parseFunctionCurve, nullptr, PTH_OFF(m_angle) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef PTH_OFF
} // namespace

PartTheHeavensUpdateModuleData::PartTheHeavensUpdateModuleData()
	: m_radius(0.0f)
	, m_opacity(1.0f)
	, m_angle(0.0f)
{
}

void PartTheHeavensUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kPartTheHeavensUpdateFieldParse);
}

PartTheHeavensUpdate::PartTheHeavensUpdate(Thing *thing, const PartTheHeavensUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	if (getObject())
	{
		m_startFrame = getObject()->logic().getFrame();
	}
}

// RW 0x8AFF59 / BFME2 0x4AC8EF
PartTheHeavensUpdate::~PartTheHeavensUpdate()
{
	if (m_shadow)
	{
		m_shadow->release();
		m_shadow = nullptr;
	}
}

// RW 0x8B0155 / BFME2 0x4ACADA
void PartTheHeavensUpdate::createEmitter()
{
	if (m_shadow != nullptr || !m_data || m_data->m_texture.empty() || !TheProjectedShadowManager)
	{
		return;
	}

	Shadow::ShadowTypeInfo ev;
	float v0 = m_data->m_radius(0.0f);
	ev.m_first = m_data->m_texture;
	float v2 = SimMath::mulf32(v0, 2.0f);
	ev.m_unknown8 = 0x40; // SHADOW_ADDITIVE_DECAL
	ev.m_floatC = v2;
	ev.m_float10 = v2;
	ev.m_byte25 = 0; // allowUpdates = 0
	ev.m_byte26 = 1; // worldAlign = 1
	ev.m_float14 = 0.0f;
	ev.m_float18 = 0.0f;

	m_shadow = TheProjectedShadowManager->createEmitter(0x40, &ev, &ev);
	if (m_shadow != nullptr && getObject())
	{
		m_shadow->m_pos08 = *getObject()->getPosition();
		m_shadow->setColor(m_data->m_color);
	}
}

// RW 0x8B0075 / BFME2 0x4AC9FA
void PartTheHeavensUpdate::sampleCurves(int elapsed)
{
	if (!m_data)
	{
		return;
	}

	float t = SimMath::sseFromInt32(elapsed);
	m_currentRadius = SimMath::mulf32(2.0f, m_data->m_radius(t));
	m_currentAngleRads = SimMath::mulf32(m_data->m_angle(t), kRadsPerDegree);
	int rawOpacity = (int)SimMath::mulf32(m_data->m_opacity(t), 255.0f);
	m_currentOpacity = (std::uint8_t)std::clamp(rawOpacity, 0, 255);

	if (m_shadow)
	{
		float doubled = m_currentRadius;
		float rads = m_currentAngleRads;
		m_shadow->m_58->m_sizeX = doubled;
		m_shadow->m_58->m_sizeY = doubled;
		m_shadow->m_5c->m_sizeX = doubled;
		m_shadow->m_5c->m_sizeY = doubled;
		m_shadow->setOpacity(m_currentOpacity);
		m_shadow->m_58->m_angle = rads;
		m_shadow->m_5c->m_angle = SimMath::subf32(0.0f, rads);
	}
}

// RW 0x008B022E / BFME2 0x4ACBB3
UpdateSleepTime PartTheHeavensUpdate::update()
{
	if (!m_data)
	{
		return UPDATE_SLEEP_FOREVER;
	}

	unsigned int elapsed = 0;
	if (getObject())
	{
		unsigned int frame = getObject()->logic().getFrame();
		if (frame >= m_startFrame)
		{
			elapsed = frame - m_startFrame;
		}
	}

	if (elapsed == 0)
	{
		createEmitter();
	}
	sampleCurves((int)elapsed);

	return UPDATE_SLEEP_NONE;
}

// RW 0x8B0263 / BFME2 0x4ACBE8
void PartTheHeavensUpdate::loadPostProcess()
{
	createEmitter();
}

void PartTheHeavensUpdate::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferUpdateModule(x);
	x.xferUnsignedInt(&m_startFrame);
}

void PartTheHeavensUpdate::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<PartTheHeavensUpdateModuleData>("PartTheHeavensUpdate", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("PartTheHeavensUpdate", MODULETYPE_BEHAVIOR,
		[](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
			const auto *typed = dynamic_cast<const PartTheHeavensUpdateModuleData *>(data);
			if (!typed)
			{
				throw std::logic_error("PartTheHeavensUpdate: module data is not typed");
			}
			return std::make_unique<PartTheHeavensUpdate>(thing, typed);
		});
}

// OpenBFME. GPL-3.0.
//
// Both editions: RW 0x004D102E, BFME2 0x004D181D. See W3DTornadoDraw.h.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DTornadoDraw.h"

#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/Thing.h"
#include "GameLogic/SnapshotXfer.h"

#include <cmath>
#include <cstddef>
#include <memory>
#include <stdexcept>

namespace
{
#define TORNADO_ROW(name, parse, field) { name, parse, nullptr, (int)offsetof(W3DTornadoDrawModuleData, field) }
const FieldParse kW3DTornadoDrawFields[] = {
	TORNADO_ROW("DecalTemplate", parseRadiusDecalTemplate, m_decalTemplate),
	TORNADO_ROW("DecalCount", INI::parseUnsignedInt, m_decalCount),
	TORNADO_ROW("DecalMaxRadius", INI::parseReal, m_decalMaxRadius),
	{ nullptr, nullptr, nullptr, 0 }
};
#undef TORNADO_ROW
} // namespace

namespace W3DTornadoDrawTables
{
const FieldParse *tornado()
{
	return kW3DTornadoDrawFields;
}
} // namespace W3DTornadoDrawTables

// RW 0x004D0F54 (W3DTornadoDraw decal list cleanup)
W3DTornadoDrawModuleData::W3DTornadoDrawModuleData()
	: m_decalCount(0), m_decalMaxRadius(200.0f)
{
}

void W3DTornadoDrawModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kW3DTornadoDrawFields);
}

namespace
{
// RW 0x644FD0 (DONOR BFME2 normalizeAngle): wraps radians into (-pi, pi]
float normalizeSpiralAngle(float angle)
{
	constexpr float kPi = 3.14159265358979323846f;
	constexpr float kTwoPi = 6.2831854820251465f; // RW 0xBDD38C
	while (angle > kPi)
	{
		angle -= kTwoPi;
	}
	while (angle <= -kPi)
	{
		angle += kTwoPi;
	}
	return angle;
}
} // namespace

W3DTornadoDraw::W3DTornadoDraw(Thing *thing, const ModuleData *moduleData)
	: DrawableModule(thing, moduleData), m_thing(thing), m_data(dynamic_cast<const W3DTornadoDrawModuleData *>(moduleData))
{
	createDecals();
}

W3DTornadoDraw::~W3DTornadoDraw()
{
	clearDecals();
}

void W3DTornadoDraw::onDrawableBoundToObject()
{
	createDecals();
	m_hasLastStepFrame = false;
}

void W3DTornadoDraw::clearDecals()
{
	m_decals.clear();
	m_hasLastStepFrame = false;
	m_lastStepFrame = 0;
}

void W3DTornadoDraw::createDecals()
{
	clearDecals();
	if (!m_data || m_data->m_decalCount == 0 || !m_thing)
	{
		return;
	}
	const Coord3D *p = m_thing->getPosition();
	const Coord3D pos = p ? *p : Coord3D{};
	float bound = m_data->m_decalTemplate.maxRadius;
	if (bound > m_data->m_decalMaxRadius)
	{
		bound = m_data->m_decalMaxRadius;
	}
	const RadiusDecalTemplate &tmpl = m_data->m_decalTemplate;
	const std::uint32_t baseRgb = (tmpl.color != 0 ? tmpl.color : 0xFFFFFFu) & 0xFFFFFFu;
	const std::uint32_t packed = RadiusCursor::packedColor(tmpl.style, baseRgb, 255);
	m_decals.reserve(m_data->m_decalCount);
	for (unsigned i = 0; i < m_data->m_decalCount; ++i)
	{
		DecalInstance d;
		d.position = pos;
		d.radius = (float)i * bound / (float)m_data->m_decalCount;
		d.angle = 0.0f;
		d.opacity = 255;
		d.color = packed;
		// RW 0x732DE7 (0x732E1D): createRadiusDecal creates no shadow when radius <= 0.0f (i == 0).
		d.hasShadow = (d.radius > 0.0f) && !tmpl.texture.empty();
		m_decals.push_back(d);
	}
}

// TARGET RW 0x004D1071 GetSnapshotName, RW 0x004D11EE scalar deleting destructor.
void W3DTornadoDraw::reactToTransformChange(const Coord3D * /*oldPos*/, float /*oldAngle*/)
{
	// RW 0x004D1000 (DONOR BFME2 0x000D17EF): updates decal positions to drawable position
	if (m_thing && m_thing->getPosition())
	{
		const Coord3D pos = *m_thing->getPosition();
		for (auto &d : m_decals)
		{
			d.position = pos;
		}
	}
}

void W3DTornadoDraw::stepDecals(unsigned clientFrame, bool drawIconUI, std::uint32_t playerColor)
{
	if (!m_data || m_decals.empty())
	{
		return;
	}
	reactToTransformChange(nullptr, 0.0f);
	if (m_hasLastStepFrame && m_lastStepFrame == clientFrame)
	{
		return;
	}
	m_hasLastStepFrame = true;
	m_lastStepFrame = clientFrame;

	const RadiusDecalTemplate &tmpl = m_data->m_decalTemplate;
	const int throbAlpha = RadiusCursor::throbOpacity(tmpl, clientFrame, drawIconUI);
	const float limit = 2.0f * tmpl.maxRadius;
	const bool isSpiral = (tmpl.rotationsPerMinute > 0.0f && tmpl.spiralAcceleration > 0.0f && limit > 0.0f);
	const std::uint32_t baseRgb = (tmpl.color != 0 ? tmpl.color : playerColor) & 0xFFFFFFu;

	for (DecalInstance &d : m_decals)
	{
		if (!d.hasShadow)
		{
			continue;
		}
		if (isSpiral)
		{
			// Both editions (RW 0x732A72..0x732B3F, DONOR BFME2 RadiusDecal.cpp:552):
			// shrink inward by SpiralAcceleration, wrap around 2 * tmpl.maxRadius, spin & fade by (1 - size/limit)^2.
			float size = d.radius * 2.0f - tmpl.spiralAcceleration;
			if (size < 0.0f)
			{
				size += limit;
			}
			else if (size > limit)
			{
				size = limit;
			}
			d.radius = size * 0.5f;
			const float u = 1.0f - (size / limit);
			const float fade = u * u;
			d.angle = normalizeSpiralAngle(d.angle + fade * tmpl.rotationsPerMinute);
			const int alpha = (int)(fade * 255.0f);
			d.opacity = alpha;
			const std::uint32_t gray = ((std::uint32_t)(unsigned)alpha << 16) | ((std::uint32_t)(unsigned)alpha << 8) | (std::uint32_t)(unsigned)alpha;
			d.color = RadiusCursor::packedColor(tmpl.style, gray, alpha);
		}
		else
		{
			// RW 0x732B41..0x732B88: throb opacity + constant spin
			d.opacity = throbAlpha;
			if (tmpl.rotationsPerMinute != 0.0f)
			{
				d.angle = normalizeSpiralAngle((float)clientFrame * ((1.0f / 30.0f) * 0.0174532924f * tmpl.rotationsPerMinute * 60.0f));
			}
			d.color = RadiusCursor::packedColor(tmpl.style, baseRgb, throbAlpha);
		}
	}
}

void W3DTornadoDraw::appendSelectionDecals(std::uint32_t objectId, unsigned drawableId, bool playerIsLocal, std::uint32_t playerColor,
	unsigned clientFrame, bool drawIconUI, std::vector<SelectionDecal> &out)
{
	if (!m_data)
	{
		return;
	}
	const RadiusDecalTemplate &tmpl = m_data->m_decalTemplate;
	if (tmpl.texture.empty())
	{
		return;
	}
	// RW 0x732E2D..0x732E40: OnlyVisibleToOwningPlayer gates creation against local player
	if (tmpl.onlyVisibleToOwningPlayer && !playerIsLocal)
	{
		return;
	}
	stepDecals(clientFrame, drawIconUI, playerColor);
	const std::string tex = tmpl.texture.find('.') == std::string::npos ? tmpl.texture + ".tga" : tmpl.texture;
	for (const DecalInstance &d : m_decals)
	{
		if (!d.hasShadow || d.radius <= 0.0f)
		{
			continue;
		}
		SelectionDecal sd;
		sd.object = objectId;
		sd.drawable = drawableId;
		sd.position = d.position;
		sd.drawZ = d.position.z;
		sd.size = d.radius * 2.0f;
		sd.sizeY = d.radius * 2.0f;
		sd.angle = d.angle;
		sd.texture = tex;
		sd.texture2 = tmpl.texture2;
		sd.style = tmpl.style;
		sd.color = d.color;
		sd.terrainOnly = false;
		out.push_back(std::move(sd));
	}
}

void W3DTornadoDraw::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	std::uint32_t count = (std::uint32_t)m_decals.size();
	x.xferCount(&count, sizeof(float) * 4, sizeof(DecalInstance));
	if (x.isLoading())
	{
		m_decals.resize(count);
	}
	for (size_t i = 0; i < count; ++i)
	{
		SnapshotXfer::coord3D(x, m_decals[i].position);
		x.xferReal(&m_decals[i].radius);
		x.xferReal(&m_decals[i].angle);
		x.xferUnsignedInt(&m_decals[i].color);
		x.xferInt(&m_decals[i].opacity);
		SnapshotXfer::checkRange(m_decals[i].opacity, 0, 255, "W3DTornadoDraw decal opacity"); // a byte's share, as the draw sets it
		x.xferBool(&m_decals[i].hasShadow);
	}
}

void W3DTornadoDraw::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<W3DTornadoDrawModuleData>("W3DTornadoDraw", MODULETYPE_DRAW);
	modules.bindModuleProc("W3DTornadoDraw", MODULETYPE_DRAW, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const auto *typed = dynamic_cast<const W3DTornadoDrawModuleData *>(data);
		return std::make_unique<W3DTornadoDraw>(thing, typed);
	});
}

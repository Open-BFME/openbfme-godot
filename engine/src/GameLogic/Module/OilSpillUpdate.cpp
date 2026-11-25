// OpenBFME. GPL-3.0.
//
// Both editions: RW 0x0088F863, BFME2 0x0048C1F7. See OilSpillUpdate.h.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/OilSpillUpdate.h"

#include "Common/AsciiString.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ModuleFactory.h"
#include "GameLogic/Construction.h"
#include "GameLogic/FXEvents.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"

#include <cstddef>
#include <memory>
#include <stdexcept>

namespace
{
void parseFXName(INI *ini, void *, void *store, const void *)
{
	const char *t = ini->getNextTokenOrNull();
	if (t && AsciiStringUtil::compareNoCase(t, "None") != 0)
	{
		*static_cast<std::string *>(store) = t;
	}
	else
	{
		static_cast<std::string *>(store)->clear();
	}
}

#define SPILL_ROW(name, parse, field) { name, parse, nullptr, (int)offsetof(OilSpillUpdateModuleData, field) }
const FieldParse kOilSpillUpdateFields[] = {
	SPILL_ROW("BreadcrumbName", INI::parseAsciiString, m_breadcrumbName),
	SPILL_ROW("IgnitionWeaponName", INI::parseAsciiString, m_ignitionWeaponName),
	SPILL_ROW("IgnitionWeaponSpacing", INI::parseReal, m_ignitionWeaponSpacing),
	SPILL_ROW("OilSpillFX", parseFXName, m_oilSpillFX),
	{ nullptr, nullptr, nullptr, 0 }
};
#undef SPILL_ROW
} // namespace

void OilSpillUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	FireWeaponUpdateModuleData::buildFieldParse(p);
	p.add(kOilSpillUpdateFields);
}

const char *OilSpillUpdate::stopLine()
{
	return "[S-6025] OilSpillUpdate: ground decal rendering for oil trails is not ported; breadcrumb detonation relies on FireWeaponWhenDeadBehavior";
}

OilSpillUpdate::OilSpillUpdate(Thing *thing, const OilSpillUpdateModuleData *data)
	: FireWeaponUpdate(thing, data), m_oilData(data)
{
}

OilSpillUpdate::~OilSpillUpdate() = default;

UpdateSleepTime OilSpillUpdate::update()
{
	FireWeaponUpdate::update();
	const OilSpillUpdateModuleData *data = m_oilData;
	if (!data || !getObject())
	{
		return UPDATE_SLEEP_FOREVER;
	}
	const Coord3D *p = getObject()->getPosition();
	if (p)
	{
		const Coord3D pos = *p;
		bool drop = true;
		if (!m_breadcrumbs.empty())
		{
			const Coord3D &last = m_breadcrumbs.back();
			const float dx = SimMath::subf32(pos.x, last.x);
			const float dy = SimMath::subf32(pos.y, last.y);
			drop = SimMath::length2d(dx, dy) >= data->m_ignitionWeaponSpacing;
		}
		if (drop)
		{
			Object *obj = getObject();
			GameLogic &logic = obj->logic();
			logic.noteStop(stopLine());
			if (!data->m_breadcrumbName.empty())
			{
				if (const ThingTemplate *tmpl = logic.things().findTemplate(data->m_breadcrumbName))
				{
					Player *owner = obj->getControllingPlayer();
					Player *neutral = logic.players().getNeutralPlayer();
					Player *effectiveOwner = owner ? owner : neutral;
					if (effectiveOwner)
					{
						Construction::buildObjectNow(logic, obj, *tmpl, pos, 0.0f, *effectiveOwner);
					}
				}
			}
			if (FXEventLog::isFXName(data->m_oilSpillFX))
			{
				FXEvent ev;
				ev.kind = FXEvent::POSITION_FX;
				ev.site = "OilSpillUpdate";
				ev.frame = logic.getFrame();
				ev.fxList = &data->m_oilSpillFX;
				ev.primary = obj->getID();
				ev.position = pos;
				logic.fxEvents().emit(ev);
			}
			m_breadcrumbs.push_back(pos);
		}
	}
	return UPDATE_SLEEP_NONE;
}

void OilSpillUpdate::crc(StateHasher &hasher) const
{
	FireWeaponUpdate::crc(hasher);
	hasher.addI32((int)m_breadcrumbs.size());
	for (const auto &pt : m_breadcrumbs)
	{
		hasher.addFloat(pt.x);
		hasher.addFloat(pt.y);
		hasher.addFloat(pt.z);
	}
}

void OilSpillUpdate::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	FireWeaponUpdate::xfer(x);
	std::uint32_t count = (std::uint32_t)m_breadcrumbs.size();
	x.xferCount(&count, sizeof(float) * 3, sizeof(Coord3D));
	if (x.isLoading())
	{
		m_breadcrumbs.resize(count);
	}
	for (size_t i = 0; i < count; ++i)
	{
		SnapshotXfer::coord3D(x, m_breadcrumbs[i]);
	}
}

void OilSpillUpdate::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<OilSpillUpdateModuleData>("OilSpillUpdate", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("OilSpillUpdate", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const auto *typed = dynamic_cast<const OilSpillUpdateModuleData *>(data);
		if (!typed)
		{
			throw std::logic_error("OilSpillUpdate: module data is not typed");
		}
		return std::make_unique<OilSpillUpdate>(thing, typed);
	});
}

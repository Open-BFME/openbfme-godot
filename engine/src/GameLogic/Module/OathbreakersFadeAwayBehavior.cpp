// OpenBFME. GPL-3.0. Both editions: RW 0x8BCC82, BFME2 0x8B8B41.
#if defined(__GNUC__) || defined(__clang__)
// Local 169: RW 0x8BCBBA (FadeOutTime)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif
#include "GameLogic/Module/OathbreakersFadeAwayBehavior.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "Common/INI.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SnapshotXfer.h"
#include <cstddef>
#include <memory>
#include <stdexcept>

namespace {
using Data = OathbreakersFadeAwayBehaviorModuleData;
const FieldParse kFields[] = {
	{ "FadeOutTime", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(Data, m_fadeOutTime) },
	{ nullptr, nullptr, nullptr, 0 }
};
}
void OathbreakersFadeAwayBehaviorModuleData::buildFieldParse(MultiIniFieldParse &p) { p.add(kFields); }
const char *OathbreakersFadeAwayBehavior::hordeStop()
{
	return "[S-6006] OathbreakersFadeAwayBehavior: HordeContain member removal RW 0x87370B / BFME2 0x870717 is only partially reproduced by removeMember; its remaining effects are unverified";
}
OathbreakersFadeAwayBehavior::OathbreakersFadeAwayBehavior(Thing *thing, const Data *data)
	: UpdateModule(thing, data), m_data(data) {}

UpdateSleepTime OathbreakersFadeAwayBehavior::update()
{
	Object *obj = getObject();
	const int oath = CombatNames::modelCondition("OATH_FULLFILLED");
	if (obj->testModelCondition(oath)) // RW 0x8BCC8E: model condition, not KindOf
	{
		if (!m_started)
		{
			// 0x26 is HORDE_MEMBER in both status tables (BFME2 BitFlags101BodyStateGetSingleBitFromName.cpp);
			// Rva004B8B0F.cpp calls it FAERIE_FIRE, which the 1.06 table disproves.
			if (!obj->testStatus(0x26)) // RW 0x008BCC50 (0x8BCC54), DONOR BFME2 0x8B8B0F
			{
				if (AIUpdateInterface *ai = obj->getAIUpdateInterface())
				{
					ai->aiBusy(CMD_FROM_AI); // RW 0x852E2A(0, 2): command 0x31
					obj->setScriptSelectable(false); // RW 0x68BE3C
				}
			}
			m_started = true;
		}
		const unsigned frames = m_data->m_fadeOutTime ? m_data->m_fadeOutTime : 1u;
		const double step = SimMath::pc24DivW(1.0, SimMath::fildU32(frames));
		const double remaining = SimMath::pc24SubW((double)m_fraction, step); // RW keeps ST0 wide for the comparison after fst
		m_fraction = SimMath::fstpDword(remaining);
		if (remaining < 0.0)
		{
			m_fraction = 0.0f;
			GameLogic &logic = obj->logic();
			Object *producer = logic.findObjectByID(obj->getProducerID());
			if (producer && producer->isKindOfName("HORDE"))
			{
				ContainModuleInterface *contain = producer->getContain();
				if (!contain || !contain->getHordeContainInterface())
					throw std::logic_error("OathbreakersFadeAwayBehavior: horde producer lacks HordeContain");
				contain->removeFromContain(obj); // RW 0x87370B / BFME2 0x870717
			}
			logic.destroyObject(obj);
		}
		// RW 0x8BCD38: drawable opacity hook applied when ObjectClientHooks::setDrawableOpacity is present
	}
	if (obj->isEffectivelyDead()) return UPDATE_SLEEP_FOREVER;
	return obj->testModelCondition(oath) ? UPDATE_SLEEP_NONE : UPDATE_SLEEP(LOGICFRAMES_PER_SECOND); // RW 0x8BCC22
}
void OathbreakersFadeAwayBehavior::crc(StateHasher &h) const { UpdateModule::crc(h); h.addBool(m_started); h.addFloat(m_fraction); }
// RW 0x008BCB69 (DONOR BFME2 0x008B8A23; dtor RW 0x008BCB05 / RW 0x008BCC06, getModuleNameKey RW 0x008BCB1E, name RW 0x008BCB63)
void OathbreakersFadeAwayBehavior::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1); // OpenBFME native, S-4190
	xferUpdateModule(x);
	x.xferReal(&m_fraction);
	x.xferBool(&m_started);
}
void OathbreakersFadeAwayBehavior::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<Data>("OathbreakersFadeAwayBehavior", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("OathbreakersFadeAwayBehavior", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const auto *typed = dynamic_cast<const Data *>(data);
		if (!typed) throw std::logic_error("OathbreakersFadeAwayBehavior: the module data is not typed");
		return std::make_unique<OathbreakersFadeAwayBehavior>(thing, typed);
	});
}

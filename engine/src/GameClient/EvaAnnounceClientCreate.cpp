// OpenBFME. GPL-3.0. See EvaAnnounceClientCreate.h.
// RW 0x8CDE48 (AnnouncementEventEnemy field table)
#include "GameClient/EvaAnnounceClientCreate.h"
#include "Common/INI.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Xfer.h"
#include "GameLogic/Module/HordeContain.h"
#include <algorithm>
#include <cstddef>
#include <stdexcept>
namespace
{
#define EA_OFF(x) (int)offsetof(EvaAnnounceClientCreateModuleData, x)
const FieldParse kFields[] = {{"AnnouncementEventEnemy", HordeContainModuleData::parseEvaEvent, nullptr, EA_OFF(enemy)},
							  {"AnnouncementEventAlly", HordeContainModuleData::parseEvaEvent, nullptr, EA_OFF(ally)},
							  {"AnnouncementEventOwner", HordeContainModuleData::parseEvaEvent, nullptr, EA_OFF(owner)},
							  {"DelayBeforeAnnouncementMS", INI::parseDurationUnsignedInt, nullptr, EA_OFF(delay)},
							  {"OnlyIfVisible", INI::parseBool, nullptr, EA_OFF(onlyVisible)},
							  {"CountAsFirstSightedAnnoucement", INI::parseBool, nullptr, EA_OFF(firstSight)},
							  {"UseObjectsPosition", INI::parseBool, nullptr, EA_OFF(usePosition)},
							  {"CreateFakeRadarEvent", INI::parseBool, nullptr, EA_OFF(fakeRadar)},
							  {nullptr, nullptr, nullptr, 0}};
#undef EA_OFF
} // namespace
void EvaAnnounceClientCreateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kFields);
}
EvaAnnounceClientCreate::EvaAnnounceClientCreate(Thing *t, const EvaAnnounceClientCreateModuleData *d) : DrawableModule(t, d), m_data(d) {}
// RW 0x8CDDD7; BFME2 0x4C9B44: version, ObjectModule base, started, due from
// version 2. The drawable snapshot does not yet call this xfer (S-7091).
void EvaAnnounceClientCreate::xfer(Xfer &x)
{
	XferVersion version{1, 2};
	x.xferVersion(version);
	XferVersion base{1, 1};
	x.xferVersion(base);
	xferModule(x);
	x.xferBool(&m_started);
	if (version.current >= 2)
		x.xferUnsignedInt(&m_due);
	else if (x.isLoading())
		m_due = 0;
}
void EvaAnnounceClientCreate::onBuildCompleteAt(std::uint32_t frame)
{
	if (m_started)
		return;
	m_started = true;
	m_due = std::max<std::uint32_t>(7, frame + std::max<std::uint32_t>(2, m_data->delay));
}
int EvaAnnounceClientCreate::update(std::uint32_t frame, bool hidden, bool hasObject, bool owner, int relationship, bool &firstSight, bool alreadySighted)
{
	firstSight = false;
	if (!m_due || m_due > frame)
		return -1;
	m_due = 0;
	if ((m_data->onlyVisible && hidden) || !hasObject)
		return -1;
	if (m_data->firstSight && alreadySighted)
		return -1;
	firstSight = m_data->firstSight;
	if (owner)
		return m_data->owner;
	if (relationship == 0 || relationship == 1)
		return m_data->enemy;
	if (relationship == 2)
		return m_data->ally;
	return -1;
}
void EvaAnnounceClientCreate::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<EvaAnnounceClientCreateModuleData>("EvaAnnounceClientCreate", MODULETYPE_CLIENT_UPDATE);
	modules.bindModuleProc("EvaAnnounceClientCreate", MODULETYPE_CLIENT_UPDATE,
						   [](Thing *t, const ModuleData *d, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module>
						   {
							   auto *typed = dynamic_cast<const EvaAnnounceClientCreateModuleData *>(d);
							   if (!typed)
								   throw std::logic_error("EvaAnnounceClientCreate: the module data is not typed");
							   return std::make_unique<EvaAnnounceClientCreate>(t, typed);
						   });
}

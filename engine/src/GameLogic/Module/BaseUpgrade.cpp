// OpenBFME. GPL-3.0.

#include "GameLogic/Module/BaseUpgrade.h"

#include "Common/INI.h"
#include "Common/Player.h"
#include "Common/Thing/ThingFactory.h"
#include "GameLogic/SnapshotXfer.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Xfer.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/WeaponDelivery.h"
#include "GameLogic/Construction.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"

#include <stdexcept>

namespace {

// RW 0xC6DEB8 (DONOR BFME2 0xB48288)
const FieldParse kBaseUpgradeFieldParse[] = {
	{ "BuildingTemplateName", INI::parseAsciiString, nullptr, (int)offsetof(BaseUpgradeModuleData, m_buildingTemplateName) },
	{ "PlacementPrefix", INI::parseAsciiString, nullptr, (int)offsetof(BaseUpgradeModuleData, m_placementPrefix) },
	{ "PlacementIndex", INI::parseInt, nullptr, (int)offsetof(BaseUpgradeModuleData, m_placementIndex) },
	{ nullptr, nullptr, nullptr, 0 }
};

} // namespace

// RW 0x8B7863 / DONOR BFME2 0x4B3670
void BaseUpgradeModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	UpgradeModuleData::buildBaseFieldParse(p);
	p.add(kBaseUpgradeFieldParse);
}

// RW 0x8B7885 / DONOR BFME2 0x4B371C
BaseUpgrade::BaseUpgrade(Thing *thing, const BaseUpgradeModuleData *data)
	: UpgradeModule(thing, data)
	, m_data(data)
{
}

// RW 0x8B7933 / DONOR BFME2 0x4B37A0 (BaseUpgrade.cpp:332)
void BaseUpgrade::upgradeImplementation()
{
	Object *self = getObject();
	if (!self || !m_data || m_data->m_buildingTemplateName.empty())
	{
		return;
	}
	Player *player = self->getControllingPlayer();
	if (!player)
	{
		return;
	}
	GameLogic &logic = self->logic();
	const ThingTemplate *tmpl = logic.things().findTemplate(m_data->m_buildingTemplateName);
	if (!tmpl)
	{
		return;
	}

	Coord3D pos = *self->getPosition();
	float angle = self->getOrientation();

	if (m_data->m_placementIndex > 0 && !m_data->m_placementPrefix.empty())
	{
		ProjectileLaunchOffsets *provider = logic.combat().launchOffsets();
		float bones[32][12];
		int count = provider ? provider->multiLogicalBones(*self, m_data->m_placementPrefix, self->getModelConditionBits(), 32, bones) : 0;
		if (count > 0 && m_data->m_placementIndex < count)
		{
			const float *b = bones[m_data->m_placementIndex];
			const float *t = self->getBasis();
			const Coord3D &p = *self->getPosition();
			const float tt[3] = { p.x, p.y, p.z };
			float w[3];
			for (int r = 0; r < 3; ++r)
			{
				const float v = SimMath::addf32(SimMath::mulf32(t[r * 3 + 2], b[11]), SimMath::mulf32(t[r * 3 + 1], b[7]));
				w[r] = SimMath::addf32(SimMath::addf32(v, SimMath::mulf32(t[r * 3 + 0], b[3])), tt[r]);
			}
			const float m00 = SimMath::addf32(SimMath::addf32(SimMath::mulf32(t[2], b[8]), SimMath::mulf32(t[1], b[4])), SimMath::mulf32(t[0], b[0]));
			const float m10 = SimMath::addf32(SimMath::addf32(SimMath::mulf32(t[5], b[8]), SimMath::mulf32(t[4], b[4])), SimMath::mulf32(t[3], b[0]));
			pos = Coord3D{ w[0], w[1], w[2] };
			angle = (float)SimMath::atan2d(m10, m00);
		}
	}

	Team *team = player->getDefaultTeam();
	Object *newObj = logic.newObject(tmpl, team, ObjectStatusMaskType{});
	if (!newObj)
	{
		return;
	}
	newObj->setProducer(self);
	newObj->setBuilder(self);
	newObj->setPosition(&pos);
	newObj->setOrientation(angle);
	if (AIWorld *ai = logic.aiWorld())
	{
		ai->addObjectToPathfindMap(*newObj);
	}
	Construction::onStructureCreated(*player, self, *newObj);
}

// RW 0x8B7880 -> UpgradeModule::crc
void BaseUpgrade::crc(StateHasher &hasher) const
{
	UpgradeModule::crc(hasher);
}

// RW 0x8B7880 -> UpgradeModule::xfer
void BaseUpgrade::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	UpgradeModule::xfer(x);
}

// RW 0x65A28A / DONOR BFME2 0x659074 (GetSnapshotName RW 0x008B77B6, getModuleNameKey RW 0x008B77BC)
void BaseUpgrade::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<BaseUpgradeModuleData>("BaseUpgrade", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(
		"BaseUpgrade",
		MODULETYPE_BEHAVIOR,
		[](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
			const auto *typed = dynamic_cast<const BaseUpgradeModuleData *>(data);
			if (!typed)
			{
				throw std::logic_error("BaseUpgrade: the module data is not typed");
			}
			return std::make_unique<BaseUpgrade>(thing, typed);
		});
}

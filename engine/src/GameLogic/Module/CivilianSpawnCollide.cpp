// OpenBFME. GPL-3.0.

#include "GameLogic/Module/CivilianSpawnCollide.h"

#include "Common/INI.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectFilterMatch.h"

#include <stdexcept>

namespace {

// RW 0xC71748 (DONOR BFME2 0xB4A5B8)
const FieldParse kCivilianSpawnCollideFieldParse[] = {
	{ "DeleteObjectFilter", ParseObjectFilter, nullptr, (int)offsetof(CivilianSpawnCollideModuleData, m_deleteObjectFilter) },
	{ nullptr, nullptr, nullptr, 0 }
};

} // namespace

// RW 0x8C17CC / DONOR BFME2 0x4BD683
void CivilianSpawnCollideModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kCivilianSpawnCollideFieldParse);
}

// RW 0x8C17DD / DONOR BFME2 0x4BD694
CivilianSpawnCollide::CivilianSpawnCollide(Thing *thing, const CivilianSpawnCollideModuleData *data)
	: BehaviorModule(thing, data)
	, m_data(data)
{
}

// RW 0x8C1823 / DONOR BFME2 0x4BD6E0 (CivilianSpawnCollide.cpp:162)
void CivilianSpawnCollide::onCollide(Object *other, const Coord3D *loc, const Coord3D *normal)
{
	(void)loc;
	(void)normal;
	if (!other || !getObject() || !m_data)
	{
		return;
	}
	Object *self = getObject();
	if (ObjectFilterMatch::allows(self->logic(), m_data->m_deleteObjectFilter, *other, self->getControllingPlayer()))
	{
		self->logic().destroyObject(other);
	}
}

// RW 0x8C651D (CollideModule / BehaviorModule xfer)
void CivilianSpawnCollide::xfer(Xfer &x)
{
	xferBehaviorModule(x);
}

// primary vtable slot 1 -> crc
void CivilianSpawnCollide::crc(StateHasher &hasher) const
{
	BehaviorModule::crc(hasher);
}

// RW 0x65B091 / DONOR BFME2 0x659C97
void CivilianSpawnCollide::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<CivilianSpawnCollideModuleData>("CivilianSpawnCollide", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(
		"CivilianSpawnCollide",
		MODULETYPE_BEHAVIOR,
		[](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
			const auto *typed = dynamic_cast<const CivilianSpawnCollideModuleData *>(data);
			if (!typed)
			{
				throw std::logic_error("CivilianSpawnCollide: the module data is not typed");
			}
			return std::make_unique<CivilianSpawnCollide>(thing, typed);
		});
}

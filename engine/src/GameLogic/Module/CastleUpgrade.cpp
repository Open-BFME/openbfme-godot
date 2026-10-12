// OpenBFME. GPL-3.0.
//
// CastleUpgrade. See GameLogic/Module/CastleUpgrade.h for the target facts. Lane FACT-B.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/CastleUpgrade.h"

#include "Common/INI.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Upgrade.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectTemplateInfo.h"

#include <cstddef>
#include <memory>
#include <stdexcept>

namespace
{
#define OFF(m) (int)offsetof(CastleUpgradeModuleData, m)
const FieldParse kCastleUpgradeFields[] = { // RW 0xC6F524
	{ "Upgrade", INI::parseAsciiString, nullptr, OFF(m_upgradeName) },
	{ "WallUpgradeRadius", INI::parseReal, nullptr, OFF(m_wallUpgradeRadius) },
	{ nullptr, nullptr, nullptr, 0 },
};
#undef OFF

CastleMemberBehavior *memberOf(Object &o)
{
	for (const std::unique_ptr<BehaviorModule> &m : o.modules())
	{
		if (CastleMemberBehavior *c = dynamic_cast<CastleMemberBehavior *>(m.get()))
		{
			return c;
		}
	}
	return nullptr;
}

CastleBehavior *castleOf(Object &o)
{
	for (const std::unique_ptr<BehaviorModule> &m : o.modules())
	{
		if (CastleBehavior *c = dynamic_cast<CastleBehavior *>(m.get()))
		{
			return c;
		}
	}
	return nullptr;
}

// RW 0x7972D3 (BFME2): each foundation gets the upgrade; the occupant of a taken one, when it lacks it and is affected, too
void giveToFoundations(GameLogic &logic, const std::vector<ObjectID> &ids, const UpgradeTemplate *u)
{
	for (ObjectID id : ids)
	{
		Object *f = logic.findObjectByID(id);
		if (!f)
		{
			continue;
		}
		f->giveUpgradeSelf(u);
		CastleMemberBehavior *plot = memberOf(*f);
		if (!plot || !plot->plotIsTaken())
		{
			continue;
		}
		Object *occupant = logic.findObjectByID(plot->occupantId());
		if (occupant && !occupant->hasUpgrade(u) && occupant->affectedByUpgrade(u))
		{
			occupant->giveUpgradeSelf(u);
		}
	}
}
} // namespace

void CastleUpgradeModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	buildBaseFieldParse(p);
	p.add(kCastleUpgradeFields);
}

void CastleUpgrade::upgradeImplementation()
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	const UpgradeTemplate *u = TheUpgradeCenter ? TheUpgradeCenter->findUpgrade(m_data->m_upgradeName) : nullptr; // RW 0x8BAB1E (RW 0x66F5E5)
	if (!u)
	{
		// retail hands a null template on (its callees then do nothing with it); the name is the data's, reported once by the INI checks
		return;
	}
	if (CastleMemberBehavior *member = memberOf(*obj))
	{
		if (Object *castle = logic.findObjectByID(member->castleId())) // RW 0x8BAB29 .. 0x8BAB3B
		{
			if (CastleBehavior *cb = castleOf(*castle)) // RW 0x8BAB3D .. 0x8BAB4D -> RW 0x79986B
			{
				if (Object *keep = logic.findObjectByID(cb->keepId()))
				{
					if (!keep->hasUpgrade(u))
					{
						keep->giveUpgradeSelf(u);
					}
				}
				giveToFoundations(logic, cb->foundations(), u);
				giveToFoundations(logic, cb->defenseFoundations(), u);
				for (ObjectID id : cb->members())
				{
					Object *o = logic.findObjectByID(id);
					if (o && !o->hasUpgrade(u) && o->affectedByUpgrade(u))
					{
						o->giveUpgradeSelf(u);
					}
				}
			}
			castle->giveUpgradeSelf(u); // RW 0x8BAB55
		}
	}
	giveToWallsInRange(u); // RW 0x8BAB5E
}

void CastleUpgrade::giveToWallsInRange(const UpgradeTemplate *u)
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	static const int walls[] = { ObjectTemplateInfoBuilder::kindOfIndex("WALL_SEGMENT"), ObjectTemplateInfoBuilder::kindOfIndex("WALL_HUB"),
		ObjectTemplateInfoBuilder::kindOfIndex("WALL_UPGRADE"), ObjectTemplateInfoBuilder::kindOfIndex("DEFENSIVE_WALL") };
	PartitionFilterFn anyWall([&](Object &o) {
		for (int bit : walls)
		{
			if (bit >= 0 && o.isKindOf((unsigned)bit))
			{
				return true;
			}
		}
		return false;
	});
	for (const PartitionHit &hit : logic.partition().iterateObjectsInRange(*obj->getPosition(), m_data->m_wallUpgradeRadius, FROM_CENTER_3D, { &anyWall }, ITER_FASTEST))
	{
		if (hit.object->getControllingPlayer() == obj->getControllingPlayer()) // RW 0x8BAA77 .. 0x8BAA89
		{
			hit.object->giveUpgrade(u); // RW 0x69388B
		}
	}
}

void CastleUpgrade::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<CastleUpgradeModuleData>("CastleUpgrade", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("CastleUpgrade", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const CastleUpgradeModuleData *typed = dynamic_cast<const CastleUpgradeModuleData *>(data);
		if (!typed)
		{
			throw std::logic_error("CastleUpgrade: the module data is not typed");
		}
		return std::make_unique<CastleUpgrade>(thing, typed);
	});
}

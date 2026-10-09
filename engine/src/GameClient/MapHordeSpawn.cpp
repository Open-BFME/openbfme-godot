// OpenBFME. GPL-3.0.
// See GameClient/MapHordeSpawn.h.

#include "GameClient/MapHordeSpawn.h"

#include "Common/AsciiString.h"
#include "Common/RandomValue.h"
#include "GameLogic/Object/Contain/HordeContainBehaviorData.h"
#include "GameLogic/Object/Contain/HordeContainCore.h"

#include <variant>

namespace
{
bool equivalentToContains(const ThingTemplate *t, const std::string &name)
{
	if (!t)
	{
		return false;
	}
	const FieldValue *v = t->findField("EquivalentTo");
	if (!v)
	{
		return false;
	}
	if (const std::vector<std::string> *list = std::get_if<std::vector<std::string>>(v))
	{
		for (const std::string &e : *list)
		{
			if (AsciiStringUtil::compareNoCase(e, name) == 0)
			{
				return true;
			}
		}
	}
	return false;
}
} // namespace

void MapHordeSpawn::bindHordeContainData(ModuleFactory &modules)
{
	modules.bindTypedData<HordeContainBehaviorData>("HordeContain", MODULETYPE_BEHAVIOR);
	modules.bindTypedData<HordeContainBehaviorData>("HorseHordeContain", MODULETYPE_BEHAVIOR);
}

bool MapHordeSpawn::unitTypeMatches(const ThingFactory &things, const std::string &unitType, const std::string &memberName)
{
	if (unitType == memberName)
	{
		return true;
	}
	const ThingTemplate *slotT = things.findTemplate(unitType);
	const ThingTemplate *memberT = things.findTemplate(memberName);
	if (!slotT || !memberT)
	{
		return false;
	}
	if (slotT->getFinalOverride() == memberT->getFinalOverride())
	{
		return true;
	}
	if (equivalentToContains(memberT, unitType) || equivalentToContains(slotT, memberName))
	{
		return true;
	}
	for (const std::string &r : memberT->reskinnedFrom())
	{
		if (r == unitType)
		{
			return true;
		}
	}
	for (const std::string &r : slotT->reskinnedFrom())
	{
		if (r == memberName)
		{
			return true;
		}
	}
	return false;
}

void MapHordeSpawn::spawn(const ThingTemplate &tmpl, const ThingFactory &things, const Coord3D &position, float angle, HordeSpawnResult &result)
{
	result = HordeSpawnResult();
	const HordeContainBehaviorData *data = nullptr;
	std::string className;
	for (const ThingTemplate::Nugget &n : tmpl.behaviorModules().nuggets())
	{
		if (n.name == "HordeContain" || n.name == "HorseHordeContain")
		{
			result.isHorde = true;
			className = n.name;
			data = dynamic_cast<const HordeContainBehaviorData *>(n.data.get());
			if (!data)
			{
				result.errors.push_back(tmpl.getName() + ": " + n.name + " module data is not typed");
				return;
			}
			break;
		}
	}
	if (!result.isHorde)
	{
		return;
	}
	const HordeContainModuleData &hd = data->horde;
	// the logic RNG of retail's map start is not reproduced; a fixed-seed generator of the donor algorithm (S-080 / S-118)
	GameLogicRandom rng(RandomAlgorithm::ZH_CarryChain);
	rng.seedRandom(1);
	HordeContainCore core(hd, rng, [&things](const std::string &slotUnitType, const std::string &memberName) {
		return MapHordeSpawn::unitTypeMatches(things, slotUnitType, memberName);
	});
	core.buildSlots(true);
	result.slots = core.slots().size();
	if (hd.m_randomOffset.x > 0.0f || hd.m_randomOffset.y > 0.0f)
	{
		result.randomOffsetSlots = core.slots().size();
	}
	HordeContainCore::Placement owner;
	owner.position = position;
	owner.angle = angle;
	HordeContainCore::ObjectId nextId = 1;
	for (const InitialPayloadEntry &e : hd.m_transport.m_initialPayload)
	{
		for (int i = 0; i < e.count; ++i)
		{
			++result.payload;
			const HordeContainCore::ObjectId id = nextId++;
			const int slot = core.addMember(id, e.name);
			if (slot < 0)
			{
				++result.unplaced;
				continue;
			}
			float slotAngle = 0.0f;
			HordeMemberSpawn m;
			m.templateName = e.name;
			m.position = core.getSlotWorldPos(id, owner, &slotAngle);
			m.angle = angle;
			m.slot = slot;
			result.members.push_back(m);
		}
	}
	result.unverified = core.unverified();
	for (const std::string &u : HordeContainModuleData::unverified())
	{
		result.unverified.push_back(u);
	}
}

// OpenBFME. GPL-3.0.
// See GameLogic/MapObjectLoop.h for the sources.

#include "GameLogic/MapObjectLoop.h"

#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Module/AIUpdate.h"

#include "Common/Team.h"
#include "Common/Thing/ThingFactory.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/System/InvisibilityManager.h"
#include "Common/Upgrade.h"

namespace
{
// the property keys of RW 0x695A06 whose effect is not applied here (the list of GameClient/MapObjectDrawables.cpp, S-110)
const char *const kUnportedKeys[] = { "objectMaxHPs", "objectVeterancy", "objectExperienceLevel", "objectInitialStance", "objectBasePhase" };

void applyContained(Object &obj, const MapObjectDrawable &d, const MapObjectLoop::AfterCreate &after, size_t &contained, size_t &hordes)
{
	ContainModuleInterface *c = obj.getContain();
	if (!c || !c->getContainedItemsList() || c->getContainedItemsList()->empty())
	{
		return;
	}
	++hordes;
	for (Object *m : *c->getContainedItemsList())
	{
		++contained;
		if (after)
		{
			after(*m, d);
		}
	}
}
} // namespace

MapObjectLoopResult MapObjectLoop::create(GameLogic &logic, const LoadedMap &map, const MapObjectDrawables &classified, const AfterCreate &afterCreate)
{
	MapObjectLoopResult result;
	result.stops = stopLines();
	ThingFactory &things = logic.things();
	PlayerList &players = logic.players();
	const size_t liveBefore = logic.getObjectCount();

	auto isBridgePass = [](const MapObjectDrawable &d) { return d.info && (d.info->isBridge || d.info->walkOnTopOfWall); };

	for (int pass = 0; pass < 2; ++pass)
	{
		for (const MapObjectDrawable &d : classified.drawables)
		{
			if (d.hordeMember || (d.fate != MAPOBJ_OBJECT && d.fate != MAPOBJ_OBJECT_BRIDGE) || !d.info)
			{
				continue;
			}
			if (isBridgePass(d) != (pass == 0))
			{
				continue;
			}
			const MapObject &o = map.chunks.objects.at(d.objectIndex);
			const ThingTemplate *tt = things.findTemplate(d.info->name); // the master template, as the retail loop finds it by name (RW 0x6D12E5)
			if (!tt)
			{
				result.errors.push_back("object " + std::to_string(d.objectIndex) + ": template '" + d.info->name + "' vanished");
				continue;
			}
			// the owner (RW 0x6A8F42)
			Team *team = players.teams().findTeam(d.owner);
			if (!team)
			{
				team = players.getNeutralPlayer()->getDefaultTeam();
				++result.ownerFallbacks["owner '" + d.owner + "' names no team: the neutral default team"];
			}
			Object *obj = logic.newObject(tt, team, ObjectStatusMaskType{});
			if (!obj)
			{
				result.errors.push_back("object " + std::to_string(d.objectIndex) + ": newObject made nothing");
				continue;
			}
			// the transform: orientation (or the terrain-aligned basis), then position (RW 0x62E0AF .. 0x62E13F)
			if (d.alignToTerrain)
			{
				obj->setTransform(&d.position, d.basis);
			}
			else
			{
				obj->setOrientation(d.angle);
				obj->setPosition(&d.position);
			}
			obj->friend_onBuildComplete(); // RW 0x62E176: a map object is complete at birth (the claim of a resource building, ...)
			team->setActive();             // lane CAMP-1H: RW 0x62E17B .. 0x62E185: its team is activated (its OnCreate script runs at the next team update)
			// updateObjValuesFromMapProperties (RW 0x695A06): the properties this layer applies
			bool exists = false;
			const std::string objName = o.m_properties.getAsciiString("objectName", &exists);
			if (exists)
			{
				obj->setName(objName);
			}
			const int health = o.m_properties.getInt("objectInitialHealth", &exists);
			if (exists)
			{
				if (BodyModuleInterface *b = obj->getBodyModule())
				{
					b->setInitialHealth(health);
					++result.initialHealthApplied;
				}
				else if (health != 100)
				{
					++result.unportedKeys["objectInitialHealth (the object has no body module)"];
				}
			}
			// lane SCRIPT-2: RW 0x695C43 .. 0x695D19, each when the dict has the key: objectEnabled -> script status 1 = !value, objectPowered -> 2 = !value
			// (Object::setScriptStatus, RW 0x69317D), objectIndestructible -> the body's setIndestructible (vslot 0x88), objectUnsellable -> 4 = value,
			// objectTargetable -> 0x10 = value (then the drawable's vslot 0x34: client)
			{
				const struct
				{
					const char *key;
					std::uint8_t bit;
					bool inverted;
				} statusKeys[] = { { "objectEnabled", 1, true }, { "objectPowered", 2, true } };
				for (const auto &k : statusKeys)
				{
					const bool v = o.m_properties.getBool(k.key, &exists);
					if (exists)
					{
						obj->setScriptStatus(k.bit, k.inverted ? !v : v);
					}
				}
			}
			// lane SCRIPT-3: RW 0x695B35 .. 0x695B5C: objectAggressiveness -> the AI's attitude (RW 0x66E12A)
			const int aggressiveness = o.m_properties.getInt("objectAggressiveness", &exists);
			if (exists && obj->getAIUpdateInterface())
			{
				obj->getAIUpdateInterface()->setAttitude(aggressiveness);
			}
			const bool indestructible = o.m_properties.getBool("objectIndestructible", &exists);
			if (exists)
			{
				if (BodyModuleInterface *b = obj->getBodyModule())
				{
					b->setIndestructible(indestructible);
					result.indestructibleApplied += indestructible ? 1 : 0;
				}
				// RW 0x86A104 / 0x86B169 (the contain's payload creation): a member made for an indestructible container is made indestructible before it
				// enters. Retail creates a horde's payload after these properties (its first update); this port makes it with the object, so the members
				// the payload already holds take the flag here (the same end state)
				ContainModuleInterface *c = obj->getContain();
				if (indestructible && c && c->getContainedItemsList())
				{
					for (Object *m : *c->getContainedItemsList())
					{
						if (m && m->getBodyModule())
						{
							m->getBodyModule()->setIndestructible(true);
						}
					}
				}
			}
			{
				const struct
				{
					const char *key;
					std::uint8_t bit;
				} statusKeys[] = { { "objectUnsellable", 4 }, { "objectTargetable", 0x10 } };
				for (const auto &k : statusKeys)
				{
					const bool v = o.m_properties.getBool(k.key, &exists);
					if (exists)
					{
						obj->setScriptStatus(k.bit, v);
					}
				}
			}
			for (const char *key : kUnportedKeys)
			{
				if (o.m_properties.known(key))
				{
					++result.unportedKeys[key];
				}
			}
			// lane UPGRADE-1. RW 0x695D87 .. 0x695E41: objectGrantUpgrade0, 1, ... ("%s%d", RW 0xC1222C) while the value is not empty; every name that is an upgrade
			// is given (Object::giveUpgrade, RW 0x69388B)
			for (int i = 0;; ++i)
			{
				const std::string name = o.m_properties.getAsciiString("objectGrantUpgrade" + std::to_string(i), &exists);
				if (name.empty())
				{
					break;
				}
				if (const UpgradeTemplate *u = TheUpgradeCenter ? TheUpgradeCenter->findUpgrade(name) : nullptr)
				{
					obj->giveUpgrade(u);
					++result.upgradesGranted;
				}
			}
			// RW 0x6946DA -> 0x693A9A: objectUpgradesList, whitespace separated names; an upgrade of a non-HORDE object is given (RW 0x69388B); a HORDE keeps it in
			// its contain (slots 0xB4 / 0xB8 of RW 0x68C866's interface), which is not ported (S-483): counted
			const std::string list = o.m_properties.getAsciiString("objectUpgradesList", &exists);
			if (exists)
			{
				static const int kHorde = ObjectTemplateInfoBuilder::kindOfIndex("HORDE"); // RW 0x693AC6: tt + 0x115 & 0x20
				size_t pos = 0;
				while (pos < list.size())
				{
					const size_t b = list.find_first_not_of(" \t\r\n", pos);
					if (b == std::string::npos)
					{
						break;
					}
					size_t e = list.find_first_of(" \t\r\n", b);
					if (e == std::string::npos)
					{
						e = list.size();
					}
					pos = e;
					const UpgradeTemplate *u = TheUpgradeCenter ? TheUpgradeCenter->findUpgrade(list.substr(b, e - b)) : nullptr;
					if (!u)
					{
						continue;
					}
					if (kHorde >= 0 && obj->isKindOf((unsigned)kHorde))
					{
						++result.unportedKeys["objectUpgradesList on a HORDE (its contain keeps the upgrade, S-483)"];
					}
					else
					{
						obj->giveUpgrade(u);
						++result.upgradesGranted;
					}
				}
			}
			if (AIWorld *ai = logic.aiWorld())
			{
				ai->addObjectToPathfindMap(*obj); // RW 0x62E192 -> 0x6E85E9: the footprint of a placed structure enters the pathfinder's map at once (lane BUILD-1)
			}
			++result.created;
			result.bridgePass += pass == 0 ? 1 : 0;
			++result.byTemplate[d.info->name];
			result.ids.push_back(obj->getID());
			result.objectIndices.push_back(d.objectIndex);
			if (afterCreate)
			{
				afterCreate(*obj, d);
			}
			applyContained(*obj, d, afterCreate, result.contained, result.hordes);
		}
	}
	// lane STEALTH-1: the terrain tree list (RW TheTerrainLogic + 0x578): the loop's KindOf TREE map objects that go to the tree buffer (RW 0x62E44F -> 0x683D89, the
	// record made when the template has a tree draw module, RW 0x683DC2 .. 0x683E63) at their placed position; INFERENCE (S-1040): the list holds the positions only
	logic.invisibility().clearTrees();
	for (const MapObjectDrawable &d : classified.drawables)
	{
		if (d.fate != MAPOBJ_CLIENT_TREE || !d.info)
		{
			continue;
		}
		for (const MapDrawModule &m : d.info->draws)
		{
			if (m.kind == W3D_DRAWKIND_TREE)
			{
				logic.invisibility().addTree(d.position);
				break;
			}
		}
	}
	(void)liveBefore;
	return result;
}

std::vector<std::string> MapObjectLoop::stopLines()
{
	return {
		"[S-150] live map objects: the order (IsBridge / WALK_ON_TOP_OF_WALL objects first, then the list order), the team lookup without the skirmish name remapping of RW 0x6A9031 (an owner that names no team gets the neutral default team), the application of objectInitialHealth through the body, and the unported property keys are inference or not ported (UPGRADE-1 applies objectGrantUpgrade<n> and objectUpgradesList, RW 0x695D87 / 0x693A9A; a HORDE's list stays with its contain in RW and is counted here, S-483); horde members are made by a live HordeContain (S-149)",
	};
}

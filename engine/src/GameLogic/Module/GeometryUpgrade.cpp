// OpenBFME. GPL-3.0.
// See GameLogic/Module/GeometryUpgrade.h for the target facts and the addresses.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/GeometryUpgrade.h"

#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Xfer.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameLogic/SnapshotXfer.h"

#include <cstddef>
#include <memory>
#include <stdexcept>

const char *const GeometryUpgrade::kStopDrawMeshes =
	"[S-1423] GeometryUpgrade (RW 0x8BADC0 / 0x8BAC96): the draw modules' WallBoundsMesh / RampMesh1 / RampMesh2 change (vslots 0xD4 / 0xD8) is not applied: the "
	"pathfinder's wall tops and ramps stay the template's draw module meshes (lane WALL-1)";
const char *const GeometryUpgrade::kStopWallUpgrade =
	"[S-1423] GeometryUpgrade (RW 0x8BAFF5): the object's WallUpgradeUpdate (not ported) does not run RW 0x8AEC79 after the geometry change";

namespace
{
const FieldParse kFields[] = {
	{ "ShowGeometry", INI::parseAsciiStringVectorAppend, nullptr, (int)offsetof(GeometryUpgradeModuleData, m_showGeometry) }, // + 0x138, RW 0x42E59E
	{ "HideGeometry", INI::parseAsciiStringVectorAppend, nullptr, (int)offsetof(GeometryUpgradeModuleData, m_hideGeometry) }, // + 0x144
	{ "WallBoundsMesh", INI::parseAsciiString, nullptr, (int)offsetof(GeometryUpgradeModuleData, m_wallBoundsMesh) },         // + 0x150, RW 0x42EE5E
	{ "RampMesh1", INI::parseAsciiString, nullptr, (int)offsetof(GeometryUpgradeModuleData, m_rampMesh1) },                   // + 0x154
	{ "RampMesh2", INI::parseAsciiString, nullptr, (int)offsetof(GeometryUpgradeModuleData, m_rampMesh2) },                   // + 0x158
	{ nullptr, nullptr, nullptr, 0 }
};
} // namespace

// RW 0x8BABA9
void GeometryUpgradeModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	buildBaseFieldParse(p); // RW 0x8D26E0: RW 0xC76AD8 at offset 8
	p.add(kFields);         // RW 0xC6F578
}

void GeometryUpgrade::registerModule(ModuleFactory &modules)
{
	static const char kName[] = "GeometryUpgrade";
	modules.bindTypedData<GeometryUpgradeModuleData>(kName, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(kName, MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const GeometryUpgradeModuleData *typed = dynamic_cast<const GeometryUpgradeModuleData *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(kName) + ": the module data is not typed");
		}
		return std::make_unique<GeometryUpgrade>(thing, typed); // RW 0x6503A4 -> constructor RW 0x8BABCB
	});
}

// RW 0x8BADC0
void GeometryUpgrade::upgradeImplementation()
{
	Object *obj = getObject();
	// RW 0x8BADC0 .. 0x8BAE2C: the masks (slot 11); a conflicting bit among the object's upgrades (+ 0x28C) or its controlling player's (+ 0x14C) stops it
	if (obj->getUpgradeMask().testForAny(m_data->m_conflictingMask)) // RW 0x8097D6
	{
		return;
	}
	const Player *player = obj->getControllingPlayer(); // RW 0x68B678
	if (!player)
	{
		throw std::logic_error("GeometryUpgrade: object " + std::to_string(obj->getID()) + " has no controlling player (RW 0x8BAE1F reads its upgrades)");
	}
	if (player->getCompletedUpgradeMask().testForAny(m_data->m_conflictingMask))
	{
		return;
	}
	setCustomAnim(true); // RW 0x8D28F1
	const bool meshes = !m_data->m_wallBoundsMesh.empty() || !m_data->m_rampMesh1.empty() || !m_data->m_rampMesh2.empty();
	if (meshes)
	{
		obj->logic().noteStop(kStopDrawMeshes); // RW 0x8BAEFB .. 0x8BAF4C: the draw modules' vslots 0xD4 / 0xD8 (client)
	}
	else if (m_data->m_hideGeometry.empty() && m_data->m_showGeometry.empty())
	{
		return; // RW 0x8BAF60 .. 0x8BAF7E
	}
	// RW 0x8BAF89 .. 0x8BAFC5: the name of the last active shape of the object's own GeometryInfo (Object + 0xA8)
	for (const ObjectGeometry::Shape &s : obj->geometryShapes())
	{
		if (s.active)
		{
			m_upgradeName = s.name;
		}
	}
	for (const std::string &n : m_data->m_hideGeometry)
	{
		obj->setGeometryActive(n, false); // RW 0x8BAC71 -> 0xAD3520(name, 0)
	}
	for (const std::string &n : m_data->m_showGeometry)
	{
		obj->setGeometryActive(n, true); // RW 0x8BAC71 -> 0xAD3520(name, 1)
	}
	obj->geometryChanged(true); // RW 0x68B244(1)
	if (obj->findModule("WallUpgradeUpdate"))
	{
		obj->logic().noteStop(kStopWallUpgrade); // RW 0x8AE8F0 -> 0x8AEC79
	}
}

// RW 0x8BAC96
void GeometryUpgrade::processUpgradeRemoval()
{
	if (!isAlreadyUpgraded()) // slot 0
	{
		return;
	}
	setCustomAnim(false); // RW 0x8D28F9
	Object *obj = getObject();
	AIWorld *ai = obj->logic().aiWorld();
	if (ai)
	{
		ai->removeObjectFromPathfindMap(*obj); // RW 0x6E85FB
	}
	obj->logic().noteStop(kStopDrawMeshes); // RW 0x8BACC0 .. 0x8BACFF: every draw module's meshes cleared (vslots 0xD4(0), 0xD8(0, 0), 0xD8(0, 1))
	for (const std::string &n : m_data->m_hideGeometry)
	{
		obj->setGeometryActive(n, false); // RW 0x8BAD15 (pushed 0)
	}
	for (const std::string &n : m_data->m_showGeometry)
	{
		obj->setGeometryActive(n, false); // RW 0x8BAD2D (pushed 0 as well)
	}
	if (!m_upgradeName.empty())
	{
		obj->setGeometryActive(m_upgradeName, true); // RW 0x8BAD46
	}
	if (ai)
	{
		ai->addObjectToPathfindMap(*obj); // RW 0x6E85E9
	}
	obj->geometryChanged(false); // RW 0x68B244(0)
	setUpgradeExecuted(false);   // slot 9
}

void GeometryUpgrade::crc(StateHasher &hasher) const
{
	UpgradeModule::crc(hasher);
	hasher.addString(m_upgradeName);
}

// RW 0x8BAB72
void GeometryUpgrade::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	x.xferAsciiString(&m_upgradeName); // + 0x1C (Xfer vslot 0x6C)
	UpgradeModule::xfer(x);            // RW 0x8D284A
}

std::vector<std::string> GeometryUpgrade::stopLines()
{
	return {
		"[S-1423] GeometryUpgrade (lane MODCOMPAT-MOD1; create RW 0x6503A4, constructor RW 0x8BABCB, table RW 0xC6F578, implementation RW 0x8BADC0, removal RW "
		"0x8BAC96, xfer RW 0x8BAB72) runs on the object's own GeometryInfo (Object::geometryShapes, RW Object + 0xA8: the pathfinder footprint, the combat "
		"bounds, the partition cells and the overlap tests read it); not ported: the draw modules' WallBoundsMesh / RampMesh1 / RampMesh2 change (vslots 0xD4 / "
		"0xD8: the pathfinder's wall tops stay the template's draw module meshes), the object's WallUpgradeUpdate call RW 0x8AEC79 (WallUpgradeUpdate is not "
		"ported) and, in the geometry refresh RW 0x68B244, the collision manager entry RW 0xB6C700 (S-363 / S-780) and the cells RW 0x68836C / 0x688354 (S-142)",
	};
}

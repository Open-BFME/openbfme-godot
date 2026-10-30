// OpenBFME. GPL-3.0.
// See GameLogic/Module/ReplaceSelfUpgrade.h (lane FACT-A).

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/ReplaceSelfUpgrade.h"
#include "GameLogic/SnapshotXfer.h"

#include "Common/ModEngineFeatures.h"
#include "Common/Player.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "GameLogic/AI/AIPathfindHost.h"
#include "GameLogic/Construction.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Module/NotifyCrushModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/WallSpan.h"

#include <cstddef>
#include <memory>
#include <stdexcept>

namespace
{
#define RS_OFF(m) (int)offsetof(ReplaceSelfUpgradeModuleData, m)
const FieldParse kReplaceSelf[] = { // RW 0xC6F6E8
	{ "ReplaceWith", INI::parseAsciiStringVectorAppend, nullptr, RS_OFF(m_pieces) },
	{ "AndThenAddA", INI::parseAsciiStringVectorAppend, nullptr, RS_OFF(m_pieces) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef RS_OFF

int kindBit(const char *name)
{
	const int bit = ObjectTemplateInfoBuilder::kindOfIndex(name);
	if (bit < 0)
	{
		throw std::logic_error(std::string("ReplaceSelfUpgrade: no KindOf ") + name);
	}
	return bit;
}

bool templateKindOf(const ThingTemplate &tt, int bit)
{
	return MaskTest(ObjectTemplateInfoBuilder::build(*tt.getFinalOverride()).kindOf, (unsigned)bit);
}

// template + 0xB0: the geometry's bounding circle radius (GeometryInfo + 0x10, computed over the active shapes by RW 0xAD2860)
float boundingCircle(const ThingTemplate &tt)
{
	PathfindGeometry g;
	ObjectGeometry::fillPathfindGeometry(*tt.getFinalOverride(), g);
	return g.boundingCircleRadius();
}

// RW 0xAD2380 -> RW 0xAD22F0: a geometry shape's point for a piece at `pos` turned by `angle` (x87 under precision control 24, the fsin / fcos results wide):
//   x = pos.x + (float)(offX * cos - offY * sin)       (the rotated offset stored as a float first: fstp dword, then fld pos.x; fadd dword)
//   y = (float)(offY * cos + offX * sin + pos.y)       (one store at the end)
//   z = pos.z + offZ
// the shape's offset is + 0x10 / + 0x14 / + 0x18. The fsin / fcos values are SimMath's deterministic double sin / cos (S-081: not proven bit equal to x87)
Coord3D shapePoint(const ObjectGeometry::Shape &shape, const Coord3D &pos, float angle)
{
	const double s = SimMath::sind(angle), c = SimMath::cosd(angle);
	const float rx = SimMath::fstpDword(SimMath::pc24SubW(SimMath::pc24MulW((double)shape.offsetX, c), SimMath::pc24MulW((double)shape.offsetY, s)));
	Coord3D out;
	out.x = SimMath::pc24Add(pos.x, rx);
	out.y = SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24AddW(SimMath::pc24MulW((double)shape.offsetY, c), SimMath::pc24MulW((double)shape.offsetX, s)), (double)pos.y));
	out.z = SimMath::pc24Add(pos.z, shape.offsetZ);
	return out;
}

// the object's direction (RW 0x70B9E0: the basis' first column)
Coord3D directionOf(const Object &obj)
{
	const float *b = obj.getBasis();
	return Coord3D{ b[0], b[3], b[6] };
}
} // namespace

void ReplaceSelfUpgradeModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	buildBaseFieldParse(p); // RW 0x8BB152: the UpgradeModule table (RW 0x8D26E0) first
	p.add(kReplaceSelf);
}

bool ReplaceSelfUpgrade::pieces(std::vector<const ThingTemplate *> &out, float &total) const
{
	out.clear();
	total = 0.0f;
	const Object *obj = getObject();
	for (const std::string &name : m_data->m_pieces)
	{
		const ThingTemplate *tt = obj->logic().things().findTemplate(name); // RW 0x6D1305
		if (!tt)
		{
			out.clear();
			return false;
		}
		out.push_back(tt);
		total = SimMath::addf32(WallSpan::halfExtentY(*tt), total); // RW 0x8BB308: addss (half, total)
	}
	return !out.empty();
}

// RW 0x8BB23F
bool ReplaceSelfUpgrade::wouldUpgrade(const UpgradeMaskType &mask) const
{
	if (!UpgradeMux::wouldUpgrade(mask)) // RW 0x8D26F3
	{
		return false;
	}
	const Object *obj = getObject();
	static const int underConstruction = ObjectTemplateInfoBuilder::objectStatusIndex("UNDER_CONSTRUCTION"), repair = ObjectTemplateInfoBuilder::objectStatusIndex("UNDERGOING_REPAIR");
	if (obj->testStatus((unsigned)underConstruction) && !obj->testStatus((unsigned)repair)) // RW 0x8BB266 .. 0x8BB27E
	{
		return false;
	}
	std::vector<const ThingTemplate *> list;
	float total = 0.0f;
	if (!pieces(list, total))
	{
		return false;
	}
	GameLogic &logic = obj->logic();
	const Coord3D pos = *obj->getPosition();
	const float angle = obj->getOrientation();
	const Coord3D d = directionOf(*obj);
	const float across = SimMath::subf32(0.0f, d.y); // RW 0x8BB341
	// the line's start: the position + across * total (RW 0x8BB345 .. 0x8BB397)
	Coord3D cur{ SimMath::addf32(pos.x, SimMath::mulf32(across, total)), SimMath::addf32(pos.y, SimMath::mulf32(d.x, total)), SimMath::addf32(pos.z, SimMath::mulf32(d.z, total)) };
	static const int wallUpgrade = kindBit("WALL_UPGRADE"), wallHub = kindBit("WALL_HUB"), keepKind = kindBit("CASTLE_KEEP");
	for (const ThingTemplate *tt : list)
	{
		const float step = SimMath::subf32(0.0f, WallSpan::halfExtentY(*tt)); // RW 0x8BB3F1
		Coord3D delta{ SimMath::mulf32(step, across), SimMath::mulf32(step, d.x), SimMath::mulf32(d.z, step) };
		cur.x = SimMath::addf32(cur.x, delta.x);
		cur.y = SimMath::addf32(cur.y, delta.y);
		cur.z = SimMath::addf32(delta.z, cur.z);
		// the walls in the way (RW 0x8BB454 .. 0x8BB503)
		const std::vector<ObjectGeometry::Shape> shapes = ObjectGeometry::shapesOf(*tt->getFinalOverride());
		PartitionFilterFn inTheWay([&](Object &o) {
			if (!o.isKindOf((unsigned)wallUpgrade) && !o.isKindOf((unsigned)wallHub))
			{
				return false;
			}
			const ThingTemplate *ot = static_cast<const ThingTemplate *>(o.getTemplate())->getFinalOverride();
			return NotifyCrushModules::geometriesOverlap(shapes, pos, angle, ObjectGeometry::shapesOf(*ot), *o.getPosition(), o.getOrientation());
		});
		const float radius = SimMath::mulf32(boundingCircle(*tt), 3.0f); // RW 0xBDD42C
		for (const PartitionHit &h : logic.partition().iterateObjectsInRange(pos, radius, FROM_BOUNDINGSPHERE_3D, { &inTheWay }, ITER_FASTEST))
		{
			if (!templateKindOf(*tt, keepKind) || (h.object && h.object->isKindOf((unsigned)keepKind))) // RW 0x8BB4D9 .. 0x8BB4F3
			{
				return false;
			}
		}
		// the ground under every shape (RW 0x8BB505 .. 0x8BB57E): the shape's point (RW 0xAD2380 -> 0xAD22F0) is written into the step's three slots
		for (const ObjectGeometry::Shape &shape : shapes)
		{
			delta = shapePoint(shape, cur, angle);
			const float ground = logic.getGroundHeight(delta.x, delta.y); // TheTerrainLogic vslot 0x18
			// x87: the height in ST0 minus the position's z (fsub dword, PC24), widened to a double for the CRT fabs (RW 0xA3CF8A), compared with the float
			if (SimMath::absD((double)SimMath::pc24Sub(ground, pos.z)) > (double)logic.settings().allowedHeightVariationForBuilding)
			{
				return false;
			}
		}
		// RW 0x8BB580: the step again (its three slots hold the last shape's point when the piece has shapes: kept as the binary has it)
		cur.x = SimMath::addf32(cur.x, delta.x);
		cur.y = SimMath::addf32(cur.y, delta.y);
		cur.z = SimMath::addf32(delta.z, cur.z);
	}
	return true;
}

// RW 0x8BB5FA
void ReplaceSelfUpgrade::upgradeImplementation()
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	if (m_data->m_pieces.empty())
	{
		return;
	}
	const Coord3D pos = *obj->getPosition();
	const float angle = obj->getOrientation();
	const std::string name = obj->getName(); // + 0x88
	Player *owner = obj->getControllingPlayer();
	const Coord3D d = directionOf(*obj);
	// lane MODS-3: edain-unchained.foundation-rebind (its 0xEDA020 before RW 0x62BBAB): the plot or castle whose occupant this object is keeps it for the
	// first piece; the object's producer link goes before the destroy
	const ObjectID replaced = obj->getID();
	Object *plot = nullptr;
	CastleMemberBehavior *plotMember = nullptr;
	CastleBehavior *plotCastle = nullptr;
	if (ModEngineFeatures::has(ModEngineFeature::EdainUnchainedFoundationRebind))
	{
		if (Object *p = logic.findObjectByID(obj->getProducerID()))
		{
			CastleMemberBehavior *m = dynamic_cast<CastleMemberBehavior *>(p->findModule("CastleMemberBehavior"));
			CastleBehavior *c = dynamic_cast<CastleBehavior *>(p->findModule("CastleBehavior"));
			if ((m && m->occupantId() == replaced) || (c && c->keepId() == replaced))
			{
				plot = p;
				plotMember = m;
				plotCastle = c;
				obj->setProducer(nullptr);
			}
		}
	}
	logic.destroyObject(obj); // RW 0x68C18F, 0x68C6EE, the drawable, RW 0x6E85FB, RW 0x62BBAB
	std::vector<const ThingTemplate *> list;
	float total = 0.0f;
	if (!pieces(list, total) || !owner)
	{
		return; // RW 0x8BB6F1: an unknown piece ends it (after the destroy)
	}
	const float across = SimMath::subf32(0.0f, d.y);
	Coord3D cur{ SimMath::addf32(pos.x, SimMath::mulf32(across, total)), SimMath::addf32(pos.y, SimMath::mulf32(d.x, total)), SimMath::addf32(pos.z, SimMath::mulf32(d.z, total)) };
	static const int wallSegment = kindBit("WALL_SEGMENT");
	const UpgradeTemplate *trigger = nullptr; // RW 0x66F468: the first upgrade of the TriggeredBy mask
	for (unsigned bit = 0; bit < UpgradeMaskType::BITS && !trigger && TheUpgradeCenter; ++bit)
	{
		trigger = muxData()->m_activationMask.test(bit) ? TheUpgradeCenter->findUpgradeByMaskBit((int)bit) : nullptr;
	}
	for (const ThingTemplate *tt : list)
	{
		const float step = SimMath::subf32(0.0f, WallSpan::halfExtentY(*tt)); // RW 0x8BB7B1
		const Coord3D delta{ SimMath::mulf32(step, across), SimMath::mulf32(step, d.x), SimMath::mulf32(d.z, step) };
		cur.x = SimMath::addf32(delta.x, cur.x);
		cur.y = SimMath::addf32(delta.y, cur.y);
		cur.z = SimMath::addf32(delta.z, cur.z);
		// the wall segments the piece covers die (RW 0x8BB7BA .. 0x8BB8AF)
		const std::vector<ObjectGeometry::Shape> shapes = ObjectGeometry::shapesOf(*tt->getFinalOverride());
		PartitionFilterFn covered([&](Object &o) {
			if (!o.isKindOf((unsigned)wallSegment))
			{
				return false;
			}
			const ThingTemplate *ot = static_cast<const ThingTemplate *>(o.getTemplate())->getFinalOverride();
			return NotifyCrushModules::geometriesOverlap(shapes, pos, angle, ObjectGeometry::shapesOf(*ot), *o.getPosition(), o.getOrientation());
		});
		const float radius = SimMath::mulf32(boundingCircle(*tt), 0.5f); // RW 0xBD869C
		for (const PartitionHit &h : logic.partition().iterateObjectsInRange(pos, radius, FROM_CENTER_3D, { &covered }, ITER_FASTEST))
		{
			if (h.object && !h.object->isEffectivelyDead())
			{
				h.object->kill(DAMAGE_UNRESISTABLE, 0); // RW 0x8BB8A0: kill(8, 0)
			}
		}
		Object *piece = Construction::buildObjectNow(logic, obj, *tt, cur, angle, *owner); // RW 0x8BB8D9
		if (!piece)
		{
			break;
		}
		piece->teleportTo(&cur, false); // RW 0x8BB8F1: RW 0x696E63(pos, 0)
		if (!name.empty())
		{
			piece->setName(name); // RW 0x8BB900
		}
		cur.x = SimMath::addf32(delta.x, cur.x);
		cur.y = SimMath::addf32(delta.y, cur.y);
		cur.z = SimMath::addf32(delta.z, cur.z);
		if (trigger)
		{
			piece->setBuildCostPaid((float)trigger->getBuildCost()); // RW 0x8BB948: + 0x33C
		}
		if (plot)
		{
			// lane MODS-3: its 0xEDA097 at RW 0x8BB965, the first piece only: a castle's keep piece takes the keep, else the plot's occupant becomes the
			// piece (when it is empty or still the replaced object); the piece's producer is the plot, and a plot built by the replaced object is built
			// by the piece
			static const int keep = kindBit("CASTLE_KEEP");
			const ObjectID occupant = plotMember ? plotMember->occupantId() : (plotCastle ? plotCastle->keepId() : (ObjectID)INVALID_ID);
			if (occupant == INVALID_ID || occupant == replaced)
			{
				if (plotCastle && piece->isKindOf((unsigned)keep))
				{
					plotCastle->rebindKeep(replaced, *piece);
				}
				else if (plotMember)
				{
					plotMember->setOccupantId(piece->getID());
				}
				piece->setProducer(plot);
				if (plot->getBuilderID() == replaced)
				{
					plot->setBuilder(piece);
				}
			}
			plot = nullptr;
		}
		logic.noteStop(stopLines()[0]); // RW 0x8BB96D .. 0x8BB9DF: the wall links
	}
}

std::vector<std::string> ReplaceSelfUpgrade::stopLines()
{
	return { "[S-1986] ReplaceSelfUpgrade (lane FACT-A, RW 0x8BB5FA): the wall links are not carried over to the new piece (RW 0x8BB96D: the interface of behaviour "
			 "slot 0x80, RW 0x68C3E6, of every CASTLE_KEEP / BRIDGE / DEFENSIVE_WALL object; no ported module answers it)" };
}

void ReplaceSelfUpgrade::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<ReplaceSelfUpgradeModuleData>("ReplaceSelfUpgrade", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("ReplaceSelfUpgrade", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const ReplaceSelfUpgradeModuleData *typed = dynamic_cast<const ReplaceSelfUpgradeModuleData *>(data);
		if (!typed)
		{
			throw std::logic_error("ReplaceSelfUpgrade: the module data is not typed");
		}
		return std::make_unique<ReplaceSelfUpgrade>(thing, typed);
	});
}

// ---- lane SAVE-4b: the snapshots (UpgradeModule's: the classes keep no state of their own) ----

void ReplaceSelfUpgrade::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	UpgradeModule::xfer(x);
}

// OpenBFME. GPL-3.0.
// See GameLogic/Module/PortalModules.h (lane WALL-2).
// BFME1 masks follow Open-BFME-1 DynamicPortalBehaviourRva001F96E0.cpp and Map/Waypoint_method001ABBB0.cpp;
// the all-kind allow default is constructor RVA 0x001FA3A0. See docs/BFME1.md for the compatibility scope.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/PortalModules.h"

#include "Common/Xfer.h"
#include "GameLogic/SnapshotXfer.h"
#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/WeaponDelivery.h"
#include "GameLogic/ConstructionSite.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/ConstructionModules.h"
#include "GameLogic/Module/GateModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameLogic/ObjectFilter.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"

#include <cstddef>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace ModelCondition
{
int indexOf(const std::string &name);
}

namespace
{
// the stops (docs/STOPS.md S-4853 .. S-4855); tests pin the text
const char *const kStopBones =
	"[S-4853] DynamicPortalBehaviour (RW 0x86404D): a WayPoint names a bone the drawable does not answer (RW reads an uninitialised slot of its 16), more than six "
	"WayPoint rows (RW writes past its six slots), more than 16 bones, or a WayPoint type other than Walk / Climb / PreClimb (RW leaves it uninitialised): no "
	"waypoint is made";
const char *const kStopWallBoundsMesh =
	"[S-4854] DynamicPortalBehaviour (RW 0x864357): a WallBoundsMesh is set: the drawable's wall mesh change (RW 0x672F68) and the pathfinder's re-classification "
	"are not ported (no retail module sets it)";
const char *const kStopFoundation =
	"[S-4855] DynamicPortalBehaviour (RW 0x864357 / 0x863D3A): the object's FoundationAIUpdate interface slot 2 (RW 0x8582DE, the castle upgrade's occupant, its "
	"statuses and drawable calls) is not called";

// RW 0x86441E: Index:<int> Type:<Walk | Climb | PreClimb>
void parseWayPoint(INI *ini, void *, void *store, const void *)
{
	DynamicPortalBehaviourModuleData::WayPointEntry e;
	e.index = ini->scanInt(ini->getNextSubToken("Index"));
	const char *type = ini->getNextSubToken("Type");
	if (std::strcmp(type, "Walk") == 0)
	{
		e.type = PWAYPOINT_WALK;
	}
	else if (std::strcmp(type, "Climb") == 0)
	{
		e.type = PWAYPOINT_CLIMB;
	}
	else if (std::strcmp(type, "PreClimb") == 0)
	{
		e.type = PWAYPOINT_PRECLIMB;
	}
	else
	{
		e.type = 0; // RW: the local stays uninitialised (S-4853)
	}
	static_cast<std::vector<DynamicPortalBehaviourModuleData::WayPointEntry> *>(store)->push_back(e);
}

// RW 0x864794: From:<n> [Via:<n>]* To:<n> (the last label is not checked)
void parseLink(INI *ini, void *, void *store, const void *)
{
	std::vector<int> list;
	list.push_back(ini->scanInt(ini->getNextSubToken("From")));
	std::string label = ini->getNextToken(":");
	while (label == "Via")
	{
		list.push_back(ini->scanInt(ini->getNextToken()));
		label = ini->getNextToken(":");
	}
	list.push_back(ini->scanInt(ini->getNextToken(":")));
	static_cast<std::vector<std::vector<int>> *>(store)->push_back(std::move(list));
}

// RW 0x76392F into a fresh filter
void parseFilter(INI *ini, void *instance, void *store, const void *userData)
{
	ObjectFilter f;
	ParseObjectFilter(ini, instance, &f, userData);
	*static_cast<std::shared_ptr<const ObjectFilter> *>(store) = std::make_shared<const ObjectFilter>(std::move(f));
}

ObjectFilter legacyPortalDefaultFilter()
{
	ObjectFilter filter;
	filter.includeKindOf.fill(~std::uint32_t{ 0 });
	filter.rule = ObjectFilter::RULE_ANY;
	return filter;
}

void parseLegacyPortalMask(INI *ini, void *instance, void *store, bool exclude)
{
	auto &filter = *static_cast<std::shared_ptr<const ObjectFilter> *>(store);
	ObjectFilter parsed = filter ? *filter : legacyPortalDefaultFilter();
	ParseKindOfMask(ini, instance, exclude ? &parsed.excludeKindOf : &parsed.includeKindOf, nullptr);
	parsed.rule = ObjectFilter::RULE_ANY;
	filter = std::make_shared<const ObjectFilter>(std::move(parsed));
}

void parseAllowKindOf(INI *ini, void *instance, void *store, const void *)
{
	parseLegacyPortalMask(ini, instance, store, false);
}

void parseRejectKindOf(INI *ini, void *instance, void *store, const void *)
{
	parseLegacyPortalMask(ini, instance, store, true);
}

#define DP_OFF(m) (int)offsetof(DynamicPortalBehaviourModuleData, m)
const FieldParse kDynamicPortal[] = { // RW 0xC59220
	{ "BonePrefix", INI::parseAsciiString, nullptr, DP_OFF(m_bonePrefix) },
	{ "NumberOfBones", INI::parseInt, nullptr, DP_OFF(m_numberOfBones) },
	{ "WayPoint", parseWayPoint, nullptr, DP_OFF(m_wayPoints) },
	{ "Link", parseLink, nullptr, DP_OFF(m_links) },
	{ "WallBoundsMesh", INI::parseAsciiString, nullptr, DP_OFF(m_wallBoundsMesh) },
	{ "GenerateNow", INI::parseBool, nullptr, DP_OFF(m_generateNow) },
	{ "AboveWall", INI::parseUnsignedInt, nullptr, DP_OFF(m_aboveWall) },
	{ "TopAttackPos", INI::parseCoord3D, nullptr, DP_OFF(m_topAttackPos) },
	{ "TopAttackRadius", INI::parseReal, nullptr, DP_OFF(m_topAttackRadius) },
	{ "AllowEnemies", INI::parseBool, nullptr, DP_OFF(m_allowEnemies) },
	{ "ActivationDelaySeconds", INI::parseReal, nullptr, DP_OFF(m_activationDelaySeconds) },
	{ "ObjectFilter", parseFilter, nullptr, DP_OFF(m_objectFilter) },
	{ nullptr, nullptr, nullptr, 0 }
};
const FieldParse kBfme1DynamicPortal[] = {
	{ "AllowKindOf", parseAllowKindOf, nullptr, DP_OFF(m_objectFilter) },
	{ "RejectKindOf", parseRejectKindOf, nullptr, DP_OFF(m_objectFilter) },
	{ nullptr, nullptr, nullptr, 0 }
};
const Bfme1FieldRowsRegistration kBfme1DynamicPortalRegistration(kDynamicPortal, kBfme1DynamicPortal);
#undef DP_OFF

#define FP_OFF(m) (int)offsetof(FakePathfindPortalBehaviourModuleData, m)
const FieldParse kFakePathfind[] = { // RW 0xC592F8
	{ "AllowEnemies", INI::parseBool, nullptr, FP_OFF(m_allowEnemies) },
	{ "AllowNonSkirmishAIUnits", INI::parseBool, nullptr, FP_OFF(m_allowNonSkirmishAIUnits) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef FP_OFF

template <class Runtime, class Data>
void bind(ModuleFactory &modules, const char *name)
{
	modules.bindTypedData<Data>(name, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(name, MODULETYPE_BEHAVIOR, [name](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(name) + ": the module data is not typed");
		}
		return std::make_unique<Runtime>(thing, typed);
	});
}

Pathfinder *pathfinderOf(const Object &obj)
{
	AIWorld *w = obj.logic().aiWorld();
	return w ? &w->pathfinder() : nullptr;
}

// RW 0x70BCE7 (convertBonePosToWorldPos, the translation through the object's transform), SSE in the order of RW 0x87CEB3 (as GarrisonContain::transformPoint)
Coord3D toWorld(const Object &obj, const Coord3D &local)
{
	const float *m = obj.getBasis();
	const Coord3D &t = *obj.getPosition();
	const float tt[3] = { t.x, t.y, t.z };
	float w[3];
	for (int r = 0; r < 3; ++r)
	{
		float v = SimMath::addf32(SimMath::mulf32(m[r * 3 + 2], local.z), SimMath::mulf32(m[r * 3 + 1], local.y));
		v = SimMath::addf32(v, SimMath::mulf32(m[r * 3 + 0], local.x));
		w[r] = SimMath::addf32(v, tt[r]);
	}
	return Coord3D{ w[0], w[1], w[2] };
}

// RW 0xAD2040: the local bounds of the active shapes (min, max), both starting at 0 (x87 compares: a NaN or an equal value takes the shape's)
void localBounds(const std::vector<ObjectGeometry::Shape> &shapes, Coord3D &lo, Coord3D &hi)
{
	lo = Coord3D{ 0.0f, 0.0f, 0.0f };
	hi = Coord3D{ 0.0f, 0.0f, 0.0f };
	auto takeMin = [](float &cur, float v) { cur = cur < v ? cur : v; };
	auto takeMax = [](float &cur, float v) { cur = cur > v ? cur : v; };
	for (const ObjectGeometry::Shape &s : shapes)
	{
		if (!s.active)
		{
			continue;
		}
		switch (s.type)
		{
		case ObjectGeometry::SHAPE_BOX:
			takeMin(lo.x, SimMath::subf32(s.offsetX, s.majorRadius));
			takeMin(lo.y, SimMath::subf32(s.offsetY, s.minorRadius));
			takeMin(lo.z, s.offsetZ);
			takeMax(hi.x, SimMath::addf32(s.majorRadius, s.offsetX));
			takeMax(hi.y, SimMath::addf32(s.offsetY, s.minorRadius));
			takeMax(hi.z, SimMath::addf32(s.height, s.offsetZ));
			break;
		case ObjectGeometry::SHAPE_CYLINDER:
			takeMin(lo.x, SimMath::subf32(s.offsetX, s.majorRadius));
			takeMin(lo.y, SimMath::subf32(s.offsetY, s.majorRadius));
			takeMin(lo.z, s.offsetZ);
			takeMax(hi.x, SimMath::addf32(s.majorRadius, s.offsetX));
			takeMax(hi.y, SimMath::addf32(s.offsetY, s.majorRadius));
			takeMax(hi.z, SimMath::addf32(s.height, s.offsetZ));
			break;
		case ObjectGeometry::SHAPE_SPHERE:
			takeMin(lo.x, SimMath::subf32(s.offsetX, s.majorRadius));
			takeMin(lo.y, SimMath::subf32(s.offsetY, s.majorRadius));
			takeMin(lo.z, SimMath::subf32(s.offsetZ, s.majorRadius));
			takeMax(hi.x, SimMath::addf32(s.majorRadius, s.offsetX));
			takeMax(hi.y, SimMath::addf32(s.offsetY, s.majorRadius));
			takeMax(hi.z, SimMath::addf32(s.offsetZ, s.majorRadius));
			break;
		default:
			break;
		}
	}
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------------------------------------------------
// DynamicPortalBehaviour
// ---------------------------------------------------------------------------------------------------------------------------------------------------------

void DynamicPortalBehaviourModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	buildBaseFieldParse(p); // RW 0x864890: the UpgradeModule table (RW 0x8D26E0) first
	p.add(kDynamicPortal);
}

DynamicPortalBehaviour::DynamicPortalBehaviour(Thing *thing, const DynamicPortalBehaviourModuleData *data)
	: UpgradeModule(thing, data)
	, m_data(data)
{
	m_waypoints.fill(PathfindWaypoint::NO_ID);
}

DynamicPortalBehaviour::~DynamicPortalBehaviour()
{
	// RW 0x863CD5 runs clearPortals; onDelete did it while the world still stood
}

void DynamicPortalBehaviour::onDelete()
{
	clearPortals(); // RW 0x863CD5 (the destructor)
}

DynamicPortalBehaviour *DynamicPortalBehaviour::of(const Object &obj)
{
	return dynamic_cast<DynamicPortalBehaviour *>(obj.findModule("DynamicPortalBehaviour"));
}

Coord3D DynamicPortalBehaviour::topAttackPosition()
{
	// RW 0x863D77 (lane SIEGE-1's reading): the object's planar X axis normalised by its larger component, its length scales TopAttackPos's X and Y; the point
	// through the object's transform (RW 0x70BFD1)
	using SimMath::addf32;
	using SimMath::mulf32;
	const Object *obj = getObject();
	const float *b = obj->getBasis();
	const Coord3D &p = *obj->getPosition();
	const Coord3D &t = m_data->m_topAttackPos;
	const double ax = SimMath::absD((double)b[0]), ay = SimMath::absD((double)b[3]);
	const float m = (float)(ax >= ay ? ax : ay);
	float lx = t.x, ly = t.y;
	if (m != 0.0f)
	{
		const float len = SimMath::length2d(SimMath::divf32(b[0], m), SimMath::divf32(b[3], m));
		lx = mulf32(t.x, len);
		ly = mulf32(t.y, len);
	}
	return Coord3D{ addf32(addf32(addf32(mulf32(b[0], lx), mulf32(b[1], ly)), mulf32(b[2], t.z)), p.x),
		addf32(addf32(addf32(mulf32(b[3], lx), mulf32(b[4], ly)), mulf32(b[5], t.z)), p.y),
		addf32(addf32(addf32(mulf32(b[6], lx), mulf32(b[7], ly)), mulf32(b[8], t.z)), p.z) };
}

int DynamicPortalBehaviour::bones(std::vector<Coord3D> &out) const
{
	// RW 0x68C650(prefix, NumberOfBones, positions[16], null, toWorld, 0): the drawable's bones prefix01 .. (RW 0x672A73 with start index 1), each to the world
	out.clear();
	const Object *obj = getObject();
	ProjectileLaunchOffsets *provider = obj->logic().combat().launchOffsets();
	const int want = m_data->m_numberOfBones;
	if (!provider || want <= 0)
	{
		return 0;
	}
	if (want > 16)
	{
		return -1; // RW's array holds 16 (S-4853)
	}
	float m[16][12];
	const int n = provider->multiLogicalBones(*obj, m_data->m_bonePrefix, obj->getModelConditionBits(), want, m);
	for (int i = 0; i < n; ++i)
	{
		out.push_back(toWorld(*obj, Coord3D{ m[i][3], m[i][7], m[i][11] }));
	}
	return n < 0 ? 0 : n;
}

bool DynamicPortalBehaviour::createWaypoints()
{
	// RW 0x86404D
	Object *obj = getObject();
	Pathfinder *pf = pathfinderOf(*obj);
	if (!pf)
	{
		return false;
	}
	const bool wasCreated = m_created;
	if (!wasCreated)
	{
		const std::vector<DynamicPortalBehaviourModuleData::WayPointEntry> &list = m_data->m_wayPoints;
		std::vector<Coord3D> pos;
		const int n = bones(pos);
		bool ok = list.size() <= (size_t)MAX_WAYPOINTS && n >= 0;
		for (const DynamicPortalBehaviourModuleData::WayPointEntry &e : list)
		{
			ok = ok && e.index >= 0 && e.index < n && e.type != 0;
		}
		if (!ok)
		{
			obj->logic().noteStop(kStopBones);
			return false;
		}
		unsigned activation = 0;
		if (m_data->m_activationDelaySeconds > 0.0f) // RW 0x8640CA: comiss with 0.0 (RW 0xC1B594)
		{
			const std::int64_t frames = SimMath::ftol2(SimMath::pc24MulW((double)LOGICFRAMES_PER_SECOND, (double)m_data->m_activationDelaySeconds)); // RW 0x8640D6
			activation = (unsigned)frames + obj->logic().getFrame();
		}
		for (size_t i = 0; i < list.size(); ++i)
		{
			PathfindWaypoint w;
			w.pos = pos[(size_t)list[i].index];
			w.type = list[i].type;
			w.owner = (PathfindObjectID)obj->getID();
			w.allowEnemies = m_data->m_allowEnemies;
			if (activation > 0) // RW 0x8641FA: jbe (unsigned)
			{
				w.activationFrame = activation;
			}
			// the name "#dynamicportal_wp" (RW 0xC590DC) is not kept; + 0xB8 = 1, + 0xBC = the data's filter: RW 0x7640C1(filter, unit, 0) in Waypoint::isValidUnitType
			w.filter = waypointFilter();
			m_waypoints[i] = pf->createWaypoint(w);
		}
		m_created = true; // RW 0x864224
	}
	for (const std::vector<int> &link : m_data->m_links)
	{
		if (link.size() < 2)
		{
			continue; // the parse makes at least from and to
		}
		auto slot = [this](int i) { return i >= 0 && i < MAX_WAYPOINTS ? m_waypoints[(size_t)i] : (int)PathfindWaypoint::NO_ID; };
		const int first = slot(link.front()), last = slot(link.back());
		if (!wasCreated)
		{
			pf->addWaypointLink(first, last); // RW 0x864274
		}
		pf->addPortal(first); // RW 0x864288
		if (!wasCreated)
		{
			for (size_t i = 0; i + 2 < link.size(); ++i) // RW 0x864293 .. 0x8642CC: n - 2 steps
			{
				pf->setWaypointNext(slot(link[i]), slot(link[i + 1]));
			}
		}
	}
	return true;
}

void DynamicPortalBehaviour::clearPortals()
{
	// RW 0x8639EF
	if (!m_created)
	{
		return;
	}
	Object *obj = getObject();
	Pathfinder *pf = obj ? pathfinderOf(*obj) : nullptr;
	for (int &id : m_waypoints)
	{
		if (id != PathfindWaypoint::NO_ID && pf)
		{
			const PathfindWaypoint *w = pf->findWaypoint(id);
			if (w && Pathfinder::isPortalWaypoint(*w)) // RW 0x6E82FD
			{
				pf->removePortal(id); // RW 0x6EA48A
			}
			pf->destroyWaypoint(id);
		}
		id = PathfindWaypoint::NO_ID;
	}
	if (pf)
	{
		pf->pathsLostPortals(); // RW 0x6E8454
	}
	m_created = false;
}

void DynamicPortalBehaviour::gate()
{
	// RW 0x864314
	if (!m_disabled && (m_data->m_generateNow || m_created))
	{
		createWaypoints();
	}
}

void DynamicPortalBehaviour::onBuildComplete()
{
	// RW 0x8643EC
	m_disabled = false;
	gate();
}

void DynamicPortalBehaviour::onDie(const DieModuleInterface::Event &event)
{
	// RW 0x863B28
	(void)event;
	clearPortals();
	m_disabled = true;
}

void DynamicPortalBehaviour::onMapPlacement(const Dict *properties)
{
	// RW 0x864341: unless a save is being loaded (TheGameLogic + 0x6E). A save here loads into a game started from its START, whose map placement ran this;
	// the snapshot then replaces the portals with the saved ones (the pathfinder's waypoints, Pathfinder::xferWalls)
	(void)properties;
	gate();
}

void DynamicPortalBehaviour::foundationOccupant(bool set)
{
	// RW 0x8643CA .. 0x8643E5 / 0x863D56 .. 0x863D73: with an AI (+ 0x260), the FoundationAIUpdate interface's slot 2 (the object's id, or 0)
	Object *obj = getObject();
	if (!obj->getAIUpdateInterface())
	{
		return;
	}
	for (const std::unique_ptr<BehaviorModule> &m : obj->modules())
	{
		if (m->getFoundationAIUpdate())
		{
			(void)set;
			obj->logic().noteStop(kStopFoundation);
			return;
		}
	}
}

void DynamicPortalBehaviour::upgradeImplementation()
{
	// RW 0x864357
	setCustomAnim(true); // RW 0x8D28F1
	Object *obj = getObject();
	if (!m_data->m_wallBoundsMesh.empty())
	{
		obj->logic().noteStop(kStopWallBoundsMesh); // RW 0x86436B .. 0x8643BE
	}
	createWaypoints(); // RW 0x8643C5
	foundationOccupant(true);
}

void DynamicPortalBehaviour::processUpgradeRemoval()
{
	// RW 0x863D3A: mux slot 9 (not executed: UpgradeMux::removeUpgrade does it after), clearPortals, the custom anim off, the FoundationAIUpdate slot 2 with 0
	clearPortals();
	setCustomAnim(false); // RW 0x8D28F9
	foundationOccupant(false);
}

bool DynamicPortalBehaviour::isEndPortalAboveWall(Coord3D *pos) const
{
	// RW 0x863EB3
	const Object *obj = getObject();
	const int above = m_data->m_aboveWall;
	if (above < 0 || above >= (int)m_data->m_wayPoints.size())
	{
		if (pos)
		{
			*pos = *obj->getPosition();
		}
		return true;
	}
	std::vector<Coord3D> b;
	const int n = bones(b);
	const int index = m_data->m_wayPoints[(size_t)above].index;
	if (index < 0 || index >= n)
	{
		obj->logic().noteStop(kStopBones); // RW reads an uninitialised slot
		return false;
	}
	if (pos)
	{
		*pos = b[(size_t)index];
	}
	const Pathfinder *pf = pathfinderOf(*obj);
	return pf && pf->isPointOnWall(b[(size_t)index], false); // RW 0x6EA857(pos, 0)
}

std::function<bool(const PathfindObject &)> DynamicPortalBehaviour::waypointFilter() const
{
	const ObjectFilter *filter = m_data->m_objectFilter.get();
	GameLogic *logic = &getObject()->logic();
	const bool bfme1 = getObject()->getTemplate()->bfme1Data();
	const int cavalry = ObjectTemplateInfoBuilder::kindOfIndex("CAVALRY");
	const int mounted = ModelCondition::indexOf("MOUNTED");
	return [logic, filter, bfme1, cavalry, mounted](const PathfindObject &o) {
		static const ObjectFilter kDefault; // RW 0x76406F, the data constructor's filter
		static const ObjectFilter kLegacyDefault = legacyPortalDefaultFilter();
		const ObjectFilter &effective = filter ? *filter : (bfme1 ? kLegacyDefault : kDefault);
		const ObjectPathfindAdapter *a = dynamic_cast<const ObjectPathfindAdapter *>(&o);
		if (!a || !ObjectFilterMatch::allows(*logic, effective, a->object(), nullptr))
		{
			return false;
		}
		return !bfme1 || cavalry < 0 || !MaskTest(effective.excludeKindOf, cavalry)
			|| !a->object().testModelCondition(mounted);
	};
}

void DynamicPortalBehaviour::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	UpgradeModule::xfer(x);
	for (int &id : m_waypoints)
	{
		x.xferInt(&id);
	}
	x.xferBool(&m_created);
	x.xferBool(&m_disabled);
}

void DynamicPortalBehaviour::crc(StateHasher &hasher) const
{
	UpgradeModule::crc(hasher);
	if (m_created || m_disabled)
	{
		hasher.addBool(m_created);
		hasher.addBool(m_disabled);
		for (int id : m_waypoints)
		{
			hasher.addI32(id);
		}
	}
}

void DynamicPortalBehaviour::registerClass(ModuleFactory &modules)
{
	bind<DynamicPortalBehaviour, DynamicPortalBehaviourModuleData>(modules, "DynamicPortalBehaviour");
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------
// FakePathfindPortalBehaviour
// ---------------------------------------------------------------------------------------------------------------------------------------------------------

void FakePathfindPortalBehaviourModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	buildBaseFieldParse(p); // RW 0x864A05: the UpgradeModule table first
	p.add(kFakePathfind);
}

FakePathfindPortalBehaviour::FakePathfindPortalBehaviour(Thing *thing, const FakePathfindPortalBehaviourModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	m_waypoints.fill(PathfindWaypoint::NO_ID);
}

FakePathfindPortalBehaviour::~FakePathfindPortalBehaviour()
{
}

void FakePathfindPortalBehaviour::onDelete()
{
	clear(); // the destructor (RW 0x864AD5)
}

int FakePathfindPortalBehaviour::makeWaypoint(const Coord3D &pos)
{
	// RW 0x864B29: type 7, the name "#fakepathfindportal_wp" (RW 0xC5937C, not kept), the owner, AllowEnemies, AllowNonSkirmishAIUnits; no filter, no activation frame
	Object *obj = getObject();
	Pathfinder *pf = pathfinderOf(*obj);
	if (!pf)
	{
		return PathfindWaypoint::NO_ID;
	}
	PathfindWaypoint w;
	w.pos = pos;
	w.type = PWAYPOINT_TYPE7;
	w.owner = (PathfindObjectID)obj->getID();
	w.allowEnemies = m_data->m_allowEnemies;
	w.allowNonSkirmishAI = m_data->m_allowNonSkirmishAIUnits;
	return pf->createWaypoint(w);
}

void FakePathfindPortalBehaviour::create()
{
	// RW 0x864D02
	if (m_created)
	{
		return;
	}
	Object *obj = getObject();
	Pathfinder *pf = pathfinderOf(*obj);
	if (!pf)
	{
		return;
	}
	GameLogic &logic = obj->logic();
	const float angle = obj->getOrientation();
	Coord3D lo, hi;
	localBounds(ConstructionSite::objectShapes(*obj), lo, hi); // RW 0xAD2040 on the object's geometry
	const float minX = lo.x > 0.0f ? lo.x : hi.x;          // RW 0x864D36: comiss with 0.0, else the max x
	const float d = SimMath::addf32(10.0f, minX);           // RW 0x864D51: addss (10.0 RW 0xDAE904, x)
	const float s = SimMath::fstpDword(SimMath::sind(angle)); // RW 0x864D5D (CRT sin)
	const float c = SimMath::fstpDword(SimMath::cosd(angle)); // RW 0x864D6B (CRT cos)
	const float ox = SimMath::subf32(SimMath::mulf32(c, d), SimMath::mulf32(s, 0.0f)); // RW 0x864D88 .. 0x864DA2
	const float oy = SimMath::addf32(SimMath::mulf32(d, s), SimMath::mulf32(c, 0.0f)); // RW 0x864D93 .. 0x864DA6
	const Coord3D &p = *obj->getPosition();
	Coord3D a{ SimMath::addf32(p.x, ox), SimMath::addf32(p.y, oy), 0.0f };
	a.z = logic.getGroundHeight(a.x, a.y); // TheTerrainLogic vslot 0x18
	m_waypoints[0] = makeWaypoint(a);
	const float nx = SimMath::subf32(0.0f, ox), ny = SimMath::subf32(0.0f, oy); // RW 0x864E17 / 0x864E1F
	Coord3D b{ SimMath::addf32(p.x, nx), SimMath::addf32(p.y, ny), 0.0f };
	b.z = logic.getGroundHeight(b.x, b.y);
	m_waypoints[1] = makeWaypoint(b);
	pf->addWaypointLink(m_waypoints[0], m_waypoints[1]); // RW 0x864E7A
	pf->addWaypointLink(m_waypoints[1], m_waypoints[0]); // RW 0x864E85
	doRegister();                                        // RW 0x864E8C
	m_created = true;
}

void FakePathfindPortalBehaviour::doRegister()
{
	// RW 0x86498E
	if (Pathfinder *pf = pathfinderOf(*getObject()))
	{
		pf->addPortal(m_waypoints[0]);
		pf->addPortal(m_waypoints[1]);
	}
	m_registered = true;
}

void FakePathfindPortalBehaviour::unregister()
{
	// RW 0x8649B7
	if (Pathfinder *pf = pathfinderOf(*getObject()))
	{
		pf->removePortal(m_waypoints[0]);
		pf->removePortal(m_waypoints[1]);
		pf->pathsLostPortals();
	}
	m_registered = false;
}

void FakePathfindPortalBehaviour::clear()
{
	// RW 0x86491E
	if (!m_created)
	{
		return;
	}
	Object *obj = getObject();
	Pathfinder *pf = obj ? pathfinderOf(*obj) : nullptr;
	for (int &id : m_waypoints)
	{
		if (id != PathfindWaypoint::NO_ID && pf)
		{
			const PathfindWaypoint *w = pf->findWaypoint(id);
			if (w && Pathfinder::isPortalWaypoint(*w))
			{
				pf->removePortal(id);
			}
			pf->destroyWaypoint(id);
		}
		id = PathfindWaypoint::NO_ID;
	}
	if (pf)
	{
		pf->pathsLostPortals();
	}
	m_created = false;
}

void FakePathfindPortalBehaviour::onDie(const DieModuleInterface::Event &event)
{
	// RW 0x8649ED
	(void)event;
	clear();
	m_dead = true;
}

UpdateSleepTime FakePathfindPortalBehaviour::update()
{
	// RW 0x864EAA
	Object *obj = getObject();
	const Player *owner = obj->getControllingPlayer();
	if (!owner || obj->logic().skirmishAI().findAI(owner->getPlayerIndex()) == nullptr) // RW 0x6A950B
	{
		return UPDATE_SLEEP_FOREVER;
	}
	GateOpenAndCloseBehavior *gate = GateOpenAndCloseBehavior::findGate(*obj); // RW 0x89BEE0 (kept at + 0x34)
	if (!gate)
	{
		return UPDATE_SLEEP_FOREVER;
	}
	m_gateFound = true;
	if (!m_created && !m_dead) // RW 0x864E98
	{
		create();
	}
	if (m_registered)
	{
		if (gate->isOpen()) // slot 6
		{
			unregister();
		}
	}
	else if (!gate->isOpen())
	{
		doRegister();
	}
	return UPDATE_SLEEP(1);
}

void FakePathfindPortalBehaviour::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferUpdateModule(x);
	for (int &id : m_waypoints)
	{
		x.xferInt(&id);
	}
	x.xferBool(&m_created);
	x.xferBool(&m_dead);
	x.xferBool(&m_registered);
	x.xferBool(&m_gateFound);
}

void FakePathfindPortalBehaviour::crc(StateHasher &hasher) const
{
	UpdateModule::crc(hasher);
	if (m_created || m_dead || m_registered)
	{
		hasher.addBool(m_created);
		hasher.addBool(m_dead);
		hasher.addBool(m_registered);
		hasher.addI32(m_waypoints[0]);
		hasher.addI32(m_waypoints[1]);
	}
}

void FakePathfindPortalBehaviour::registerClass(ModuleFactory &modules)
{
	bind<FakePathfindPortalBehaviour, FakePathfindPortalBehaviourModuleData>(modules, "FakePathfindPortalBehaviour");
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------

void PortalModules::registerAll(ModuleFactory &modules)
{
	DynamicPortalBehaviour::registerClass(modules);
	FakePathfindPortalBehaviour::registerClass(modules);
}

std::vector<std::string> PortalModules::stopLines()
{
	return { kStopBones, kStopWallBoundsMesh, kStopFoundation };
}

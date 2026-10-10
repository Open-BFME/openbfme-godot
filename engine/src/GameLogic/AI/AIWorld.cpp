// OpenBFME. GPL-3.0.
// See GameLogic/AI/AIWorld.h for the sources and what is inference.

#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameLogic/Module/CollideModule.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/AsciiString.h"
#include "Common/JobSystem.h"
#include "Common/INI.h"
#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Team.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIPathfindConfig.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/AI/AIGroup.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"
#include "Common/INI/HostRealText.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace
{
const char *const kStopAI =
	"S-220 AIWorld reads the movement data of a template (Geometry*, FenceWidth, PathfindDiameter, SlopeLimitIndex, CanPathThroughGates, CrusherLevel, CrushableLevel) from the "
	"raw field slots and LocomotorSet through the real RW parser over the raw blocks; canCrushOrSquish is crusher level above crushable level (RW 0x68D524 not read); the "
	"collision pass is a circle overlap over a 10-unit grid of the AI units (the partition and collision managers of RW 0x62E93B are not ported, S-142); the unit's "
	"veterancy level is 0 (no experience tracker)";
// lane PHYS-1: what the overlap pass is not (RW's collision manager), noted when the pass meets its first pair
const char *const kStopCollisionManager =
	"[S-780] collision: the contact pairs are a circle overlap of the AI units' bounding circles (RotWK: the collision manager RW 0xDE4360, vtable 0xD0BF08, update "
	"vslot 0x28 RW 0xB6C420 -> 0xB6E6B0: RW 0xB6CF90 walks the contact table (0x493 buckets) and drops the entries of dead endpoints and clears the cached test of an entry "
	"with a dirty endpoint; RW 0xB6D8A0 is the broad phase (interval endpoints per collidable on up to three axes, a candidate pair table of 0x2B7B buckets, a contact "
	"entry when every axis overlaps); RW 0xB6D060 walks the contact table: the test vslot 0x20 when the cached result is invalid, then onCollide vslot 0x24 A -> B and "
	"B -> A with the normal reversed; both tables key the ordered id pair; the partition manager RW 0xDE4354, update RW 0xA3B4E0, is not read): eligibility, the pair "
	"order, the shape test (only bounding circles) and the location / normal (null) are not retail";
// lane PHYS-1 (review r1): AIUpdate::onCollide (RW 0x66E233) reacts to enemies only and PhysicsBehavior has no collide interface; allied path clearing is a separate
// mechanism (RW 0x6F503B / 0x6F53AF -> 0x66C66E -> 0x66DA5F, the move-away search RW 0x6FB231 and state 26)
const char *const kStopAlliedStack =
	"[S-781] collision: allied ground units overlap: AIUpdate::onCollide (RW 0x66E233) reacts to enemies only and PhysicsBehavior has no collide interface; RotWK's allied "
	"path clearing and crowd avoidance (RW 0x6F503B / 0x6F53AF -> 0x66C66E -> 0x66DA5F, move-away search RW 0x6FB231, AI state 26) is a separate mechanism; the cause of "
	"the measured member overlap is unverified";
// lane PATH-2 (item 2): the incremental registration of placed objects
const char *const kStopRegistration =
	"S-610 a placed structure's footprint enters the pathfinder at the creation sites the port has (map objects RW 0x62E192, starting bases RW 0x629EC9, construction, "
	"castle unpack, LiveGame::createObject as RW 0x62C98D); the other callers of RW addObjectToPathfindMap 0x6E85E9 (an OCL's object RW 0x5F1673, Object state changes "
	"RW 0x68B2A1 / 0x68BCDB / 0x6923EB / 0x694838 / 0x698BDA, RW 0x67B43B, 0x6AA755 and 39 module sites RW 0x797949 .. 0x911DB9) are not traced";

// ---- raw field readers (the object table's rows are kept as raw tokens, S-072) ------------------------------------------------
const RawTokens *rawField(const ThingTemplate &t, const char *name)
{
	const FieldValue *v = t.findField(name);
	return v ? std::get_if<RawTokens>(v) : nullptr;
}

// the first token of a field, macro-expanded as at parse time
std::string firstToken(const RawTokens &raw)
{
	if (raw.tokens.empty())
	{
		return std::string();
	}
	if (!raw.macros.empty() && raw.macros[0].isMacro)
	{
		return raw.macros[0].value;
	}
	return raw.tokens[0];
}

bool realField(const ThingTemplate &t, const char *name, float &out, std::vector<std::string> &errors)
{
	const FieldValue *v = t.findField(name);
	if (!v || std::holds_alternative<std::monostate>(*v))
	{
		return false;
	}
	if (const float *f = std::get_if<float>(v))
	{
		out = *f;
		return true;
	}
	if (const long long *n = std::get_if<long long>(v))
	{
		out = (float)*n;
		return true;
	}
	if (const RawTokens *raw = std::get_if<RawTokens>(v))
	{
		const std::string tok = firstToken(*raw);
		char *end = nullptr;
		const float f = strtofPortable(tok.c_str(), &end);
		if (!tok.empty() && end != tok.c_str())
		{
			out = f;
			return true;
		}
	}
	errors.push_back(std::string(name) + " has an unreadable value");
	return false;
}

bool intField(const ThingTemplate &t, const char *name, long long &out, std::vector<std::string> &errors)
{
	float f = 0.0f;
	if (!realField(t, name, f, errors))
	{
		return false;
	}
	out = SimMath::truncToInt32(f); // the INI integer: cvttss2si
	return true;
}

bool boolField(const ThingTemplate &t, const char *name, bool &out)
{
	const FieldValue *v = t.findField(name);
	if (!v)
	{
		return false;
	}
	if (const bool *b = std::get_if<bool>(v))
	{
		out = *b;
		return true;
	}
	if (const RawTokens *raw = std::get_if<RawTokens>(v))
	{
		const std::string tok = firstToken(*raw);
		if (tok.empty())
		{
			return false;
		}
		out = AsciiStringUtil::compareNoCase(tok, "Yes") == 0 || tok == "1" || AsciiStringUtil::compareNoCase(tok, "True") == 0;
		return true;
	}
	return false;
}

struct SetOwner : LocomotorSetOwner
{
	std::string name;
	bool hasAI = false;
	LocomotorSetTemplate set;
	const std::string &locomotorSetObjectName() const override { return name; }
	bool hasAIUpdateModule() const override { return hasAI; }
	bool aiUpdateHasLocomotorsFor(int) const override { return false; } // a later block of the same condition replaces the earlier (ChildObject semantics)
	LocomotorSetTemplate &locomotorSet() override { return set; }
};

void parseLocomotorBlocks(const ThingTemplate &tt, INIEnvironment &env, ObjectMovementInfo &out)
{
	std::string text;
	for (const RawBlock &b : tt.rawBlocks())
	{
		if (b.field != "LocomotorSet")
		{
			continue;
		}
		for (const RawLine &l : b.lines)
		{
			text += l.text;
			text += '\n';
		}
	}
	if (text.empty())
	{
		return;
	}
	SetOwner owner;
	owner.name = tt.getName();
	owner.hasAI = out.hasAIModule;
	// one private handler for the block: the INI machinery does the rest (macros, field parsers, errors) exactly as the retail load
	static SetOwner *current = nullptr;
	current = &owner;
	if (!env.blocks.contains("LocomotorSet"))
	{
		env.blocks.registerBlock("LocomotorSet", [](INI *ini) { parseLocomotorSet(ini, static_cast<LocomotorSetOwner *>(current), nullptr, nullptr); });
	}
	INI ini(env);
	try
	{
		ini.loadMemory(tt.getName() + "#LocomotorSet", std::vector<std::uint8_t>(text.begin(), text.end()), INI_LOAD_CHILD_OBJECT);
	}
	catch (const std::exception &e)
	{
		out.errors.push_back(std::string("LocomotorSet: ") + e.what());
	}
	current = nullptr;
	out.locomotorSets = std::move(owner.set);
}

void buildInfo(const ThingTemplate &tt, INIEnvironment &env, ObjectMovementInfo &out)
{
	for (const ThingTemplate::Nugget &n : tt.behaviorModules().nuggets())
	{
		if (n.data && n.data->isAiModuleData())
		{
			out.hasAIModule = true;
		}
	}
	std::vector<std::string> errs;
	float major = 0.0f, minor = 0.0f, height = 0.0f;
	realField(tt, "GeometryMajorRadius", major, errs);
	realField(tt, "GeometryMinorRadius", minor, errs);
	realField(tt, "GeometryHeight", height, errs);
	bool isSmall = false;
	boolField(tt, "GeometryIsSmall", isSmall);
	PathfindGeometryType type = PATHFIND_GEOMETRY_BOX;
	if (const RawTokens *g = rawField(tt, "Geometry"))
	{
		const std::string tok = firstToken(*g);
		if (AsciiStringUtil::compareNoCase(tok, "BOX") == 0) type = PATHFIND_GEOMETRY_BOX;
		else if (AsciiStringUtil::compareNoCase(tok, "CYLINDER") == 0) type = PATHFIND_GEOMETRY_CYLINDER;
		else if (AsciiStringUtil::compareNoCase(tok, "SPHERE") == 0) type = PATHFIND_GEOMETRY_SPHERE;
		else errs.push_back("Geometry '" + tok + "' is not BOX, CYLINDER or SPHERE");
		out.hasGeometry = true;
	}
	out.geometry.type = type;
	out.geometry.majorRadius = major;
	out.geometry.minorRadius = minor;
	out.geometry.height = height;
	out.geometry.isSmall = isSmall;
	// lane PATH-2 (S-341): the shape list the Geometry rows build in order (RW 0xAD4040 / 0xAD3BC0 and the rows that change the last shape,
	// GameLogic/Object/ObjectGeometry.h). Shape 0 gives the single-shape fields; the footprint (RW 0x936B7D) and the bounding circle / sphere
	// (RW 0xAD2860) read every shape. A template without a Geometry row keeps the fields above as its one shape.
	if (out.hasGeometry)
	{
		try
		{
			ObjectGeometry::fillPathfindGeometry(tt, out.geometry);
		}
		catch (const std::exception &e)
		{
			errs.push_back(std::string("Geometry: ") + e.what());
		}
	}
	realField(tt, "FenceWidth", out.fenceWidth, errs);
	realField(tt, "FenceXOffset", out.fenceXOffset, errs);
	realField(tt, "PathfindDiameter", out.pathfindDiameter, errs);
	long long v = 0;
	if (intField(tt, "SlopeLimitIndex", v, errs))
	{
		out.slopeLimitIndex = (int)v;
	}
	bool gates = false;
	if (boolField(tt, "CanPathThroughGates", gates))
	{
		out.canPathThroughGates = gates;
	}
	if (intField(tt, "CrusherLevel", v, errs))
	{
		out.crusherLevel = (unsigned)v;
	}
	if (intField(tt, "CrushableLevel", v, errs))
	{
		out.crushableLevel = (unsigned)v;
	}
	for (const std::string &e : errs)
	{
		out.errors.push_back(tt.getName() + ": " + e);
	}
	parseLocomotorBlocks(tt, env, out);
	for (std::string &e : out.errors)
	{
		if (e.compare(0, tt.getName().size(), tt.getName()) != 0)
		{
			e = tt.getName() + ": " + e;
		}
	}
}

// the KindOf bit of every PathfindKind, resolved against the binary's own table
struct KindBits
{
	int bit[PK_COUNT];
	int pathThroughEachOther;
	KindBits()
	{
		struct Map
		{
			PathfindKind kind;
			const char *name;
		};
		static const Map table[] = {
			{ PK_MINE, "MINE" },
			{ PK_PROJECTILE, "PROJECTILE" },
			{ PK_BRIDGE_TOWER, "BRIDGE_TOWER" },
			{ PK_DEFENSIVE_WALL, "DEFENSIVE_WALL" },
			{ PK_BLAST_CRATER, "BLAST_CRATER" },
			{ PK_STRUCTURE, "STRUCTURE" },
			{ PK_CAN_SEE_THROUGH_STRUCTURE, "CAN_SEE_THROUGH_STRUCTURE" },
			{ PK_INFANTRY, "INFANTRY" },
			{ PK_CAVALRY, "CAVALRY" },
			{ PK_MONSTER, "MONSTER" },
			{ PK_MACHINE, "MACHINE" },
			{ PK_DOZER, "DOZER" },
			{ PK_HARVESTER, "HARVESTER" },
			{ PK_AIRCRAFT, "AIRCRAFT" },
			{ PK_IMMOBILE, "IMMOBILE" },
			{ PK_HERO, "HERO" },
			{ PK_HORDE, "HORDE" },
			{ PK_SHIP, "SHIP" },
			{ PK_PATH_THROUGH_INFANTRY, "PATH_THROUGH_INFANTRY" },
			{ PK_HEAVY_MELEE_HITTER, "HEAVY_MELEE_HITTER" },
			{ PK_LARGE_RECTANGLE_PATHFIND, "LARGE_RECTANGLE_PATHFIND" },
			{ PK_WALK_ON_TOP_OF_WALL, "WALK_ON_TOP_OF_WALL" },
			{ PK_DO_NOT_CLASSIFY, "DO_NOT_CLASSIFY" },
			{ PK_SIEGE_LADDER, "SIEGE_LADDER" },
			{ PK_BLOCKING_GATE, "BLOCKING_GATE" },
			{ PK_SCALEABLE_WALL, "SCALEABLE_WALL" },
			{ PK_BASE_SITE, "BASE_SITE" },
			{ PK_WALL_UPGRADE, "WALL_UPGRADE" },
		};
		for (int &b : bit)
		{
			b = -1;
		}
		for (const Map &m : table)
		{
			bit[(int)m.kind] = ObjectTemplateInfoBuilder::kindOfIndex(m.name);
		}
		pathThroughEachOther = ObjectTemplateInfoBuilder::kindOfIndex("PATH_THROUGH_EACH_OTHER");
	}
};
const KindBits &kindBits()
{
	static const KindBits k;
	return k;
}

int statusIndex(const char *name)
{
	const int b = ObjectTemplateInfoBuilder::objectStatusIndex(name);
	if (b < 0)
	{
		throw std::logic_error(std::string("object status registry has no ") + name);
	}
	return b;
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------------------------
// config
// ---------------------------------------------------------------------------------------------------------------------------------
bool AIWorldConfigLoader::load(ArchiveFileSystem &fs, AIWorldConfig &out, std::string *error)
{
	AIWorldConfig c;
	if (!PathfindConfigLoader::load(fs, c.pathfind, error))
	{
		return false;
	}
	std::vector<std::uint8_t> bytes;
	if (!fs.readFile("data\\ini\\gamedata.ini", bytes, error))
	{
		return false;
	}
	const std::string text(bytes.begin(), bytes.end());
	std::string value;
	auto collect = [](void *user, const std::string &key, const std::string &val) {
		std::string *s = static_cast<std::string *>(user);
		if (AsciiStringUtil::compareNoCase(key, "MovementPenaltyDamageState") == 0)
		{
			*s = val;
		}
	};
	std::string err;
	if (!PathfindConfigLoader::scanBlock(text, "GameData", &err, collect, &value))
	{
		if (error)
		{
			*error = "data\\ini\\gamedata.ini: " + err;
		}
		return false;
	}
	if (value.empty())
	{
		if (error)
		{
			*error = "data\\ini\\gamedata.ini: key MovementPenaltyDamageState not found";
		}
		return false;
	}
	// ZH GlobalData: parseBodyDamageType over PRISTINE, DAMAGED, REALLYDAMAGED, RUBBLE
	static const char *const names[] = { "PRISTINE", "DAMAGED", "REALLYDAMAGED", "RUBBLE" };
	int index = -1;
	for (int i = 0; i < 4; ++i)
	{
		if (AsciiStringUtil::compareNoCase(value, names[i]) == 0)
		{
			index = i;
		}
	}
	if (index < 0)
	{
		if (error)
		{
			*error = "data\\ini\\gamedata.ini: MovementPenaltyDamageState '" + value + "' is not a body damage type";
		}
		return false;
	}
	c.movementPenaltyDamageState = index;
	out = c;
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// ObjectPathfindAdapter
// ---------------------------------------------------------------------------------------------------------------------------------
ObjectPathfindAdapter::ObjectPathfindAdapter(AIWorld &world, Object &object)
	: m_world(world)
	, m_object(&object)
{
}

AIUpdateInterface *ObjectPathfindAdapter::ai() const
{
	return m_object->getAIUpdateInterface();
}

PathfindObjectID ObjectPathfindAdapter::getID() const { return m_object->getID(); }
const Coord3D &ObjectPathfindAdapter::getPosition() const { return *m_object->getPosition(); }
float ObjectPathfindAdapter::getOrientation() const { return m_object->getOrientation(); }

float ObjectPathfindAdapter::getHeightAboveTerrain() const
{
	const Coord3D &p = *m_object->getPosition();
	return SimMath::subf32(p.z, m_object->logic().getGroundHeight(p.x, p.y));
}

bool ObjectPathfindAdapter::isMobile() const
{
	return !isKindOf(PK_IMMOBILE);
}

bool ObjectPathfindAdapter::isKindOf(PathfindKind kind) const
{
	const int bit = kindBits().bit[(int)kind];
	return bit >= 0 && m_object->isKindOf((unsigned)bit);
}

const ObjectMovementInfo &ObjectPathfindAdapter::info() const
{
	const ThingTemplate *tt = m_object->getTemplate();
	if (tt != m_infoTemplate || !m_info)
	{
		m_info = &m_world.movementInfo(*tt);
		m_infoTemplate = tt;
	}
	return *m_info;
}

const PathfindGeometry &ObjectPathfindAdapter::getGeometry() const
{
	// lane HUD-5: an object whose own GeometryInfo changed (Object + 0xA8, RW 0xAD3520) answers with its own shape flags
	const std::vector<std::uint8_t> &active = m_object->geometryActive();
	if (active.empty())
	{
		return info().geometry;
	}
	if (m_ownGeometryVersion != m_object->geometryVersion())
	{
		m_ownGeometry = info().geometry;
		for (size_t i = 0; i < m_ownGeometry.shapes.size() && i < active.size(); ++i)
		{
			m_ownGeometry.shapes[i].active = active[i] != 0;
		}
		m_ownGeometryVersion = m_object->geometryVersion();
	}
	return m_ownGeometry;
}

float ObjectPathfindAdapter::getFenceWidth() const { return info().fenceWidth; }
float ObjectPathfindAdapter::getFenceXOffset() const { return info().fenceXOffset; }
float ObjectPathfindAdapter::getPathfindDiameter() const { return info().pathfindDiameter; }
int ObjectPathfindAdapter::getSlopeLimitIndex() const { return info().slopeLimitIndex; }
bool ObjectPathfindAdapter::canPathThroughGates() const { return info().canPathThroughGates; }

bool ObjectPathfindAdapter::isRubble() const
{
	const BodyModuleInterface *body = m_object->getBodyModule();
	return body && body->getDamageState() == BODY_RUBBLE;
}

// RW 0x68B68A is the human test
bool ObjectPathfindAdapter::isComputerControlled() const
{
	const Player *p = m_object->getControllingPlayer();
	return p && p->getPlayerType() == PLAYER_COMPUTER;
}

unsigned ObjectPathfindAdapter::getCrusherLevel() const { return info().crusherLevel; }

PathfindRelationship ObjectPathfindAdapter::getRelationship(const PathfindObject &other) const
{
	const ObjectPathfindAdapter *o = dynamic_cast<const ObjectPathfindAdapter *>(&other);
	if (!o || !m_object->getTeam() || !o->m_object->getTeam())
	{
		return PATHFIND_NEUTRAL;
	}
	return (PathfindRelationship)(int)m_object->getTeam()->getRelationship(o->m_object->getTeam());
}

bool ObjectPathfindAdapter::canCrushOrSquish(const PathfindObject &other) const
{
	const ObjectPathfindAdapter *o = dynamic_cast<const ObjectPathfindAdapter *>(&other);
	if (!o)
	{
		return false;
	}
	return info().crusherLevel > o->info().crushableLevel; // S-220
}

const PathfindObject *ObjectPathfindAdapter::getContainer() const
{
	Object *c = m_object->getContainedBy();
	return c ? &m_world.adapterFor(*c) : nullptr;
}

const PathfindObject *ObjectPathfindAdapter::getTopContainer() const
{
	Object *top = m_object;
	while (top->getContainedBy())
	{
		top = top->getContainedBy();
	}
	return top == m_object ? this : &m_world.adapterFor(*top);
}

// RW 0x68B47F: the goal slot and the position slot of the unit name the same cell
bool ObjectPathfindAdapter::isParked() const
{
	const ICoord2D g = m_world.pathfinder().pathfindGoalCell(m_object->getID());
	const ICoord2D c = m_world.pathfinder().curPathfindCell(m_object->getID());
	return g.x >= 0 && g.x == c.x && g.y == c.y;
}

bool ObjectPathfindAdapter::hasAI() const { return m_object->getAIUpdateInterface() != nullptr; }

PathfindObjectID ObjectPathfindAdapter::getIgnoredObstacleID() const
{
	AIUpdateInterface *a = ai();
	return a ? a->mover().ignoredObstacleID() : PATHFIND_INVALID_ID;
}

bool ObjectPathfindAdapter::canPathThroughUnits() const
{
	AIUpdateInterface *a = ai();
	return a && a->canPathThroughUnits();
}

bool ObjectPathfindAdapter::isAircraftThatAdjustsDestination() const
{
	AIUpdateInterface *a = ai();
	return a && a->isAircraftThatAdjustsDestination();
}

bool ObjectPathfindAdapter::isDoingGroundMovement() const
{
	AIUpdateInterface *a = ai();
	return a ? a->isDoingGroundMovement() : true;
}

int ObjectPathfindAdapter::aiPriority() const
{
	AIUpdateInterface *a = ai();
	return a ? a->pathPriority() : 0;
}

bool ObjectPathfindAdapter::hasLocomotor() const
{
	AIUpdateInterface *a = ai();
	return a && a->curLocomotor() != nullptr;
}

// RW 0x6ED21E: hypot(distance) / speed (seconds)
float ObjectPathfindAdapter::secondsToReach(float x, float y) const
{
	AIUpdateInterface *a = ai();
	if (!a || !a->curLocomotor())
	{
		return 0.0f;
	}
	const Coord3D &p = *m_object->getPosition();
	const float dist = SimMath::length2d(SimMath::subf32(x, p.x), SimMath::subf32(y, p.y));
	const float speed = a->curLocomotorSpeed();
	return speed > 0.0f ? SimMath::divf32(dist, speed) : 0.0f;
}

// RW 0x66288A (AI vtable +0x228): false (stationary) unless the unit follows a path with a special node
bool ObjectPathfindAdapter::isStationary() const
{
	AIUpdateInterface *a = ai();
	if (!a || !a->curLocomotor())
	{
		return true;
	}
	const AIGoalType g = a->mover().goalType();
	if (g != AIGOAL_ON_PATH && g != AIGOAL_EXPLICIT_WITH_PATH)
	{
		return true;
	}
	const Path *path = a->mover().path();
	return !(path && path->hasExplicitZ());
}

// RW 0x936C57 (the footprint registration RW 0x936B7D, reached through 0x6E85E9): the one status that keeps an object out of the map is 0x58 = PHANTOM_STRUCTURE (the binary's
// status name table). An ordinary foundation (UNDER_CONSTRUCTION, status 2) is an obstacle from the moment it is placed (lane BUILD-1, review r1). The 0x4D of the earlier note is
// not read by that function.
bool ObjectPathfindAdapter::footprintSkippedByStatus() const
{
	static const int phantom = statusIndex("PHANTOM_STRUCTURE");
	return phantom >= 0 && m_object->testStatus((unsigned)phantom);
}

bool ObjectPathfindAdapter::ignoresAsTarget(const PathfindObject &) const
{
	return false; // the attack machine's target (RW 0x8DBACE, 0x668303) belongs to the combat milestone
}

bool ObjectPathfindAdapter::pathsThroughEachOther() const
{
	const int bit = kindBits().pathThroughEachOther;
	return bit >= 0 && m_object->isKindOf((unsigned)bit);
}

// lane PHYS-1 (INFERENCE S-783): RW AI + 0x16C read by RW 0x6F1C90 is taken to be the blocked-frame counter
int ObjectPathfindAdapter::aiBlockedFrames() const
{
	const AIUpdateInterface *a = ai();
	return a ? a->mover().blockedFrames() : 0;
}

// lane MOVE-3: RW 0x8E24D3's horde branch -> Object's contain (+ 0x258) slot 0x7C (the horde contain interface) -> its slot 0xC8 (RW 0x86EF13)
void ObjectPathfindAdapter::onHordeGoalChanged()
{
	ContainModuleInterface *contain = m_object->getContain();
	if (HordeContainInterface *h = contain ? contain->getHordeContainInterface() : nullptr)
	{
		h->reserveMemberGoals();
	}
}

bool ObjectPathfindAdapter::isEffectivelyDead() const
{
	return m_object->isEffectivelyDead();
}

bool ObjectPathfindAdapter::hordeFill(int &count, int &slots) const
{
	ContainModuleInterface *contain = m_object->getContain();
	HordeContainInterface *h = contain ? contain->getHordeContainInterface() : nullptr;
	if (h == nullptr || h->getSlotCapacity() <= 0)
	{
		return false;
	}
	count = (int)contain->getContainCount();
	slots = h->getSlotCapacity();
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// AIWorld
// ---------------------------------------------------------------------------------------------------------------------------------
AIWorld::AIWorld(GameLogic &logic, const AIWorldConfig &config, const INIMacroTable &macros)
	: m_logic(logic)
	, m_config(config)
	, m_env(std::make_unique<INIEnvironment>())
{
	m_env->macros = macros;
	m_pathfinder = std::make_unique<Pathfinder>(m_config.pathfind, this);
	m_moveWorld = std::make_unique<AIMoveWorld>(*m_pathfinder);
}

AIWorld::~AIWorld()
{
	detach();
}

void AIWorld::attach()
{
	if (m_attached)
	{
		return;
	}
	m_attached = true;
	m_logic.setAIWorld(this);
	m_hooksToken = m_logic.addWorldHooks([this](Object &o) { objectEnteredWorld(o); }, [this](Object &o) { objectLeftWorld(o); });
	m_hashToken = m_logic.addStateHashContributor([this](StateHasher &h) { crc(h); }, "pathfinder and AI movers");
	m_logic.installPhaseWork("pathfinderQueue", [this]() { processPathfindQueue(); });
	m_logic.installPhaseWork("partitionAndCollision", [this]() { processCollisions(); });
	// PROD-1's hand-off: what production asks the AI of a produced object (move to the rally point, follow the exit path, idle, regroup) runs here, at the call sites
	// production already has (GameLogic/AI/AICommandSink.h)
	m_logic.aiCommands().setHandler([this](Object &obj, const AICommand &c) { return executeCommand(obj, c); });
}

// the AICommandSink handler: the commands production issues (RW AICommandParms numbers, all with CMD_FROM_AI)
bool AIWorld::executeCommand(Object &obj, const AICommand &c)
{
	AIUpdateInterface *ai = obj.getAIUpdateInterface();
	if (!ai)
	{
		return false; // an object without an AI module cannot take it: counted by the sink as unexecuted
	}
	switch (c.type)
	{
		case AICMD_MOVE_TO_POSITION:
			if (c.path.empty())
			{
				return false;
			}
			ai->aiMoveToPosition(c.path.front(), c.source);
			return true;
		case AICMD_IDLE:
			ai->aiIdle(c.source);
			return true;
		case AICMD_FOLLOW_EXIT_PRODUCTION_PATH:
			if (c.path.empty())
			{
				return false;
			}
			ai->aiFollowPath(c.path, c.target != INVALID_ID ? m_logic.findObjectByID(c.target) : nullptr, c.source, true);
			return true;
		case AICMD_MOVE_TO_OBJECT:
		{
			Object *target = m_logic.findObjectByID(c.target);
			if (!target)
			{
				return false;
			}
			ai->aiMoveToObject(target, c.source);
			return true;
		}
		case AICMD_HORDE_RETURN_TO_FORMATION:
		{
			ContainModuleInterface *contain = obj.getContain();
			HordeContainInterface *horde = contain ? contain->getHordeContainInterface() : nullptr;
			if (!horde)
			{
				return false;
			}
			horde->updateFormation(); // the members walk back to their slots (the member pass runs again)
			return true;
		}
	}
	return false;
}

void AIWorld::detach()
{
	if (!m_attached)
	{
		return;
	}
	m_attached = false;
	m_logic.aiCommands().setHandler(AICommandSink::Handler());
	m_logic.removeWorldHooks(m_hooksToken); // the registrations capture this: they go with the world, never outlive it
	m_logic.removeStateHashContributor(m_hashToken);
	m_hooksToken = m_hashToken = 0;
	m_logic.installPhaseWork("pathfinderQueue", std::function<void()>());
	m_logic.installPhaseWork("partitionAndCollision", std::function<void()>());
	if (m_logic.aiWorld() == this)
	{
		m_logic.setAIWorld(nullptr);
	}
	// the world hooks stay installed in the logic (they are append only) but do nothing once detached
}

void AIWorld::reportMovementFailure(const Object &obj, const std::string &text)
{
	const std::string key = obj.getTemplate()->getName() + ": " + text;
	if (++m_failures[key] == 1)
	{
		m_errors.push_back("movement failure of " + key);
	}
}

void AIWorld::noteStop(const char *stop)
{
	m_noted.insert(stop);
}

std::vector<std::string> AIWorld::allStops()
{
	// every stop line of the movement lane: the world's own (S-220), the AI module's (S-221, S-222), the group commands' (S-223; lane MOVE-3: the group manager's
	// move order S-1831) and the horde's (S-224)
	std::vector<std::string> out{ kStopAI };
	for (const std::string &s : AIUpdateInterface::allStops())
	{
		out.push_back(s);
	}
	out.push_back(AIGroup::stopLine());
	out.push_back(AIGroup::planningStopLine());
	out.push_back(HordeContain::movementStop());
	return out;
}

std::string AIWorld::registrationStop()
{
	return kStopRegistration;
}

std::vector<std::string> AIWorld::stops() const
{
	std::vector<std::string> out;
	out.push_back(kStopAI);
	out.push_back(kStopRegistration);
	for (const std::string &s : m_noted)
	{
		if (std::find(out.begin(), out.end(), s) == out.end())
		{
			out.push_back(s);
		}
	}
	for (const std::string &s : m_pathfinder->stops())
	{
		if (std::find(out.begin(), out.end(), s) == out.end())
		{
			out.push_back(s);
		}
	}
	for (const auto &kv : m_ais)
	{
		for (const std::string &s : kv.second->stopsRaised())
		{
			if (std::find(out.begin(), out.end(), s) == out.end())
			{
				out.push_back(s);
			}
		}
	}
	return out;
}

// ---- the map ----------------------------------------------------------------------------------------------------------------------
void AIWorld::newMap(const PathfindTerrain &terrain)
{
	// RW 0x6EA1F2: classify the terrain, then every object's footprint, then the zones
	std::vector<PathfindObject *> structures;
	for (Object *o = m_logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->isDestroyed())
		{
			continue;
		}
		ObjectPathfindAdapter &a = adapterFor(*o);
		if (a.isKindOf(PK_STRUCTURE) || a.getFenceWidth() > 0.0f)
		{
			structures.push_back(&a);
		}
	}
	m_pathfinder->newMap(terrain, &structures);
	m_mapReady = m_pathfinder->isMapReady();
}

// ---- objects ----------------------------------------------------------------------------------------------------------------------
ObjectPathfindAdapter &AIWorld::adapterFor(Object &obj)
{
	const size_t id = obj.getID();
	if (id >= m_adapters.size())
	{
		m_adapters.resize(id + 1);
	}
	std::unique_ptr<ObjectPathfindAdapter> &slot = m_adapters[id];
	if (!slot)
	{
		slot = std::make_unique<ObjectPathfindAdapter>(*this, obj);
	}
	return *slot;
}

ObjectPathfindAdapter *AIWorld::findAdapter(PathfindObjectID id) const
{
	if ((size_t)id >= m_adapters.size())
	{
		return nullptr;
	}
	return m_adapters[(size_t)id].get();
}

Object *AIWorld::findObject(PathfindObjectID id) const
{
	return m_logic.findObjectByID(id);
}

PathfindObject *AIWorld::findObjectByID(PathfindObjectID id) const
{
	Object *o = m_logic.findObjectByID(id);
	if (!o)
	{
		return nullptr;
	}
	return &const_cast<AIWorld *>(this)->adapterFor(*o);
}

unsigned AIWorld::getFrame() const
{
	return m_logic.getFrame();
}

const ObjectMovementInfo &AIWorld::movementInfo(const ThingTemplate &tt)
{
	const ThingTemplate *key = tt.getFinalOverride();
	auto it = m_info.find(key);
	if (it == m_info.end())
	{
		std::unique_ptr<ObjectMovementInfo> info = std::make_unique<ObjectMovementInfo>();
		buildInfo(*key, *m_env, *info);
		for (const std::string &e : info->errors)
		{
			if (std::find(m_errors.begin(), m_errors.end(), e) == m_errors.end())
			{
				m_errors.push_back(e);
			}
		}
		it = m_info.emplace(key, std::move(info)).first;
	}
	return *it->second;
}

void AIWorld::objectEnteredWorld(Object &)
{
	// a structure's footprint enters the map with newMap (the live game) or addObjectToPathfindMap (construction); nothing to do for the seam itself
}

void AIWorld::objectLeftWorld(Object &obj)
{
	if (!m_attached)
	{
		return;
	}
	ObjectPathfindAdapter *a = findAdapter(obj.getID());
	if (!a)
	{
		return;
	}
	if (m_mapReady)
	{
		// ZH Object::~Object: removeObjectFromPathfindMap (a structure's footprint, a unit's reservations)
		if (a->isKindOf(PK_STRUCTURE) || a->getFenceWidth() > 0.0f)
		{
			m_pathfinder->removeObjectFromPathfindMap(*a);
		}
		m_pathfinder->removeUnitFromPathfindMap(*a);
	}
	m_adapters[(size_t)obj.getID()].reset();
}

void AIWorld::addObjectToPathfindMap(Object &obj)
{
	if (m_mapReady)
	{
		m_pathfinder->addObjectToPathfindMap(adapterFor(obj));
	}
}

void AIWorld::removeObjectFromPathfindMap(Object &obj)
{
	if (m_mapReady)
	{
		m_pathfinder->removeObjectFromPathfindMap(adapterFor(obj));
	}
}

AIUpdateInterface *AIWorld::aiOf(PathfindObjectID id) const
{
	auto it = m_ais.find(id);
	return it == m_ais.end() ? nullptr : it->second;
}

AIUpdateInterface *AIWorld::aiOf(const Object &obj) const
{
	return aiOf((PathfindObjectID)obj.getID());
}

void AIWorld::registerAI(AIUpdateInterface &ai)
{
	m_ais[(PathfindObjectID)ai.getObject()->getID()] = &ai;
}

void AIWorld::unregisterAI(AIUpdateInterface &ai)
{
	auto it = m_ais.find((PathfindObjectID)ai.getObject()->getID());
	if (it != m_ais.end() && it->second == &ai)
	{
		m_ais.erase(it);
	}
}

void AIWorld::crc(StateHasher &h)
{
	h.addBool(m_mapReady);
	m_pathfinder->crc(h); // grid, occupancy, reservations, zones, queues
	h.addU32((std::uint32_t)m_ais.size());
}

// ---- per frame -------------------------------------------------------------------------------------------------------------------
void AIWorld::processPathfindQueue()
{
	if (m_mapReady)
	{
		const size_t queued = m_pathfinder->queuedRequests() + m_pathfinder->queuedBlockedRepaths();
		m_moveWorld->processQueues();
		// diagnostics only (not logic state, not hashed): the cells one tick allocated and the deepest queue a tick found
		const int cells = m_pathfinder->cumulativeCellsAllocated();
		m_peakTickCells = cells > m_peakTickCells ? cells : m_peakTickCells;
		m_peakQueued = queued > m_peakQueued ? queued : m_peakQueued;
	}
}

// the overlap pass: every pair of AI units that overlap meets in onCollide, both ways, in id order. The units go into a grid of 10-unit cells, in id order; a
// pair is found once through the cell of the lower id's neighbourhood.
void AIWorld::processCollisions()
{
	m_collisionPairs = 0;
	if (!m_mapReady || m_ais.size() < 2)
	{
		return;
	}
	// lane PERF-1: the same pass on arrays kept between frames (no allocation per frame or per bucket); the buckets are a counting sort of the units
	// (each bucket lists its units in index order, as the per-bucket vectors that were filled in index order did), so every unit meets the same
	// candidates in the same sorted order and the pairs are handled exactly as before
	std::vector<CollisionUnit> &units = m_collisionUnits;
	units.clear();
	for (const auto &kv : m_ais) // ordered by id
	{
		AIUpdateInterface *ai = kv.second;
		if (ai->getObject()->isDestroyed() || ai->isImmobile() || !ai->getObject()->isInWorld())
		{
			continue; // lane GARRISON-1: a contain's rider out of the world (RW 0x68C18F) is not in the partition (ZH PartitionManager::processCollisions)
		}
		units.push_back(CollisionUnit{ kv.first, ai, 0.0f, 0.0f, 0.0f, 0, &ai->adapter().getGeometry() });
	}
	if (units.size() < 2)
	{
		return;
	}
	// lane PERF-2: every unit's position and footprint radius on the logic job pool (each job writes only its own units; nothing changes the state
	// while the pass reads it; the geometry was resolved above, on this thread, because the adapter's movement info is filled on first use)
	constexpr size_t kUnitsPerJob = 64;
	JobSystem &jobs = JobSystem::logic();
	jobs.parallelFor(units.size(), kUnitsPerJob, [&units](size_t, size_t begin, size_t end) {
		for (size_t i = begin; i < end; ++i)
		{
			CollisionUnit &u = units[i];
			const Coord3D &p = *u.ai->getObject()->getPosition();
			u.x = p.x;
			u.y = p.y;
			u.r = u.geometry->boundingCircleRadius();
		}
	});
	// uniform grid over the units' own extent, 40-unit buckets (a bucket spans several footprints); buckets are visited in index order
	float minX = units[0].x, minY = units[0].y, maxX = units[0].x, maxY = units[0].y;
	for (const CollisionUnit &u : units)
	{
		minX = std::min(minX, u.x);
		maxX = std::max(maxX, u.x);
		minY = std::min(minY, u.y);
		maxY = std::max(maxY, u.y);
	}
	const float cell = 40.0f;
	const int w = SimMath::truncToInt32(SimMath::divf32(SimMath::subf32(maxX, minX), cell)) + 1, h = SimMath::truncToInt32(SimMath::divf32(SimMath::subf32(maxY, minY), cell)) + 1;
	const size_t bucketCount = (size_t)w * (size_t)h;
	std::vector<int> &start = m_collisionBucketStart, &fill = m_collisionBucketFill, &items = m_collisionBucketUnits;
	start.assign(bucketCount + 1, 0);
	for (CollisionUnit &u : units)
	{
		const int cx = SimMath::truncToInt32(SimMath::divf32(SimMath::subf32(u.x, minX), cell)), cy = SimMath::truncToInt32(SimMath::divf32(SimMath::subf32(u.y, minY), cell));
		u.bucket = cy * w + cx;
		++start[(size_t)u.bucket + 1];
	}
	for (size_t b = 0; b < bucketCount; ++b)
	{
		start[b + 1] += start[b];
	}
	fill.assign(start.begin(), start.end() - 1);
	items.resize(units.size());
	for (size_t i = 0; i < units.size(); ++i)
	{
		items[(size_t)fill[(size_t)units[i].bucket]++] = (int)i;
	}
	float maxRadius = 0.0f;
	for (const CollisionUnit &u : units)
	{
		maxRadius = u.r > maxRadius ? u.r : maxRadius;
	}
	// lane PERF-2: the pair search is a pure function of the snapshot above (positions, radii, buckets): the logic job pool searches fixed runs of
	// kUnitsPerJob units, each run listing its overlapping pairs in the serial order (by the lower index, then the partner's index); the pairs then
	// meet in onCollide on this thread, run after run, exactly in the order of the single-threaded pass (whatever the thread count)
	const size_t chunkCount = JobSystem::chunkCount(units.size(), kUnitsPerJob);
	if (m_collisionChunks.size() < chunkCount)
	{
		m_collisionChunks.resize(chunkCount);
	}
	jobs.parallelFor(units.size(), kUnitsPerJob, [&](size_t chunk, size_t first, size_t last) {
		std::vector<int> &candidates = m_collisionChunks[chunk].candidates;
		std::vector<std::pair<int, int>> &pairs = m_collisionChunks[chunk].pairs;
		pairs.clear();
		for (size_t i = first; i < last; ++i)
		{
			const CollisionUnit &a = units[i];
			const int cx = a.bucket % w, cy = a.bucket / w;
			// a partner overlaps when its centre is within a.r + b.r <= a.r + maxRadius: the buckets that reach that far, however large the footprints are
			const int reach = SimMath::truncToInt32(SimMath::divf32(SimMath::addf32(a.r, maxRadius), cell)) + 1;
			candidates.clear();
			for (int ny = std::max(0, cy - reach); ny <= std::min(h - 1, cy + reach); ++ny)
			{
				for (int nx = std::max(0, cx - reach); nx <= std::min(w - 1, cx + reach); ++nx)
				{
					const size_t b = (size_t)ny * (size_t)w + (size_t)nx;
					for (int k = start[b]; k < start[b + 1]; ++k)
					{
						const int j = items[(size_t)k];
						if ((size_t)j > i) // the pair is handled from the lower index
						{
							candidates.push_back(j);
						}
					}
				}
			}
			std::sort(candidates.begin(), candidates.end()); // the pairs of one unit meet in id order, whatever the bucket layout
			for (int j : candidates)
			{
				const CollisionUnit &b = units[(size_t)j];
				const float dx = SimMath::subf32(a.x, b.x), dy = SimMath::subf32(a.y, b.y), rr = SimMath::addf32(a.r, b.r);
				if (SimMath::addf32(SimMath::mulf32(dx, dx), SimMath::mulf32(dy, dy)) >= SimMath::mulf32(rr, rr))
				{
					continue;
				}
				pairs.emplace_back((int)i, j);
			}
		}
	});
	bool alliedPair = false;
	for (size_t chunk = 0; chunk < chunkCount; ++chunk)
	{
		for (const std::pair<int, int> &pair : m_collisionChunks[chunk].pairs)
		{
			const CollisionUnit &a = units[(size_t)pair.first], &b = units[(size_t)pair.second];
			++m_collisionPairs;
			alliedPair = alliedPair || a.ai->adapter().getRelationship(b.ai->adapter()) != PATHFIND_ENEMIES;
			a.ai->onCollide(b.ai->adapter());
			b.ai->onCollide(a.ai->adapter());
			// lane HORDE-2: the collide modules of both objects (ZH Object::onCollide: SquishCollide, HordeMemberCollide, ...), the lower id first (inference, S-581)
			ObjectCollide::onCollide(*a.ai->getObject(), *b.ai->getObject());
			ObjectCollide::onCollide(*b.ai->getObject(), *a.ai->getObject());
		}
	}
	// lane PHYS-1: the stops are noted once a frame at most (no string per pair)
	if (m_collisionPairs > 0)
	{
		m_logic.noteStop(kStopCollisionManager);
	}
	if (alliedPair)
	{
		m_logic.noteStop(kStopAlliedStack);
	}
}

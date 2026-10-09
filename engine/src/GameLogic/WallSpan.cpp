// OpenBFME. GPL-3.0.
// See GameLogic/WallSpan.h for the sources of every rule.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/WallSpan.h"

#include "Common/BuildAssistant.h"
#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/BuildPlacement.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Construction.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/ConstructionModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameLogic/SimMath.h"

#include <cstddef>
#include <stdexcept>


namespace
{
// RW 0xDAE268: the CommandButton option names (bit = index), the table WallHubBehavior's Options row reads
const char *const kOptionNames[] = { "NEED_TARGET_ENEMY_OBJECT", "NEED_TARGET_NEUTRAL_OBJECT", "NEED_TARGET_ALLY_OBJECT", "NO_PLAY_UNIT_SPECIFIC_SOUND_FOR_AUTO_ABILITY",
	"ALLOW_SHRUBBERY_TARGET", "NEED_TARGET_POS", "NEED_UPGRADE", "NEED_SPECIAL_POWER_SCIENCE", "OK_FOR_MULTI_SELECT", "CONTEXTMODE_COMMAND", "CHECK_LIKE", "NEEDS_CASTLE_KINDOF",
	"ATTACK_OBJECTS_POSITION", "OPTION_ONE", "OPTION_TWO", "OPTION_THREE", "NOT_QUEUEABLE", "SINGLE_USE_COMMAND", "---DO-NOT-USE---", "SCRIPT_ONLY", "OK_FOR_MULTI_EXECUTE",
	"ALLOW_ROCK_TARGET", "HIDE_WHILE_DISABLED", "TOGGLE_IMAGE_ON_WEAPON", "TOGGLE_IMAGE_ON_WEAPONSET", "TOGGLE_IMAGE_ON_FORMATION", "MOUNTED_ONLY", "UNMOUNTED_ONLY",
	"NONPRESSABLE", "AUTO_ABILITY_TRIGGERED", "ON_GROUND_ONLY", "CANCELABLE", nullptr };

#define WH_OFF(member) (int)offsetof(WallHubBehaviorModuleData, member)
const FieldParse kWallHubFieldParse[] = {
	{ "SegmentTemplateName", INI::parseAsciiStringVectorAppend, nullptr, WH_OFF(m_segmentTemplateNames) },
	{ "DefaultSegmentTemplateName", INI::parseAsciiString, nullptr, WH_OFF(m_defaultSegmentTemplateName) },
	{ "HubCapTemplateName", INI::parseAsciiString, nullptr, WH_OFF(m_hubCapTemplateName) },
	{ "CliffCapTemplateName", INI::parseAsciiString, nullptr, WH_OFF(m_cliffCapTemplateName) },
	{ "ShoreCapTemplateName", INI::parseAsciiString, nullptr, WH_OFF(m_shoreCapTemplateName) },
	{ "BorderCapTemplateName", INI::parseAsciiString, nullptr, WH_OFF(m_borderCapTemplateName) },
	{ "ElevatedSegmentTemplateName", INI::parseAsciiString, nullptr, WH_OFF(m_elevatedSegmentTemplateName) },
	{ "StaggeredBuildFactor", INI::parseInt, nullptr, WH_OFF(m_staggeredBuildFactor) },
	{ "BuilderRadius", INI::parseReal, nullptr, WH_OFF(m_builderRadius) },
	{ "MaxBuildoutDistance", INI::parseReal, nullptr, WH_OFF(m_maxBuildoutDistance) },
	{ "Options", INI::parseBitString32, kOptionNames, WH_OFF(m_options) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef WH_OFF

// RW 0x644FD0: into (-PI, PI] (SSE float compares and steps; constants RW 0xBDD388 / 0xBDD38C / 0xBDD390)
float normalizeAngle(float a)
{
	const float pi = 3.14159274101257324f, twoPi = 6.28318548202514648f;
	while (a > pi)
	{
		a = SimMath::subf32(a, twoPi);
	}
	while (-pi >= a)
	{
		a = SimMath::addf32(a, twoPi);
	}
	return a;
}

// RW 0x405406 Coord2D::toAngle: len = sqrt((double)(x*x + y*y)) stored float; 0 when 0; c = clamp(x / len, -1, 1) (SSE); acos(c) (MSVCR71 through RW 0x42F4F0, S-167),
// negated when y < 0
float toAngle(float x, float y)
{
	const float len = SimMath::length2d(x, y);
	if (len == 0.0f)
	{
		return 0.0f;
	}
	float c = SimMath::divf32(x, len);
	if (-1.0f > c)
	{
		c = -1.0f;
	}
	else if (c > 1.0f)
	{
		c = 1.0f;
	}
	const float a = (float)SimMath::acosDet((double)c);
	return 0.0f <= y ? a : -a;
}

// RW 0x403175 Coord3D::normalize: len = (float)RW 0x403111; unless 0, each component times (1 / len) (SSE)
void normalize(Coord3D &v)
{
	const float len = (float)SimMath::length3d(v.x, v.y, v.z);
	if (len == 0.0f)
	{
		return;
	}
	const float inv = SimMath::divf32(1.0f, len);
	v.x = SimMath::mulf32(v.x, inv);
	v.y = SimMath::mulf32(v.y, inv);
	v.z = SimMath::mulf32(v.z, inv);
}

int costOf(const ThingTemplate &tt, Object &hub)
{
	return BuildAssistant::calcCostToBuild(tt, hub.getControllingPlayer(), &hub, -1);
}

// RW 0x6B00F3 -> 0x6B00CE: the positions of the player's COMMANDCENTER objects (its objects in object-list order)
std::vector<Coord3D> commandCentres(GameLogic &logic, const Player *player)
{
	std::vector<Coord3D> out;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == player && !o->isDestroyed() && o->isKindOfName("COMMANDCENTER"))
		{
			out.push_back(*o->getPosition());
		}
	}
	return out;
}
} // namespace

// ---- WallHubBehavior -------------------------------------------------------------------------------------------------------------------------
void WallHubBehaviorModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kWallHubFieldParse);
}

WallHubBehavior::WallHubBehavior(Thing *thing, const WallHubBehaviorModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	// RW 0x855EC7: wakes next frame (its update then sleeps for ever); the hub registers its interface with the template (RW 0x696183, not needed here); + 0x24 is the
	// BuilderRadius, or the object's bounding circle radius (Object + 0xB8) when the row was not given
	setWakeFrame(getObject(), UPDATE_SLEEP_NONE);
}

float WallHubBehavior::builderRadius() const
{
	// module + 0x24, set by the constructor; the bounding circle (a function of the template's geometry, fixed for the object) is asked when needed, so a logic without an
	// AIWorld (whose geometry query needs one) can still make hubs
	return m_data->m_builderRadius == 9.876f ? CombatQueries::boundingCircleRadius(*getObject()) : m_data->m_builderRadius;
}

const ThingTemplate *WallHubBehavior::byName(const std::string &name) const
{
	return name.empty() ? nullptr : getObject()->logic().things().findTemplate(name);
}

const ThingTemplate *WallHubBehavior::patternTemplate(unsigned index) const
{
	// RW 0x855FF1: an empty list answers the empty string (no template)
	if (m_data->m_segmentTemplateNames.empty())
	{
		return nullptr;
	}
	return byName(m_data->m_segmentTemplateNames[index % m_data->m_segmentTemplateNames.size()]);
}
const ThingTemplate *WallHubBehavior::hubCapTemplate() const { return byName(m_data->m_hubCapTemplateName); }
const ThingTemplate *WallHubBehavior::defaultSegmentTemplate() const { return byName(m_data->m_defaultSegmentTemplateName); }
const ThingTemplate *WallHubBehavior::cliffCapTemplate() const { return byName(m_data->m_cliffCapTemplateName); }

bool WallHubBehavior::maxBuildoutDistance(float &out) const
{
	out = m_data->m_maxBuildoutDistance;
	return !(m_data->m_maxBuildoutDistance == 54321.0f); // RW 0x855EAC: true unless it is the unset default
}

WallHubBehavior *WallHubBehavior::find(const Object &obj, unsigned options)
{
	// RW 0x693B55: the hub module whose Options equal `options`; 0 asks for the first one
	for (const std::unique_ptr<BehaviorModule> &m : obj.modules())
	{
		if (WallHubBehavior *w = dynamic_cast<WallHubBehavior *>(m.get()))
		{
			if (options == 0 || w->m_data->m_options == options)
			{
				return w;
			}
		}
	}
	return nullptr;
}

void WallHubBehavior::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
}

// ---- the span ---------------------------------------------------------------------------------------------------------------------------------
float WallSpan::halfExtentY(const ThingTemplate &tt)
{
	float minY = 0.0f, maxY = 0.0f;
	for (const ObjectGeometry::Shape &s : ObjectGeometry::shapesOf(tt))
	{
		if (!s.active)
		{
			continue;
		}
		const float r = s.type == ObjectGeometry::SHAPE_BOX ? s.minorRadius : s.majorRadius;
		const float lo = SimMath::subf32(s.offsetY, r), hi = SimMath::addf32(s.offsetY, r);
		minY = lo < minY ? lo : minY;
		maxY = maxY <= hi ? hi : maxY;
	}
	const float negMin = -minY;
	return negMin < maxY ? maxY : negMin; // RW 0xAD28F4: + 0x28 = max(-min y, max y)
}

bool WallSpan::plan(GameLogic &logic, Object &hub, const Coord3D &start, const Coord3D &end, unsigned options, Plan &out)
{
	out = Plan{};
	if (!hub.isKindOfName("WALL_HUB"))
	{
		return false;
	}
	WallHubBehavior *wh = WallHubBehavior::find(hub, options);
	if (!wh)
	{
		return false;
	}
	const ThingTemplate *cap = wh->hubCapTemplate();
	if (!cap)
	{
		return false;
	}
	Player *owner = hub.getControllingPlayer();
	const float capHalf = halfExtentY(*cap); // -0x30
	// RW 0x7958F5: the closest object of the owner within 20.0 of the end (partition getClosestObject, mode 3; the filter is the owner's: the port takes a WALL_HUB of the
	// same owner, list order on equal distance, stop S-657); within 2/3 of the cap's half extent of its builder radius the span ends on it
	Coord3D endAt = end;
	Object *endHub = nullptr;
	{
		float best = 0.0f;
		for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
		{
			if (o == &hub || o->isDestroyed() || !o->isInWorld() || o->getControllingPlayer() != owner || !o->isKindOfName("WALL_HUB"))
			{
				continue;
			}
			const float d = (float)SimMath::length3d(SimMath::subf32(o->getPosition()->x, end.x), SimMath::subf32(o->getPosition()->y, end.y), SimMath::subf32(o->getPosition()->z, end.z));
			if (d <= 20.0f && (!endHub || d < best))
			{
				endHub = o;
				best = d;
			}
		}
	}
	float extent = 0.0f;
	if (endHub)
	{
		WallHubBehavior *other = WallHubBehavior::find(*endHub, 0);
		extent = other ? other->builderRadius() : CombatQueries::boundingCircleRadius(*endHub);
		if (SimMath::mulf32(capHalf, 0.666f) <= extent)
		{
			endAt = *endHub->getPosition();
			out.endsOnHub = true;
			out.endHub = endHub->getID();
		}
		else
		{
			endHub = nullptr;
		}
	}
	bool endCap = !out.endsOnHub; // + 0xB
	float buildout = 99990.0f;    // -0x7C (RW 0xC30848)
	std::vector<Coord3D> centres;
	if (wh->maxBuildoutDistance(buildout) && owner)
	{
		centres = commandCentres(logic, owner);
	}
	float vx = SimMath::subf32(endAt.x, start.x), vy = SimMath::subf32(endAt.y, start.y);
	const float angle = normalizeAngle(SimMath::addf32(toAngle(vx, vy), 1.5707963705062866f)); // -0x4C (RW 0xBD89D0)
	Coord3D dir{ vx, vy, 0.0f };
	normalize(dir);
	if (out.endsOnHub && capHalf < extent)
	{
		const float back = SimMath::subf32(extent, capHalf);
		vx = SimMath::subf32(SimMath::subf32(endAt.x, SimMath::mulf32(back, dir.x)), start.x);
		vy = SimMath::subf32(SimMath::subf32(endAt.y, SimMath::mulf32(back, dir.y)), start.y);
	}
	float done = wh->builderRadius(); // -0x50
	Coord3D pos = start;
	pos.x = SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24MulW((double)dir.x, (double)done), (double)pos.x));
	pos.y = SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24MulW((double)done, (double)dir.y), (double)pos.y));
	const std::uint32_t money = owner ? owner->getMoney()->countMoney() : 0u;
	float remaining = SimMath::subf32((float)SimMath::length3d(vx, vy, 0.0f), capHalf); // -0x20
	auto legal = [&](const Coord3D &p, const ThingTemplate &tt) {
		int code = (int)BuildPlacement::isLocationLegalToBuild(logic, p, tt, angle, 0x55, &hub, nullptr);
		if (!centres.empty())
		{
			bool near = false;
			for (const Coord3D &c : centres)
			{
				near = near || (float)SimMath::length3d(SimMath::subf32(p.x, c.x), SimMath::subf32(p.y, c.y), SimMath::subf32(p.z, c.z)) <= buildout;
			}
			code = near ? code : 9;
		}
		return code;
	};
	const int maxTiles = logic.settings().maxLineBuildObjects; // GameData + 0xA94
	unsigned i = 0;
	if (done < remaining)
	{
		while ((int)i < maxTiles - 1)
		{
			const ThingTemplate *tt = wh->patternTemplate(i);
			tt = tt ? tt : hub.getTemplate();
			float half = SimMath::subf32(halfExtentY(*tt), 1.0f);
			if (remaining < SimMath::addf32(SimMath::mulf32(half, 2.0f), done))
			{
				tt = wh->defaultSegmentTemplate();
				tt = tt ? tt : hub.getTemplate();
				half = SimMath::subf32(halfExtentY(*tt), 1.0f);
			}
			const float sx = SimMath::mulf32(half, dir.x), sy = SimMath::mulf32(half, dir.y);
			pos.x = SimMath::addf32(sx, pos.x);
			pos.y = SimMath::addf32(sy, pos.y);
			pos.z = logic.getGroundHeight(pos.x, pos.y);
			Tile t;
			t.pos = pos;
			t.angle = angle;
			t.tmpl = tt;
			t.code = legal(pos, *tt);
			out.tiles.push_back(t);
			// RW 0x795D56: codes 5 / 6 (RotWK's cliff codes) put the cliff cap here: not reachable with the port's codes (S-657)
			pos.x = SimMath::addf32(sx, pos.x);
			pos.y = SimMath::addf32(sy, pos.y);
			out.cost += costOf(*tt, hub);
			if ((int)money < out.cost)
			{
				out.worstCode = 7;
				out.tiles.back().code = 7;
			}
			done = SimMath::addf32(SimMath::mulf32(half, 2.0f), done);
			++i;
			if (maxTiles <= (int)i || remaining <= done)
			{
				break;
			}
		}
	}
	if (!out.endsOnHub)
	{
		if (endCap)
		{
			const float sx = SimMath::mulf32(dir.x, capHalf), sy = SimMath::mulf32(dir.y, capHalf);
			pos.x = SimMath::addf32(sx, pos.x);
			pos.y = SimMath::addf32(sy, pos.y);
			pos.z = logic.getGroundHeight(pos.x, pos.y);
			Tile t;
			t.pos = pos;
			t.angle = angle;
			t.tmpl = cap;
			t.code = legal(pos, *cap);
			out.tiles.push_back(t);
			out.worstCode = out.worstCode < t.code ? t.code : out.worstCode;
			out.cost += costOf(*cap, hub);
			if ((int)money < out.cost)
			{
				out.worstCode = 7;
				out.tiles.back().code = 7;
			}
			++i;
		}
	}
	else if (!out.tiles.empty())
	{
		// RW 0x796090: the link path (RW 0x793DB9, slot 0x58) is not ported (S-657); the last tile is made legal
		out.tiles.back().code = 0;
	}
	out.endCapPlaced = endCap;
	if (out.tiles.size() > 1)
	{
		for (const Tile &t : out.tiles)
		{
			out.worstCode = out.worstCode <= t.code ? t.code : out.worstCode;
		}
		return true;
	}
	out.tiles.clear();
	return false;
}

bool WallSpan::build(GameLogic &logic, Object &hub, const Coord3D &start, const Coord3D &end, Player &owner, unsigned options, std::vector<ObjectID> *made)
{
	Plan p;
	if (!plan(logic, hub, start, end, options, p) || (p.worstCode != 0 && p.worstCode != 10) || owner.getMoney()->countMoney() < (std::uint32_t)p.cost)
	{
		return false;
	}
	WallHubBehavior *wh = WallHubBehavior::find(hub, options);
	std::vector<ObjectID> ids;
	Object *lastHub = nullptr;
	for (unsigned i = 0; i < p.tiles.size(); ++i)
	{
		const Tile &t = p.tiles[i];
		// RW 0x7952F4: BuildAssistant slot 0x38 with the hub as builder (the structure is made complete, then started again below)
		Object *obj = Construction::buildObjectNow(logic, &hub, *t.tmpl, t.pos, t.angle, owner);
		if (!obj)
		{
			continue;
		}
		ids.push_back(obj->getID());
		obj->setBuilder(&hub);
		if (ActiveBody *body = dynamic_cast<ActiveBody *>(obj->getBodyModule()))
		{
			body->internalChangeHealth(SimMath::fstpDword(SimMath::pc24SubW(1.0, (double)body->getHealth()))); // RW 0x79536C
		}
		GettingBuiltBehavior *gb = dynamic_cast<GettingBuiltBehavior *>(obj->findModule("GettingBuiltBehavior"));
		// lane BUILD-4: the order of RW 0x795425 .. 0x7954B6. First the tile's GettingBuiltBehavior runs its update now (it starts the self-build: percent =
		// health %, the construction conditions) and sleeps StaggeredBuildFactor * index frames - unless the tile is a WALL_HUB (template + 0x11B bit 4) or
		// the plan's worst code (ebp - 0x20) is 10 (RW 0x79544D); the product is a wrapping 32-bit imul (an INI value of 2147483647 is accepted). Only then
		// (RW 0x795466 ..) the percent is set to 0, so a tile waiting for its turn shows 0 % (not its 1-health percent) until its update wakes
		if (gb && wh && !obj->isKindOfName("WALL_HUB") && p.worstCode != 10)
		{
			gb->runNowThenSleep((UnsignedInt)wh->staggeredBuildFactor() * (UnsignedInt)i);
		}
		obj->setConstructionPercent(0.0f);                      // RW 0x79546F: Object + 0x288
		obj->setStatus(87, false);                              // RW 0x795477
		obj->setStatus(OBJECT_STATUS_UNDER_CONSTRUCTION, true); // RW 0x795482
		Construction::setModelConditions(*obj, {}, { "PARTIALLY_CONSTRUCTED", "ACTIVELY_BEING_CONSTRUCTED" }); // RW 0x795487 .. 0x795498: bits 0x44 / 0x45 set
		// RW 0x79549D .. 0x7954B6: when the object has a drawable (Object + 0x84), Drawable::fadeIn(0x8A = 138 client frames) (RW 0x670AA2): client only
		if (ObjectClientHooks *client = logic.clientHooks())
		{
			client->fadeIn(*obj, 138);
		}
		if (obj->isKindOfName("WALL_HUB"))
		{
			lastHub = obj;
		}
		obj->setBuildCostPaid((float)BuildAssistant::calcCostToBuild(*t.tmpl, &owner, &hub, -1)); // RW 0x7953F5
	}
	// RW 0x79541C ff: the hub, the end hub and every tile know each other (GettingBuiltBehavior slot 18)
	Object *endHub = logic.findObjectByID(p.endHub);
	auto link = [&](Object &a, Object *b) {
		GettingBuiltBehavior *gb = dynamic_cast<GettingBuiltBehavior *>(a.findModule("GettingBuiltBehavior"));
		if (gb && b)
		{
			gb->addLinkedPiece(*b);
		}
	};
	if (endHub)
	{
		link(*endHub, &hub);
		link(hub, endHub); // RW 0x795542: the source hub learns the end hub
		for (ObjectID id : ids)
		{
			link(hub, logic.findObjectByID(id)); // RW 0x7955F9..0x79560C: and every tile
		}
	}
	for (ObjectID id : ids)
	{
		Object *tile = logic.findObjectByID(id);
		if (!tile)
		{
			continue;
		}
		if (endHub)
		{
			link(*tile, endHub);
		}
		link(*tile, &hub);
		for (ObjectID other : ids)
		{
			link(*tile, logic.findObjectByID(other));
		}
		link(hub, tile);
	}
	(void)lastHub;
	owner.withdrawMoney((std::uint32_t)p.cost, true); // RW 0x7954D6
	// RW 0x795637 .. 0x795647 (lane END-2): the spend by kind with the first planned tile's template (RW 0x795126(0) -> TheThingFactory by name)
	owner.getScoreKeeper().addMoneySpentByKind(logic, p.tiles.empty() ? nullptr : p.tiles[0].tmpl, p.cost);
	if (made)
	{
		*made = ids;
	}
	return true;
}

void WallSpan::registerModules(ModuleFactory &modules)
{
	modules.bindTypedData<WallHubBehaviorModuleData>("WallHubBehavior", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("WallHubBehavior", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const WallHubBehaviorModuleData *typed = dynamic_cast<const WallHubBehaviorModuleData *>(data);
		if (!typed)
		{
			throw std::logic_error("WallHubBehavior: the module data is not typed");
		}
		return std::make_unique<WallHubBehavior>(thing, typed);
	});
}

std::vector<std::string> WallSpan::stopLines()
{
	return {
		"[S-657] walls: WallHubBehavior and MSG_WALL_HUB_CONSTRUCT_SPAN (RW 0x77C4C3 -> BuildAssistant RW 0x795221 / 0x79586E) are ported for a straight span; not ported: RotWK's legal-build "
		"codes the plan acts on (5 / 6 cliff cap, 8 blocked mid-way, 10 rebuild of a dead equivalent segment: RW 0x797A96 is not read, the port's codes only refuse a tile), the end-hub link "
		"path (RW 0x793DB9 then slot 0x58), the filter of the end-hub search (the port takes the owner's WALL_HUBs), the consumers of the linked-piece list (RW 0x856E1F / 0x856E5C), "
		"CANCEL_NEIGHBORHOOD, WallUpgradeUpdate and CastleUpgrade",
	};
}

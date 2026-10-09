// OpenBFME. GPL-3.0.
// See GameClient/MapObjectDrawables.h for the sources.

#include "GameClient/MapObjectDrawables.h"

#include "Common/AsciiString.h"
#include "Common/Thing/KindOfTokens.h"
#include "GameClient/MapHordeSpawn.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDraw.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DTreeDraw.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>

// RW 0xDA0E68 (generated: GameLogic/BitFlagNames.cpp). Declared here instead of including GameLogic/BitFlags.h, whose
// raw array alias (ModelConditionMask) is another type than Common/ModelState.h's ModelConditionFlags.
extern const char *const TheKindOfNames[];

namespace
{
const float kReaderMinZ = -1000.0f;               // RW 0xBDD420
const float kReaderMaxZ = 12799.8046875f;         // RW 0xC1EEF0
const double kPi = 3.14159265358979323846;

std::string lowerCopy(const std::string &s) { return AsciiStringUtil::lowered(s); }

// RW 0x644FD0: into (-PI, PI] (the loops subtract / add 2 PI; the constants are RW 0xBDD388 / 0xBDD38C / 0xBDD390).
float normalizeAngle(float a)
{
	const float pi = (float)kPi, twoPi = (float)(2.0 * kPi);
	while (a > pi)
	{
		a -= twoPi;
	}
	while (-pi >= a)
	{
		a += twoPi;
	}
	return a;
}

bool isNone(const std::string &s) { return AsciiStringUtil::compareNoCase(s, "NONE") == 0; }

// RW 0xAD14C0 -> RW 0x42F298: Coord2D "X:ax Y:ay" (tokens may be split at the colon).
bool parseCoord2D(const std::vector<std::string> &tokens, float &x, float &y)
{
	std::string joined;
	for (const std::string &t : tokens)
	{
		joined += t + " ";
	}
	bool gotX = false, gotY = false;
	size_t pos = 0;
	while (pos < joined.size())
	{
		const size_t colon = joined.find(':', pos);
		if (colon == std::string::npos)
		{
			break;
		}
		size_t keyStart = colon;
		while (keyStart > pos && joined[keyStart - 1] != ' ')
		{
			--keyStart;
		}
		const std::string key = lowerCopy(joined.substr(keyStart, colon - keyStart));
		size_t valStart = colon + 1;
		while (valStart < joined.size() && joined[valStart] == ' ')
		{
			++valStart;
		}
		size_t valEnd = valStart;
		while (valEnd < joined.size() && joined[valEnd] != ' ')
		{
			++valEnd;
		}
		const float v = (float)std::atof(joined.substr(valStart, valEnd - valStart).c_str());
		if (key == "x")
		{
			x = v;
			gotX = true;
		}
		else if (key == "y")
		{
			y = v;
			gotY = true;
		}
		pos = valEnd;
	}
	return gotX && gotY;
}

// The keys of RW 0x695A06 that change what a drawable shows beyond what this lane applies (S-110).
const char *const kUnportedKeys[] = { "objectInitialHealth", "objectMaxHPs", "objectVeterancy", "objectExperienceLevel", "objectUpgradesList",
	"objectInitialStance", "objectEnabled", "objectPowered", "objectBasePhase" };
} // namespace

const char *MapObjectFateName(MapObjectFate f)
{
	switch (f)
	{
	case MAPOBJ_CULLED_Z: return "culled by the reader (z)";
	case MAPOBJ_ROAD_BRIDGE_POINT: return "road/bridge point (terrain side)";
	case MAPOBJ_NO_TEMPLATE_EMPTY: return "no template: empty name";
	case MAPOBJ_WAYPOINT: return "no template: waypoint";
	case MAPOBJ_GENERIC_AI: return "no template: generic AI object";
	case MAPOBJ_SCORCH: return "no template: scorch decal";
	case MAPOBJ_LIGHT: return "no template: light";
	case MAPOBJ_NO_TEMPLATE_STAR: return "no template: other * name";
	case MAPOBJ_UNRESOLVED: return "UNRESOLVED template";
	case MAPOBJ_SHRUBBERY_OFF: return "shrubbery switched off";
	case MAPOBJ_CLIENT_TREE: return "client-only tree";
	case MAPOBJ_CLIENT_SHRUB: return "client-only shrub";
	case MAPOBJ_CLIENT_PROP: return "client-only prop";
	case MAPOBJ_CLIENT_SOUND: return "optimized sound object";
	case MAPOBJ_OBJECT_BRIDGE: return "bridge object";
	case MAPOBJ_OBJECT: return "object";
	default: return "?";
	}
}

bool MapObjectFateDrawsAnything(MapObjectFate f)
{
	return f == MAPOBJ_CLIENT_TREE || f == MAPOBJ_CLIENT_SHRUB || f == MAPOBJ_CLIENT_PROP || f == MAPOBJ_OBJECT_BRIDGE || f == MAPOBJ_OBJECT;
}

int MapObjectCreation::kindOfIndex(const std::string &name)
{
	return KindOfTokens::indexOf(name);
}

std::vector<std::string> MapObjectCreation::stopLines()
{
	return {
		"[S-110] initial object state: a full object's model condition flags are NIGHT / SNOW (map time of day and weather, objectTime / objectWeather) and nothing else; the rest of what object creation sets (weapon set, damage state from objectInitialHealth / objectMaxHPs, veterancy, upgrades, power, construction) is not derived, and the owner's house colour is the SidesList colour, not a lobby choice; the Lua OnCreated creation hooks of a full object's AILuaEventsList (scriptevents.xml, scripts.lua) are run by MapObjectRuntime::build on the real Lua script engine, after the object's draw modules exist (this loop only counts which handlers a template has: the report counts them per function, per template and per permanently hidden sub object); what a handler does that needs an engine callee that does not exist yet (granting an upgrade: ObjectGrantUpgrade -> S-124) is reported and not done, the order register-then-OnCreated is inferred; the logic RNG draw of sendObjectCreated (GetGameLogicRandomValue(1, 999), RW 0x628892) IS made, by MapObjectRuntime::build, but in drawable-list order and not in retail's order (the bridge / wall pass creates its objects first), which MapObjectRuntime reports as a runtime [S-110] line whenever such an object follows a normal one; a child list's OnCreated replaces the one it inherits (RW 0x733FAF)",
		"[S-111] draw modules W3DTreeDraw: the dynamic tree behaviour (push aside, topple, sink, morph, fade) and the FXList / tree-template names are stored but not run; the client-only tree, shrub and prop path draws the first draw module's model only",
		"[S-112] W3DTruckDraw / W3DSailModelDraw / W3DQuadrupedDraw / W3DTankDraw / W3DSupplyDraw: the base model is drawn through the model draw runtime, the variant's own motion (tires, treads, sail, feet) is not",
		"[S-113] effect draw classes (Light, Streak, Buff, Tornado, Laser, Debris, ProjectileStream, BoatWake, Rope) are not drawn",
		"[S-114] GameData switches the object loop reads that have no INI field or no ported GameData block: the trees-off byte (GlobalData +0xEC, treated as off), the low-detail tree replacement templates (GlobalData +0xD8 / +0xDC..+0xE4, treated as high detail), and the draw module LOD gate (RW 0xDE4364 +0x24, treated as every module created); ForceModelsToFollowTimeOfDay / Weather are read from gamedata.ini by a name-level scan",
		"[S-115] static draw fields: W3DFloorDraw StartHidden (the floor is not drawn at map start; when retail shows it is not recovered) and the fields that change how a W3DPropDraw / W3DFloorDraw model is drawn are stored, not applied: WeatherTexture (the snowy / normal texture swap), ForceToBack (draw order), StaticModelLODMode, and DistanceFog = No (the model takes the scene fog); the report counts the placed models per field (MapRuntimeReport::ignoredDrawFields)",
		"[S-116] map.ini: only the Object / ChildObject / ObjectReskin blocks of a map.ini are applied (load type 2); its other blocks are recorded by the stub registry and not applied",
		"[S-117] scorch decals (Scorch objects, 412 in the corpus) are terrain decals the terrain visual draws; they are not drawn",
		"[S-118] horde members: the members of a horde object's InitialPayload stand on HordeContainCore's formation slots at the horde's position and angle (RW 0x877751 / 0x873F30 / 0x875847); that they are created with the horde in payload order and take the free slots one by one, that a member faces its horde's angle, and that banner carriers, leaders and the minimum horde size are ignored is inference; RandomOffset slot draws use a fixed-seed generator, not the map start's logic RNG",
		"[S-119] house colour: the owner's colour is the SidesList playerColor, or when the map sets none the PreferredColor of its faction's PlayerTemplate (templates that define a StartingBuilding only: Civilian, Neutral and Observer have none; retail takes the lobby colour of the slot), and a model draws it as base = mix(base, base * team, house texture alpha) where housecolor.ini names a texture for the stage 0 texture (the HC_ textures are 64 x 64 images whose alpha marks the team region). The combine is a HYPOTHESIS: defaultw3d.fxo has HouseColorEnable / HouseColorTexture parameters (the engine sets the texture, RW 0x54C0D2) but none of its pixel shaders samples a house colour texture, so the real combine is not recoverable from the effect (S-022)",
	};
}

bool MapObjectGameData::scanText(const std::string &text, MapObjectOptions &out, std::string *error)
{
	bool haveTod = false, haveWeather = false, inBlock = false, doneBlock = false;
	size_t pos = 0;
	while (pos <= text.size() && !doneBlock)
	{
		size_t nl = text.find('\n', pos);
		if (nl == std::string::npos)
		{
			nl = text.size();
		}
		std::string line = text.substr(pos, nl - pos);
		pos = nl + 1;
		const size_t semi = line.find(';');
		if (semi != std::string::npos)
		{
			line.erase(semi);
		}
		size_t a = 0, b = line.size();
		while (a < b && std::isspace((unsigned char)line[a])) ++a;
		while (b > a && std::isspace((unsigned char)line[b - 1])) --b;
		line = line.substr(a, b - a);
		if (line.empty())
		{
			continue;
		}
		if (!inBlock)
		{
			inBlock = AsciiStringUtil::compareNoCase(line, "GameData") == 0;
			continue;
		}
		if (AsciiStringUtil::compareNoCase(line, "End") == 0)
		{
			doneBlock = true;
			continue;
		}
		const size_t eq = line.find('=');
		if (eq == std::string::npos)
		{
			continue;
		}
		std::string key = line.substr(0, eq), val = line.substr(eq + 1);
		while (!key.empty() && std::isspace((unsigned char)key.back())) key.pop_back();
		size_t v0 = 0;
		while (v0 < val.size() && std::isspace((unsigned char)val[v0])) ++v0;
		val = val.substr(v0);
		while (!val.empty() && std::isspace((unsigned char)val.back())) val.pop_back();
		const bool yes = AsciiStringUtil::compareNoCase(val, "Yes") == 0 || val == "1" || AsciiStringUtil::compareNoCase(val, "True") == 0;
		if (AsciiStringUtil::compareNoCase(key, "ForceModelsToFollowTimeOfDay") == 0)
		{
			out.forceModelsToFollowTimeOfDay = yes;
			haveTod = true;
		}
		else if (AsciiStringUtil::compareNoCase(key, "ForceModelsToFollowWeather") == 0)
		{
			out.forceModelsToFollowWeather = yes;
			haveWeather = true;
		}
	}
	if (!haveTod || !haveWeather)
	{
		if (error)
		{
			*error = std::string("gamedata.ini: no ") + (haveTod ? "ForceModelsToFollowWeather" : "ForceModelsToFollowTimeOfDay") + " in the GameData block";
		}
		return false;
	}
	return true;
}

bool MapObjectGameData::scanPlayerTemplates(const std::string &text, MapObjectOptions &out, std::string *error)
{
	std::string name;
	bool inBlock = false, plays = false, haveColor = false;
	std::uint32_t color = 0;
	size_t templates = 0;
	size_t pos = 0;
	auto flush = [&]() {
		if (inBlock && plays && haveColor)
		{
			out.factionColors[name] = color;
		}
		inBlock = false;
		plays = false;
		haveColor = false;
	};
	while (pos <= text.size())
	{
		size_t nl = text.find('\n', pos);
		if (nl == std::string::npos)
		{
			nl = text.size();
		}
		std::string line = text.substr(pos, nl - pos);
		pos = nl + 1;
		const size_t semi = line.find(';');
		if (semi != std::string::npos)
		{
			line.erase(semi);
		}
		size_t a = 0, b = line.size();
		while (a < b && std::isspace((unsigned char)line[a])) ++a;
		while (b > a && std::isspace((unsigned char)line[b - 1])) --b;
		line = line.substr(a, b - a);
		if (line.empty())
		{
			continue;
		}
		if (!inBlock)
		{
			if (line.size() > 13 && AsciiStringUtil::compareNoCase(line.substr(0, 14), "PlayerTemplate") == 0 && std::isspace((unsigned char)line[14 < line.size() ? 14 : 0]))
			{
				name = line.substr(14);
				const size_t n0 = name.find_first_not_of(" \t");
				name = n0 == std::string::npos ? std::string() : name.substr(n0);
				while (!name.empty() && std::isspace((unsigned char)name.back())) name.pop_back();
				inBlock = true;
				++templates;
			}
			continue;
		}
		if (AsciiStringUtil::compareNoCase(line, "End") == 0)
		{
			flush();
			continue;
		}
		const size_t eq = line.find('=');
		if (eq == std::string::npos)
		{
			continue;
		}
		std::string key = line.substr(0, eq), val = line.substr(eq + 1);
		while (!key.empty() && std::isspace((unsigned char)key.back())) key.pop_back();
		size_t v0 = val.find_first_not_of(" \t");
		val = v0 == std::string::npos ? std::string() : val.substr(v0);
		if (AsciiStringUtil::compareNoCase(key, "StartingBuilding") == 0)
		{
			plays = !val.empty() && AsciiStringUtil::compareNoCase(val, "None") != 0; // a faction that starts with a building is a real faction (not Civilian / Neutral / Observer)
		}
		else if (AsciiStringUtil::compareNoCase(key, "PreferredColor") == 0)
		{
			int r = 0, g = 0, bl = 0;
			if (std::sscanf(val.c_str(), "R:%d G:%d B:%d", &r, &g, &bl) == 3)
			{
				color = ((std::uint32_t)(r & 255) << 16) | ((std::uint32_t)(g & 255) << 8) | (std::uint32_t)(bl & 255);
				haveColor = true;
			}
		}
	}
	if (templates == 0)
	{
		if (error)
		{
			*error = "playertemplate.ini: no PlayerTemplate block";
		}
		return false;
	}
	return true;
}

bool MapObjectGameData::loadPlayerTemplates(ArchiveFileSystem &fs, MapObjectOptions &out, std::string *error)
{
	std::vector<std::uint8_t> bytes;
	if (!fs.readFile("data\\ini\\playertemplate.ini", bytes, error))
	{
		return false;
	}
	return scanPlayerTemplates(std::string(bytes.begin(), bytes.end()), out, error);
}

bool MapObjectGameData::load(ArchiveFileSystem &fs, MapObjectOptions &out, std::string *error)
{
	std::vector<std::uint8_t> bytes;
	if (!fs.readFile("data\\ini\\gamedata.ini", bytes, error))
	{
		return false;
	}
	return scanText(std::string(bytes.begin(), bytes.end()), out, error);
}

namespace
{
// Per-template information (the retail code reads these fields of the ThingTemplate at +0x108, +0x4B0, +0x4F0, +0x5F5, +0xA8).
MapTemplateInfo buildTemplateInfo(const ThingTemplate *tmpl, const CreationScriptData &scripts, std::vector<std::string> &errors)
{
	MapTemplateInfo info;
	info.tmpl = tmpl;
	info.name = tmpl->getName();
	if (const FieldValue *k = tmpl->findField("KindOf"))
	{
		if (const RawTokens *raw = std::get_if<RawTokens>(k))
		{
			std::string err;
			KindOfTokens::parseTemplate(*tmpl, info.kindOf, err); // lane BUILD-3: the inherited rows first
			if (!err.empty())
			{
				info.kindOfError = true;
				info.kindOfErrorText = err;
				std::string toks;
				for (const std::string &t : raw->tokens)
				{
					toks += (toks.empty() ? "" : " ") + t;
				}
				errors.push_back("template " + info.name + ": " + err + " (KindOf = " + toks + " at " + raw->line.file + ":" + std::to_string(raw->line.sourceLine) + ")");
			}
		}
	}
	info.isBridge = false;
	if (const FieldValue *v = tmpl->findField("IsBridge"))
	{
		if (const bool *b = std::get_if<bool>(v))
		{
			info.isBridge = *b;
		}
	}
	if (const FieldValue *v = tmpl->findField("Scale"))
	{
		if (const float *f = std::get_if<float>(v))
		{
			info.scale = *f;
		}
	}
	if (const FieldValue *v = tmpl->findField("FenceWidth"))
	{
		if (const float *f = std::get_if<float>(v))
		{
			info.fenceWidth = *f;
		}
	}
	if (const FieldValue *v = tmpl->findField("GeometryRotationAnchorOffset"))
	{
		if (const RawTokens *raw = std::get_if<RawTokens>(v))
		{
			float x = 0, y = 0;
			if (parseCoord2D(raw->tokens, x, y))
			{
				info.anchorX = x;
				info.anchorY = y;
			}
			else
			{
				errors.push_back("template " + info.name + ": GeometryRotationAnchorOffset is not X: Y:");
			}
		}
	}
	info.shrubbery = info.kindOf_test(MapObjectCreation::kindOfIndex("SHRUBBERY"));
	info.tree = info.kindOf_test(MapObjectCreation::kindOfIndex("TREE"));
	info.shrub = info.kindOf_test(MapObjectCreation::kindOfIndex("SHRUB"));
	info.optimizedProp = info.kindOf_test(MapObjectCreation::kindOfIndex("OPTIMIZED_PROP"));
	info.clearedByBuild = info.kindOf_test(MapObjectCreation::kindOfIndex("CLEARED_BY_BUILD"));
	info.optimizedSound = info.kindOf_test(MapObjectCreation::kindOfIndex("OPTIMIZED_SOUND"));
	info.walkOnTopOfWall = info.kindOf_test(MapObjectCreation::kindOfIndex("WALK_ON_TOP_OF_WALL"));
	info.canCastReflections = info.kindOf_test(MapObjectCreation::kindOfIndex("CAN_CAST_REFLECTIONS"));

	for (const ThingTemplate::Nugget &n : tmpl->drawModules().nuggets())
	{
		MapDrawModule m;
		m.className = n.name;
		m.tag = n.tag;
		m.data = n.data.get();
		if (const W3DDrawClassInfo *ci = W3DDrawModules::find(n.name))
		{
			m.kind = ci->kind;
		}
		else
		{
			m.kind = W3D_DRAWKIND_NOT_DRAWN;
			errors.push_back("template " + info.name + ": draw module class '" + n.name + "' is not a registered draw class");
		}
		info.draws.push_back(m);
	}
	info.aiEventLists = MapCreationHooks::templateEventLists(*tmpl);
	if (scripts.loaded)
	{
		info.creationHooks = MapCreationHooks::templateHooks(*tmpl, scripts, &errors);
	}
	return info;
}

// Model the module shows for the given condition flags; "" with a reason when it shows none.
void resolveDrawModel(MapDrawModule &m, const ModelConditionFlags &flags, const std::string &templateName, std::vector<std::string> &errors)
{
	m.model.clear();
	m.notDrawnReason.clear();
	switch (m.kind)
	{
	case W3D_DRAWKIND_MODEL:
	{
		const W3DModelDrawModuleData *d = dynamic_cast<const W3DModelDrawModuleData *>(m.data);
		if (!d)
		{
			m.notDrawnReason = "module data of " + m.className + " is not typed";
			errors.push_back("template " + templateName + ": " + m.notDrawnReason);
			return;
		}
		const ModelConditionInfo *state = d->findBestInfo(flags);
		if (!state)
		{
			m.notDrawnReason = "no model condition state matches";
			errors.push_back("template " + templateName + ": " + m.className + ": " + m.notDrawnReason);
			return;
		}
		const std::string &name = state->modelName();
		if (name.empty() || isNone(name))
		{
			m.notDrawnReason = "the state's model is empty or NONE";
			return;
		}
		m.model = name;
		return;
	}
	case W3D_DRAWKIND_TREE:
	{
		const W3DTreeDrawModuleData *d = dynamic_cast<const W3DTreeDrawModuleData *>(m.data);
		if (!d)
		{
			m.notDrawnReason = "W3DTreeDraw data is not typed";
			errors.push_back("template " + templateName + ": " + m.notDrawnReason);
			return;
		}
		if (d->m_modelName.empty() || isNone(d->m_modelName))
		{
			m.notDrawnReason = "W3DTreeDraw without a ModelName";
			return;
		}
		m.model = d->m_modelName;
		return;
	}
	case W3D_DRAWKIND_PROP:
	case W3D_DRAWKIND_FLOOR:
	{
		const W3DPropDrawModuleData *d = dynamic_cast<const W3DPropDrawModuleData *>(m.data);
		if (!d)
		{
			m.notDrawnReason = m.className + " data is not typed";
			errors.push_back("template " + templateName + ": " + m.notDrawnReason);
			return;
		}
		if (const W3DFloorDrawModuleData *fl = dynamic_cast<const W3DFloorDrawModuleData *>(d))
		{
			if (fl->m_startHidden)
			{
				m.notDrawnReason = "W3DFloorDraw StartHidden (S-115)";
				return;
			}
			for (const ModelConditionFlags &hide : fl->m_hideIfModelConditions)
			{
				if (hide.any() && flags.testForAll(hide))
				{
					m.notDrawnReason = "W3DFloorDraw HideIfModelConditions";
					return;
				}
			}
		}
		if (d->m_modelName.empty() || isNone(d->m_modelName))
		{
			m.notDrawnReason = m.className + " without a ModelName";
			return;
		}
		m.model = d->m_modelName;
		return;
	}
	case W3D_DRAWKIND_NOTHING:
		m.notDrawnReason = "W3DDefaultDraw draws nothing";
		return;
	case W3D_DRAWKIND_NOT_DRAWN:
	default:
		m.notDrawnReason = m.className + " is an effect draw (S-113)";
		return;
	}
}
} // namespace

// LOGIC-1: the live Drawable (GameClient/Drawable.h) resolves the model of a draw module for its model condition flags with the same rules
void MapObjectCreation::resolveDrawModel(MapDrawModule &m, const ModelConditionFlags &flags, const std::string &templateName, std::vector<std::string> &errors)
{
	::resolveDrawModel(m, flags, templateName, errors);
}

namespace
{
// The sides of a map as the SidesList names them.
void collectSides(const LoadedMap &map, const MapObjectOptions &options, std::vector<MapSidePlayer> &sides, MapObjectReport &rep)
{
	for (const SidesInfo &s : map.sides.sides)
	{
		MapSidePlayer p;
		p.name = s.dict.getAsciiString("playerName");
		bool exists = false;
		const std::int32_t color = s.dict.getInt("playerColor", &exists);
		p.isHuman = s.dict.getBool("playerIsHuman");
		p.faction = s.dict.getAsciiString("playerFaction");
		if (exists)
		{
			p.hasColor = true;
			p.colorRGB = (std::uint32_t)color & 0xFFFFFFu;
			p.colorSource = MapSidePlayer::COLOR_MAP;
			++rep.sidesWithMapColor;
		}
		else if (options.factionColors.count(p.faction))
		{
			p.hasColor = true;
			p.colorRGB = options.factionColors.at(p.faction);
			p.colorSource = MapSidePlayer::COLOR_FACTION;
			++rep.sidesWithFactionColor;
		}
		else
		{
			++rep.sidesWithoutColor;
		}
		sides.push_back(p);
	}
}

// originalOwner is "Player/team" (RotWK) or a plain team name (ZH). Returns the side index or -1.
int ownerSide(const LoadedMap &map, const std::vector<MapSidePlayer> &sides, const std::string &owner, std::string &problem)
{
	std::string player;
	const size_t slash = owner.find('/');
	if (slash != std::string::npos)
	{
		player = owner.substr(0, slash);
	}
	else
	{
		// a team name: its owner is teamOwner in the Teams chunk
		bool found = false;
		for (const Dict &t : map.sides.teams)
		{
			if (t.getAsciiString("teamName") == owner)
			{
				player = t.getAsciiString("teamOwner");
				found = true;
				break;
			}
		}
		if (!found)
		{
			problem = "owner '" + owner + "' names no team";
			return -1;
		}
	}
	for (size_t i = 0; i < sides.size(); ++i)
	{
		if (sides[i].name == player)
		{
			return (int)i;
		}
	}
	problem = "owner side '" + player + "' is not in the SidesList";
	return -1;
}

void composeRotationBasis(MapObjectDrawable &d, float angle, bool align, const Coord3D &normal)
{
	if (!align)
	{
		const float c = (float)std::cos((double)angle), s = (float)std::sin((double)angle);
		// columns X = (c, s, 0), Y = (-s, c, 0), Z = (0, 0, 1); stored row major
		const float m[9] = { c, -s, 0, s, c, 0, 0, 0, 1 };
		std::copy(m, m + 9, d.basis);
		return;
	}
	// RW 0x67D208: X = normalize((cos, sin, -(nx cos + ny sin) / nz)) (z left 0 when nz == 0), Y = normalize(n x X), Z = n.
	const double c = std::cos((double)angle), s = std::sin((double)angle);
	double xz = 0.0;
	if (normal.z != 0.0f)
	{
		xz = -((double)normal.y * s + (double)normal.x * c) / (double)normal.z;
	}
	double xl = std::sqrt(c * c + s * s + xz * xz);
	const double X[3] = { c / xl, s / xl, xz / xl };
	double Y[3] = { (double)normal.y * X[2] - (double)normal.z * X[1], (double)normal.z * X[0] - (double)normal.x * X[2],
		(double)normal.x * X[1] - (double)normal.y * X[0] };
	const double yl = std::sqrt(Y[0] * Y[0] + Y[1] * Y[1] + Y[2] * Y[2]);
	if (yl > 0.0)
	{
		Y[0] /= yl;
		Y[1] /= yl;
		Y[2] /= yl;
	}
	const double N[3] = { normal.x, normal.y, normal.z };
	for (int r = 0; r < 3; ++r)
	{
		d.basis[r * 3 + 0] = (float)X[r];
		d.basis[r * 3 + 1] = (float)Y[r];
		d.basis[r * 3 + 2] = (float)N[r];
	}
}

// S-110: the OnCreated hooks of a full object's template that are not run
void recordCreationHooks(MapObjectReport &rep, const MapTemplateInfo &info)
{
	if (info.creationHooks.empty())
	{
		return;
	}
	++rep.creationHookObjects;
	std::set<std::string> functions, hides, shows;
	for (const MapCreationHook &h : info.creationHooks)
	{
		std::string key = info.name + ": " + h.eventList + " ->";
		for (size_t i = 0; i < h.functions.size(); ++i)
		{
			key += (i ? ", " : " ") + h.functions[i];
			functions.insert(h.functions[i]);
		}
		++rep.creationHooksByTemplate[key];
		for (const std::string &x : h.permanentHides) hides.insert(lowerCopy(x)); // sub object names match without case
		for (const std::string &x : h.permanentShows) shows.insert(lowerCopy(x));
	}
	for (const std::string &f : functions)
	{
		++rep.creationHooksByFunction[f];
	}
	for (const std::string &h : hides)
	{
		++rep.creationHookHides[h];
	}
	for (const std::string &h : shows)
	{
		++rep.creationHookShows[h];
	}
}
} // namespace

void MapObjectCreation::build(const LoadedMap &map, const std::string &mapName, ThingFactory &things, const TerrainLogic &terrain,
	const MapObjectOptions &options, MapObjectDrawables &out)
{
	out = MapObjectDrawables();
	MapObjectReport &rep = out.report;
	rep.map = mapName;
	rep.objects = map.chunks.objects.size();
	rep.stops = stopLines();
	rep.creationHooksScanned = options.creationScripts.loaded;
	collectSides(map, options, out.sides, rep);

	// ObjectsList chunk version: <= 2 forces z to 0 in the reader (RW 0x70F403)
	int objectsVersion = 3;
	{
		auto it = map.versions.find("/ObjectsList");
		if (it != map.versions.end() && !it->second.empty())
		{
			objectsVersion = *it->second.begin();
		}
	}
	const bool night = map.chunks.hasGlobalLighting && map.chunks.lighting.timeOfDay == 4;
	const bool snowy = map.chunks.hasWorldInfo && map.chunks.worldInfo.getInt("weather") == 1;

	// case-insensitive index of the template names, to explain a name that differs from the INI only in case (retail's find is case sensitive)
	std::map<std::string, std::string> lowerNames;
	bool lowerNamesBuilt = false;

	std::map<std::string, const ThingTemplate *> resolved; // object name -> template (nullptr = none)

	for (size_t i = 0; i < map.chunks.objects.size(); ++i)
	{
		const MapObject &o = map.chunks.objects[i];
		MapObjectRecord rec;
		rec.objectIndex = i;
		rec.templateName = o.m_objectName;
		auto finish = [&](MapObjectFate fate, const std::string &detail) {
			rec.fate = fate;
			rec.detail = detail;
			++rep.byFate[fate];
			out.records.push_back(rec);
		};

		// ---- RW 0x70F403: the reader --------------------------------------------------------------------------------------
		const float z = objectsVersion <= 2 ? 0.0f : o.m_location.z;
		if (z < kReaderMinZ || z > kReaderMaxZ)
		{
			finish(MAPOBJ_CULLED_Z, "z " + std::to_string(z));
			++rep.notDrawnByReason["reader drops objects with z outside [-1000, 12799.8046875]"];
			continue;
		}
		// ---- RW 0x62DCE4 -------------------------------------------------------------------------------------------------
		if (o.getFlag(FLAG_ROAD_FLAGS | FLAG_BRIDGE_FLAGS))
		{
			finish(MAPOBJ_ROAD_BRIDGE_POINT, "");
			++rep.notDrawnByReason["road and bridge points are built by the terrain side"];
			continue;
		}
		const ThingTemplate *tmpl = nullptr;
		{
			auto it = resolved.find(o.m_objectName);
			if (it != resolved.end())
			{
				tmpl = it->second;
			}
			else
			{
				tmpl = o.m_objectName.empty() ? nullptr : things.findTemplate(o.m_objectName); // case sensitive (RW 0x6D12E5)
				resolved[o.m_objectName] = tmpl;
			}
		}
		if (!tmpl)
		{
			const Dict &p = o.m_properties;
			bool ex = false;
			MapObjectFate fate;
			std::string why;
			p.getInt("waypointID", &ex);
			if (ex)
			{
				fate = MAPOBJ_WAYPOINT;
				why = "waypointID";
			}
			else if (p.getInt("GenericAIObjectID", &ex), ex)
			{
				fate = MAPOBJ_GENERIC_AI;
				why = "GenericAIObjectID";
			}
			else if (p.getInt("scorchType", &ex), ex)
			{
				fate = MAPOBJ_SCORCH;
				why = "scorchType";
			}
			else if (p.getReal("lightHeightAboveTerrain", &ex), ex)
			{
				fate = MAPOBJ_LIGHT;
				why = "lightHeightAboveTerrain";
			}
			else if (o.m_objectName.empty())
			{
				fate = MAPOBJ_NO_TEMPLATE_EMPTY;
				why = "empty name";
			}
			else if (o.m_objectName[0] == '*')
			{
				fate = MAPOBJ_NO_TEMPLATE_STAR;
				why = "* name";
			}
			else
			{
				fate = MAPOBJ_UNRESOLVED;
				if (!lowerNamesBuilt)
				{
					for (const ThingTemplate *t : things.templates())
					{
						lowerNames.emplace(lowerCopy(t->getName()), t->getName());
					}
					lowerNamesBuilt = true;
				}
				auto it = lowerNames.find(lowerCopy(o.m_objectName));
				if (it != lowerNames.end())
				{
					why = "differs only in case from '" + it->second + "' (retail's lookup is case sensitive)";
					++rep.caseMismatch[o.m_objectName];
				}
				else
				{
					why = "no template of that name";
				}
				++rep.unresolved[o.m_objectName];
				rep.errors.push_back("object " + std::to_string(i) + " '" + o.m_objectName + "': " + why);
			}
			finish(fate, why);
			++rep.notDrawnByReason[std::string(MapObjectFateName(fate))];
			continue;
		}
		const ThingTemplate *finalTmpl = tmpl->getFinalOverride();
		auto ti = out.templates.find(finalTmpl);
		if (ti == out.templates.end())
		{
			ti = out.templates.emplace(finalTmpl, buildTemplateInfo(finalTmpl, options.creationScripts, rep.errors)).first;
		}
		const MapTemplateInfo &info = ti->second;

		// SHRUBBERY with the trees-off switch (the bridge pass comes first in retail; a bridge is not shrubbery)
		const bool bridgePass = info.isBridge || info.walkOnTopOfWall;
		if (info.shrubbery && !options.useTrees && !bridgePass)
		{
			finish(MAPOBJ_SHRUBBERY_OFF, "");
			++rep.notDrawnByReason["shrubbery switched off"];
			continue;
		}

		// ---- location, ground height, angle ------------------------------------------------------------------------------
		// RW 0x70EE32 / 0xAD1A60: the template's GeometryRotationAnchorOffset moves the location (cos / sin of the MapObject's angle at +0x1C,
		// which the constructor normalised with RW 0x644FD0)
		float lx = o.m_location.x, ly = o.m_location.y;
		bool moved = false;
		if (info.anchorX != 0.0f || info.anchorY != 0.0f)
		{
			const float na = normalizeAngle(o.m_angle);
			const float c = (float)std::cos((double)na), s = (float)std::sin((double)na);
			lx = lx + info.anchorX * c + info.anchorY * s;
			ly = ly + info.anchorY * c + info.anchorX * s;
			moved = true;
		}
		bool alignExists = false;
		const bool align = o.m_properties.getBool("alignToTerrain", &alignExists) && alignExists;
		Coord3D normal = { 0.0f, 0.0f, 1.0f };
		const float ground = terrain.getGroundHeight(lx, ly, align ? &normal : nullptr);

		MapObjectDrawable d;
		d.objectIndex = i;
		d.info = &info;
		d.position = { lx, ly, z + ground };
		d.angle = normalizeAngle(o.m_angle);
		d.alignToTerrain = align;
		d.normal = normal;
		composeRotationBasis(d, d.angle, align, normal);
		d.scale = info.scale;
		bool protoExists = false;
		const float protoScale = o.m_properties.getReal("objectPrototypeScale", &protoExists);
		if (protoExists)
		{
			d.scale = info.scale * protoScale;
		}

		// ---- which path ------------------------------------------------------------------------------------------------
		const bool prop = info.optimizedProp || (info.clearedByBuild && info.fenceWidth == 0.0f);
		bool nameExists = false;
		const std::string objName = o.m_properties.getAsciiString("objectName", &nameExists);
		const bool soundOnly = info.optimizedSound && !(nameExists && !objName.empty());
		MapObjectFate fate;
		if (bridgePass)
		{
			fate = info.isBridge ? MAPOBJ_OBJECT_BRIDGE : MAPOBJ_OBJECT;
		}
		else if (prop || soundOnly || info.tree || info.shrub)
		{
			fate = info.tree ? MAPOBJ_CLIENT_TREE : info.shrub ? MAPOBJ_CLIENT_SHRUB : soundOnly ? MAPOBJ_CLIENT_SOUND : MAPOBJ_CLIENT_PROP;
			if (fate == MAPOBJ_CLIENT_TREE && !options.useTrees)
			{
				// RW 0x62E433: a TREE is added only when the trees switch is on (RW 0x62E3E7 .. 0x62E454)
				finish(MAPOBJ_SHRUBBERY_OFF, "TREE with the trees switch off");
				++rep.notDrawnByReason["shrubbery switched off"];
				continue;
			}
		}
		else
		{
			fate = MAPOBJ_OBJECT;
		}
		d.fate = fate;

		// ---- initial model condition flags -----------------------------------------------------------------------------
		const bool clientOnly = fate == MAPOBJ_CLIENT_TREE || fate == MAPOBJ_CLIENT_SHRUB || fate == MAPOBJ_CLIENT_PROP || fate == MAPOBJ_CLIENT_SOUND;
		if (clientOnly)
		{
			// DONOR ZH W3DTerrainVisual::addProp: SNOW for a snowy map, NIGHT at night; the object keys are not read on this path
			if (snowy)
			{
				d.flags.set(ModelCondition::indexOf("SNOW"));
			}
			if (night)
			{
				d.flags.set(ModelCondition::indexOf("NIGHT"));
			}
		}
		else
		{
			const int nightBit = ModelCondition::indexOf("NIGHT"), snowBit = ModelCondition::indexOf("SNOW");
			if (options.forceModelsToFollowTimeOfDay && night)
			{
				d.flags.set(nightBit);
			}
			if (options.forceModelsToFollowWeather && snowy)
			{
				d.flags.set(snowBit);
			}
			bool ex = false;
			const int objectTime = o.m_properties.getInt("objectTime", &ex);
			if (ex)
			{
				if (objectTime == 1)
				{
					d.flags.clearBit(nightBit);
				}
				else if (objectTime == 2)
				{
					d.flags.set(nightBit);
				}
			}
			const int objectWeather = o.m_properties.getInt("objectWeather", &ex);
			if (ex)
			{
				if (objectWeather == 1)
				{
					d.flags.clearBit(snowBit);
				}
				else if (objectWeather == 2)
				{
					d.flags.set(snowBit);
				}
			}
			d.drawsInMirror = o.getFlag(FLAG_DRAWS_IN_MIRROR) || info.canCastReflections;
			for (const char *key : kUnportedKeys)
			{
				if (o.m_properties.known(key))
				{
					++rep.unportedKeys[key];
				}
			}
			// the keys with a clear default: how many objects actually differ from it (the default is what object creation gives anyway)
			{
				bool ex2 = false;
				const int health = o.m_properties.getInt("objectInitialHealth", &ex2);
				if (ex2 && health != 100)
				{
					++rep.unportedKeys["objectInitialHealth != 100"];
				}
				const bool enabled = o.m_properties.getBool("objectEnabled", &ex2);
				if (ex2 && !enabled)
				{
					++rep.unportedKeys["objectEnabled = false"];
				}
				const bool powered = o.m_properties.getBool("objectPowered", &ex2);
				if (ex2 && !powered)
				{
					++rep.unportedKeys["objectPowered = false"];
				}
			}
			for (const Dict::Pair &pr : o.m_properties.pairs())
			{
				if (pr.key.rfind("objectGrantUpgrade", 0) == 0)
				{
					++rep.unportedKeys["objectGrantUpgrade*"];
					break;
				}
			}
		}

		// ---- owner --------------------------------------------------------------------------------------------------------
		if (!clientOnly)
		{
			d.owner = o.m_properties.getAsciiString("originalOwner");
			std::string problem;
			d.sideIndex = ownerSide(map, out.sides, d.owner, problem);
			d.ownerResolved = d.sideIndex >= 0;
			if (!d.ownerResolved)
			{
				++rep.unownedSides;
				++rep.ownerProblems[problem];
			}
		}

		// ---- the draw modules ---------------------------------------------------------------------------------------------
		std::vector<MapDrawModule> draws = info.draws;
		if (clientOnly && draws.size() > 1)
		{
			draws.resize(1); // the client-only path reads the template's first draw module only
		}
		bool anyModel = false;
		for (MapDrawModule &m : draws)
		{
			resolveDrawModel(m, d.flags, info.name, rep.errors);
			if (!m.model.empty())
			{
				anyModel = true;
				++rep.drawnModels;
				++rep.byDrawClass[m.className];
			}
			else
			{
				++rep.notDrawnByReason[m.notDrawnReason.empty() ? std::string("no draw module") : m.notDrawnReason];
			}
		}
		if (draws.empty())
		{
			++rep.notDrawnByReason["template has no draw module"];
		}
		if (fate == MAPOBJ_CLIENT_SOUND)
		{
			anyModel = false;
			++rep.notDrawnByReason["optimized sound object (audio only)"];
		}
		++rep.byFate[fate];
		rec.fate = fate;
		rec.templateName = info.name;
		out.records.push_back(rec);
		++rep.byTemplate[info.name];
		if (moved)
		{
			++rep.movedByAnchor;
		}
		if (align)
		{
			++rep.alignedToTerrain;
		}
		if (protoExists)
		{
			++rep.withPrototypeScale;
		}
		if (d.flags.test(ModelCondition::indexOf("NIGHT")))
		{
			++rep.nightFlagged;
		}
		if (d.flags.test(ModelCondition::indexOf("SNOW")))
		{
			++rep.snowFlagged;
		}
		if (anyModel)
		{
			++rep.drawables;
		}
		d.draws = draws;
		if (!clientOnly)
		{
			recordCreationHooks(rep, info);
		}
		const bool mayBeHorde = fate == MAPOBJ_OBJECT || fate == MAPOBJ_OBJECT_BRIDGE;
		const Coord3D hordePos = d.position;
		const float hordeAngle = d.angle;
		const ModelConditionFlags hordeFlags = d.flags;
		const int hordeSide = d.sideIndex;
		const std::string hordeOwner = d.owner;
		const bool hordeOwnerResolved = d.ownerResolved;
		out.drawables.push_back(std::move(d));

		// ---- hordes: the members of the InitialPayload stand on the formation slots (S-118) -------------------------------
		if (mayBeHorde)
		{
			HordeSpawnResult hs;
			MapHordeSpawn::spawn(*finalTmpl, things, hordePos, hordeAngle, hs);
			for (const std::string &e : hs.errors)
			{
				rep.errors.push_back("object " + std::to_string(i) + ": " + e);
			}
			if (hs.isHorde)
			{
				++rep.hordes;
				rep.hordeSlots += hs.slots;
				rep.hordePayload += hs.payload;
				rep.hordeUnplaced += hs.unplaced;
				rep.hordeRandomOffset += hs.randomOffsetSlots ? 1 : 0;
				for (const HordeMemberSpawn &m : hs.members)
				{
					const ThingTemplate *mt = things.findTemplate(m.templateName);
					if (!mt)
					{
						rep.errors.push_back("object " + std::to_string(i) + ": horde member template '" + m.templateName + "' does not exist");
						++rep.unresolved[m.templateName];
						continue;
					}
					const ThingTemplate *mFinal = mt->getFinalOverride();
					auto mi = out.templates.find(mFinal);
					if (mi == out.templates.end())
					{
						mi = out.templates.emplace(mFinal, buildTemplateInfo(mFinal, options.creationScripts, rep.errors)).first;
					}
					const MapTemplateInfo &minfo = mi->second;
					MapObjectDrawable md;
					md.objectIndex = i;
					md.fate = MAPOBJ_OBJECT;
					md.hordeMember = true;
					md.info = &minfo;
					const float mg = terrain.getGroundHeight(m.position.x, m.position.y);
					md.position = { m.position.x, m.position.y, mg };
					md.angle = hordeAngle;
					composeRotationBasis(md, md.angle, false, Coord3D{ 0.0f, 0.0f, 1.0f });
					md.scale = minfo.scale;
					md.flags = hordeFlags;
					md.sideIndex = hordeSide;
					md.owner = hordeOwner;
					md.ownerResolved = hordeOwnerResolved;
					md.draws = minfo.draws;
					bool any = false;
					for (MapDrawModule &dm : md.draws)
					{
						resolveDrawModel(dm, md.flags, minfo.name, rep.errors);
						if (!dm.model.empty())
						{
							any = true;
							++rep.drawnModels;
							++rep.byDrawClass[dm.className];
						}
						else
						{
							++rep.notDrawnByReason[dm.notDrawnReason.empty() ? std::string("no draw module") : dm.notDrawnReason];
						}
					}
					++rep.hordeMembers;
					recordCreationHooks(rep, minfo);
					++rep.byTemplate[minfo.name];
					if (any)
					{
						++rep.drawables;
					}
					out.drawables.push_back(std::move(md));
				}
				for (const std::string &u : hs.unverified)
				{
					bool have = false;
					for (const std::string &x : rep.stops)
					{
						have = have || x == u;
					}
					if (!have)
					{
						rep.stops.push_back(u);
					}
				}
			}
		}
	}
}

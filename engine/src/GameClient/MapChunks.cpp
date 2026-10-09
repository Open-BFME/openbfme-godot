// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameClient/MapChunks.h for the source of every layout.

#include "GameClient/MapChunks.h"

#include "Common/DataChunk.h"
#include "Common/MapReaderWriterInfo.h"

#include <algorithm>

namespace
{

std::int32_t readCount(DataChunkInput &file, const char *what, std::int32_t limit = 10000000)
{
	std::int32_t n = file.readInt();
	if (n < 0 || n > limit)
	{
		throw MapParseError(std::string(what) + " count " + std::to_string(n) + " out of range");
	}
	return n;
}

std::string reversed4(DataChunkInput &file)
{
	char b[4];
	file.readArrayOfBytes(b, 4);
	return std::string{ b[3], b[2], b[1], b[0] };
}

std::vector<Point2F> readPoints2(DataChunkInput &file, const char *what)
{
	std::int32_t n = readCount(file, what);
	std::vector<Point2F> pts((size_t)n);
	for (Point2F &p : pts)
	{
		p.x = file.readReal();
		p.y = file.readReal();
	}
	return pts;
}

bool parseWorldInfo(DataChunkInput &file, DataChunkInfo *, void *ud)
{
	MapChunks *m = (MapChunks *)ud;
	m->hasWorldInfo = true;
	m->worldInfo = file.readDict();
	return true;
}

bool parseMPPositionList(DataChunkInput &file, DataChunkInfo *, void *ud)
{
	((MapChunks *)ud)->hasMPPositionList = true;
	return file.parse(ud);
}

bool parseMPPositionInfo(DataChunkInput &file, DataChunkInfo *, void *ud)
{
	MPPositionInfo i;
	i.isHuman = file.readByte() != 0;
	i.isComputer = file.readByte() != 0;
	i.loadAIScript = file.readByte() != 0;
	i.team = file.readInt();
	std::int32_t n = readCount(file, "MPPosition side restrictions");
	for (std::int32_t k = 0; k < n; ++k)
	{
		i.sideRestriction.push_back(file.readAsciiString());
	}
	((MapChunks *)ud)->mpPositions.push_back(std::move(i));
	return true;
}

bool parseObjectsList(DataChunkInput &file, DataChunkInfo *, void *ud)
{
	((MapChunks *)ud)->hasObjectsList = true;
	return file.parse(ud);
}

// ZH WorldHeightMap::ParseObjectData; BFME2 writer MapObjectWriteObjectsDataChunk.cpp:69-84.
bool parseObject(DataChunkInput &file, DataChunkInfo *info, void *ud)
{
	MapChunks *m = (MapChunks *)ud;
	MapObject o;
	o.m_location.x = file.readReal();
	o.m_location.y = file.readReal();
	o.m_location.z = file.readReal();
	if (info->version <= K_OBJECTS_VERSION_2)
	{
		o.m_location.z = 0; // ZH: z is not stored before v3
	}
	o.m_angle = file.readReal();
	o.m_flags = file.readInt();
	o.m_objectName = file.readAsciiString();
	if (info->version >= K_OBJECTS_VERSION_2)
	{
		o.m_properties = file.readDict();
	}
	// ZH: waypointID int => waypoint; lightHeightAboveTerrain real => light; scorchType int => scorch.
	const Dict::Pair *p = o.m_properties.find("waypointID");
	o.m_isWaypoint = p && p->type == Dict::DICT_INT;
	p = o.m_properties.find("lightHeightAboveTerrain");
	o.m_isLight = p && p->type == Dict::DICT_REAL;
	p = o.m_properties.find("scorchType");
	o.m_isScorch = p && p->type == Dict::DICT_INT;

	const float minZ = -100 * MAP_XY_FACTOR;
	const float maxZ = (255 * 10) * (MAP_XY_FACTOR / 16.0f); // ZH MAP_HEIGHT_SCALE = MAP_XY_FACTOR/16
	if (o.m_location.z < minZ || o.m_location.z > maxZ)
	{
		++m->objectsOutsideZhZRange;
	}
	m->objects.push_back(std::move(o));
	return true;
}

// ZH PolygonTrigger::ParsePolygonTriggersDataChunk (PolygonTrigger.cpp:135-230); v5 per spec 1.7.11.
bool parsePolygonTriggers(DataChunkInput &file, DataChunkInfo *info, void *ud)
{
	MapChunks *m = (MapChunks *)ud;
	m->hasPolygonTriggers = true;
	std::int32_t n = readCount(file, "polygon triggers");
	for (std::int32_t i = 0; i < n; ++i)
	{
		PolygonTrigger t;
		t.version = info->version;
		t.name = file.readAsciiString();
		if (info->version >= K_TRIGGERS_VERSION_4)
		{
			t.layer = file.readAsciiString();
		}
		t.id = file.readInt();
		if (info->version >= K_TRIGGERS_VERSION_2)
		{
			t.isWaterArea = file.readByte() != 0;
		}
		if (info->version >= K_TRIGGERS_VERSION_3)
		{
			t.isRiver = file.readByte() != 0;
			t.riverStart = file.readInt();
		}
		if (info->version >= K_TRIGGERS_VERSION_5)
		{
			t.riverTexture = file.readAsciiString();
			t.noiseTexture = file.readAsciiString();
			t.alphaEdgeTexture = file.readAsciiString();
			t.sparkleTexture = file.readAsciiString();
			t.bumpTexture = file.readAsciiString();
			t.skyTexture = file.readAsciiString();
			t.additiveBlending = file.readByte() != 0;
			t.colorR = file.readByte();
			t.colorG = file.readByte();
			t.colorB = file.readByte();
			file.readByte(); // 4th colour byte, 0
			t.uvScrollX = file.readReal();
			t.uvScrollY = file.readReal();
			t.alpha = file.readReal();
		}
		std::int32_t np = readCount(file, "polygon trigger points");
		for (std::int32_t k = 0; k < np; ++k)
		{
			PolygonTriggerPoint p;
			p.x = file.readInt();
			p.y = file.readInt();
			p.z = file.readInt();
			t.points.push_back(p);
		}
		m->polygonTriggers.push_back(std::move(t));
	}
	return true;
}

bool parseTriggerAreas(DataChunkInput &file, DataChunkInfo *, void *ud)
{
	MapChunks *m = (MapChunks *)ud;
	m->hasTriggerAreas = true;
	std::int32_t n = readCount(file, "trigger areas");
	for (std::int32_t i = 0; i < n; ++i)
	{
		TriggerArea t;
		t.name = file.readAsciiString();
		t.layer = file.readAsciiString();
		t.id = file.readInt();
		t.points = readPoints2(file, "trigger area points");
		t.tail = file.readInt();
		m->triggerAreas.push_back(std::move(t));
	}
	return true;
}

bool parseStandingWaterAreas(DataChunkInput &file, DataChunkInfo *, void *ud)
{
	MapChunks *m = (MapChunks *)ud;
	m->hasStandingWaterAreas = true;
	std::int32_t n = readCount(file, "standing water areas");
	for (std::int32_t i = 0; i < n; ++i)
	{
		StandingWaterArea a;
		a.uniqueId = file.readUnsignedInt();
		a.name = file.readAsciiString();
		a.layer = file.readAsciiString();
		a.uvScrollSpeed = file.readReal();
		a.additiveBlending = file.readByte() != 0;
		a.bumpMapTexture = file.readAsciiString();
		a.skyTexture = file.readAsciiString();
		a.points = readPoints2(file, "standing water points");
		a.waterHeight = file.readInt();
		a.fxShader = file.readAsciiString();
		a.depthColors = file.readAsciiString();
		m->standingWaterAreas.push_back(std::move(a));
	}
	return true;
}

bool parseRiverAreas(DataChunkInput &file, DataChunkInfo *info, void *ud)
{
	MapChunks *m = (MapChunks *)ud;
	if (info->version > 2)
	{
		// OpenSAGE's v3 adds an astr riverType; no retail map carries it, so the layout is
		// unverified here: stop instead of guessing.
		throw MapParseError("RiverAreas version " + std::to_string(info->version) + " is not in the retail corpus (layout unverified)");
	}
	m->hasRiverAreas = true;
	std::int32_t n = readCount(file, "river areas");
	for (std::int32_t i = 0; i < n; ++i)
	{
		RiverArea a;
		a.version = info->version;
		a.uniqueId = file.readUnsignedInt();
		a.name = file.readAsciiString();
		a.layer = file.readAsciiString();
		a.uvScrollSpeed = file.readReal();
		a.additiveBlending = file.readByte() != 0;
		a.riverTexture = file.readAsciiString();
		a.noiseTexture = file.readAsciiString();
		a.alphaEdgeTexture = file.readAsciiString();
		a.sparkleTexture = file.readAsciiString();
		a.r = file.readByte();
		a.g = file.readByte();
		a.b = file.readByte();
		a.a0 = file.readByte();
		a.alpha = file.readReal();
		a.waterHeight = file.readInt();
		a.minimumWaterLod = file.readAsciiString();
		std::int32_t nl = readCount(file, "river lines");
		for (std::int32_t k = 0; k < nl; ++k)
		{
			RiverArea::Line l;
			l.x0 = file.readReal();
			l.y0 = file.readReal();
			l.x1 = file.readReal();
			l.y1 = file.readReal();
			a.lines.push_back(l);
		}
		m->riverAreas.push_back(std::move(a));
	}
	return true;
}

bool parseStandingWaveAreas(DataChunkInput &file, DataChunkInfo *info, void *ud)
{
	MapChunks *m = (MapChunks *)ud;
	m->hasStandingWaveAreas = true;
	std::int32_t n = readCount(file, "standing wave areas");
	for (std::int32_t i = 0; i < n; ++i)
	{
		StandingWaveArea a;
		a.version = info->version;
		a.uniqueId = file.readUnsignedInt();
		a.name = file.readAsciiString();
		a.layer = file.readAsciiString();
		a.uvScrollSpeed = file.readReal();
		a.additive = file.readByte() != 0;
		a.points = readPoints2(file, "standing wave points");
		a.zero = file.readInt();
		a.finalWidth = file.readInt();
		a.finalHeight = file.readInt();
		a.initialWidthFraction = file.readInt();
		a.initialHeightFraction = file.readInt();
		a.initialVelocity = file.readInt();
		a.timeToFadeMs = file.readInt();
		a.timeToCompressMs = file.readInt();
		a.timeOffset2ndWaveMs = file.readInt();
		a.distanceFromShore = file.readInt();
		a.texture = file.readAsciiString();
		if (info->version == 2)
		{
			a.enablePcaWave = file.readInt();
		}
		m->standingWaveAreas.push_back(std::move(a));
	}
	return true;
}

bool parsePostEffects(DataChunkInput &file, DataChunkInfo *info, void *ud)
{
	MapChunks *m = (MapChunks *)ud;
	m->hasPostEffects = true;
	std::int32_t n = info->version < 2 ? (std::int32_t)file.readByte() : readCount(file, "post effects");
	for (std::int32_t i = 0; i < n; ++i)
	{
		PostEffect e;
		e.name = file.readAsciiString();
		e.blendFactor = file.readReal();
		e.lookupImage = file.readAsciiString();
		m->postEffects.push_back(std::move(e));
	}
	return true;
}

void readLight(DataChunkInput &file, GlobalLight &l)
{
	for (float &f : l.ambient) f = file.readReal();
	for (float &f : l.diffuse) f = file.readReal();
	for (float &f : l.lightPos) f = file.readReal();
}

// File order per time of day (spec 1.7.17): terrain[0], objects[0], objects[1], objects[2],
// terrain[1], terrain[2], infantry[0], infantry[1], infantry[2].
bool parseGlobalLighting(DataChunkInput &file, DataChunkInfo *info, void *ud)
{
	MapChunks *m = (MapChunks *)ud;
	if (info->version != 7 && info->version != 8)
	{
		throw MapParseError("GlobalLighting version " + std::to_string(info->version) + " is not in the retail corpus (v7, v8)");
	}
	m->hasGlobalLighting = true;
	GlobalLightingData &g = m->lighting;
	g.version = info->version;
	g.timeOfDay = file.readInt();
	for (int t = 0; t < 4; ++t)
	{
		TimeOfDayLights &L = g.tod[t];
		readLight(file, L.terrain[0]);
		readLight(file, L.objects[0]);
		readLight(file, L.objects[1]);
		readLight(file, L.objects[2]);
		readLight(file, L.terrain[1]);
		readLight(file, L.terrain[2]);
		readLight(file, L.infantry[0]);
		readLight(file, L.infantry[1]);
		readLight(file, L.infantry[2]);
	}
	g.terrainLightingMultiplier = file.readReal();
	g.flagDBD = file.readInt();
	for (float &f : g.vecA) f = file.readReal();
	for (float &f : g.vecB) f = file.readReal();
	for (float &f : g.vecC) f = file.readReal();
	g.shadowColor = file.readUnsignedInt();
	if (info->version >= 8)
	{
		g.hasV8Extra = true;
		for (float &f : g.v8Extra) f = file.readReal();
	}
	return true;
}

bool parseEnvironmentData(DataChunkInput &file, DataChunkInfo *info, void *ud)
{
	MapChunks *m = (MapChunks *)ud;
	m->hasEnvironmentData = true;
	EnvironmentData &e = m->environment;
	e.version = info->version;
	if (info->version >= 3)
	{
		e.waterMaxAlphaDepth = file.readReal();
		e.deepWaterAlpha = file.readReal();
	}
	e.isMacroTextureStretched = file.readByte() != 0;
	e.macroTexture = file.readAsciiString();
	e.cloudTexture = file.readAsciiString();
	return true;
}

bool parseNamedCameras(DataChunkInput &file, DataChunkInfo *, void *ud)
{
	MapChunks *m = (MapChunks *)ud;
	m->hasNamedCameras = true;
	std::int32_t n = readCount(file, "named cameras");
	for (std::int32_t i = 0; i < n; ++i)
	{
		NamedCamera c;
		c.position.x = file.readReal();
		c.position.y = file.readReal();
		c.position.z = file.readReal();
		c.name = file.readAsciiString();
		for (float &f : c.values) f = file.readReal();
		m->namedCameras.push_back(std::move(c));
	}
	return true;
}

bool parseCameraAnimationList(DataChunkInput &file, DataChunkInfo *info, void *ud)
{
	MapChunks *m = (MapChunks *)ud;
	m->hasCameraAnimations = true;
	std::int32_t n = readCount(file, "camera animations");
	for (std::int32_t i = 0; i < n; ++i)
	{
		CameraAnimation a;
		a.version = info->version;
		a.type = reversed4(file);
		a.name = file.readAsciiString();
		a.numFrames = file.readUnsignedInt();
		a.startOffset = file.readUnsignedInt();
		if (a.type == "free")
		{
			std::uint32_t k = file.readUnsignedInt();
			for (std::uint32_t j = 0; j < k; ++j)
			{
				CameraAnimationFrame f;
				f.frame = file.readUnsignedInt();
				f.interp = reversed4(file);
				for (float &v : f.pos) v = file.readReal();
				for (float &v : f.quat) v = file.readReal();
				f.fovLike = file.readReal();
				a.keys.push_back(std::move(f));
			}
		}
		else if (a.type == "look")
		{
			std::uint32_t k = file.readUnsignedInt();
			for (std::uint32_t j = 0; j < k; ++j)
			{
				CameraAnimationFrame f;
				f.frame = file.readUnsignedInt();
				f.interp = reversed4(file);
				for (float &v : f.pos) v = file.readReal();
				f.roll = file.readReal();
				f.fovLike = file.readReal();
				a.keys.push_back(std::move(f));
			}
			std::uint32_t mm = file.readUnsignedInt();
			for (std::uint32_t j = 0; j < mm; ++j)
			{
				CameraLookAtKey f;
				f.frame = file.readUnsignedInt();
				f.interp = reversed4(file);
				for (float &v : f.lookAt) v = file.readReal();
				a.lookAtKeys.push_back(std::move(f));
			}
		}
		else
		{
			throw MapParseError("camera animation type '" + a.type + "'");
		}
		m->cameraAnimations.push_back(std::move(a));
	}
	return true;
}

bool parseWaypointsList(DataChunkInput &file, DataChunkInfo *, void *ud)
{
	MapChunks *m = (MapChunks *)ud;
	m->hasWaypointsList = true;
	std::int32_t n = readCount(file, "waypoint links");
	for (std::int32_t i = 0; i < n; ++i)
	{
		WaypointLink l;
		l.from = file.readInt();
		l.to = file.readInt();
		m->waypointLinks.push_back(l);
	}
	return true;
}

// RW 0x731010 (see MapChunks.h)
bool parseCastleTemplates(DataChunkInput &file, DataChunkInfo *info, void *ud)
{
	MapChunks *m = (MapChunks *)ud;
	m->hasCastleTemplates = true;
	CastleTemplate t;
	t.version = info ? (int)info->version : 0;
	t.name = file.readNameKey();
	const std::int32_t n = readCount(file, "castle template entries");
	for (std::int32_t i = 0; i < n; ++i)
	{
		CastleTemplateEntry e;
		e.firstName = file.readAsciiString();
		e.templateName = file.readAsciiString();
		e.x = file.readReal();
		e.y = file.readReal();
		e.z = file.readReal();
		e.angle = file.readReal();
		if (t.version >= 4)
		{
			e.value1 = file.readInt();
			e.value2 = file.readInt();
		}
		t.entries.push_back(std::move(e));
	}
	if (t.version >= 2)
	{
		const std::int32_t lines = readCount(file, "castle template lines");
		for (std::int32_t i = 0; i < lines; ++i)
		{
			if (t.version >= 5)
			{
				(void)file.readAsciiString();
			}
			const std::int32_t k = readCount(file, "castle template line points");
			CastleTemplateLine line;
			for (std::int32_t j = 0; j < k; ++j)
			{
				Point2F p;
				if (t.version >= 3)
				{
					p.x = file.readReal();
					p.y = file.readReal();
				}
				else
				{
					p.x = (float)file.readInt();
					p.y = (float)file.readInt();
					(void)file.readInt();
				}
				line.points.push_back(p);
			}
			t.lines.push_back(std::move(line));
		}
	}
	m->castleTemplates.push_back(std::move(t));
	return true;
}

bool parseSkybox(DataChunkInput &file, DataChunkInfo *, void *ud)
{
	MapChunks *m = (MapChunks *)ud;
	m->hasSkybox = true;
	for (float &f : m->skybox.position) f = file.readReal();
	m->skybox.scale = file.readReal();
	m->skybox.rotation = file.readReal();
	m->skybox.textureScheme = file.readAsciiString();
	return true;
}

bool parseScriptImportSize(DataChunkInput &file, DataChunkInfo *, void *ud)
{
	MapChunks *m = (MapChunks *)ud;
	m->scb.hasImportSize = true;
	m->scb.importSize0 = file.readUnsignedInt();
	m->scb.importSize1 = file.readUnsignedInt();
	return true;
}

// ZH SidesList::ParsePlayersDataChunk (SidesList.cpp:431-452)
bool parseScriptsPlayers(DataChunkInput &file, DataChunkInfo *, void *ud)
{
	MapChunks *m = (MapChunks *)ud;
	m->scb.hasScriptsPlayers = true;
	std::int32_t hasDicts = file.readInt();
	std::int32_t n = readCount(file, "script players");
	for (std::int32_t i = 0; i < n; ++i)
	{
		m->scb.playerNames.push_back(file.readAsciiString());
		if (hasDicts)
		{
			m->scb.playerDicts.push_back(file.readDict());
		}
	}
	return true;
}

// ZH SidesList::ParseTeamsDataChunk (SidesList.cpp:460-475): Dicts until the end of the chunk.
bool parseScriptTeams(DataChunkInput &file, DataChunkInfo *, void *ud)
{
	MapChunks *m = (MapChunks *)ud;
	m->scb.hasScriptTeams = true;
	while (!file.atEndOfChunk())
	{
		m->scb.scriptTeams.push_back(file.readDict());
	}
	return true;
}

} // namespace

namespace MapChunkParse
{
void registerParsers(DataChunkInput &file, MapChunks *c)
{
	file.registerParser("WorldInfo", "", parseWorldInfo, c);
	file.registerParser("MPPositionList", "", parseMPPositionList, c);
	file.registerParser("MPPositionInfo", "MPPositionList", parseMPPositionInfo, c);
	file.registerParser("ObjectsList", "", parseObjectsList, c);
	file.registerParser("Object", "ObjectsList", parseObject, c);
	file.registerParser("PolygonTriggers", "", parsePolygonTriggers, c);
	file.registerParser("TriggerAreas", "", parseTriggerAreas, c);
	file.registerParser("StandingWaterAreas", "", parseStandingWaterAreas, c);
	file.registerParser("RiverAreas", "", parseRiverAreas, c);
	file.registerParser("StandingWaveAreas", "", parseStandingWaveAreas, c);
	file.registerParser("PostEffectsChunk", "", parsePostEffects, c);
	file.registerParser("GlobalLighting", "", parseGlobalLighting, c);
	file.registerParser("EnvironmentData", "", parseEnvironmentData, c);
	file.registerParser("NamedCameras", "", parseNamedCameras, c);
	file.registerParser("CameraAnimationList", "", parseCameraAnimationList, c);
	file.registerParser("WaypointsList", "", parseWaypointsList, c);
	file.registerParser("SkyboxSettings", "", parseSkybox, c);
	file.registerParser("CastleTemplates", "", parseCastleTemplates, c);
	file.registerParser("ScriptImportSize", "", parseScriptImportSize, c);
	file.registerParser("ScriptsPlayers", "", parseScriptsPlayers, c);
	file.registerParser("ScriptTeams", "", parseScriptTeams, c);
}
} // namespace MapChunkParse

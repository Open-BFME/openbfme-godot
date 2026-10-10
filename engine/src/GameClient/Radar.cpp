// OpenBFME. GPL-3.0.
// See Radar.h.

#include "GameClient/Radar.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/Player.h"
#include "GameClient/TGAFile.h"
#include "GameClient/HudObjects.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIPathfindHost.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/NotifyCrushModules.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/System/InvisibilityManager.h"
#include "GameLogic/System/ShroudManager.h"

#include <algorithm>
#include <cmath>
#include <variant>

bool Radar::setupFromTerrain()
{
	const TerrainLogic *t = m_ctx.logic.terrain();
	if (!t)
	{
		m_ready = false;
		return false;
	}
	float maxX = 0, maxY = 0;
	if (!t->getExtent(0, maxX, maxY) || maxX <= 0.0f || maxY <= 0.0f)
	{
		m_ready = false;
		return false;
	}
	m_minX = 0.0f;
	m_minY = 0.0f;
	m_maxX = maxX;
	m_maxY = maxY;
	m_imageSize = 0;
	m_ready = true;
	m_forced = false; // RW 0x6D8E25: the radar's reset clears + 0x11 (the next update sets it again)
	// lane RADAR-1, RW 0x6D8EBE (Radar::newMap with the terrain): the samples (+ 0x24 / + 0x28) are the extent's size x 0.0078125 (RW 0xBDAD58); EVERY radar
	// cell (x, y) of 128 x 128 (RW 0x6D8F2C .. 0x6D8FBC, y outer) is radarToWorld (RW 0x6D879A: x * sample), its ground height (TerrainLogic vslot 0x18)
	// truncated to an integer (__ftol2), added as a float to the water sum (+ 0x20) when the point is under water (vslot 0x4C; here a standing water surface
	// above the ground) else to the terrain sum (+ 0x1C); each sum over its count (0 -> 1)
	const float xSample = maxX * 0.0078125f, ySample = maxY * 0.0078125f;
	float sum = 0.0f;
	int samples = 0;
	for (int y = 0; y < kCells; ++y)
	{
		for (int x = 0; x < kCells; ++x)
		{
			const float wx = (float)x * xSample, wy = (float)y * ySample;
			const float ground = m_ctx.logic.getGroundHeight(wx, wy);
			const int z = (int)ground;
			float waterZ = 0.0f;
			if (!(t->getStandingWaterHeight(wx, wy, waterZ) && waterZ > ground))
			{
				sum += (float)z;
				++samples;
			}
		}
	}
	m_terrainAverageZ = sum / (float)(samples == 0 ? 1 : samples);
	return true;
}

void Radar::worldToRadar(float wx, float wy, int size, float &rx, float &ry) const
{
	rx = (wx - m_minX) / (m_maxX - m_minX) * (float)size;
	ry = (1.0f - (wy - m_minY) / (m_maxY - m_minY)) * (float)size; // north is up
}

void Radar::radarToWorld(float rx, float ry, int size, float &wx, float &wy) const
{
	wx = m_minX + rx / (float)size * (m_maxX - m_minX);
	wy = m_minY + (1.0f - ry / (float)size) * (m_maxY - m_minY);
}

const std::vector<std::uint8_t> &Radar::terrainImage(int size)
{
	if (m_imageSize == size && !m_image.empty())
	{
		return m_image;
	}
	m_image.assign((size_t)size * (size_t)size * 4, 0);
	m_imageSize = size;
	if (!m_ready)
	{
		return m_image;
	}
	// a shaded height field: the colour follows the height between the map's lowest and highest sample, the shade the slope towards the north west light
	std::vector<float> h((size_t)size * (size_t)size);
	float lo = 1e9f, hi = -1e9f;
	for (int y = 0; y < size; ++y)
	{
		for (int x = 0; x < size; ++x)
		{
			float wx, wy;
			radarToWorld((float)x + 0.5f, (float)y + 0.5f, size, wx, wy);
			const float v = m_ctx.logic.getGroundHeight(wx, wy);
			h[(size_t)y * (size_t)size + (size_t)x] = v;
			lo = std::min(lo, v);
			hi = std::max(hi, v);
		}
	}
	const float span = std::max(hi - lo, 1.0f);
	for (int y = 0; y < size; ++y)
	{
		for (int x = 0; x < size; ++x)
		{
			const float v = h[(size_t)y * (size_t)size + (size_t)x];
			const float t = (v - lo) / span;
			const float west = x > 0 ? h[(size_t)y * (size_t)size + (size_t)(x - 1)] : v;
			const float north = y > 0 ? h[(size_t)(y - 1) * (size_t)size + (size_t)x] : v;
			float shade = 1.0f + std::clamp(((v - west) + (v - north)) * 0.08f, -0.35f, 0.35f);
			const float r = (0.20f + 0.45f * t) * shade, g = (0.32f + 0.30f * t) * shade, b = (0.16f + 0.22f * t) * shade;
			std::uint8_t *p = &m_image[((size_t)y * (size_t)size + (size_t)x) * 4];
			p[0] = (std::uint8_t)std::clamp((int)(r * 255.0f), 0, 255);
			p[1] = (std::uint8_t)std::clamp((int)(g * 255.0f), 0, 255);
			p[2] = (std::uint8_t)std::clamp((int)(b * 255.0f), 0, 255);
			p[3] = 255;
		}
	}
	return m_image;
}

std::string Radar::artFileFor(const std::string &mapFile)
{
	// RW 0x44F3EF .. 0x44F41F: the name is cut at its LAST '.', then "_art.tga" is appended
	const std::size_t dot = mapFile.find_last_of('.');
	return (dot == std::string::npos || dot == 0 ? mapFile : mapFile.substr(0, dot)) + "_art.tga";
}

bool Radar::loadArt(ArchiveFileSystem &fs, const std::string &mapFile, std::string *error)
{
	m_art.clear();
	m_artW = m_artH = 0;
	m_artFile = artFileFor(mapFile);
	if (!fs.doesFileExist(m_artFile))
	{
		return true; // the radar texture is built from the terrain (RW 0x44F49F)
	}
	std::vector<std::uint8_t> bytes;
	std::string err;
	if (!fs.readFile(m_artFile, bytes, &err))
	{
		if (error)
		{
			*error = "radar art " + m_artFile + ": " + err;
		}
		return false;
	}
	int w = 0, h = 0;
	if (!decodeArt(bytes, w, h, m_art, &err))
	{
		m_art.clear();
		if (error)
		{
			*error = "radar art " + m_artFile + ": " + err;
		}
		return false;
	}
	m_artW = w;
	m_artH = h;
	return true;
}

bool Radar::decodeArt(const std::vector<std::uint8_t> &bytes, int &w, int &h, std::vector<std::uint8_t> &rgba, std::string *error)
{
	TGAImage img;
	// INFERENCE: retail loads the file through the texture manager, which takes any TGA; the core reader takes the uncompressed true-colour form every
	// retail art file has (2 / 32-bit) and refuses the others with an error, never a silent default
	if (!TGAFile::decode(bytes.data(), bytes.size(), img, error, 4096) || img.width <= 0 || img.height <= 0 || bytes.size() < 18)
	{
		return false;
	}
	// texture row 0 is the picture's bottom (see Radar.h): a bottom-origin file in storage order, a top-origin one reversed; shown north up. The horizontal
	// origin (descriptor bit 0x10, right-to-left) is honoured as the loader does (the check at RW 0xA28564 calls XFlip): column x comes from w - 1 - x.
	const bool rightOrigin = (bytes[17] & 0x10) != 0;
	w = img.width;
	h = img.height;
	rgba.assign((size_t)w * (size_t)h * 4, 0);
	for (int r = 0; r < h; ++r)
	{
		const int src = img.topDownFlag ? r : h - 1 - r;
		for (int x = 0; x < w; ++x)
		{
			const int srcX = rightOrigin ? w - 1 - x : x;
			std::copy_n(&img.rgba[((size_t)src * (size_t)w + (size_t)srcX) * 4], 4, &rgba[((size_t)r * (size_t)w + (size_t)x) * 4]);
		}
	}
	return true;
}

void Radar::drawRect(int x, int y, int w, int h, int ul[2], int lr[2]) const
{
	// RW 0x6D89F2 (SSE single precision, cvttss2si truncation)
	const float width = m_maxX - m_minX, height = m_maxY - m_minY;
	const float rx = width / (float)w, ry = height / (float)h;
	if (rx >= ry)
	{
		const float s = 1.0f / rx;
		ul[0] = 0;
		ul[1] = (int)(((float)h - height * s) * 0.5f);
		lr[0] = (int)(width * s);
		lr[1] = h - ul[1];
	}
	else
	{
		const float s = 1.0f / ry;
		ul[1] = 0;
		ul[0] = (int)(((float)w - width * s) * 0.5f);
		lr[0] = w - ul[0];
		lr[1] = (int)(height * s);
	}
	ul[0] += x;
	ul[1] += y;
	lr[0] += x;
	lr[1] += y;
}

std::vector<std::int16_t> Radar::shroudAlpha() const
{
	const ShroudManager *sm = m_ctx.logic.shroud();
	const Player *local = m_ctx.localPlayer();
	if (!m_ready || !sm || !sm->displayed() || !local)
	{
		return {};
	}
	const int li = local->getPlayerIndex();
	return shroudAlphaOf(sm->cellCountX(), sm->cellCountY(), sm->cellSize(), [sm, li](int cx, int cy) { return sm->getCellStatus(li, cx, cy); });
}

std::vector<std::int16_t> Radar::shroudAlpha(const ShroudView &view) const
{
	// SMOOTH-1 (merge with HUD-2): the same texels from the snapshot's view of the local player's shroud (the draw callback runs beside the logic worker)
	if (!m_ready || !view.displayed || view.localPlayer < 0)
	{
		return {};
	}
	return shroudAlphaOf(view.countX, view.countY, view.cellSize, [&view](int cx, int cy) { return view.cellStatus(cx, cy); });
}

std::vector<std::int16_t> Radar::shroudAlphaOf(int countX, int countY, float cell, const std::function<CellShroudStatus(int, int)> &statusOf) const
{
	const int n = kShroudTexture;
	std::vector<std::int16_t> out((size_t)n * (size_t)n, -1);
	const float width = m_maxX - m_minX, height = m_maxY - m_minY;
	const float sampleX = width / (float)n, sampleY = height / (float)n;
	// RW 0x6D8960: the squeeze of the map's aspect (1 on the longer side)
	const float sx = width > height ? 1.0f : width / height;
	const float sy = width > height ? height / width : 1.0f;
	auto toRadar = [&](float wx, float wy, int &tx, int &ty) {
		// RW 0x6D881F
		tx = std::clamp((int)(wx / sampleX), 0, n - 1);
		ty = std::clamp((int)(wy / sampleY), 0, n - 1);
	};
	for (int cy = 0; cy < countY; ++cy)
	{
		for (int cx = 0; cx < countX; ++cx)
		{
			const CellShroudStatus s = statusOf(cx, cy);
			// RW 0x44E400 .. 0x44E49B: the cell's corners in world units (truncated) to texels
			int x0, y0, x1, y1;
			toRadar((float)(int)((float)cx * cell), (float)(int)((float)cy * cell), x0, y0);
			toRadar((float)(int)((float)(cx + 1) * cell), (float)(int)((float)(cy + 1) * cell), x1, y1);
			if (sx > sy)
			{
				const float off = (float)n * (1.0f - sy) * 0.5f;
				y0 = (int)((float)(int)((float)y0 * sy) + off);
				y1 = (int)((float)(int)((float)y1 * sy) + off);
			}
			else
			{
				const float off = (float)n * (1.0f - sx) * 0.5f;
				x0 = (int)((float)(int)((float)x0 * sx) + off);
				x1 = (int)((float)(int)((float)x1 * sx) + off);
			}
			// RW 0x44E57F: SHROUDED 0xFF, FOGGED 0x7F, otherwise 0
			const std::int16_t a = s == CELLSHROUD_SHROUDED ? 255 : (s == CELLSHROUD_FOGGED ? 127 : 0);
			for (int ty = y0; ty <= y1; ++ty)
			{
				for (int tx = x0; tx <= x1; ++tx)
				{
					if (tx >= 0 && ty >= 0 && tx < n && ty < n) // RW 0x44DDA3
					{
						out[(size_t)(n - 1 - ty) * (size_t)n + (size_t)tx] = a; // texture row ty is shown from the bottom
					}
				}
			}
		}
	}
	return out;
}

std::vector<Radar::Blip> Radar::blips(int size) const
{
	std::vector<Blip> out;
	if (!m_ready)
	{
		return out;
	}
	for (Object *o = m_ctx.logic.getFirstObject(); o; o = o->getNextObject())
	{
		Player *owner = o->getControllingPlayer();
		if (o->isDestroyed() || o->getContainedBy() || !owner || !owner->hasTeamColor() || !HudObjects::isSelectable(*o))
		{
			continue; // map props, neutral scenery and the horde members (the horde is the blip) are not on the radar
		}
		if (ShroudManager *sm = m_ctx.logic.shroud())
		{
			if (sm->displayed() && m_ctx.localPlayer() && sm->clientObjectStatus(*o, m_ctx.localPlayer()->getPlayerIndex()) == OBJECTSHROUD_SHROUDED)
			{
				continue; // VIS-1: the local player cannot see it
			}
		}
		if (InvisibilityManager::clientLook(*o, m_ctx.localPlayer()) == 5)
		{
			continue; // lane STEALTH-1: an enemy's invisible, undetected object is not drawn (RW 0x81AA03 state 5), nor on the radar
		}
		Blip b;
		worldToRadar(o->getPosition()->x, o->getPosition()->y, size, b.x, b.y);
		b.color = owner->getPlayerColor() | 0xFF000000u;
		b.radius = o->isKindOfName("STRUCTURE") ? 3 : 2;
		b.mine = owner == m_ctx.localPlayer();
		b.object = o->getID();
		out.push_back(b);
	}
	return out;
}

std::vector<Radar::Blip> Radar::blips(const LogicSnapshot &snapshot, int size, int localPlayerIndex) const
{
	std::vector<Blip> out;
	if (!m_ready)
	{
		return out;
	}
	for (const ObjectSnapshot &o : snapshot.objects)
	{
		if (!o.radarBlip || o.shroudedForLocal || o.stealthLook == 5) // lane STEALTH-1: an enemy's invisible, undetected object
		{
			continue; // map props, neutral scenery and the horde members (the horde is the blip) are not on the radar
		}
		Blip b;
		worldToRadar(o.position.x, o.position.y, size, b.x, b.y);
		b.color = o.ownerColor | 0xFF000000u;
		b.radius = o.structure ? 3 : 2;
		b.mine = o.ownerIndex == localPlayerIndex;
		b.object = o.id;
		out.push_back(b);
	}
	return out;
}

unsigned long long Radar::shroudVersion(const ShroudView *view)
{
	return view ? view->version : 0;
}

unsigned long long Radar::shroudVersion() const
{
	const ShroudManager *sm = m_ctx.logic.shroud();
	return sm ? sm->stats().edges * 2 + (sm->displayed() ? 1 : 0) : 0;
}

std::vector<std::uint8_t> Radar::shroudedImage(int size)
{
	std::vector<std::uint8_t> img = terrainImage(size);
	const ShroudManager *sm = m_ctx.logic.shroud();
	const Player *local = m_ctx.localPlayer();
	if (!sm || !sm->displayed() || !local || img.size() < (size_t)size * (size_t)size * 4)
	{
		return img;
	}
	for (int py = 0; py < size; ++py)
	{
		for (int px = 0; px < size; ++px)
		{
			float wx, wy;
			radarToWorld((float)px + 0.5f, (float)py + 0.5f, size, wx, wy);
			const CellShroudStatus s = sm->getStatusAt(local->getPlayerIndex(), wx, wy);
			const unsigned level = sm->displayLevel(s);
			std::uint8_t *p = &img[((size_t)py * (size_t)size + (size_t)px) * 4];
			for (int c = 0; c < 3; ++c)
			{
				p[c] = (std::uint8_t)((unsigned)p[c] * level / 255u);
			}
		}
	}
	return img;
}

bool Radar::viewBox(int size, float out[8]) const
{
	if (!m_ready)
	{
		return false;
	}
	const ICoord2D s = m_ctx.view.size();
	const ICoord2D corners[4] = { { 0, 0 }, { s.x, 0 }, { s.x, s.y }, { 0, s.y } };
	for (int i = 0; i < 4; ++i)
	{
		Coord3D w;
		if (!m_ctx.view.screenToTerrain(corners[i], m_ctx.logic, w, 30000.0f))
		{
			return false;
		}
		worldToRadar(w.x, w.y, size, out[i * 2], out[i * 2 + 1]);
	}
	return true;
}

bool Radar::click(float rx, float ry, int size, float &wx, float &wy) const
{
	if (!m_ready || rx < 0.0f || ry < 0.0f || rx > (float)size || ry > (float)size)
	{
		return false;
	}
	radarToWorld(rx, ry, size, wx, wy);
	return true;
}

std::vector<std::string> Radar::acceptanceStops()
{
	return {
		"[S-1674] radar events (HUD-4): Space visits the radar's events as RW 0x5DCB28 (the next one while the window lasts, else the first; the view looks at it); "
		"not ported: the creators of the events (attacks, EVA, scripts, the movie's pings) and the window's source (TheRadar + 0x90, 0 here): no event exists in a game yet",
		"[S-291] radar (narrowed, RADAR-1): a map with a <map>_art.tga shows it (W3DRadar RW 0x44F3AB) under the ScrollShroud shroud (RW 0x44E3B6); a map without art "
		"shows a shaded height field, not W3DRadar's terrain texture (RW 0x44F4AD loop not ported); the +0x1494 image is not ported; the objects (RW 0x44F9DA) and the "
		"view box band (RW 0x50434F) are RotWK's (Radar.h's table)",
		"[S-761] radar shroud (HUD-2, narrowed by RADAR-1): the shroud texture starts as ScrollShroud's texels (RW 0x44EF41 .. 0x44EFDC copies the rows when the "
		"format and the 128 x 128 size match, else it stays cleared); INFERENCE: the compiled .jpg + .png pair loads in the shroud texture's format "
		"(A8R8G8B8), so the copy happens; an art TGA other than uncompressed 24/32-bit is refused with an error (retail's texture loader takes it)",
		"[S-2450] radar objects (RADAR-1): INFERENCE: within a radar list and priority the newer object (the higher id) is drawn first (RW 0x6D9042 inserts a new "
		"object before the older ones of its priority); re-adds by capture, disguise or the map boundary are not tracked",
		"[S-2451] radar markers (RADAR-1): RadarMarkerClientUpdate (crates: PingOneRing / PingBeacon) and the marker list TheRadar moves (RW 0x44EDAE) are not ported",
		"[S-2452] radar events (RADAR-1, narrowed): the 64 events (RW 0x6D93EB, tryEvent RW 0x6D9B4D, expiry RW 0x6D8E2B), the attacks of the local player's "
		"objects (RW 0x67B4B7 -> tryUnderAttackEvent RW 0x6D9BF3) and the Palantir pings (RW 0x44DE58: CreateRadarPing / MoveRadarPing / FadeOutRadarPing) are "
		"ported; not ported: the other creators (scripts' radar event actions, stealth detection, infiltration, battle plans, the dozer), the RadarEvent sound "
		"and the under-attack messages / EVA sounds / control bar flash of RW 0x6D9BF3",
		"[S-2453] radar gate (RADAR-1): W3DRadar::draw (RW 0x44FE0B) and the radar's clicks draw / work when Player::hasRadar or TheRadar + 0x11; every radar update "
		"(RW 0x6D8E41, once a client frame) sets + 0x11, the reset clears it: the radar shows from the first client frame of a game. Not ported: Player::hasRadar "
		"and the RadarUpgrade / RadarUpdate modules (no retail object has one; it never matters while + 0x11 is set), the hidden flag + 0x10 (RW 0x7BCEE2's script "
		"actions)",
		"[S-2454] radar stealth blink (RADAR-1): INFERENCE: RW 0x44DFBA's states (stealthed and undetected, detected status 0x11, RW 0x693BF2) are taken from "
		"InvisibilityManager::clientLook (1 / 4 own pulse and 3 detected blink, 5 hidden)",
		"[S-2455] radar view box (RADAR-1): INFERENCE: the corners are projected at every draw (retail rebuilds the shape when the zoom or angle changed and moves "
		"it with the camera, RW 0x44D47A); the terrain average height is RotWK's (RW 0x6D8EBE, every cell, heights truncated)",
		"[S-2456] radar colour (RADAR-1): the disguise colour (RW 0x6D90DC) and the object + 0x250 colour interface of RW 0x6D9042 and a custom indicator colour "
		"(Object + 0x324) are not ported: the blip is the team owner's colour",
		"[S-2457] radar attacks (RADAR-1): INFERENCE: the drawable's damage notice (RW 0x67B4B7) is reached whenever RW 0x6968BC runs; the client reports a hit "
		"once per object and attack frame between logic frames, in the object list's order (retail: per hit, in damage order, inside the frame)",
	};
}

// lane HUD-4: RW 0x5DCB28 (see Radar.h)
void Radar::tryJumpToNextEvent(TacticalView &view)
{
	if (m_events.empty())
	{
		return;
	}
	const unsigned now = m_clientFrame;
	// RW 0x5DCB59 .. 0x5DCB7F: fild the unsigned window, fmul 0.03f, ftol, + the last time, compared unsigned with now
	const unsigned windowFrames = (unsigned)((m_eventWindow * 3u) / 100u); // INFERENCE: the integer form of ftol(window * 0.03f); exact for the window 0 used here
	bool next = false;
	if (m_eventCursor >= 0 && now <= windowFrames + m_lastJump)
	{
		++m_eventCursor;
		next = m_eventCursor < (int)m_events.size();
	}
	if (!next)
	{
		m_eventCursor = 0;
	}
	view.lookAt(m_events[(size_t)m_eventCursor].position);
	m_lastJump = now;
}

// ---- lane RADAR-1 ---------------------------------------------------------------------------------------------------------------------------------------------
namespace
{
struct RadarKinds
{
	int structure, hero, wall, commandCenter, garrison, linkedToFlag, capturable, walkOnWall, defensiveWall;
};

const RadarKinds &radarKinds()
{
	static const RadarKinds k{ ObjectTemplateInfoBuilder::kindOfIndex("STRUCTURE"),     ObjectTemplateInfoBuilder::kindOfIndex("HERO"),
		                       ObjectTemplateInfoBuilder::kindOfIndex("WALL_SEGMENT"),  ObjectTemplateInfoBuilder::kindOfIndex("COMMANDCENTER"),
		                       ObjectTemplateInfoBuilder::kindOfIndex("GARRISON"),      ObjectTemplateInfoBuilder::kindOfIndex("LINKED_TO_FLAG"),
		                       ObjectTemplateInfoBuilder::kindOfIndex("CAPTURABLE"),    ObjectTemplateInfoBuilder::kindOfIndex("WALK_ON_TOP_OF_WALL"),
		                       ObjectTemplateInfoBuilder::kindOfIndex("DEFENSIVE_WALL") };
	return k;
}

bool has(const KindOfMaskType &mask, int bit)
{
	return bit >= 0 && MaskTest(mask, (unsigned)bit);
}

void components(std::uint32_t c, unsigned &a, unsigned &r, unsigned &g, unsigned &b)
{
	a = c >> 24;
	r = (c >> 16) & 0xFF;
	g = (c >> 8) & 0xFF;
	b = c & 0xFF;
}

std::uint32_t pack(unsigned a, unsigned r, unsigned g, unsigned b)
{
	return ((a & 0xFF) << 24) | ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
}

// the overlay surface (W3DRadar's A8R8G8B8 texture +0x147C): legalRadarPoint (RW 0x44DDA3) and DrawPixel (RW 0x5165E0, a plain write)
struct Surface
{
	std::vector<std::uint32_t> &t;
	static bool legal(int x, int y) { return x >= 0 && y >= 0 && x < Radar::kCells && y < Radar::kCells; }
	void put(int x, int y, std::uint32_t c)
	{
		if (legal(x, y))
		{
			t[(size_t)y * (size_t)Radar::kCells + (size_t)x] = c;
		}
	}
	void blend(int x, int y, std::uint32_t c, std::uint8_t f)
	{
		std::uint32_t &d = t[(size_t)y * (size_t)Radar::kCells + (size_t)x];
		d = Radar::blendColor(d, c, f);
	}
};

// RW 0x44E8ED (n = 3, the halo, mask RW 0xBDAE24) and RW 0x44EB51 (n = 2, the core, mask RW 0xBDAE20): a 2n x 2n square around (x, y), x - n .. x + n - 1,
// each quadrant the n x n mask mirrored, mask[column * n + row] counted from the square's edge; the locked rectangle is clamped to the texture
void drawMasked(Surface &s, int x, int y, std::uint32_t color, const std::uint8_t *mask, int n)
{
	const int x0 = std::max(x - n, 0), y0 = std::max(y - n, 0), x1 = std::min(x + n, Radar::kCells), y1 = std::min(y + n, Radar::kCells);
	for (int py = y0; py < y1; ++py)
	{
		const int row = py < y ? py - (y - n) : (y + n - 1) - py;
		for (int px = x0; px < x1; ++px)
		{
			const int col = px < x ? px - (x - n) : (x + n - 1) - px;
			s.blend(px, py, color, mask[col * n + row]);
		}
	}
}

// ZH / BFME2 DiscreteCircle (RW 0x6E0AE4; BFME2 decomp discrete_circle.cpp): Bresenham edge rows, the later of two rows with the same y kept
struct HorzLine
{
	int yPos, xStart, xEnd; // BFME's layout (RW 0x44FCB3 reads y at + 0, the ends at + 4 / + 8)
};

std::vector<HorzLine> discreteCircle(int xCenter, int yCenter, int radius)
{
	std::vector<HorzLine> edges;
	int x = 0, y = radius, d = (1 - radius) << 1;
	while (y >= 0)
	{
		edges.push_back(HorzLine{ yCenter + y, xCenter - x, xCenter + x });
		if (d + y > 0)
		{
			--y;
			d -= ((y << 1) - 1);
		}
		if (x > d)
		{
			++x;
			d += ((x << 1) + 1);
		}
	}
	for (size_t i = 0; i + 1 < edges.size();)
	{
		if (edges[i].yPos == edges[i + 1].yPos)
		{
			edges.erase(edges.begin() + (std::ptrdiff_t)i);
		}
		else
		{
			++i;
		}
	}
	return edges;
}

// RW 0x5039E4 / 0x503A43 / 0x503A21: the angle helpers of the view box band
float wrapAngle(float a)
{
	const float twoPi = 6.28318548f; // RW 0xBDD38C
	if (0.0f > a)
	{
		return twoPi - (float)std::fmod((double)(0.0f - a), (double)twoPi);
	}
	return (float)std::fmod((double)a, (double)twoPi);
}

float angleDiff(float a, float b)
{
	if (b > a)
	{
		a += 6.28318548f;
	}
	return a - b;
}

float angleOf(float dx, float dy)
{
	return wrapAngle(std::atan2(dy, dx));
}
} // namespace

void Radar::captureObject(GameLogic &logic, const Object &o, const Player *local, ObjectSnapshot::RadarEntry &out)
{
	out = ObjectSnapshot::RadarEntry();
	const ThingTemplate *tt = o.getTemplate();
	if (!tt || o.isDestroyed() || o.isEffectivelyDead())
	{
		return; // Object::onDie (RW 0x698F80) takes it off the radar
	}
	const RadarKinds &k = radarKinds();
	const KindOfMaskType &kind = logic.templateInfo(tt->getFinalOverride()).kindOf; // retail reads the template's bits (+ 0x108)
	// RW 0x68EBE9 (Object::getRadarPriority): the template's RadarPriority (+ 0x600); INVALID becomes STRUCTURE for a garrisonable contain (slot 0x10) or
	// a CAPTURABLE template (+ 0x10E & 2)
	long long priority = RADAR_PRIORITY_INVALID;
	if (const FieldValue *v = tt->getFinalOverride()->findField("RadarPriority"))
	{
		if (const long long *i = std::get_if<long long>(v))
		{
			priority = *i;
		}
	}
	if (priority == RADAR_PRIORITY_INVALID)
	{
		if (o.getContain() && o.getContain()->isGarrisonable())
		{
			priority = RADAR_PRIORITY_STRUCTURE;
		}
		if (has(kind, k.capturable))
		{
			priority = RADAR_PRIORITY_STRUCTURE;
		}
	}
	out.priority = (std::int8_t)priority;
	// RW 0x6D9042 (Radar::addObject): RW 0x6D8B21 isPriorityVisible (not 0 .. 1)
	if (priority >= RADAR_PRIORITY_INVALID && priority <= RADAR_PRIORITY_NOT_ON_RADAR)
	{
		return;
	}
	const Player *owner = o.getControllingPlayer();
	if (has(kind, k.structure))
	{
		// a COMMANDCENTER, GARRISON or LINKED_TO_FLAG structure always; else not a neutral-controlled one, nor one whose team the local player is NEUTRAL to
		if (!has(kind, k.commandCenter) && !has(kind, k.garrison) && !has(kind, k.linkedToFlag))
		{
			if (owner && owner == logic.players().getNeutralPlayer())
			{
				return;
			}
			if (!local || local->getRelationship(o.getTeam()) == NEUTRAL)
			{
				return;
			}
		}
	}
	else if (has(kind, k.walkOnWall) || has(kind, k.defensiveWall))
	{
		return;
	}
	out.listed = true;
	out.local = owner && owner == local; // RW 0x68B749 isLocallyControlled
	// RW 0x68B6F5: the custom indicator colour (not ported: none is set) else the team's controlling player's colour, else 0xFF000000
	const Team *team = o.getTeam();
	const Player *teamOwner = team ? team->getControllingPlayer() : nullptr;
	out.indicator = teamOwner ? teamOwner->getPlayerColor() : 0xFF000000u;
	// INFERENCE (stop S-2456): the disguise colour (RW 0x6D90DC .. 0x6D9105) and the object + 0x250 interface's colour are not ported
	out.color = saturateColor(out.indicator);
	// RW 0x44FB11 .. 0x44FC4B: HERO first (the disguise template is not ported), then WALL_SEGMENT, then COMMANDCENTER
	out.shape = has(kind, k.hero) ? SHAPE_HERO : has(kind, k.wall) ? SHAPE_WALL : has(kind, k.commandCenter) ? SHAPE_COMMAND_CENTER : SHAPE_DOT;
	if (out.shape == SHAPE_WALL || out.shape == SHAPE_COMMAND_CENTER)
	{
		// Object + 0xB8: the pathfinder's cached geometry of the template (CombatQueries::boundingCircleRadius); a game without an AIWorld (tests) parses it
		if (logic.aiWorld())
		{
			out.boundingRadius = CombatQueries::boundingCircleRadius(o);
		}
		else
		{
			PathfindGeometry g;
			ObjectGeometry::fillPathfindGeometry(*tt->getFinalOverride(), g);
			out.boundingRadius = g.boundingCircleRadius();
		}
	}
}

std::uint32_t Radar::saturateColor(std::uint32_t argb)
{
	// RW 0x6D8C53 (BFME2 decomp Rva002D7FAEFind.cpp rva002D7BFD, tier A): the float channels (RW 0x6D3AD5), WW3D RGB_To_HSV, S * 1.6 at most 1, HSV_To_RGB,
	// each channel (unsigned char)(c * 255.0f) (x87 truncation)
	const float a = (float)(argb >> 24) * (1.0f / 255.0f);
	const float r = (float)((argb >> 16) & 0xFF) * (1.0f / 255.0f), g = (float)((argb >> 8) & 0xFF) * (1.0f / 255.0f), b = (float)(argb & 0xFF) * (1.0f / 255.0f);
	// ZH colorspace.h RGB_To_HSV
	const float mx = std::max(std::max(r, g), b), mn = std::min(std::min(r, g), b);
	float h = -1.0f, s = mx != 0.0f ? (mx - mn) / mx : 0.0f;
	const float v = mx;
	if (s != 0.0f)
	{
		const float delta = mx - mn;
		if (r == mx)
		{
			h = (g - b) / delta;
		}
		else if (g == mx)
		{
			h = 2.0f + (b - r) / delta;
		}
		else
		{
			h = 4.0f + (r - g) / delta;
		}
		h *= 60.0f;
		if (h < 0.0f)
		{
			h += 360.0f;
		}
	}
	s *= 1.6f;
	if (s > 1.0f)
	{
		s = 1.0f;
	}
	// HSV_To_RGB
	float ro = v, go = v, bo = v;
	if (s != 0.0f)
	{
		if (h == 360.0f)
		{
			h = 0.0f;
		}
		h /= 60.0f;
		const int i = (int)std::floor(h);
		const float f = h - (float)i, p = v * (1.0f - s), q = v * (1.0f - (s * f)), t = v * (1.0f - (s * (1.0f - f)));
		switch (i)
		{
		case 0: ro = v; go = t; bo = p; break;
		case 1: ro = q; go = v; bo = p; break;
		case 2: ro = p; go = v; bo = t; break;
		case 3: ro = p; go = q; bo = v; break;
		case 4: ro = t; go = p; bo = v; break;
		case 5: ro = v; go = p; bo = q; break;
		default: break;
		}
	}
	return pack((unsigned)(unsigned char)(a * 255.0f), (unsigned)(unsigned char)(ro * 255.0f), (unsigned)(unsigned char)(go * 255.0f),
	            (unsigned)(unsigned char)(bo * 255.0f));
}

std::uint32_t Radar::modulateColor(std::uint32_t c1, std::uint32_t c2)
{
	unsigned a1, r1, g1, b1, a2, r2, g2, b2;
	components(c1, a1, r1, g1, b1);
	components(c2, a2, r2, g2, b2);
	return pack(a1 * a2 / 255u, r1 * r2 / 255u, g1 * g2 / 255u, b1 * b2 / 255u);
}

std::uint32_t Radar::blendColor(std::uint32_t dst, std::uint32_t src, std::uint8_t factor)
{
	// RW 0x44D717 (BFME2 reverse/attempts/0x0004de72.cpp, checked against the disassembly)
	unsigned a1, r1, g1, b1, a2, r2, g2, b2;
	components(dst, a1, r1, g1, b1);
	components(src, a2, r2, g2, b2);
	const unsigned t = (a2 * (unsigned)factor / 255u) & 0xFF;
	if (a1 == 0 || t == 0xFF)
	{
		return pack(t, r2, g2, b2);
	}
	const unsigned den = a1 + t;
	return pack(255u - ((255u - a1) * (255u - t)) / 255u, (r1 * a1 + r2 * t) / den, (g1 * a1 + g2 * t) / den, (b1 * a1 + b2 * t) / den);
}

bool Radar::stealthBlink(int stealthLook, unsigned clientFrame, std::uint32_t &color)
{
	// RW 0x44DFBA: an object stealthed and undetected (RW 0x694C0D) is drawn by its controller and, when detected (status 0x11) or RW 0x693BF2, by the
	// others, its alpha a triangle wave: frame = clientFrame % 60; 64 + frame * 191 / 30 up to 30, then 255 - (frame - 30) * 191 / 30 (the half period is
	// the global RW 0xD9F60C = 30); an enemy's undetected one is not drawn. INFERENCE (stop S-2454): the stealthed / detected states are
	// InvisibilityManager::clientLook's (1 / 4 the friend's pulse, 3 detected, 5 hidden for the local player)
	if (stealthLook == 5)
	{
		return false;
	}
	if (stealthLook != 1 && stealthLook != 3 && stealthLook != 4)
	{
		return true;
	}
	const unsigned half = 30;
	const unsigned frame = clientFrame % (half * 2);
	const unsigned alpha = frame >= half ? 255u - ((frame - half) * 0xBFu) / half : 64u + (frame * 0xBFu) / half;
	color = (color & 0x00FFFFFFu) | ((alpha & 0xFF) << 24);
	return true;
}

void Radar::renderOverlay(const LogicSnapshot &snapshot, unsigned clientFrame, std::uint32_t tint, std::vector<std::uint32_t> &texels) const
{
	if (!m_ready)
	{
		texels.assign((size_t)kCells * (size_t)kCells, 0u);
		return;
	}
	renderObjects(snapshot, m_maxX - m_minX, m_maxY - m_minY, clientFrame, tint, texels, &m_wallShapes);
}

void Radar::renderObjects(const LogicSnapshot &snapshot, float mapWidth, float mapHeight, unsigned clientFrame, std::uint32_t tint, std::vector<std::uint32_t> &texels,
                          std::map<const ThingTemplate *, std::vector<ObjectGeometry::Shape>> *wallShapes)
{
	// RW 0x450127 .. 0x450172: the surface is cleared, then the others' list (+ 0x14) and the local list (+ 0x18) are drawn into it
	texels.assign((size_t)kCells * (size_t)kCells, 0u);
	// the lists' order (RW 0x6D9042: each list sorted by priority, a new object before the older ones of its priority). INFERENCE (stop S-2450): the time an
	// object was added is its creation (a higher id is newer); re-adds (capture, disguise, map boundary) are not tracked
	std::vector<const ObjectSnapshot *> order;
	order.reserve(snapshot.objects.size());
	for (const ObjectSnapshot &o : snapshot.objects)
	{
		if (o.radar.listed)
		{
			order.push_back(&o);
		}
	}
	std::stable_sort(order.begin(), order.end(), [](const ObjectSnapshot *a, const ObjectSnapshot *b) {
		if (a->radar.local != b->radar.local)
		{
			return !a->radar.local;
		}
		if (a->radar.priority != b->radar.priority)
		{
			return a->radar.priority < b->radar.priority;
		}
		return a->id > b->id;
	});
	Surface surf{ texels };
	// RW 0x44FA28 .. 0x44FA59: the samples per world unit
	const float xs = 128.0f / mapWidth, ys = 128.0f / mapHeight;
	static const std::uint8_t kHalo[9] = { 0x03, 0x24, 0x45, 0x24, 0x4F, 0x4F, 0x45, 0x4F, 0x4F }; // RW 0xBDAE24
	static const std::uint8_t kCore[4] = { 0x00, 0x4A, 0x4A, 0xFF };                               // RW 0xBDAE20
	for (const ObjectSnapshot *op : order)
	{
		const ObjectSnapshot &o = *op;
		// RW 0x6D86D8 (isTemporarilyHidden): a hidden drawable (+ 0x43D, the contains) or a stealth-hidden one (+ 0x164 == 5 / + 0x43E)
		if (o.drawableHidden || o.stealthLook == 5)
		{
			continue;
		}
		// RW 0x44FA8A: fogged or shrouded for the local player
		if (o.objectShroud > OBJECTSHROUD_PARTIAL_CLEAR)
		{
			continue;
		}
		// RW 0x44FAAB: LOCAL_UNIT_ONLY of another player while the local player is active (INFERENCE: the local player is always active here), not for a
		// COMMANDCENTER
		if (o.radar.shape != SHAPE_COMMAND_CENTER && o.radar.priority == RADAR_PRIORITY_LOCAL_UNIT_ONLY && !o.radar.local)
		{
			continue;
		}
		const int x = (int)(xs * o.position.x), y = (int)(o.position.y * ys);
		std::uint32_t c = modulateColor(o.radar.color, tint);
		switch (o.radar.shape)
		{
		case SHAPE_HERO:
		{
			if (!stealthBlink(o.stealthLook, clientFrame, c))
			{
				break;
			}
			// RW 0x44FB6B .. 0x44FC05: the halo in the colour, the core with each channel halfway to 255
			unsigned a, r, g, b;
			components(c, a, r, g, b);
			const std::uint32_t light = pack(a, r + ((255u - r) >> 1), g + ((255u - g) >> 1), b + ((255u - b) >> 1));
			drawMasked(surf, x, y, c, kHalo, 3);
			drawMasked(surf, x, y, light, kCore, 2);
			break;
		}
		case SHAPE_WALL:
		{
			// RW 0x44D814: the texels of the bounding square whose sample point the wall's geometry touches with a sphere of 30 (alpha 0x80) or 5 (0xFF),
			// in Object::getIndicatorColor; the sample runs from the far side ((centre - texel) / scale + position: the square turned half a turn)
			const int rad = (int)o.radar.boundingRadius;
			const int cx = (int)(o.position.x * xs), cy = (int)(o.position.y * ys);
			const float fx = (float)rad * xs, fy = (float)rad * ys;
			int x0 = (int)((float)cx - fx), y0 = (int)((float)cy - fy);
			int x1 = (int)(fx + (float)cx), y1 = (int)(fy + (float)cy);
			x0 = x0 > 0 ? x0 : 0;
			y0 = y0 > 0 ? y0 : 0;
			x1 = x1 < kCells ? x1 : kCells;
			y1 = y1 < kCells ? y1 : kCells;
			if (!o.tmpl || x0 >= x1 || y0 >= y1)
			{
				break;
			}
			std::vector<ObjectGeometry::Shape> parsed;
			const std::vector<ObjectGeometry::Shape> *cached = nullptr;
			if (wallShapes)
			{
				auto it = wallShapes->find(o.tmpl);
				if (it == wallShapes->end())
				{
					it = wallShapes->emplace(o.tmpl, ObjectGeometry::shapesOf(*o.tmpl)).first;
				}
				cached = &it->second;
			}
			else
			{
				parsed = ObjectGeometry::shapesOf(*o.tmpl);
			}
			const std::vector<ObjectGeometry::Shape> &shapes = cached ? *cached : parsed;
			std::vector<ObjectGeometry::Shape> near(1), far(1);
			far[0].type = near[0].type = ObjectGeometry::SHAPE_SPHERE; // RW 0xBC112B / 0xBC1159: GeometryInfo(SPHERE, small, 30 / 5 x 3)
			far[0].height = far[0].majorRadius = far[0].minorRadius = 30.0f;
			near[0].height = near[0].majorRadius = near[0].minorRadius = 5.0f;
			const float ix = 1.0f / xs, iy = 1.0f / ys;
			for (int py = y0; py < y1; ++py)
			{
				const float wy = (float)(cy - py) * iy + o.position.y;
				for (int px = x0; px < x1; ++px)
				{
					const Coord3D sample{ (float)(cx - px) * ix + o.position.x, wy, o.position.z };
					if (NotifyCrushModules::geometriesOverlap(shapes, o.position, o.angle, far, sample, 0.0f))
					{
						const bool inner = NotifyCrushModules::geometriesOverlap(shapes, o.position, o.angle, near, sample, 0.0f);
						surf.blend(px, py, o.radar.indicator, inner ? 0xFF : 0x80);
					}
				}
			}
			break;
		}
		case SHAPE_COMMAND_CENTER:
		{
			// RW 0x44FC51 .. 0x44FD4E: radius max(2, floor(radius * scale + 0.5)); each row's two ends, then the top row's span and its mirror
			const int r = std::max(2, (int)std::floor((double)o.radar.boundingRadius * (double)xs + 0.5));
			const std::vector<HorzLine> edges = discreteCircle(x, y, r);
			if (edges.empty())
			{
				break;
			}
			const HorzLine *top = &edges[0];
			for (const HorzLine &e : edges)
			{
				surf.put(e.xStart, e.yPos, c); // RW 0x44DE11
				surf.put(e.xEnd, e.yPos, c);
				surf.put(e.xStart, 2 * y - e.yPos, c);
				surf.put(e.xEnd, 2 * y - e.yPos, c);
				if (top->yPos < e.yPos)
				{
					top = &e;
				}
			}
			for (int px = top->xStart; px < top->xEnd; ++px)
			{
				surf.put(px, top->yPos, c);
				surf.put(px, 2 * y - top->yPos, c);
			}
			break;
		}
		default:
			// RW 0x44FD50 .. 0x44FDC0: the stealth check, then (x, y), (x - 1, y), (x - 1, y - 1), (x, y - 1)
			if (!stealthBlink(o.stealthLook, clientFrame, c))
			{
				break;
			}
			surf.put(x, y, c);
			surf.put(x - 1, y, c);
			surf.put(x - 1, y - 1, c);
			surf.put(x, y - 1, c);
			break;
		}
	}
}

bool Radar::viewBoxCorners(float ulx, float uly, int width, int height, float out[8]) const
{
	if (!m_ready)
	{
		return false;
	}
	// RW 0x44D2C4: TheTacticalView vslot 0x34, ZH View::getScreenCornerWorldPointsAtZ: the view's corners (0, 0), (w, 0), (w, h), (0, h) at the terrain average
	// height (Radar + 0x1C). INFERENCE (stop S-2455): retail rebuilds the box's shape only when the zoom or the angle changed (RW 0x450274 .. 0x4502CB) and moves
	// it with the camera's position (RW 0x44D4A9 .. 0x44D4C3); here the corners are projected at every draw
	const ICoord2D s = m_ctx.view.size();
	const ICoord2D corners[4] = { { 0, 0 }, { s.x, 0 }, { s.x, s.y }, { 0, s.y } };
	const float kx = (m_maxX - m_minX) * 0.0078125f, ky = (m_maxY - m_minY) * 0.0078125f; // RW 0xBDAD58
	for (int i = 0; i < 4; ++i)
	{
		Coord3D o, d;
		if (!m_ctx.view.screenToRay(corners[i], o, d) || d.z == 0.0f)
		{
			return false;
		}
		const float t = (m_terrainAverageZ - o.z) / d.z;
		if (!(t > 0.0f))
		{
			return false;
		}
		const float rx = (o.x + d.x * t) / kx, ry = (o.y + d.y * t) / ky;
		// RW 0x44D41B: x * width / 128 + ul.x, (127 - y) * height / 128 + ul.y
		out[i * 2] = rx * (float)width / 128.0f + ulx;
		out[i * 2 + 1] = (127.0f - ry) * (float)height / 128.0f + uly;
	}
	return true;
}

float Radar::viewBoxThickness(int imageWidth, unsigned displayWidth, const float corners[8])
{
	// RW 0x503C33 .. 0x503D0C: the shortest edge (sqrt of the squared length), the band image width x the display width / 1024, at most that edge
	float shortest = 3.40282347e+38f; // RW 0xBD1910
	for (int i = 0; i < 4; ++i)
	{
		const int n = (i + 1) % 4;
		const float dx = corners[n * 2] - corners[i * 2], dy = corners[n * 2 + 1] - corners[i * 2 + 1];
		const float len = (float)std::sqrt((double)dx * (double)dx + (double)dy * (double)dy);
		if (shortest > len)
		{
			shortest = len;
		}
	}
	float t = (float)((double)displayWidth * (double)imageWidth * (double)0.0009765625f);
	if (t > shortest)
	{
		t = shortest;
	}
	return t;
}

void Radar::viewBoxBand(const float corners[8], float thickness, float outer[8], float inner[8])
{
	// RW 0x503D23 .. 0x503EC9: each corner moved by half the band along the bisector of its two edges, one copy inwards (+ 0x48), one outwards (+ 0x68)
	for (int i = 0; i < 4; ++i)
	{
		const float cx = corners[i * 2], cy = corners[i * 2 + 1];
		const int n = (i + 1) % 4, p = (i + 3) % 4;
		const float nx = corners[n * 2], ny = corners[n * 2 + 1], px = corners[p * 2], py = corners[p * 2 + 1];
		outer[i * 2] = inner[i * 2] = cx;
		outer[i * 2 + 1] = inner[i * 2 + 1] = cy;
		if ((cx == nx && cy == ny) || (cx == px && cy == py))
		{
			continue;
		}
		const float toNext = angleOf(nx - cx, ny - cy), toPrev = angleOf(px - cx, py - cy);
		const float half = wrapAngle(angleDiff(toPrev, toNext) * 0.5f + toNext);
		const float d = angleDiff(half, toNext);
		if (!(std::fabs((double)d) > 1e-5))
		{
			continue;
		}
		const float len = (thickness * 0.5f) / (float)std::sin((double)d);
		const float dx = (float)std::cos((double)half) * len, dy = (float)std::sin((double)half) * len;
		inner[i * 2] += dx;
		inner[i * 2 + 1] += dy;
		outer[i * 2] -= dx;
		outer[i * 2 + 1] -= dy;
	}
}

// ---- lane RADAR-1: the radar events and their pings ---------------------------------------------------------------------------------------------------------
void Radar::createEvent(const Coord3D &world, int type, float seconds)
{
	if (!m_ready)
	{
		return;
	}
	// RW 0x6D93EB: worldToRadar (RW 0x6D881F: (int)(x / sample) clamped to 0 .. 127)
	const float xSample = (m_maxX - m_minX) / (float)kCells, ySample = (m_maxY - m_minY) / (float)kCells;
	RadarEventRecord &e = m_radarEvents[m_nextEvent];
	if (e.ping >= 0)
	{
		m_pings[(size_t)e.ping].released = true; // RW 0x6D93EB: the record's ping reference is cleared (RW 0x6D8D1C)
	}
	e.type = type;
	e.active = true;
	e.createFrame = m_clientFrame;
	// dieFrame = ftol(now + 30 * seconds), fadeFrame = ftol(dieFrame - 30 * 0.5) (x87, RW 0x6D9464 .. 0x6D94BB)
	e.dieFrame = (unsigned)(long long)((double)m_clientFrame + (double)kClientFramesPerSecond * (double)seconds);
	e.fadeFrame = (unsigned)(long long)((double)e.dieFrame - (double)kClientFramesPerSecond * 0.5);
	e.worldLoc = world;
	e.radarX = std::clamp((int)(world.x / xSample), 0, kCells - 1);
	e.radarY = std::clamp((int)(world.y / ySample), 0, kCells - 1);
	e.soundPlayed = false;
	e.ping = -1;
	if (++m_nextEvent >= kMaxEvents)
	{
		m_nextEvent = 0;
	}
}

bool Radar::tryEvent(int type, const Coord3D &world)
{
	// RW 0x6D9B4D: the squared distance (SSE single) against 360000 (RW 0xC16784), the frames since the record's creation (unsigned) against 30 * 10
	if (type >= RADAR_EVENT_INVALID)
	{
		return false;
	}
	const unsigned now = m_clientFrame, between = kClientFramesPerSecond * 10u;
	for (const RadarEventRecord &e : m_radarEvents)
	{
		if (e.type != type)
		{
			continue;
		}
		const float dy = world.y - e.worldLoc.y, dx = world.x - e.worldLoc.x;
		if (!(360000.0f < dy * dy + dx * dx) && now - e.createFrame < between)
		{
			return false;
		}
	}
	createEvent(world, type, 4.0f);
	return true;
}

bool Radar::tryUnderAttackEvent(const Coord3D &position)
{
	// RW 0x6D9BF3: tryEvent(UNDER_ATTACK) at the object; when it made one: the control bar's flash (RW 0x71AEC9), the message RADAR:UnitUnderAttack /
	// HarvesterUnderAttack / StructureUnderAttack / UnderAttack and the MiscAudio under-attack sound are not ported (stop S-2452)
	return tryEvent(RADAR_EVENT_UNDER_ATTACK, position);
}

void Radar::noteAttacks(GameLogic &logic)
{
	// RW 0x67B54C .. 0x67B57B: the controlling player is the local player and the object is on the radar (+ 0x268). Run between logic frames (InGameHud::update
	// runs while the logic is idle). INFERENCE (stop S-2457): a hit is reported once per object and attack frame (a hit of the last frame or later the first
	// time an object is seen), in the object list's order (retail: per hit, in damage order)
	const UnsignedInt frame = logic.getFrame();
	const Player *local = logic.players().getLocalPlayer();
	++m_attackStats[0];
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		const UnsignedInt f = o->radarAttackFrame();
		if (f != 0xFFFFFFFFu)
		{
			++m_attackStats[1];
		}
		if (f == 0xFFFFFFFFu || !local || o->getControllingPlayer() != local)
		{
			continue;
		}
		++m_attackStats[2];
		auto it = m_attackSeen.find(o->getID());
		if (it != m_attackSeen.end() ? it->second == f : f + 1u < frame)
		{
			m_attackSeen[o->getID()] = f;
			continue;
		}
		m_attackSeen[o->getID()] = f;
		ObjectSnapshot::RadarEntry e;
		captureObject(logic, *o, local, e);
		if (e.listed)
		{
			tryUnderAttackEvent(*o->getPosition());
		}
	}
}

const double Radar::kClientFrameSeconds = 1.0 / 30.0;

unsigned Radar::advanceClock(double seconds)
{
	m_clockSeconds += seconds;
	unsigned ticks = 0;
	while (m_clockSeconds >= kClientFrameSeconds)
	{
		m_clockSeconds -= kClientFrameSeconds;
		++m_clientFrame;
		update();
		++ticks;
	}
	return ticks;
}

void Radar::update()
{
	// RW 0x6D8E41: mov byte [this + 0xD], 1 (this is the secondary base at Radar + 4: Radar + 0x11, the forced flag W3DRadar::draw tests at RW 0x44FE19)
	m_forced = true;
	// RW 0x6D8E49 .. 0x6D8E6D: active, created (frame != 0) and the client frame past the die frame: inactive, the ping reference cleared. The terrain
	// refresh of RW 0x6D8E6F .. 0x6D8EB5 (+ 0x145C: a queued refreshTerrain after 3 x RW 0xD9F608 logic frames) is not ported (no terrain change reaches the radar)
	const unsigned now = m_clientFrame;
	for (RadarEventRecord &e : m_radarEvents)
	{
		if (e.active && e.createFrame != 0 && now > e.dieFrame)
		{
			e.active = false;
			if (e.ping >= 0)
			{
				m_pings[(size_t)e.ping].released = true;
				e.ping = -1;
			}
		}
	}
}

const char *Radar::pingName(int type)
{
	// RW 0xBDAD30: { 6 PingBeacon, 3 PingAttack, 1 PingGeneric, 0 PingInformation, 2 PingUpgrade } searched over 5 rows (RW 0x44DEF3)
	switch (type)
	{
	case 6: return "PingBeacon";
	case 3: return "PingAttack";
	case 1: return "PingGeneric";
	case 0: return "PingInformation";
	case 2: return "PingUpgrade";
	default: return nullptr;
	}
}

bool Radar::picture(int out[4]) const
{
	for (int i = 0; i < 4; ++i)
	{
		out[i] = m_picture[i];
	}
	return m_picture[2] > 0 && m_picture[3] > 0;
}

void Radar::drawEvents(int ulx, int uly, int width, int height, std::vector<PingCall> &calls)
{
	const unsigned now = m_clientFrame;
	auto fade = [&calls](Ping &p) {
		// RW 0x6D5BD9: once; the movie hears it only when the ping was created
		if (!p.faded)
		{
			if (p.created)
			{
				PingCall c;
				c.kind = PingCall::FadeOut;
				c.id = p.id;
				calls.push_back(c);
			}
			p.faded = true;
		}
	};
	for (RadarEventRecord &e : m_radarEvents)
	{
		// RW 0x44DE78 .. 0x44DE86: active, not FAKE
		if (!e.active || e.type == RADAR_EVENT_FAKE)
		{
			continue;
		}
		// RW 0x44DE8C .. 0x44DEE3: the first draw of a non-beacon event plays the RadarEvent sound (not ported, stop S-2452)
		e.soundPlayed = true;
		if (e.ping < 0)
		{
			// RW 0x44DEEB .. 0x44DF33: the type's ping (AptPalantir RW 0x6D60BF -> RW 0x6D600F: a new RadarPing in the Palantir's list)
			if (const char *name = pingName(e.type))
			{
				Ping p;
				p.id = m_nextPingId++;
				if (m_nextPingId >= 0xFFFF) // RW 0x6D4D43
				{
					m_nextPingId = 0;
				}
				p.name = name;
				e.ping = (int)m_pings.size();
				m_pings.push_back(p);
			}
		}
		if (e.ping < 0)
		{
			continue;
		}
		Ping &p = m_pings[(size_t)e.ping];
		// RW 0x44DF56: radarToPixel (RW 0x44D3D8, integers), then the ping's DoMove (vtable + 8, RW 0x6D5B23)
		const float px = (float)(e.radarX * width / kCells + ulx), py = (float)((kCells - 1 - e.radarY) * height / kCells + uly);
		// RW 0x6D5B28 .. 0x6D5B65: only when x moved half a pixel (the second test reads fabs(0): y alone never moves it)
		if (std::fabs((double)(px - p.x)) >= 0.5)
		{
			if (p.created)
			{
				PingCall c;
				c.kind = PingCall::Move;
				c.id = p.id;
				c.x = px;
				c.y = py;
				calls.push_back(c);
			}
			p.x = px;
			p.y = py;
		}
		// RW 0x44DF79 .. 0x44DF8C: after the fade frame, the ping's fade (vtable + 0xC)
		if (now > e.fadeFrame)
		{
			fade(p);
		}
	}
	// the Palantir's update (RW 0x6D7B00 .. 0x6D7B16: every ping of its list): a new ping is created at its last position (RW 0x6D5A9D)
	for (Ping &p : m_pings)
	{
		if (p.released)
		{
			fade(p); // the ping's release (RW 0x6D638B) fades it
			continue;
		}
		if (!p.created)
		{
			PingCall c;
			c.kind = PingCall::Create;
			c.id = p.id;
			c.name = p.name;
			calls.push_back(c);
			c.kind = PingCall::Move;
			c.name.clear();
			c.x = p.x;
			c.y = p.y;
			calls.push_back(c);
			p.created = true;
		}
	}
	// a released ping is faded above and leaves the list (the RadarPing's last reference, RW 0x6D638B); the events' indices follow
	std::vector<int> moved(m_pings.size(), -1);
	size_t kept = 0;
	for (size_t i = 0; i < m_pings.size(); ++i)
	{
		if (!m_pings[i].released)
		{
			moved[i] = (int)kept;
			if (kept != i)
			{
				m_pings[kept] = std::move(m_pings[i]);
			}
			++kept;
		}
	}
	m_pings.resize(kept);
	for (RadarEventRecord &e : m_radarEvents)
	{
		if (e.ping >= 0)
		{
			e.ping = moved[(size_t)e.ping];
		}
	}
}

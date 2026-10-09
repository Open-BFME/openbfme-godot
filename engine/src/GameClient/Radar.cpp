// OpenBFME. GPL-3.0.
// See Radar.h.

#include "GameClient/Radar.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/Player.h"
#include "GameClient/TGAFile.h"
#include "GameClient/HudObjects.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/System/InvisibilityManager.h"
#include "GameLogic/System/ShroudManager.h"

#include <algorithm>
#include <cmath>

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
		"[S-291] radar: a map with a <map>_art.tga shows it (W3DRadar RW 0x44F3AB) under the ScrollShroud shroud (RW 0x44E3B6); a map without art shows a shaded height field, "
		"not W3DRadar's terrain texture (RW 0x44F4AD loop not ported); the blips are the owners' colours (the radar upgrade, hidden radar, object icons, attack blinks, the "
		"+0x1494 image and radar events are not ported); the view box is the camera's ground footprint (a corner that misses the ground hides the box)",
		"[S-761] radar art / shroud (HUD-2): INFERENCE: texels no shroud cell wrote keep ScrollShroud's own alpha (the texture's initial alpha after the copy at RW 0x44EF41 "
		"was not read); ScrollShroud's DDS is shown v-flipped like every radar image (UV (0,1)-(1,0)); an art TGA other than uncompressed 24/32-bit is refused with an "
		"error (retail's texture loader takes it)",
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

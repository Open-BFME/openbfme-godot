// OpenBFME. GPL-3.0. See W3DStreakDraw.h (RW 0x4CFD13 / 0xBE39F8 / 0x4CF884, donor BFME1 0x77D6B0).

#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DStreakDraw.h"

#include <cmath>
#include <cstddef>

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

namespace
{
// RW 0xDA3A84
const char *const kWeatherNames[] = { "NORMAL", "SNOWY", nullptr };

// RW 0x4CFEB9: WeatherTexture = WEATHER NAME (parseIndexList over RW 0xDA3A84, then parseAsciiString), appended.
void parseWeatherTexture(INI *ini, void *, void *store, const void *)
{
	const int weather = INI::scanIndexList(ini->getNextToken(), kWeatherNames);
	const std::string name = ini->getNextAsciiString();
	static_cast<std::vector<std::pair<int, std::string>> *>(store)->emplace_back(weather, name);
}

#define STREAK_ROW(name, parse, field) { name, parse, nullptr, (int)offsetof(W3DStreakDrawModuleData, field) }
// RW 0xBE39F8, 7 rows in the binary's order
const FieldParse kStreakFieldParse[] = {
	STREAK_ROW("Length", INI::parseReal, m_length),
	STREAK_ROW("Width", INI::parseReal, m_width),
	STREAK_ROW("Additive", INI::parseBool, m_additive),
	STREAK_ROW("Color", INI::parseRGBColor, m_color),
	STREAK_ROW("Texture", INI::parseAsciiString, m_texture),
	STREAK_ROW("NumSegments", INI::parseUnsignedInt, m_numSegments),
	STREAK_ROW("WeatherTexture", parseWeatherTexture, m_weatherTextures),
	{ nullptr, nullptr, nullptr, 0 }
};
#undef STREAK_ROW

// RW 0x4CF8E5 ..: float32 squares summed z, y, x (SSE), the root by x87 fsqrt stored to float
float distance(const float a[3], const float b[3])
{
	const float x = a[0] - b[0];
	const float y = a[1] - b[1];
	const float z = a[2] - b[2];
	const float square = z * z + y * y + x * x;
	return (float)std::sqrt((double)square);
}

void setPoint(W3DStreakPoint &p, const float pos[3], float length)
{
	p.pos[0] = pos[0];
	p.pos[1] = pos[1];
	p.pos[2] = pos[2];
	p.length = length;
}

} // namespace

void W3DStreakDrawModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kStreakFieldParse); // registry: tables [0xBE39F8]
}

namespace W3DStreakDrawTables
{
const FieldParse *streak() { return kStreakFieldParse; }
}

void W3DStreakTrail::update(const W3DStreakDrawModuleData &data, const float position[3])
{
	W3DStreakPoint np;
	const size_t n = m_points.size();
	if (n == 0)
	{
		setPoint(np, position, 0.0f);
		m_points.push_back(np);
		return;
	}
	if (n == 1)
	{
		setPoint(np, position, distance(position, m_points[0].pos) + m_points[0].length);
		m_points.push_back(np);
		return;
	}
	const float d = distance(position, m_points[n - 2].pos);
	const float len = d + m_points[n - 2].length;
	setPoint(m_points[n - 1], position, len);
	// RW 0x4CF9B1: Length / (float)NumSegments (the unsigned int converted with the 2^32 correction); a zero count divides to +inf
	const float segment = data.m_length / (float)data.m_numSegments;
	if (d > segment)
	{
		setPoint(np, position, len);
		m_points.push_back(np);
	}
	while (m_points.size() >= 2 && len - data.m_length > m_points[1].length)
	{
		m_points.erase(m_points.begin());
	}
}

void W3DStreakDiagnostics::recordFailure(const std::string &materialKey, const std::string &message)
{
	m_failed.insert(materialKey);
	m_errors.push_back(message);
}

std::string W3DStreakDrawStopLine()
{
	return "[S-391] streak drawing: W3DStreakDraw's points follow RW 0x4CF884; lane PROJ-2 (S-1002) ported the texture's V along the trail (RW 0x525E1F: 1 at the "
		"head, 0 one Length behind), the blend of the two shader presets (RW 0x4CFA9A: Additive ONE / ONE, else SRC_ALPHA / INV_SRC_ALPHA), the colour and opacity "
		"(RW 0x4CFA25) and the WeatherTexture choice; the strip is a camera-facing ribbon of Width (the rest of RW 0x525E1F's vertex construction not compared)";
}

std::string W3DStreakRenderStopLine()
{
	return "[S-1002] streak render (lane PROJ-2): ported from RotWK: the texture V along the trail (RW 0x525E1F: v = (point length - (head length - Length)) / Length, "
		"the head at V 1), the shader presets (RW 0x4CFA9A: Additive DAT 0xD9B2E4 = SRCBLEND ONE / DSTBLEND ONE, else DAT 0xD9B2F0 = SRC_ALPHA / INV_SRC_ALPHA), "
		"the colour and the drawable's opacity (RW 0x4CF884), the WeatherTexture of the map's weather; not read / inference: the primary gradient 6 of both presets "
		"(drawn as the texture times the colour), the texture address mode (clamped here: the oldest point's V can be below 0), the rest of RW 0x525E1F's vertex "
		"construction (a camera-facing ribbon of Width here)";
}

const std::string &W3DStreakTexture(const W3DStreakDrawModuleData &data, int weather)
{
	for (const std::pair<int, std::string> &w : data.m_weatherTextures)
	{
		if (w.first == weather)
		{
			return w.second; // RW 0x4CFA9A: the first entry of the current weather (GlobalData +0x138)
		}
	}
	return data.m_texture;
}

std::vector<W3DStreakVertex> W3DStreakStrip(const std::vector<W3DStreakPoint> &points, float width, float length, const float eye[3])
{
	std::vector<W3DStreakVertex> out;
	const size_t n = points.size();
	if (n < 2 || !(length > 0.0f))
	{
		return out;
	}
	const float head = points[n - 1].length;
	if (!(head - points[0].length > 0.0f))
	{
		return out;
	}
	out.reserve(n * 2);
	const float half = width * 0.5f;
	const float inv = 1.0f / length;     // RW 0x525E1F: 1.0 / the renderer's length (+0x1C, the streak's Length at object +0x100)
	const float base = head - length;    // RW 0x525E1F: the newest point's length less Length
	for (size_t i = 0; i < n; ++i)
	{
		const W3DStreakPoint &a = points[i == 0 ? 0 : i - 1];
		const W3DStreakPoint &b = points[i + 1 < n ? i + 1 : n - 1];
		float t[3] = { b.pos[0] - a.pos[0], b.pos[1] - a.pos[1], b.pos[2] - a.pos[2] };
		const float *p = points[i].pos;
		const float e[3] = { eye[0] - p[0], eye[1] - p[1], eye[2] - p[2] };
		// side = normalize(t x e): perpendicular to the trail and to the view ray
		float s[3] = { t[1] * e[2] - t[2] * e[1], t[2] * e[0] - t[0] * e[2], t[0] * e[1] - t[1] * e[0] };
		const float sl = std::sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
		if (sl > 0.0f)
		{
			for (float &c : s) c *= half / sl;
		}
		// RW 0x525E1F: v = (length of the point - (length of the newest point - Length)) / Length; the first vertex of a point takes u 0, the second u 1
		const float v = (points[i].length - base) * inv;
		W3DStreakVertex l = { { p[0] - s[0], p[1] - s[1], p[2] - s[2] }, 0.0f, v };
		W3DStreakVertex r = { { p[0] + s[0], p[1] + s[1], p[2] + s[2] }, 1.0f, v };
		out.push_back(l);
		out.push_back(r);
	}
	return out;
}

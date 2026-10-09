// OpenBFME. GPL-3.0.
//
// W3DStreakDraw: the trail draw of arrows and other fast projectiles (lane RENDER-1, stops S-365 / S-391). Client / draw only.
// ZH has no such module; RotWK and BFME1/2 do.
//
// TARGET (RotWK game.dat, read statically):
//   * module data: createData RW 0x46473E (object size 0x34), constructor RW 0x4CFD13: Length 50.0 (RW 0xBD88C4) at +0x08,
//     Width 0.5 (RW 0xBD869C) at +0x0C, Additive true at +0x10, Color (1, 1, 1) (RW 0xBD1908) at +0x14, NumSegments 5 at +0x20,
//     Texture "" at +0x24, the weather texture list at +0x28; field table RW 0xBE39F8 (parse RW 0x4CFF19): Length / Width
//     parseReal RW 0x42ED00, Additive parseBool RW 0x42E558, Color parseRGBColor RW 0x42EF99, Texture parseAsciiString RW 0x42EE5E,
//     NumSegments parseUnsignedInt RW 0x42ECB2, WeatherTexture RW 0x4CFEB9 (weather word over RW 0xDA3A84 NORMAL / SNOWY, then the
//     name; appended).
//   * per draw (RW 0x4CF884): with the drawable's position P and the streak's points (position, accumulated length):
//       no point: add (P, 0); one point: add (P, |P - p0| + len0);
//       else: d = |P - p[n-2]|, len = d + len[n-2]; the last point becomes (P, len); when d > Length / NumSegments a point (P, len) is
//       added; then while there are 2 points or more and len - Length > len[1], point 0 is deleted.
//     The distance is a float32 sum of squares (SSE) and an x87 sqrt. The streak's colour is Color times the drawable's opacity
//     (RW 0x672FC4) when Additive (opacity 1), else Color with that opacity.
// DONOR: BFME1 W3DStreakDraw (Open-BFME-1 W3DStreakDrawBody.cpp, retail 0x77D6B0) has the same point logic.
//   * the render object (lane PROJ-2, stop S-1002): RW 0x4CFA9A makes it (RW 0x567CA9, the renderer at +0xE4) with the texture of the current weather
//     (the WeatherTexture entry whose weather is GlobalData +0x138, else Texture), the shader preset DAT 0xD9B2E4 (Additive: SRCBLEND ONE, DSTBLEND ONE,
//     no depth write, no culling) or DAT 0xD9B2F0 (not Additive: SRC_ALPHA, INV_SRC_ALPHA), Width, Color and Length (RW 0x5677B2: object +0x100, the
//     renderer's +0x1C). RW 0x525E1F (the renderer's draw) maps the texture V along the points: v = (point length - (newest point length - Length)) /
//     Length, so the head (the newest point) is at V 1 and the texture spans one Length behind it; U is 0 / 1 across. The retail arrow textures (16 x 64 /
//     32 x 64) have the fletching at V 0 and the head at V 1; EXArrowStreakFire's flame pixels are nearly transparent (alpha 9) and only show added ONE / ONE.
// INFERENCE (stop S-391 / S-1002): the strip is drawn here as a camera-facing ribbon of `Width` through the points; the rest of RW 0x525E1F's vertex
// construction was not compared.

#pragma once

#include "Common/INI.h"
#include "Common/INIDataTypes.h"
#include "Common/Module.h"

#include <set>
#include <string>
#include <utility>
#include <vector>

class W3DStreakDrawModuleData : public ModuleData
{
public:
	W3DStreakDrawModuleData() = default;
	static void buildFieldParse(MultiIniFieldParse &p);

	float m_length = 50.0f;     ///< Length (RW +0x08)
	float m_width = 0.5f;       ///< Width (+0x0C)
	bool m_additive = true;     ///< Additive (+0x10)
	RGBColor m_color = { 1.0f, 1.0f, 1.0f }; ///< Color, 0..1 (+0x14)
	unsigned m_numSegments = 5; ///< NumSegments (+0x20)
	std::string m_texture;      ///< Texture (+0x24)
	std::vector<std::pair<int, std::string>> m_weatherTextures; ///< WeatherTexture = WEATHER NAME (+0x28), file order
};

namespace W3DStreakDrawTables
{
const FieldParse *streak(); ///< RW 0xBE39F8, 7 rows
}

struct W3DStreakPoint
{
	float pos[3] = { 0, 0, 0 };
	float length = 0.0f; ///< accumulated length at this point
};

// The points of one streak (RW 0x4CF884).
class W3DStreakTrail
{
public:
	void update(const W3DStreakDrawModuleData &data, const float position[3]);
	const std::vector<W3DStreakPoint> &points() const { return m_points; }
	void clear() { m_points.clear(); }

private:
	std::vector<W3DStreakPoint> m_points;
};

struct W3DStreakVertex
{
	float pos[3];
	float u, v;
};

// The ribbon through the points (inference S-391): two vertices per point (left, right), as a triangle strip per streak; empty with
// fewer than two points or a zero total length. `eye` is the camera position (same space as the points).
// The texture failures of the streak draw (review r2): a material whose requested texture did not load is not drawn, and the
// failure is reported. Both live per scene: beginScene() (a map load / clear) forgets the failed keys AND the messages together,
// so the next scene asks for the texture again and reports it again instead of skipping silently.
class W3DStreakDiagnostics
{
public:
	void beginScene() { m_failed.clear(); m_errors.clear(); }
	bool skipped(const std::string &materialKey) const { return m_failed.count(materialKey) != 0; }
	void recordFailure(const std::string &materialKey, const std::string &message);
	void recordError(const std::string &message) { m_errors.push_back(message); }
	const std::vector<std::string> &errors() const { return m_errors; }
	size_t failedCount() const { return m_failed.size(); }

private:
	std::set<std::string> m_failed;
	std::vector<std::string> m_errors;
};

// The run report line of stop S-391.
std::string W3DStreakDrawStopLine();
// The run report line of stop S-1002 (lane PROJ-2: the retail streak render, what is ported and what is not)
std::string W3DStreakRenderStopLine();

// RW 0x525E1F's V: 1 at the head, 0 one `length` (the module's Length) behind it
std::vector<W3DStreakVertex> W3DStreakStrip(const std::vector<W3DStreakPoint> &points, float width, float length, const float eye[3]);

// RW 0x4CFA9A: the texture for the map's weather (`weather`: the WeatherType index of the WeatherTexture list, 0 NORMAL, 1 SNOWY)
const std::string &W3DStreakTexture(const W3DStreakDrawModuleData &data, int weather);

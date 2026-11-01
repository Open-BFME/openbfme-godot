// OpenBFME. GPL-3.0.
// See GameClient/WaterGeometry.h.

#include "GameClient/WaterGeometry.h"
#include "Common/AsciiString.h"
#include "Libraries/WWVegas/WW3D2/meshmdl.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <locale>
#include <stdexcept>
#include <limits>

namespace
{

// RW 0x70E911 (RenderableStandingWaterArea::AllocateAndFillBuffers' cell test, RW 0x482A16). The donor matched
// in Open-BFME-2 is rva0030B7C2 / rva0030B5AF (GameLogic/Map/Rva0030B7C2PointQuery.cpp). Target facts, read from RW:
// a bounds precheck (lo.x > x, lo.y > y, x > hi.x, y > hi.y reject), then crossing parity over every
// vertex i against its predecessor (the last for i 0): skip a horizontal edge, skip when x is right of both
// ends, order the edge by y, skip unless a.y < y <= b.y, then toggle when (y - a.y) * (b.x - a.x) >=
// (x - a.x) * (b.y - a.y) (single-precision products). A polygon that revisits a vertex, as the pinched river
// outline of Adorn River does, needs no special case: parity counts every edge.
bool standingWaterContains(const std::vector<Point2F> &points, const Point2F &lo, const Point2F &hi, const Point2F &p)
{
	if (lo.x > p.x || lo.y > p.y || p.x > hi.x || p.y > hi.y)
	{
		return false;
	}
	bool inside = false;
	const size_t n = points.size();
	for (size_t i = 0; i < n; ++i)
	{
		Point2F a = points[i];
		Point2F b = points[i > 0 ? i - 1 : n - 1];
		if (a.y == b.y)
		{
			continue;
		}
		if (p.x > a.x && p.x > b.x)
		{
			continue;
		}
		if (a.y > b.y)
		{
			std::swap(a, b);
		}
		if (p.y > b.y || a.y >= p.y)
		{
			continue;
		}
		const float dy = b.y - a.y;
		const float lhs = (p.y - a.y) * (b.x - a.x);
		const float rhs = (p.x - a.x) * dy;
		if (lhs >= rhs)
		{
			inside = !inside;
		}
	}
	return inside;
}

} // namespace


bool WaterGeometry::buildRiverStrip(const RiverArea &river, RiverStrip &out)
{
	out = RiverStrip();
	const size_t n = river.lines.size();
	if (n < 2)
	{
		return false;
	}
	const std::vector<RiverArea::Line> &l = river.lines;
	auto dist = [](float x0, float y0, float x1, float y1) { return std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0)); };
	// the outline: first[0], second[0..n-1], first[n-1..1] (the closing edge first[1] -> first[0] left out)
	const float end = dist(l[0].x0, l[0].y0, l[0].x1, l[0].y1);
	float total = end + dist(l[n - 1].x1, l[n - 1].y1, l[n - 1].x0, l[n - 1].y0);
	for (size_t i = 1; i < n; ++i)
	{
		total += dist(l[i - 1].x1, l[i - 1].y1, l[i].x1, l[i].y1);
		if (i > 1)
		{
			total += dist(l[i - 1].x0, l[i - 1].y0, l[i].x0, l[i].y0);
		}
	}
	const float vScale = end > 0.0f ? ((total / 2.0f - end) / end) / (float)(n - 1) : 0.0f;
	const float z = (float)river.waterHeight;
	for (size_t i = 0; i < n; ++i)
	{
		const float v = vScale * (float)i;
		out.position.insert(out.position.end(), { l[i].x1, l[i].y1, z, l[i].x0, l[i].y0, z });
		out.uv0.insert(out.uv0.end(), { 0.5f, v, 0.0f, v });
		out.uv1.insert(out.uv1.end(), { 1.0f, v, 0.0f, v });
		if (i > 0)
		{
			const std::uint32_t a = (std::uint32_t)(2 * (i - 1));
			out.index.insert(out.index.end(), { a, a + 1, a + 3, a, a + 3, a + 2 });
		}
	}
	return true;
}

void WaterGeometry::riverVertexColour(const RiverArea &river, const TimeOfDayLights *tod, const float waterDiffuse[4], float out[4])
{
	if (river.r == 255 && river.g == 255 && river.b == 255)
	{
		for (int c = 0; c < 3; ++c)
		{
			float v = tod ? tod->terrain[0].ambient[c] : 0.0f;
			for (int i = 0; tod && i < 3; ++i)
			{
				const float down = -tod->terrain[i].lightPos[2];
				if (down > 0.0f)
				{
					v += down * tod->terrain[i].diffuse[c];
				}
			}
			out[c] = v * waterDiffuse[c];
		}
	}
	else if (river.r == 0 && river.g == 0 && river.b == 0)
	{
		out[0] = out[1] = out[2] = 1.0f;
	}
	else
	{
		out[0] = river.r / 255.0f;
		out[1] = river.g / 255.0f;
		out[2] = river.b / 255.0f;
	}
	// the game packs these into colour bytes unclamped; past 255 a channel would spill into the next, which no retail
	// lighting reaches
	for (int c = 0; c < 3; ++c)
	{
		out[c] = std::min(std::max(out[c], 0.0f), 1.0f);
	}
	out[3] = waterDiffuse[3];
}

// Target data: watershader.fxo parameters and ps blob 8 / vs blob 9, decoded
// with tools/render/{fx_params,d3d9_disasm}. No executable provenance claim.
bool WaterGeometry::readStandingMaterial(const MeshShaderMaterialDef &source, StandingMaterial &out, std::string *error)
{
	if (error) error->clear();
	if (AsciiStringUtil::compareNoCase(source.TypeName, "WaterShader.FX") != 0) return false;
	out = StandingMaterial{};
	out.technique = source.Technique;
	for (const auto &p : source.Properties)
	{
		const std::string n = AsciiStringUtil::lowered(p.Name);
		unsigned expected = 0;
		if (n == "materialcolordiffuse" || n == "gwampfreqphasesteepness1" || n == "gwampfreqphasesteepness2" || n == "gwampfreqphasesteepness3") expected = W3DSHADERMATERIAL_PROPERTY_VECTOR4;
		else if (n == "specularcolor") expected = W3DSHADERMATERIAL_PROPERTY_VECTOR3;
		else if (n == "bumpuvscale" || n == "envuvscale" || n == "waveuvscale" || n == "pcabumpuvscale" || n == "pcawaveuvscale") expected = W3DSHADERMATERIAL_PROPERTY_VECTOR2;
		else if (n == "blendusingterrainalpha") expected = W3DSHADERMATERIAL_PROPERTY_BOOL;
		else if (n == "dimmingfactor" || n == "bumpscale" || n == "specularexponent" || n == "windangle" || n == "windspeed" || n == "waveangle" || n == "wavespeed" || n == "reflectionperturbation") expected = W3DSHADERMATERIAL_PROPERTY_FLOAT;
		else if (n == "waterpcatexture1" || n == "waterpcatexture2" || n == "waterpcatexture3" || n == "pcanoisetexture" || n == "pcafrothtexture" || n == "waterbumptexture" || n == "waterenvtexture" || n == "wavetexture") expected = W3DSHADERMATERIAL_PROPERTY_TEXTURE;
		const bool wider = expected >= W3DSHADERMATERIAL_PROPERTY_VECTOR2 && expected <= W3DSHADERMATERIAL_PROPERTY_VECTOR4
			&& p.Type > expected && p.Type <= W3DSHADERMATERIAL_PROPERTY_VECTOR4;
		if (expected && p.Type != expected && !wider)
		{
			if (error) *error = "WaterShader.FX property " + p.Name + ": expected type " + std::to_string(expected) + ", got " + std::to_string(p.Type);
			out = StandingMaterial{};
			return false;
		}

		if (n == "materialcolordiffuse") std::copy(p.Float, p.Float + 4, out.diffuse);
		else if (n == "gwampfreqphasesteepness1") std::copy(p.Float, p.Float + 4, out.gerstner[0]);
		else if (n == "gwampfreqphasesteepness2") std::copy(p.Float, p.Float + 4, out.gerstner[1]);
		else if (n == "gwampfreqphasesteepness3") std::copy(p.Float, p.Float + 4, out.gerstner[2]);
		else if (n == "dimmingfactor") out.dimming = p.Float[0];
		else if (n == "bumpscale") out.bumpScale = p.Float[0];
		else if (n == "specularexponent") out.specularExponent = p.Float[0];
		else if (n == "windangle") out.windAngle = p.Float[0];
		else if (n == "windspeed") out.windSpeed = p.Float[0];
		else if (n == "waveangle") out.waveAngle = p.Float[0];
		else if (n == "wavespeed") out.waveSpeed = p.Float[0];
		else if (n == "bumpuvscale") std::copy(p.Float, p.Float + 2, out.bumpUV);
		else if (n == "envuvscale") std::copy(p.Float, p.Float + 2, out.envUV);
		else if (n == "waveuvscale") std::copy(p.Float, p.Float + 2, out.waveUV);
		else if (n == "blendusingterrainalpha") out.blendTerrainAlpha = p.Bool;
		else if (n == "pcabumpuvscale") std::copy(p.Float, p.Float + 2, out.pcaUV);
		else if (n == "pcawaveuvscale") std::copy(p.Float, p.Float + 2, out.pcaWaveUV);
		else if (n == "specularcolor") std::copy(p.Float, p.Float + 3, out.specular);
		else if (n == "reflectionperturbation") out.reflectionPerturbation = p.Float[0];
		else if (n == "waterpcatexture1") out.pcaTexture[0] = p.StringValue;
		else if (n == "waterpcatexture2") out.pcaTexture[1] = p.StringValue;
		else if (n == "waterpcatexture3") out.pcaTexture[2] = p.StringValue;
		else if (n == "pcanoisetexture") out.pcaNoiseTexture = p.StringValue;
		else if (n == "pcafrothtexture") out.pcaFrothTexture = p.StringValue;
		else if (n == "waterbumptexture") out.bumpTexture = p.StringValue;
		else if (n == "waterenvtexture") out.envTexture = p.StringValue;
		else if (n == "wavetexture") out.waveTexture = p.StringValue;
	}
	return true;
}


// BFME2 donor: GameEngine/Source/GameLogic/Map/StandingWaterAreaLoadPCA.cpp
// RVA 0x3093F6. Samplers: GameEngineDevice/Source/W3DDevice/GameClient/
// StandingWaterArea.cpp, RVAs 0x30822F/0x308325/0x308417/0x308509.
// Target data validates the layout; RotWK caller/clock equivalence is S-035.
// Malformed inputs are rejected explicitly; donor malformed-text parity is unverified.
bool WaterGeometry::readPcaBasis(const std::string &text, PcaBasis &out, std::string *error)
{
	out = PcaBasis{};
	auto fail = [&](const char *why) { if (error) *error = why; return false; };
	std::istringstream input(text);
	input.imbue(std::locale::classic());
	std::string line, label;
	unsigned count = 0;
	if (!std::getline(input, line)) return fail("PCA basis: missing frame count");
	std::istringstream header(line);
	header.imbue(std::locale::classic());
	if (!(header >> label >> count) || count == 0 || count > text.size()) return fail("PCA basis: invalid frame count");
	if (!std::getline(input, line)) return fail("PCA basis: missing speed");
	std::istringstream speed(line);
	speed.imbue(std::locale::classic());
	PcaBasis parsed;
	if (!(speed >> label >> parsed.speed) || !std::isfinite(parsed.speed) || parsed.speed < 0.0f) return fail("PCA basis: invalid speed");
	// 13 rows * 3 axes per frame, each requiring at least a digit and a delimiter.
	// Bound from the remaining payload before allocating any frame storage.
	const auto payload = input.tellg();
	if (payload < 0 || count > (text.size() - static_cast<size_t>(payload)) / (13u * 3u * 2u))
		return fail("PCA basis: frame count exceeds coefficient payload");
	parsed.frames.resize(count);
	for (unsigned row = 0; row < 13; ++row)
	{
		if (!std::getline(input, line)) return fail("PCA basis: missing coefficient row");
		std::istringstream values(line);
		values.imbue(std::locale::classic());
		if (!(values >> label)) return fail("PCA basis: missing coefficient label");
		// Disk axis order is X, Z, Y; each group of four rows packs four bases.
		for (unsigned diskAxis = 0; diskAxis < 3; ++diskAxis)
			for (unsigned frame = 0; frame < count; ++frame)
			{
				double value;
				if (!(values >> value) || !std::isfinite(value) || std::abs(value / 255.0) > std::numeric_limits<float>::max()) return fail("PCA basis: invalid coefficient");
				const unsigned axis = diskAxis == 1 ? 2 : diskAxis == 2 ? 1 : 0;
				const float normalized = static_cast<float>(value / 255.0);
				if (row == 0) parsed.frames[frame][0][axis] = normalized;
				else parsed.frames[frame][1 + ((row - 1) / 4) * 3 + axis][(row - 1) % 4] = normalized;
			}
		std::string extra;
		if (values >> extra) return fail("PCA basis: extra coefficient");
	}
	while (std::getline(input, line)) if (line.find_first_not_of(" \t\r") != std::string::npos) return fail("PCA basis: trailing data");
	out = std::move(parsed);
	return true;
}

WaterGeometry::PcaFrame WaterGeometry::samplePcaBasis(const PcaBasis &basis, float clientTime)
{
	PcaFrame result{};
	result[0][2] = 1.0f;
	if (basis.frames.empty() || !std::isfinite(clientTime) || clientTime < 0.0f) return result;
	const float scaled = clientTime * basis.speed;
	if (!std::isfinite(scaled)) return result;
	const double whole = std::floor(static_cast<double>(scaled));
	const size_t index = static_cast<size_t>(std::fmod(whole, static_cast<double>(basis.frames.size())));
	const size_t next = (index + 1) % basis.frames.size();
	const float fraction = static_cast<float>(scaled - whole);
	for (size_t row = 0; row < result.size(); ++row)
		for (size_t c = 0; c < 4; ++c)
			result[row][c] = basis.frames[index][row][c] * (1.0f - fraction) + basis.frames[next][row][c] * fraction;
	return result;
}

bool WaterGeometry::readEffectTechniques(const std::vector<std::uint8_t> &bytes, std::vector<std::string> &names, std::string *error)
{
	names.clear();
	try
	{
		auto word = [&](size_t offset) -> std::uint32_t {
			if (offset > bytes.size() || bytes.size() - offset < 4) throw std::runtime_error("effect table outside file");
			return bytes[offset] | (std::uint32_t(bytes[offset + 1]) << 8) | (std::uint32_t(bytes[offset + 2]) << 16) | (std::uint32_t(bytes[offset + 3]) << 24);
		};
		if (word(0) != 0xfeff0901u) throw std::runtime_error("not an fx_2_0 effect");
		const size_t table = size_t(word(4)) + 8;
		const unsigned parameters = word(table), techniques = word(table + 4);
		size_t cursor = table + 16;
		if (cursor > bytes.size()) throw std::runtime_error("truncated effect header");
		auto skip = [&](size_t size) { if (cursor > bytes.size() || size > bytes.size() - cursor) throw std::runtime_error("truncated effect table"); cursor += size; };
		auto skipRecords = [&](size_t count, size_t stride) {
			if (cursor > bytes.size() || count > (bytes.size() - cursor) / stride) throw std::runtime_error("truncated effect table");
			skip(count * stride);
		};
		for (unsigned i = 0; i < parameters; ++i) { const size_t annotations = word(cursor + 12); skip(16); skipRecords(annotations, 8); }
		for (unsigned i = 0; i < techniques; ++i)
		{
			const size_t stringOffset = size_t(word(cursor)) + 8;
			const size_t annotations = word(cursor + 4), passes = word(cursor + 8);
			const size_t length = word(stringOffset);
			if (!length || stringOffset + 4 > bytes.size() || length > bytes.size() - stringOffset - 4 || bytes[stringOffset + 4 + length - 1] != 0) throw std::runtime_error("invalid effect technique name");
			names.emplace_back(reinterpret_cast<const char *>(bytes.data() + stringOffset + 4), length - 1);
			skip(12); skipRecords(annotations, 8);
			for (size_t pass = 0; pass < passes; ++pass) { const size_t pa = word(cursor + 4), states = word(cursor + 8); skip(12); skipRecords(pa, 8); skipRecords(states, 16); }
		}
		return true;
	}
	catch (const std::runtime_error &e) { names.clear(); if (error) *error = e.what(); return false; }
}

// RotWK RenderableStandingWaterArea::AllocateAndFillBuffers, RW 0x482A16 (donor: Open-BFME-2 W3DWater.cpp,
// native 0x0008304A). This is the only way retail turns a standing-water polygon into triangles: a grid of
// `spacing` cells over the polygon's bounds, each cell kept when its centre is inside the polygon
// (standingWaterContains), two triangles per kept cell. Target facts, read from RW 0x482A16: the
// cell counts and the origin scale the coordinates by the reciprocal 1.0f / spacing (single-precision divide,
// then multiply, then truncate); the cell centre is float(x) * spacing + (origin + spacing * 0.5f); the cell
// limit is 60000 (30000 with 40.0f spacing when the LOD manager's level is <= 2, which Godot has no
// equivalent for); the spacing doubles until the cell count fits. Godot's terrain sampler and the high-detail
// choice are presentation bridges, not verified RotWK LOD/height parity (S-035).
bool WaterGeometry::buildStandingGrid(const StandingWaterArea &area, const std::vector<StandingWaveArea> &waves,
	const std::function<float(float,float)> &ground, StandingGrid &out)
{
	out = StandingGrid{};
	if (area.points.size() < 3) return false;
	float lx=area.points[0].x, hx=lx, ly=area.points[0].y, hy=ly;
	for (const auto &p : area.points)
	{
		// Retail converts coordinates * (1 / 20) to int before reducing detail.
		// Reject unrepresentable mod data instead of invoking undefined C++ casts.
		const double maxCell = std::numeric_limits<int>::max();
		const float firstInverse = 1.0f / out.spacing;
		if (!std::isfinite(p.x) || !std::isfinite(p.y)
			|| std::abs(double(p.x * firstInverse)) >= maxCell
			|| std::abs(double(p.y * firstInverse)) >= maxCell) return false;
		lx = std::min(lx, p.x); hx = std::max(hx, p.x);
		ly = std::min(ly, p.y); hy = std::max(hy, p.y);
	}
	if (!std::isfinite(lx+hx+ly+hy) || hx-lx > 1e8f || hy-ly > 1e8f) return false;
	const Point2F boundsLo = { lx, ly }, boundsHi = { hx, hy }; // the polygon's own bounds, for the cell test
	int nx,ny;
	do {
		const float inverse = 1.0f / out.spacing;
		nx=int(hx*inverse)-int(lx*inverse)+1;
		ny=int(hy*inverse)-int(ly*inverse)+1;
		if (int64_t(nx)*ny <= 60000) break;
		out.spacing *= 2;
	} while (true);
	const float inverse = 1.0f / out.spacing;
	lx=float(int(lx*inverse))*out.spacing; ly=float(int(ly*inverse))*out.spacing;
	const float centreX = lx + out.spacing * 0.5f, centreY = ly + out.spacing * 0.5f;
	std::vector<int> cache(size_t(nx+1)*(ny+1),-1);
	auto vertex=[&](int x,int y) {
		int &index=cache[size_t(y)*(nx+1)+x];
		if(index>=0) return uint32_t(index);
		StandingVertex v; v.point={lx+x*out.spacing,ly+y*out.spacing}; v.ground=ground(v.point.x,v.point.y);
		float nearestDistance=100000;
		if (std::max(float(area.waterHeight)-v.ground,0.0f)<100) {
			for (const auto &w:waves) if(w.enablePcaWave) for(size_t i=1;i<w.points.size();++i) {
				const auto &a=w.points[i-1], &b=w.points[i];
				float dx=b.x-a.x,dy=b.y-a.y,len=std::sqrt(dx*dx+dy*dy);
				if(len==0) continue;
				Point2F mid={(a.x+b.x)*.5f,(a.y+b.y)*.5f};
				float qx=v.point.x-mid.x,qy=v.point.y-mid.y,dist=std::sqrt(qx*qx+qy*qy);
				if(dist<len*.5f+50 && dist<nearestDistance) {
					nearestDistance=dist; v.waveArea=int(&w-waves.data()); v.nearest=mid; v.direction={dy/len,-dx/len}; v.halfLength=len*.5f;
				}
			}
		}
		index=int(out.vertices.size()); out.vertices.push_back(v); return uint32_t(index);
	};
	for(int y=0;y<ny;++y) for(int x=0;x<nx;++x) {
		const Point2F c={float(x)*out.spacing+centreX,float(y)*out.spacing+centreY};
		if(!standingWaterContains(area.points,boundsLo,boundsHi,c)) continue;
		auto a=vertex(x,y),b=vertex(x+1,y),c0=vertex(x,y+1),d=vertex(x+1,y+1);
		out.indices.insert(out.indices.end(),{a,d,c0,a,b,d});
	}
	return !out.indices.empty();
}

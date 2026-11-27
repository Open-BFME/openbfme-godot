// OpenBFME. GPL-3.0.
//
// W3DLaserDraw: the lightning / beam draw of Gandalf's staff, the Eye of Sauron and the other laser powers (both editions).
// Client / draw only.
//
// TARGET (RotWK 2.01 game.dat, read statically with tools/mod/rw.py):
//   * module data: factory RW 0x464159 (`new 0x74`, RW 0x464165), constructor RW 0x4C917B, vtable RW 0xBE18B8, field table
//     RW 0xBE1798 (16 rows + terminator) added by buildFieldParse RW 0x4C8989 to the base table RW 0xBE1320.
//   * 16 fields, in the binary's own order: NumBeams +0x20 (parseUnsignedInt RW 0x42ECB2), InnerBeamWidth +0x10 (parseReal
//     RW 0x42ED00), OuterBeamWidth +0x14 (parseReal), InnerColor +0x08 (parseColorInt RW 0x42F13E), OuterColor +0x0C
//     (parseColorInt), MaxIntensityLifetime +0x24 (RW 0x73A429), FadeLifetime +0x28 (RW 0x73A429), Texture +0x2C (the appending
//     string-vector parser RW 0x42E59E), ScrollRate +0x18 (parseReal), Tile +0x1C (parseBool RW 0x42E558), Segments +0x38
//     (parseUnsignedInt), ArcHeight +0x3C (parseReal), SegmentOverlapRatio +0x40 (parseReal), TilingScalar +0x44 (parseReal),
//     FanWidth +0x48 (parseReal), Envelope +0x4C (RW 0x73A74B, a 0x28-byte OrnamentData).
//   * the draw: vtable RW 0xBE1938; doDrawModule RW 0x4C8A6C (slot +0x2C), getLaserTemplateWidth RW 0x4C8A56 (the secondary
//     vtable RW 0xBE1934 slot 0), getModuleNameKey RW 0x4C931D (cached form RW 0x4C92D8), loadPostProcess RW 0x4C89E2, xfer
//     RW 0x4C899A. The endpoints come from the "LaserUpdate" client update (name key RW 0xBE18A8, nameToKey RW 0x5487EC, the
//     drawable lookup RW 0x6747C2): start = update +0x0C/+0x10/+0x14, end = +0x18/+0x1C/+0x20, the width scale +0x3C, the dirty
//     byte +0x24.
//   * the envelope: initialiser RW 0x4C894B, update RW 0x740659 (five states: wait, attack, decay, sustain, release).
// DONOR BFME2 1.06 (readable C++): W3DLaserDrawBfmeDoDraw.cpp / W3DLaserDraw.cpp / W3DLaserDrawBfmeConstructor.cpp / LaserUpdate.cpp.
//   Every branch ported here was first read there and then confirmed in RW at the addresses above.
//
// What the port draws with this: the beam geometry RW 0x4C8A6C computes (the point pair, width, fan width, colour and texture
// index of every line of the beam) is produced by W3DLaserDrawState as plain data. The render object of RW (SegmentedLineClass,
// the +0x10 array) and the module that positions the beam (LaserUpdate) are not part of this class.

#pragma once

#include "Common/INI.h"
#include "Common/INIDataTypes.h"
#include "Common/Module.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class LaserUpdate;

// The OrnamentData envelope of the laser (RW module data +0x4C .. +0x73, 0x28 bytes), shared by the module data and the draw
// module (the constructor copies the ten dwords at RW 0x4C94CB .. 0x4C94D1).
class W3DLaserDrawEnvelope
{
public:
	// RW 0x740659: the switch on the state at +0x24 (0 -> 0x7407BF, 1 -> 0x740764, 2 -> 0x740704, 3 -> 0x7406E5, 4 -> 0x740687)
	enum
	{
		STATE_WAIT = 0,
		STATE_ATTACK = 1,
		STATE_DECAY = 2,
		STATE_SUSTAIN = 3,
		STATE_RELEASE = 4,
		STATE_DONE = 5
	};

	// RW 0x4C894B: the three opacities become 1.0 and the delay / attack / decay become one frame (the sustain and release
	// spans are the caller's zeroes), the counter +0x0C and the state +0x24 zero.
	void init();
	// RW 0x73A74B: the key/value pairs of the Envelope line; the times are milliseconds, stored as frames by
	// floor(value * 0.005) (RW 0xD9F610, LOGICFRAMES_PER_MSEC_REAL).
	void parse(INI *ini);
	// RW 0x740659: one frame of the envelope. Writes the opacity to *out and returns false once the release is over.
	bool update(float *out);

	float m_initialOpacity = 1.0f; ///< InitialOpacity +0x00
	float m_peakOpacity = 1.0f;    ///< PeakOpacity +0x04
	float m_sustainOpacity = 1.0f; ///< SustainOpacity +0x08
	unsigned m_counter = 0;        ///< +0x0C, the frame counter of the current state
	unsigned m_initialDelay = 1;   ///< InitialDelay +0x10 (frames)
	unsigned m_attackTime = 1;     ///< AttackTime +0x14
	unsigned m_decayTime = 1;      ///< DecayTime +0x18
	unsigned m_sustainTime = 0;    ///< SustainTime +0x1C
	unsigned m_releaseTime = 0;    ///< ReleaseTime +0x20
	int m_state = STATE_WAIT;      ///< +0x24
};

class W3DLaserDrawModuleData : public ModuleData
{
public:
	W3DLaserDrawModuleData() = default;
	static void buildFieldParse(MultiIniFieldParse &p);

	// RW 0x4C8A56: the LaserDrawInterface slot 0 (secondary vtable RW 0xBE1934), `[this-8]` = the module data, `[+0x14]` = OuterBeamWidth.
	float getLaserTemplateWidth() const { return m_outerBeamWidth * 0.5f; }

	std::uint32_t m_innerColor = 0xFFFFFFFFu; ///< InnerColor +0x08 (one packed ABGR word, RW 0x4C917F `or dword [esi+8], -1`)
	std::uint32_t m_outerColor = 0xFFFFFFFFu; ///< OuterColor +0x0C (RW 0x4C9183)
	float m_innerBeamWidth = 0.0f;            ///< InnerBeamWidth +0x10 (RW 0x4C91BB)
	float m_outerBeamWidth = 1.0f;            ///< OuterBeamWidth +0x14 (RW 0x4C91C0, constant RW 0xBD1908)
	float m_scrollRate = 0.0f;                ///< ScrollRate +0x18 (RW 0x4C91CB)
	bool m_tile = false;                      ///< Tile +0x1C (RW 0x4C91D0)
	unsigned m_numBeams = 1;                  ///< NumBeams +0x20 (RW 0x4C91B5)
	unsigned m_maxIntensityLifetime = 0;      ///< MaxIntensityLifetime +0x24 (RW 0x4C91C5)
	unsigned m_fadeLifetime = 0;              ///< FadeLifetime +0x28 (RW 0x4C91C8)
	std::vector<std::string> m_textures;      ///< Texture +0x2C, file order (the vector constructor RW 0x478CBE)
	unsigned m_segments = 1;                  ///< Segments +0x38 (RW 0x4C91B8)
	float m_arcHeight = 0.0f;                 ///< ArcHeight +0x3C (RW 0x4C91D3)
	float m_segmentOverlapRatio = 0.0f;       ///< SegmentOverlapRatio +0x40
	float m_tilingScalar = 1.0f;              ///< TilingScalar +0x44
	float m_fanWidth = 0.0f;                  ///< FanWidth +0x48
	W3DLaserDrawEnvelope m_envelope;          ///< Envelope +0x4C .. +0x73 (0x28)
};

namespace W3DLaserDrawTables
{
const FieldParse *laser(); ///< RW 0xBE1798, 16 rows in the binary's order
}

// The two endpoints the LaserUpdate client update hands the draw (RW 0x4C8B10 .. 0x4C8B29).
struct W3DLaserEndpoints
{
	float start[3] = { 0, 0, 0 }; ///< update +0x0C, +0x10, +0x14
	float end[3] = { 0, 0, 0 };   ///< update +0x18, +0x1C, +0x20
};

// One entry of the SegmentedLineClass array RW doDrawModule writes (index = segment * NumBeams + i), with what it sets on it.
struct W3DLaserBeam
{
	int index = 0;          ///< segment * m_numBeams + i
	int segment = 0;
	int beam = 0;           ///< i, counted down from NumBeams - 1
	float points[2][3] = { { 0, 0, 0 }, { 0, 0, 0 } }; ///< Set_Points(2, laserPoints)
	float width = 0.0f;     ///< Set_Width(width, fanWidth)
	float fanWidth = 0.0f;  ///< the draw module's +0x30 copy of module data FanWidth (RW 0x4C94C0)
	bool tileAppliedSkip = false; ///< the Tile branch was skipped (no texture, or Tile = No)
	float tileFactor = 0.0f;      ///< Set_Texture_Tile_Factor, when the Tile branch runs (RW 0x4C8F73 .. 0x4C8F95)
	float color[3] = { 0, 0, 0 }; ///< Set_Color, already multiplied by the envelope opacity (RW 0x4C9062 .. 0x4C90A8)
	float opacity = 0.0f;         ///< m_at2C after the envelope update
	unsigned textureIndex = 0;    ///< the index into the module data's Texture list this frame (the draw module's +0x28)
};

// The per-draw state of W3DLaserDraw (RW draw module +0x10 .. +0x5C, constructor RW 0x4C9483) and the beam geometry of
// doDrawModule RW 0x4C8A6C. The textures themselves are the renderer's (the port has no SegmentedLineClass): the caller says
// whether the first texture loaded and how many there are, exactly what RW asks the vector at +0x14 for.
class W3DLaserDrawState
{
public:
	// RW 0x4C9483: the aspect ratio and the envelope opacity start at 1.0, the draw is dirty, the texture index is 0, the fan
	// width and the envelope are copies of the module data (RW 0x4C94B4 / 0x4C94CB).
	void reset(const W3DLaserDrawModuleData &data);

	// RW 0x4C8A6C. Empty when the draw is skipped (RW 0x4C8AC8: no update; RW 0x4C8AD8: neither the update nor the draw is
	// dirty; RW 0x4C8AEF: Segments 0). `updateDirty` is the LaserUpdate's +0x24 byte; RW clears it at RW 0x4C8AE2, the caller
	// owns that byte in the port. `groundHeight` is TheTerrainLogic's getGroundHeight (RW 0xDE4690, slot +0x18), used by the
	// arc branch only.
	std::vector<W3DLaserBeam> build(const W3DLaserDrawModuleData &data, const W3DLaserEndpoints &endpoints, float widthScale,
	                                bool updateDirty, bool firstTextureLoaded, float textureAspectRatio, unsigned textureCount,
	                                const std::function<float(float, float)> &groundHeight);

	// RW 0x4C8A6C overload with live LaserUpdate. Extracts endpoints, width scalar, updateDirty,
	// and clears update's dirty flag at RW 0x4C8AE2.
	std::vector<W3DLaserBeam> build(const W3DLaserDrawModuleData &data, LaserUpdate &update,
	                                bool firstTextureLoaded, float textureAspectRatio, unsigned textureCount,
	                                const std::function<float(float, float)> &groundHeight);

	bool selfDirty() const { return m_selfDirty; }
	unsigned textureIndex() const { return m_textureIndex; }
	float opacity() const { return m_opacity; }
	float fanWidth() const { return m_fanWidth; }
	float textureAspectRatio() const { return m_textureAspectRatio; }
	W3DLaserDrawEnvelope &envelope() { return m_envelope; }
	const W3DLaserDrawEnvelope &envelope() const { return m_envelope; }

private:
	float m_textureAspectRatio = 1.0f; ///< +0x20
	bool m_selfDirty = true;           ///< +0x24
	unsigned m_textureIndex = 0;       ///< +0x28, advanced once per draw (RW 0x4C90DD .. 0x4C90F0)
	float m_opacity = 1.0f;            ///< +0x2C, the envelope's output
	float m_fanWidth = 0.0f;           ///< +0x30
	W3DLaserDrawEnvelope m_envelope;   ///< +0x34
};

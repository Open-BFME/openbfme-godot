// OpenBFME. GPL-3.0. See W3DLaserDraw.h (RW 0x004C917B / 0x00BE1798 / 0x004C8A6C / 0x00740659, donor BFME2 W3DLaserDraw*).
// TARGET RW 0x004C93FD W3DLaserDraw scalar deleting destructor (~W3DLaserDraw RW 0x004C9229).

#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DLaserDraw.h"
#include "GameClient/LaserUpdate.h"

#include "Common/INIException.h"

#include <cmath>
#include <cstddef>
#include <cstring>
#include <stdexcept>

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

namespace
{
// RW 0xD9F610: the millisecond -> frame scale of the envelope times (the port's LOGICFRAMES_PER_MSEC_REAL)
const float kFramesPerMsec = 0.005f;
// RW 0xBD1908: 1.0
const float kOne = 1.0f;
// RW 0xBD869C: 0.5
const float kHalf = 0.5f;
// RW 0xBD89D0: PI * 0.5
const float kPiOver2 = 1.5707964f;
// RW 0xBD889C: 2.0, how far above the terrain the arc branch keeps the beam
const float kGroundClearance = 2.0f;

// RW 0x6D3AD5 (Get_Color_Components): one packed GameMakeColor word -> four floats in 0..1, red/green/blue/alpha.
void colorComponents(std::uint32_t color, float out[4])
{
	out[0] = (float)((color >> 16) & 0xFF) / 255.0f;
	out[1] = (float)((color >> 8) & 0xFF) / 255.0f;
	out[2] = (float)(color & 0xFF) / 255.0f;
	out[3] = (float)((color >> 24) & 0xFF) / 255.0f;
}

// RW 0x740659 / donor Rva001048C0::update: a span of zero frames is one frame.
float atLeastOneFrame(unsigned frames)
{
	const float f = (float)frames;
	return kOne > f ? kOne : f;
}

// RW 0x4C8F22 .. 0x4C8F6D: the squares summed z, y, x (SSE), the root by an x87 fsqrt stored to float.
float length3(const float a[3], const float b[3])
{
	const float x = a[0] - b[0];
	const float y = a[1] - b[1];
	const float z = a[2] - b[2];
	const float square = z * z + y * y + x * x;
	return (float)std::sqrt((double)square);
}

// RW 0x73A74B: the Envelope of the laser, a list of key/value pairs in one line.
void parseEnvelope(INI *ini, void *, void *store, const void *)
{
	static_cast<W3DLaserDrawEnvelope *>(store)->parse(ini);
}

// RW 0x73A429: a duration in milliseconds stored as frames (the parse of MaxIntensityLifetime / FadeLifetime).
void parseLaserFrames(INI *ini, void *, void *store, const void *)
{
	const int msec = ini->scanInt(ini->getNextToken());
	*(unsigned *)store = (unsigned)(int)((float)msec * kFramesPerMsec);
}

#define LASER_ROW(name, parse, field) { name, parse, nullptr, (int)offsetof(W3DLaserDrawModuleData, field) }
// RW 0xBE1798, 16 rows in the binary's order (the class table buildFieldParse RW 0x4C8989 adds with offset 0).
const FieldParse kLaserFieldParse[] = {
	LASER_ROW("NumBeams", INI::parseUnsignedInt, m_numBeams),
	LASER_ROW("InnerBeamWidth", INI::parseReal, m_innerBeamWidth),
	LASER_ROW("OuterBeamWidth", INI::parseReal, m_outerBeamWidth),
	LASER_ROW("InnerColor", INI::parseColorInt, m_innerColor),
	LASER_ROW("OuterColor", INI::parseColorInt, m_outerColor),
	LASER_ROW("MaxIntensityLifetime", parseLaserFrames, m_maxIntensityLifetime),
	LASER_ROW("FadeLifetime", parseLaserFrames, m_fadeLifetime),
	LASER_ROW("Texture", INI::parseAsciiStringVectorAppend, m_textures),
	LASER_ROW("ScrollRate", INI::parseReal, m_scrollRate),
	LASER_ROW("Tile", INI::parseBool, m_tile),
	LASER_ROW("Segments", INI::parseUnsignedInt, m_segments),
	LASER_ROW("ArcHeight", INI::parseReal, m_arcHeight),
	LASER_ROW("SegmentOverlapRatio", INI::parseReal, m_segmentOverlapRatio),
	LASER_ROW("TilingScalar", INI::parseReal, m_tilingScalar),
	LASER_ROW("FanWidth", INI::parseReal, m_fanWidth),
	LASER_ROW("Envelope", parseEnvelope, m_envelope),
	{ nullptr, nullptr, nullptr, 0 }
};
#undef LASER_ROW

bool sameToken(const char *a, const char *b)
{
	return a && b && std::strcmp(a, b) == 0;
}

} // namespace

const FieldParse *W3DLaserDrawTables::laser()
{
	return kLaserFieldParse;
}

void W3DLaserDrawModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kLaserFieldParse, 0); // RW 0x4C8989: MultiIniFieldParse::add RW 0x42B8D7 with the class table and offset 0
}

// ---------------------------------------------------------------------------------------------------------------------
// Envelope
// ---------------------------------------------------------------------------------------------------------------------

void W3DLaserDrawEnvelope::init() // RW 0x4C894B
{
	m_initialOpacity = kOne;
	m_peakOpacity = kOne;
	m_sustainOpacity = kOne;
	m_initialDelay = 1;
	m_attackTime = 1;
	m_decayTime = 1;
	m_sustainTime = 0;
	m_releaseTime = 0;
	m_counter = 0;
	m_state = STATE_WAIT;
}

void W3DLaserDrawEnvelope::parse(INI *ini) // RW 0x73A74B
{
	m_counter = 0; // RW 0x73A759: the two resets are inline in the parser, whatever the data carries
	m_state = 0;   // RW 0x73A75C
	for (;;)
	{
		const char *token = ini->getNextTokenOrNull();
		if (!token)
		{
			break;
		}
		const char *value = ini->getNextToken();
		if (sameToken(token, "InitialOpacity"))
		{
			m_initialOpacity = ini->scanReal(value);
		}
		else if (sameToken(token, "PeakOpacity"))
		{
			m_peakOpacity = ini->scanReal(value);
		}
		else if (sameToken(token, "SustainOpacity"))
		{
			m_sustainOpacity = ini->scanReal(value);
		}
		else if (sameToken(token, "AttackTime"))
		{
			m_attackTime = (unsigned)(int)((float)ini->scanInt(value) * kFramesPerMsec); // RW 0x73A802 .. 0x73A82A
		}
		else if (sameToken(token, "DecayTime"))
		{
			m_decayTime = (unsigned)(int)((float)ini->scanInt(value) * kFramesPerMsec); // RW 0x73A855 .. 0x73A882
		}
		else if (sameToken(token, "SustainTime"))
		{
			m_sustainTime = (unsigned)(int)((float)ini->scanInt(value) * kFramesPerMsec);
		}
		else if (sameToken(token, "ReleaseTime"))
		{
			m_releaseTime = (unsigned)(int)((float)ini->scanInt(value) * kFramesPerMsec);
		}
		else if (sameToken(token, "InitialDelay"))
		{
			m_initialDelay = (unsigned)(int)((float)ini->scanInt(value) * kFramesPerMsec); // RW 0x73A943 .. 0x73A978
		}
		else
		{
			throw INIException(3, "W3DLaserDraw: unknown Envelope key '%s'", token); // RW 0x73A97D
		}
	}
}

bool W3DLaserDrawEnvelope::update(float *out) // RW 0x740659
{
	switch (m_state)
	{
		case STATE_WAIT: // RW 0x7407BF
			if (m_counter++ >= m_initialDelay)
			{
				m_state = STATE_ATTACK;
				m_counter = 0;
				*out = m_initialOpacity;
			}
			*out = 0.0f;
			return true;
		case STATE_ATTACK: // RW 0x740764
			*out += (m_peakOpacity - m_initialOpacity) / atLeastOneFrame(m_attackTime);
			if (*out >= m_peakOpacity)
			{
				*out = m_peakOpacity;
				m_state = STATE_DECAY;
			}
			return true;
		case STATE_DECAY: // RW 0x740704
			*out -= (m_peakOpacity - m_sustainOpacity) / atLeastOneFrame(m_decayTime);
			if (*out <= m_sustainOpacity)
			{
				*out = m_sustainOpacity;
				m_state = STATE_SUSTAIN;
				m_counter = 0;
			}
			return true;
		case STATE_SUSTAIN: // RW 0x7406E5
			if (m_counter++ >= m_sustainTime)
			{
				m_state = STATE_RELEASE;
			}
			*out = m_sustainOpacity;
			return true;
		case STATE_RELEASE: // RW 0x740687
			*out -= m_sustainOpacity / atLeastOneFrame(m_releaseTime);
			if (*out <= 0.0f) // RW 0x7406C6: comiss 0.0, *out; clamp when *out <= 0
			{
				*out = 0.0f;
				m_state = STATE_DONE;
				m_counter = 0;
			}
			return true;
		default:
			*out = 0.0f;
			return false;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// The draw
// ---------------------------------------------------------------------------------------------------------------------

void W3DLaserDrawState::reset(const W3DLaserDrawModuleData &data) // RW 0x4C9483
{
	m_textureAspectRatio = kOne; // RW 0x4C94A3
	m_selfDirty = true;          // RW 0x4C94A8
	m_textureIndex = 0;          // RW 0x4C94AC
	m_opacity = kOne;            // RW 0x4C94AF
	m_fanWidth = data.m_fanWidth;  // RW 0x4C94B4 / 0x4C94C0: the module data's FanWidth, copied at construction
	m_envelope = data.m_envelope;  // RW 0x4C94CB .. 0x4C94D1
}

std::vector<W3DLaserBeam> W3DLaserDrawState::build(const W3DLaserDrawModuleData &data, LaserUpdate &update,
                                                   bool firstTextureLoaded, float textureAspectRatio, unsigned textureCount,
                                                   const std::function<float(float, float)> &groundHeight)
{
	W3DLaserEndpoints ep;
	const Coord3D &s = update.startPos();
	const Coord3D &e = update.endPos();
	ep.start[0] = s.x;
	ep.start[1] = s.y;
	ep.start[2] = s.z;
	ep.end[0] = e.x;
	ep.end[1] = e.y;
	ep.end[2] = e.z;

	const float widthScale = update.currentWidthScalar();
	const bool updateDirty = update.isDirty();

	// RW 0x4C8AE2: mov byte ptr [esi+0x24], 0 (clears LaserUpdate's dirty byte)
	update.setDirty(false);

	return build(data, ep, widthScale, updateDirty, firstTextureLoaded, textureAspectRatio, textureCount, groundHeight);
}

std::vector<W3DLaserBeam> W3DLaserDrawState::build(const W3DLaserDrawModuleData &data, const W3DLaserEndpoints &endpoints,
                                                   float widthScale, bool updateDirty, bool firstTextureLoaded,
                                                   float textureAspectRatio, unsigned textureCount,
                                                   const std::function<float(float, float)> &groundHeight)
{
	std::vector<W3DLaserBeam> beams;
	m_textureAspectRatio = textureAspectRatio;

	// RW 0x4C8ACE: nothing moves the laser and this draw has not been reset -> nothing to do
	if (!updateDirty && !m_selfDirty)
	{
		return beams;
	}
	m_selfDirty = false; // RW 0x4C8AE6 (the update's own byte, RW 0x4C8AE2, belongs to LaserUpdate)

	// RW 0x4C8AEA: Segments 0 draws nothing (the unsigned jbe) and still advances the texture index at RW 0x4C90DD
	if (data.m_segments != 0)
	{
		const bool arc = data.m_arcHeight > 0.0f && data.m_segments > 1; // RW 0x4C8AF5: ArcHeight > 0.0 (RW 0xC1B594) and Segments > 1

		float inner[4];
		float outer[4];
		colorComponents(data.m_innerColor, inner); // RW 0x4C8E7F
		colorComponents(data.m_outerColor, outer); // RW 0x4C8E9F

		for (unsigned segment = 0; segment < data.m_segments; ++segment) // RW 0x4C8AF5
		{
			float laserPoints[2][3] = { { 0, 0, 0 }, { 0, 0, 0 } };

			if (arc)
			{
				const float *lineStart = endpoints.start;
				const float *lineEnd = endpoints.end;
				const float lineVector[3] = { lineEnd[0] - lineStart[0], lineEnd[1] - lineStart[1], lineEnd[2] - lineStart[2] };
				const float lineLength = length3(lineEnd, lineStart);
				const float lineMiddle[3] = { (float)(((double)lineStart[0] + lineEnd[0]) * 0.5), (float)(((double)lineStart[1] + lineEnd[1]) * 0.5),
					                          (float)(((double)lineStart[2] + lineEnd[2]) * 0.5) };
				const float halfLength = lineLength * kHalf;

				float startRatio = (float)segment / (float)data.m_segments;     // RW 0x4C8BAA .. 0x4C8BC9
				float endRatio = (float)(segment + 1) / (float)data.m_segments; // (the same division, end+1)
				if (segment > 0)
				{
					startRatio -= data.m_segmentOverlapRatio; // RW 0x4C8BE6
				}
				if (segment < data.m_segments - 1)
				{
					endRatio += data.m_segmentOverlapRatio; // RW 0x4C8BF4
				}

				float segmentStart[3];
				float segmentEnd[3];
				for (int k = 0; k < 3; ++k)
				{
					segmentStart[k] = lineStart[k] + lineVector[k] * startRatio; // RW 0x4C8C1F .. 0x4C8C3F
					segmentEnd[k] = lineStart[k] + lineVector[k] * endRatio;     // RW 0x4C8C44 .. 0x4C8CC2
				}

				// RW 0x4C8CCF: cos(distance from the middle / half length * PI * 0.5) * ArcHeight added to z
				segmentStart[2] += (float)std::cos(length3(lineMiddle, segmentStart) / halfLength * kPiOver2) * data.m_arcHeight;
				segmentEnd[2] += (float)std::cos(length3(lineMiddle, segmentEnd) / halfLength * kPiOver2) * data.m_arcHeight;

				// RW 0x4C8D4E: MAX(z, 2.0 + getGroundHeight(x, y)) so the beam skims the terrain instead of penetrating it
				for (int k = 0; k < 2; ++k)
				{
					const float *p = (k == 0) ? segmentStart : segmentEnd;
					const float ground = groundHeight ? groundHeight(p[0], p[1]) : 0.0f;
					laserPoints[k][0] = p[0];
					laserPoints[k][1] = p[1];
					laserPoints[k][2] = (p[2] > kGroundClearance + ground) ? p[2] : kGroundClearance + ground;
				}
			}
			else
			{
				// RW 0x4C8E38: no arc, the two endpoints as the update has them
				for (int k = 0; k < 3; ++k)
				{
					laserPoints[0][k] = endpoints.start[k];
					laserPoints[1][k] = endpoints.end[k];
				}
			}

			// RW 0x4C8EB1: the beams of this segment, i from NumBeams - 1 down to 0
			for (int i = (int)data.m_numBeams - 1; i >= 0; --i)
			{
				W3DLaserBeam beam;
				beam.segment = (int)segment;
				beam.beam = i;
				beam.index = (int)segment * (int)data.m_numBeams + i; // RW 0x4C8EBF
				// RW 0x4C8F95 -> SegmentedLineClass::Set_Width (RW 0x0055DBE0): clamps both width and fanWidth to >= 0.0f
				beam.fanWidth = m_fanWidth > 0.0f ? m_fanWidth : 0.0f;

				float width;
				if (data.m_numBeams == 1) // RW 0x4C8EC7
				{
					width = data.m_innerBeamWidth * widthScale;
				}
				else
				{
					const float scale = (float)i / ((float)data.m_numBeams - kOne); // RW 0x4C8EE0 .. 0x4C8F09
					width = (data.m_innerBeamWidth + scale * (data.m_outerBeamWidth - data.m_innerBeamWidth)) * widthScale;
				}
				beam.width = width > 0.0f ? width : 0.0f;

				// RW 0x4C8F14: m_textures[0] (Set_Texture RW 0x0055DB80 -> RW 0x00B57240) and Tile
				beam.tileAppliedSkip = true;
				if (firstTextureLoaded && data.m_tile)
				{
					const float length = length3(laserPoints[1], laserPoints[0]);
					// RW 0x4C8F73 .. 0x4C8F84 -> SegmentedLineClass::Set_Texture_Tile_Factor (RW 0x0055DCE0)
					// -> SegLineRendererClass::Set_Texture_Tile_Factor (RW 0x00590A40): clamps factor to [0.0f, 50.0f] (RW 0xBD88C4)
					float tf = data.m_tilingScalar / width * m_textureAspectRatio * length;
					if (tf > 50.0f)
						tf = 50.0f;
					else if (!(tf > 0.0f))
						tf = 0.0f;
					beam.tileFactor = tf;
					beam.tileAppliedSkip = false;
				}

				for (int k = 0; k < 3; ++k)
				{
					beam.points[0][k] = laserPoints[0][k]; // RW 0x4C8FBB -> Set_Points (RW 0x0055E9F0)
					beam.points[1][k] = laserPoints[1][k];
				}

				float red, green, blue;
				if (data.m_numBeams == 1) // RW 0x4C8FC8
				{
					red = inner[0] * inner[3];
					green = inner[1] * inner[3];
					blue = inner[2] * inner[3];
				}
				else
				{
					const float scale = (float)i / ((float)data.m_numBeams - kOne); // RW 0x4C9002 .. 0x4C9050
					red = inner[0] + scale * (outer[0] - inner[0]) * inner[3];
					green = inner[1] + scale * (outer[1] - inner[1]) * inner[3];
					blue = inner[2] + scale * (outer[2] - inner[2]) * inner[3];
				}

				// RW 0x4C905D: the envelope update runs once per beam, in and out are the module's +0x2C
				m_envelope.update(&m_opacity);
				beam.opacity = m_opacity;
				beam.color[0] = red * m_opacity; // RW 0x4C9062 .. 0x4C90A8 -> Set_Color (RW 0x0055DC20)
				beam.color[1] = green * m_opacity;
				beam.color[2] = blue * m_opacity;
				beam.textureIndex = m_textureIndex; // RW 0x4C90B6: m_textures[m_at28]

				beams.push_back(beam);
			}
		}
	}

	// RW 0x4C90DD: one texture per draw, wrapping at the end of the list
	++m_textureIndex;
	if (textureCount == 0 || m_textureIndex >= textureCount)
	{
		m_textureIndex = 0;
	}
	return beams;
}

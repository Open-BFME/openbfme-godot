// OpenBFME. GPL-3.0. See MilesMix.h (client audio: no simulation state, plain CRT maths).

#include "Common/Audio/MilesMix.h"

#include "Common/Audio/AudioEventInfo.h"
#include "Common/Audio/AudioSettings.h"

#include <algorithm>
#include <cmath>

namespace
{
const double kVolumeExponent = 1.6666666269302368; // msssoft 0x2240E4F0 / mss32 0x2114C020 (5/3 rounded to a float, stored as a double)
const double kPanExponent = 0.30000001192092896;   // mss32 0x2114C010
const float kCentreLevel = 0.8122522234916687f;    // mss32 0x2114C018 (2^-0.3)
const float kInvPi = 0.31830987334251404f;         // msssoft 0x2240E4E4
const float kBehindScale = 0.75f;                  // msssoft 0x2240E4F8
const float kEpsilon = 0.0001f;                    // msssoft 0x2240E4E8

float milesVolume(float volume)
{
	return volume > 0.0f ? (float)std::pow((double)volume, kVolumeExponent) : 0.0f;
}
} // namespace

MilesChannelGains milesSampleGains(float volume, float pan)
{
	// mss32 0x2112AD60
	const float v = milesVolume(volume);
	MilesChannelGains g;
	if (pan == 0.5f)
	{
		g.left = g.right = v * kCentreLevel;
		return g;
	}
	g.left = (float)std::pow((double)(1.0f - pan), kPanExponent) * v;
	g.right = (float)std::pow((double)pan, kPanExponent) * v;
	return g;
}

MilesChannelGains milesFast2DGains(float volume, const Coord3D &listenerPos, const Coord3D &listenerFace, const Coord3D &soundPos, float maxDistance)
{
	// msssoft 0x22401060, in Miles' frame: the game's (x, y, -z) for positions and the face, up (0, 0, -1) (RW 0x4525D5)
	const float dx = soundPos.x - listenerPos.x;
	const float dy = soundPos.y - listenerPos.y;
	const float dz = -(soundPos.z - listenerPos.z);
	const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
	MilesChannelGains g;
	if (dist > maxDistance)
	{
		return g; // AIL_set_sample_volume_pan(0, 0.5): silent
	}
	float v = milesVolume(volume);
	// the rolloff term min / ((dist - min) * rolloff + min) is 1: RotWK sets the rolloff factor to 0 (RW 0x45FD77)
	// the unit direction (0x22401000: a zero vector becomes (1, 0, 0))
	float nx = 1.0f, ny = 0.0f, nz = 0.0f;
	if (dist != 0.0f)
	{
		const float inv = 1.0f / dist;
		nx = dx * inv;
		ny = dy * inv;
		nz = dz * inv;
	}
	const float fx = listenerFace.x, fy = listenerFace.y, fz = -listenerFace.z;
	const float front = fx * nx + fy * ny + fz * nz;
	// R = up x face with up (0, 0, -1): (fy, -fx, 0)
	float p = 0.5f;
	if (dist > kEpsilon)
	{
		const float side = std::max(-1.0f, std::min(1.0f, fy * nx - fx * ny));
		p = (float)std::acos((double)side) * kInvPi;
	}
	if (front < 0.0f)
	{
		v *= kBehindScale;
	}
	g.left = p * v;
	g.right = (1.0f - p) * v;
	return g;
}

float milesMaxDistance(const AudioEventInfo &info, float minVolume, const AudioSettings &settings)
{
	// RW 0x45C084: a global event, or one whose MinVolume exceeds MinSampleVolume, takes GlobalMaxRange (AudioSettings +0x74)
	const float range = ((info.type & ST_GLOBAL) || minVolume > settings.minSampleVolume) ? (float)settings.globalMaxRange : info.maxRange;
	return std::max(info.minRange, range * 2.0f); // mss32 0x2111FE0D: the larger of (MinRange, 2 x range) is the maximum
}

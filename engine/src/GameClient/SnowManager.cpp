// OpenBFME. GPL-3.0.
// Lane WEATHER-1. See GameClient/SnowManager.h for the target facts.

#include "GameClient/SnowManager.h"

#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDrawServices.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>

namespace
{
// RW 0x492AC8 -> 0x492AAD: (float)fmod((double)a, (double)b)
float fmodf32(float a, float b) { return (float)std::fmod((double)a, (double)b); }

// rotwk_crt_ftol2: truncation toward zero of the x87 value
int ftol(double v) { return (int)v; }
} // namespace

float SnowManager::fastSin(float x)
{
	// WWMath::Fast_Sin (donor ZH wwmath.h): the index is the float scaled by 1024 / (2 pi) (RW 0xBDE0B0 = 162.97466f, applied by the caller),
	// here `x` is already the scaled index
	static float table[1024];
	static bool built = false;
	if (!built)
	{
		for (int i = 0; i < 1024; ++i)
		{
			table[i] = (float)std::sin((double)i * 6.283185307179586 / 1024.0);
		}
		built = true;
	}
	const float fi = std::floor(x);
	const int i = (int)fi;
	const float frac = x - fi;
	return (1.0f - frac) * table[i & 1023] + table[(i + 1) & 1023] * frac;
}

void SnowManager::init(const WeatherSetting &s)
{
	m_time = 0.0f;
	updateIniSettings(s);
}

void SnowManager::updateIniSettings(const WeatherSetting &s)
{
	const int box = (int)s.snowBoxHeight;
	for (float &h : m_heights)
	{
		const int r = m_rand.rand();
		h = (float)(box != 0 ? r % box : 0); // RW 0x600B0B: idiv by (int)SnowBoxHeight (a zero box is retail's divide fault; here 0)
	}
	m_velocity = s.snowSpeed;
	m_freqX = s.snowFrequencyScaleX;
	m_freqY = s.snowFrequencyScaleY;
	m_amplitude = s.snowAmplitude;
	m_quadSize = s.snowQuadSize;
	m_box = s.snowBoxHeight;
	m_spacing = s.snowSpacing;
	m_tiles = s.numberTiles;
	m_windX = s.snowXYSpeed.x;
	m_windY = s.snowXYSpeed.y;
	m_fullTimePeriod = m_box / m_velocity;
	m_snowing = s.isSnowing;
	m_lightningFrames = s.lightningDuration;
	m_spellFrames = s.spellDuration;
	m_startFraction = s.rampControl.x;
	m_endFraction = s.rampControl.y;
	m_rampSpacing0 = s.rampSpacing.x;
	m_rampSpacing1 = s.rampSpacing.y;
	m_rampVelocity0 = s.rampSpeed.x;
	m_rampVelocity1 = s.rampSpeed.y;
}

void SnowManager::reset(const WeatherSetting &baseSetting, int globalWeather)
{
	m_visible = true;               // RW 0x600AAD
	updateIniSettings(baseSetting); // the overrides are gone: the final setting is the base
	finish(baseSetting, globalWeather);
	m_weatherState = 0;
	m_lightningOn = false;
}

void SnowManager::update(const WeatherSetting &s, std::uint32_t elapsedMs, int globalWeather)
{
	advanceTime(elapsedMs);
	clientFrame(s, globalWeather);
}

void SnowManager::clientFrame(const WeatherSetting &s, int globalWeather)
{
	transition(s, globalWeather);
	lightning(s);
}

void SnowManager::advanceTime(std::uint32_t elapsedMs)
{
	const float old = m_time;
	// RW 0x4943E6 .. 0x494419: fild of the signed difference, + 2^32 when negative, * 0.001f, + time (x87), stored as float
	long double ms = (long double)(std::int32_t)elapsedMs;
	if ((std::int32_t)elapsedMs < 0)
	{
		ms += 4294967296.0L;
	}
	m_time = (float)(ms * (long double)0.001f + (long double)m_time);
	m_time = fmodf32(m_time, m_fullTimePeriod);
	m_heightTraveled = (m_time - old) * m_velocity + m_heightTraveled;
	m_driftX = (m_time - old) * m_windX + m_driftX;
	m_driftY = (m_time - old) * m_windY + m_driftY;
}

void SnowManager::transition(const WeatherSetting &s, int globalWeather)
{
	if (!s.spellEnabled)
	{
		return;
	}
	if (m_phase == 0)
	{
		if (globalWeather == 2 && m_weatherState != 2)
		{
			m_weatherState = 2;
			start(s);
		}
		return;
	}
	if (m_phase == 1 || m_phase == 3)
	{
		--m_spellFrames;
	}
	blend(s, globalWeather);
	if (m_weatherState == 2 && globalWeather != 2)
	{
		m_phase = 3;
		m_spellFrames = (int)((1.0f - m_endFraction) * (float)s.spellDuration);
		m_weatherState = globalWeather;
	}
}

void SnowManager::start(const WeatherSetting &s)
{
	m_phase = 1;
	m_snowing = true;
	m_spellFrames = s.spellDuration;
	m_startFraction = s.rampControl.x;
	m_endFraction = s.rampControl.y;
}

void SnowManager::blend(const WeatherSetting &s, int globalWeather)
{
	if (!s.spellEnabled || m_phase == 0)
	{
		return;
	}
	const float f = (float)(s.spellDuration - m_spellFrames) / (float)s.spellDuration;
	if (f > 1.0f)
	{
		finish(s, globalWeather);
		return;
	}
	if (!(f <= m_endFraction))
	{
		const float b = (f - m_endFraction) / (1.0f - m_endFraction);
		m_phase = 3;
		m_spacing = m_rampSpacing1 - (m_rampSpacing1 - m_rampSpacing0) * b;
		m_velocity = m_rampVelocity1 - (m_rampVelocity1 - m_rampVelocity0) * b;
		return;
	}
	if (!(f <= m_startFraction))
	{
		m_spacing = m_rampSpacing1;
		m_phase = 2;
		m_velocity = m_rampVelocity1;
		return;
	}
	if (f <= 0.0f)
	{
		finish(s, globalWeather);
		return;
	}
	m_phase = 1;
	float b = f / m_startFraction;
	if (m_startFraction <= 9.9999997e-06f) // RW 0xBDE0B4
	{
		b = 1.0f;
	}
	m_spacing = (m_rampSpacing1 - m_rampSpacing0) * b + m_rampSpacing0;
	m_velocity = (m_rampVelocity1 - m_rampVelocity0) * b + m_rampVelocity0;
}

void SnowManager::finish(const WeatherSetting &s, int globalWeather)
{
	m_spellFrames = 0;
	m_phase = 0;
	m_weatherState = globalWeather; // RW 0x492E71: TheGlobalWeatherSystem + 0x10
	if (!s.isSnowing)
	{
		m_spacing = m_rampSpacing0;
		m_snowing = false;
		m_velocity = m_rampSpacing1; // RW 0x492EB7: + 0x68 (the peak ramp SPACING) as retail stores it
		return;
	}
	m_spacing = s.snowSpacing;
	m_velocity = s.snowSpeed;
	m_windX = s.snowXYSpeed.x;
	m_windY = s.snowXYSpeed.y;
}

void SnowManager::lightning(const WeatherSetting &s)
{
	if (!s.lightningEnabled)
	{
		return;
	}
	if (!m_lightningOn)
	{
		const float roll = m_rand.randomFloat();
		if (roll < s.lightningChance)
		{
			m_lightningOn = true;
			const float r = m_rand.randomFloat();
			m_lightningFrames = ftol(((double)r * (double)0.50000006f + (double)0.7f) * (double)s.lightningDuration + 0.5);
		}
		return;
	}
	--m_lightningFrames;
	if (m_lightningFrames < 1)
	{
		m_lightningFrames = 0;
		m_lightningOn = false;
	}
}

float CrtRand::randomFloat()
{
	return (float)((double)(rand() & 0xFFF) * (double)(1.0f / 4095.0f));
}

SnowDrawParams SnowManager::drawParams(const WeatherSetting &s, int mapWidth, int mapHeight, int mapBorder, W3DDrawRandom &clientRandom) const
{
	SnowDrawParams p;
	p.draw = s.snowEnabled && m_snowing && m_visible;
	p.pointSprites = s.snowPointSprites;
	p.texture = s.snowTexture;
	p.tiles = std::max(1, m_tiles);
	p.originX = 0; // WorldHeightMap draw origin (+0x120E0) * 10: 0 (S-4890)
	p.originY = 0;
	const int dimX = (int)((float)(mapWidth - mapBorder * 2) * 10.0f);
	const int dimY = (int)((float)(mapHeight - mapBorder * 2) * 10.0f);
	p.stepX = (dimX - p.originX) / p.tiles;
	p.stepY = (dimY - p.originY) / p.tiles;
	// SnowSpacing is any float a map's Weather block writes: at least 1 as retail's max, at most kMaxSnowSpacing (beyond any map: no particle), the
	// products in 64 bits (a spacing of 46341 or more squared overflows an int, 65536 squared wraps to 0); a NaN draws no snow (review: #59)
	if (m_spacing != m_spacing)
	{
		p.draw = false;
		p.spacing = 1;
		p.count = 0;
		p.columns = 0;
	}
	else
	{
		p.spacing = m_spacing >= 1.0f ? (m_spacing < (float)kMaxSnowSpacing ? (int)m_spacing : kMaxSnowSpacing) : 1;
		// review of #59: at most kMaxSnowParticles per tile. Beyond it the spacing widens to the least one whose grid fits (the count falls as the spacing
		// grows), so the field thins evenly instead of stopping after the first rows; retail draws any count (S-4890)
		const std::int64_t area = (std::int64_t)p.stepY * (std::int64_t)p.stepX;
		auto countAt = [area](std::int64_t sp) { return area / (sp * sp); };
		if (countAt(p.spacing) > (std::int64_t)kMaxSnowParticles)
		{
			std::int64_t lo = p.spacing, hi = kMaxSnowSpacing; // countAt(lo) is above the bound, countAt(hi) within it (no particle)
			while (hi - lo > 1)
			{
				const std::int64_t mid = lo + (hi - lo) / 2;
				(countAt(mid) > (std::int64_t)kMaxSnowParticles ? lo : hi) = mid;
			}
			p.spacing = (int)hi;
		}
		const std::int64_t count = countAt(p.spacing);
		p.count = (int)std::min<std::int64_t>(std::max<std::int64_t>(count, 0), INT_MAX);
		p.columns = p.stepX > 0 ? (int)(((std::int64_t)p.stepX + p.spacing - 1) / p.spacing) : 0;
	}
	p.heightTraveled = m_heightTraveled;
	p.driftX = m_driftX;
	p.driftY = m_driftY;
	const float windScale = m_box / m_velocity;
	p.windStepX = m_windX * windScale;
	p.windStepY = m_windY * windScale;
	p.box = m_box;
	p.ceiling = m_box;
	p.amplitude = m_amplitude;
	p.frequencyX = m_freqX;
	p.frequencyY = m_freqY;
	p.quadHalf = m_quadSize * 0.707f;
	int gray = 255;
	if (m_phase != 0)
	{
		// RW 0x492CE9
		float factor = 1.0f;
		if (s.lightningEnabled && m_lightningOn)
		{
			factor = s.lightningFactor.getValue(clientRandom);
		}
		gray = ftol((double)factor * 255.0);
	}
	p.color = (std::uint32_t)gray * 0x10101u + 0xFF000000u;
	p.heights = m_heights.data();
	return p;
}

void SnowManager::particleCenter(const SnowDrawParams &p, int gx, int gy, float out[3])
{
	const int x = p.originX + gx * p.spacing;
	const int y = p.originY + gy * p.spacing;
	const unsigned noise = (((unsigned)(y - 32) & 63u) * 64u) + ((unsigned)(x - 32) & 63u);
	const float h = p.heights[noise] + p.heightTraveled;
	const float cycles = (float)std::floor((double)(h / p.box));
	const float z = p.ceiling - (h - p.box * cycles);
	float fx = (float)x, fy = (float)y;
	if (p.amplitude > 0.0f)
	{
		fx = fastSin((p.frequencyX * z + fx) * 162.97466f) * p.amplitude + fx;
		fy = fastSin((p.frequencyY * z + fy) * 162.97466f) * p.amplitude + fy;
	}
	out[0] = (p.driftX - cycles * p.windStepX) + fx;
	out[1] = (p.driftY - cycles * p.windStepY) + fy;
	out[2] = z;
}

std::vector<std::string> SnowManager::stopLines()
{
	return { "[S-4890] SnowManager (RW 0x4943E1 / 0x4936CB): the falling snow / rain, the Freezing Rain spell ramp and the drops' lightning are "
			 "ported; the CRT rand() of the starting heights and the lightning rolls is an MSVCR71 generator of the manager's own (retail shares the "
			 "process's rand state), the WorldHeightMap draw origin is taken as 0, the point sprite path (SnowPointSprites with the caps flag) is "
			 "drawn as the quad path, the snow shader word 0x198B3's gradient bits are not decoded" };
}

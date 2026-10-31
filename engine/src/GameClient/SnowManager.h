// OpenBFME. GPL-3.0.
//
// Lane WEATHER-1: TheSnowManager (RotWK W3DSnowManager, global RW 0xDE3B54): the falling snow / rain of the Weather block, its Freezing Rain spell
// ramp and the lightning flash of the drops. Client only: no simulation state, no logic random number.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; BFME2 1.06 matching C++ donor Code\GameEngine\Source\GameClient\Snow.cpp and
// Code\GameEngineDevice\Source\W3DDevice\GameClient\W3DSnow*.cpp agree with every offset below):
//   layout: +0x0C the 64 x 64 starting heights, +0x10 time, +0x14 velocity, +0x18 fullTimePeriod, +0x1C / +0x20 frequency x / y, +0x24 amplitude,
//     +0x28 .. +0x30 point sizes, +0x34 quad size, +0x38 box dimensions, +0x3C emitter spacing, +0x40 visible, +0x41 snowing, +0x44 tiles,
//     +0x48 / +0x4C wind, +0x50 lightning on, +0x54 lightning frames, +0x58 spell frames, +0x5C / +0x60 ramp start / end fraction, +0x64 / +0x68
//     ramp spacing initial / peak, +0x6C / +0x70 ramp velocity initial / peak, +0x90 ceiling, +0x94 height travelled, +0x98 / +0x9C drift origin,
//     +0xA0 the particle count drawn, +0xA8 the weather the ramp follows, +0xAC the ramp phase (0 none, 1 rising, 2 peak, 3 falling).
//   updateIniSettings RW 0x600ADE (base) + RW 0x4934D3 (the texture): starting height i = (float)(rand() % (int)SnowBoxHeight) for the 4096 cells in
//     order (MSVCR71 rand), then velocity = SnowSpeed, frequencies, amplitude, point size, quad size, box = SnowBoxHeight, spacing = SnowSpacing,
//     the point size limits, tiles = NumberTiles, wind = SnowXYSpeed, fullTimePeriod = box / velocity, snowing = IsSnowing, lightning frames =
//     LightningDuration, spell frames = SpellDuration, the ramp fractions = RampControl, the ramp spacings = RampSpacing, velocities = RampSpeed.
//   reset RW 0x493547: base reset RW 0x600AAD (visible, the overrides deleted, updateIniSettings), finish RW 0x492E5F, weather state 0, lightning off.
//   update RW 0x4943E1 (GameClient::update RW 0x64849E calls it once per client frame): time += (WW3D SyncTime - PreviousSyncTime) ms * 0.001f
//     (x87; a negative difference + 2^32), time = (float)fmod(time, fullTimePeriod) (RW 0x492AC8 / 0x492AAD), then height travelled += dtime *
//     velocity, drift x += dtime * wind x, drift y += dtime * wind y; the spell ramp transition RW 0x49405A; the drops' lightning RW 0x492C35.
//   the ramp: RW 0x49405A (only with SpellEnabled): phase 0 and TheGlobalWeatherSystem's weather RAINY (2) while the followed weather is not: follow
//     it and start RW 0x492D51 (phase 1, snowing on, frames = SpellDuration, fractions = RampControl); otherwise phase 1 or 3 counts the frames
//     down, then the blend RW 0x493563, then a followed RAINY that has gone becomes phase 3 with frames = (int)((1 - end) * SpellDuration) and the
//     new weather followed. The blend: f = (float)(SpellDuration - frames) / (float)SpellDuration; f > 1: finish; f > end: phase 3, b = (f - end) /
//     (1 - end), spacing = peak - (peak - initial) * b, velocity likewise; f > start: phase 2, the peak values; f <= 0: finish; else phase 1,
//     b = f / start (1 when start <= 1e-5), spacing = (peak - initial) * b + initial, velocity likewise. finish RW 0x492E5F: frames 0, phase 0,
//     the followed weather = the current one; without IsSnowing: spacing = the initial ramp spacing, snowing off, velocity = the PEAK RAMP
//     SPACING (+0x68, retail's own field choice); with IsSnowing: spacing, velocity and wind back to the setting's.
//   the drops' lightning RW 0x492C35 (only with LightningEnabled): off: when Random_Float() < LightningChance, on with frames =
//     ftol((Random_Float() * 0.50000006f + 0.7f) * LightningDuration + 0.5f); on: --frames, off at < 1. Random_Float RW 0xB25D60 =
//     (rand() & 0xFFF) * (float)(1 / 4095).
//   render RW 0x494117 (the shadow pass and a disabled / not snowing / invisible manager draw nothing): the point sprite path needs the caps flag and
//     SnowPointSprites (retail weather.ini: No); the quad path RW 0x4936CB over the playable area: origin = WorldHeightMap draw origin * 10,
//     dims = (int)((float)(width - 2 * border) * 10.0f); ceiling = box; tiles >= 1, step = (dim - origin) / tiles, spacing = max(1, (int)spacing),
//     count = (stepY * stepX) / spacing^2 particles in row order from (originX, originY) step `spacing`; each: noise index = ((y - 32) & 63) * 64 +
//     ((x - 32) & 63), h = heights[index] + travelled, cycles = (float)floor(h / box), z = ceiling - (h - box * cycles), x' = x + amp *
//     Fast_Sin((freqX * z + x) * 162.97466f) and y' likewise with freqY when amplitude > 0, then x' += driftX - cycles * windX * box / velocity,
//     y' likewise; the quad corners centre + (-r + u), (-r - u), (r - u), (r + u) with r the camera's right axis (its transform column 0) and
//     u = (0, 0, 1), both times quadSize * 0.707f, UV (0,0) (0,1) (1,1) (1,0), indices 3 0 2 / 2 0 1; vertex colour gray * 0x10101 + 0xFF000000
//     with gray = 255 while the ramp phase is 0, else (int)(the lightning factor * 255.0) where the factor is 1.0 unless LightningEnabled and the
//     lightning is on, then LightningFactor.getValue() (RW 0x492CE9); the batch is drawn tiles x tiles times, translated by (j * stepX, i * stepY);
//     shader word RW 0xD997D0 = 0x198B3 (alpha blend SRC_ALPHA / INV_SRC_ALPHA, depth test LEQUAL without depth write, fog off), material
//     PRELIT_DIFFUSE, texture stage 0 the snow texture.
// INFERENCE (stop S-4890): the CRT rand() state is the process's (every caller of rand() shares it), so the starting heights and the lightning
//   rolls cannot match a retail session; this manager owns an MSVCR71-equivalent generator of its own seeded with the CRT default 1. The
//   WorldHeightMap draw origin (+0x120E0) is taken as 0 (the whole map is drawn). Fast_Sin is the 1024-entry WWMath table lerp (donor WWMath).

#pragma once

#include "GameClient/WeatherSetting.h"

#include <cstdint>
#include <string>
#include <vector>

class W3DDrawRandom;

// MSVCR71 rand(): holdrand = holdrand * 214013 + 2531011, ((holdrand >> 16) & 0x7FFF); srand's default seed is 1.
class CrtRand
{
public:
	explicit CrtRand(std::uint32_t seed = 1u) : m_holdrand(seed) {}
	int rand()
	{
		m_holdrand = m_holdrand * 214013u + 2531011u;
		return (int)((m_holdrand >> 16) & 0x7FFFu);
	}
	// WWMath::Random_Float RW 0xB25D60: (rand() & 0xFFF) * (float)(1.0 / 4095) on the x87, stored as float (SnowManager.cpp: the weather headers reach an
	// audited simulation-input file, tools/sim/sim_audit.py, so their float maths lives in the .cpp files)
	float randomFloat();
	std::uint32_t state() const { return m_holdrand; }

private:
	std::uint32_t m_holdrand;
};

// What the renderer needs for one frame of the quad path (RW 0x4936CB), in SAGE world space.
struct SnowDrawParams
{
	bool draw = false;              ///< RW 0x494117's gates: SnowEnabled, snowing (+0x41), visible (+0x40)
	bool pointSprites = false;      ///< the point sprite path would run (caps + SnowPointSprites); the port draws quads either way (S-4890)
	std::string texture;            ///< SnowTexture
	int tiles = 1;
	int originX = 0, originY = 0;   ///< the grid origin (draw origin * 10)
	int stepX = 0, stepY = 0;       ///< one tile
	int spacing = 1;
	int count = 0;                  ///< particles per tile ((stepY * stepX) / spacing^2)
	int columns = 0;                ///< grid points per row: ceil(stepX / spacing)
	float heightTraveled = 0.0f;    ///< +0x94
	float driftX = 0.0f, driftY = 0.0f; ///< +0x98 / +0x9C
	float windStepX = 0.0f, windStepY = 0.0f; ///< wind * box / velocity
	float box = 0.0f;               ///< +0x38
	float ceiling = 0.0f;           ///< +0x90 (= box)
	float amplitude = 0.0f, frequencyX = 0.0f, frequencyY = 0.0f;
	float quadHalf = 0.0f;          ///< quadSize * 0.707f
	std::uint32_t color = 0xFFFFFFFFu; ///< ARGB (gray * 0x10101 + 0xFF000000)
	const float *heights = nullptr; ///< 4096 starting heights (row y * 64 + x)
};

class SnowManager
{
public:
	enum { kMaxSnowSpacing = 1 << 20 }; ///< drawParams' bound of a map's SnowSpacing (wider than any map: no particle)
	// review of #59: drawParams' most particles per tile (a client limit, not retail's: S-4890). The stock Weather (SnowSpacing 30, NumberTiles 4) gives
	// about 2,500 on a 600 x 600 map; NumberTiles = 1 with SnowSpacing = 1 there asked for 33.6 million, which crashed the client
	enum { kMaxSnowParticles = 1 << 16 };
	SnowManager() : m_heights(64 * 64, 0.0f) {}

	// SnowManager::init (RW 0x600B.. / W3D 0x494642): time 0, then updateIniSettings
	void init(const WeatherSetting &s);
	// RW 0x600ADE + RW 0x4934D3
	void updateIniSettings(const WeatherSetting &s);
	// RW 0x493547 with the store's overrides deleted by the caller first (RW 0x600AAD): visible, updateIniSettings, finish, weather state 0, lightning off
	void reset(const WeatherSetting &baseSetting, int globalWeather);
	// RW 0x4943E1: one client frame, `elapsedMs` the WW3D sync time difference (unsigned 32-bit wrap as retail), `globalWeather` TheGlobalWeatherSystem + 0x10
	void update(const WeatherSetting &s, std::uint32_t elapsedMs, int globalWeather);
	// the two halves of update, for a renderer that draws faster than the client frames: the time part (RW 0x4943E6 .. 0x49449F) at every render
	// frame with the real elapsed milliseconds, the counter part (RW 0x49405A, 0x492C35) once per client frame (S-4890)
	void advanceTime(std::uint32_t elapsedMs);
	void clientFrame(const WeatherSetting &s, int globalWeather);
	// RW 0x4936CB's per-frame values for the playable area of a map (width, height, border of the WorldHeightMap) and the lightning colour draw
	SnowDrawParams drawParams(const WeatherSetting &s, int mapWidth, int mapHeight, int mapBorder, W3DDrawRandom &clientRandom) const;
	// the position RW 0x4936CB computes for grid point (gx, gy) of tile (0, 0) (the reference of the device layer's vertex shader)
	static void particleCenter(const SnowDrawParams &p, int gx, int gy, float out[3]);
	// WWMath::Fast_Sin (the 1024-entry table, lerped): sin(2*pi*i/1024) at the two nearest entries
	static float fastSin(float x);

	CrtRand &crtRand() { return m_rand; }
	void setVisible(bool v) { m_visible = v; }

	// the state, for tests and the report
	float time() const { return m_time; }
	float velocity() const { return m_velocity; }
	float fullTimePeriod() const { return m_fullTimePeriod; }
	float spacing() const { return m_spacing; }
	float heightTraveled() const { return m_heightTraveled; }
	float driftX() const { return m_driftX; }
	float driftY() const { return m_driftY; }
	bool snowing() const { return m_snowing; }
	bool visible() const { return m_visible; }
	int phase() const { return m_phase; }
	int spellFrames() const { return m_spellFrames; }
	int followedWeather() const { return m_weatherState; }
	bool lightningOn() const { return m_lightningOn; }
	int lightningFrames() const { return m_lightningFrames; }
	const std::vector<float> &startingHeights() const { return m_heights; }

	static std::vector<std::string> stopLines();

private:
	void transition(const WeatherSetting &s, int globalWeather); ///< RW 0x49405A
	void start(const WeatherSetting &s);                        ///< RW 0x492D51
	void blend(const WeatherSetting &s, int globalWeather);     ///< RW 0x493563
	void finish(const WeatherSetting &s, int globalWeather);    ///< RW 0x492E5F
	void lightning(const WeatherSetting &s);                    ///< RW 0x492C35

	CrtRand m_rand;
	std::vector<float> m_heights;  ///< +0x0C
	float m_time = 0.0f;           ///< +0x10
	float m_velocity = 1.0f;       ///< +0x14
	float m_fullTimePeriod = 0.0f; ///< +0x18
	float m_freqX = 0.0f, m_freqY = 0.0f, m_amplitude = 0.0f; ///< +0x1C .. +0x24
	float m_quadSize = 0.0f;       ///< +0x34
	float m_box = 0.0f;            ///< +0x38
	float m_spacing = 0.0f;        ///< +0x3C
	bool m_visible = true;         ///< +0x40
	bool m_snowing = false;        ///< +0x41
	int m_tiles = 0;               ///< +0x44
	float m_windX = 0.0f, m_windY = 0.0f; ///< +0x48 / +0x4C
	bool m_lightningOn = false;    ///< +0x50
	int m_lightningFrames = 0;     ///< +0x54
	int m_spellFrames = 0;         ///< +0x58
	float m_startFraction = 0.0f, m_endFraction = 0.0f; ///< +0x5C / +0x60
	float m_rampSpacing0 = 0.0f, m_rampSpacing1 = 0.0f; ///< +0x64 / +0x68
	float m_rampVelocity0 = 0.0f, m_rampVelocity1 = 0.0f; ///< +0x6C / +0x70
	float m_heightTraveled = 0.0f; ///< +0x94
	float m_driftX = 0.0f, m_driftY = 0.0f; ///< +0x98 / +0x9C
	int m_weatherState = 0;        ///< +0xA8
	int m_phase = 0;               ///< +0xAC
};

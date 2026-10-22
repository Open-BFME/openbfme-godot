// OpenBFME. GPL-3.0.
// Lane WEATHER-1. See GameClient/CloudEffect.h for the target facts.

#include "GameClient/CloudEffect.h"

#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDrawServices.h"

#include <cmath>

namespace
{
int ftol(double v) { return (int)v; } // rotwk_crt_ftol2: truncation
int clamp255(int v) { return v > 255 ? 255 : (v < 0 ? 0 : v); }
} // namespace

void CloudEffect::refreshSettings(const CloudEffectData &d)
{
	m_s.propagateSpeed = d.propagateSpeed * 0.001f;               // rotwk_g_Shared001Conversion RW 0xBD88A0
	m_s.angle = (float)((double)d.angle * (double)0.017453292f);    // RW 0xBD1900 (x87: fild, fmul, stored)
	m_s.dissipateStartLevel = d.dissipateStartLevel;
	m_s.dissipateSpeed = (float)((double)d.dissipateSpeed * 0.001); // RW 0xBDCD88 (double)
	m_s.darkening = d.darkeningFactor;
	m_s.darkeningRain = d.darkeningFactorRain;
	m_s.lightningShadows = d.lightningShadows;
	m_s.jitterPosition = d.jitterLightningLightPosition;
	m_s.jitterIntensity = d.jitterLightningLightIntensity;
	m_s.lightningChance = d.lightningChance;
	m_s.shadowColor = d.lightningShadowColor;
	m_s.shadowIntensity = d.lightningShadowIntensity;
	m_s.lightningDuration = d.lightningDuration;
	m_s.lightningFrequency = d.lightningFrequency;
	m_s.lightningIntensity = d.lightningIntensity;
	for (int i = 0; i < 3; ++i)
	{
		m_s.positions[i] = d.lightningLightPosition[i];
	}
	m_s.lightningFX = d.lightningFX;
	m_s.scrollX = (float)std::cos((double)m_s.angle) * d.cloudScrollSpeed * -0.0099999998f; // RW 0xBDD74C
	m_s.scrollY = (float)std::sin((double)m_s.angle) * d.cloudScrollSpeed * 0.0099999998f;  // RW 0xBE5600
}

void CloudEffect::lightDirection(float x, float y, float out[3])
{
	const float a = y * 0.017453292f;
	const float b = x * 0.017453292f + 1.5707964f; // RW 0xBD89D0
	float px = -((float)std::sin((double)b) * (float)std::cos((double)a));
	float py = -((float)std::sin((double)b) * (float)std::sin((double)a));
	float pz = -(float)std::cos((double)b);
	const float len2 = pz * pz + py * py + px * px;
	if (len2 != 0.0f)
	{
		const float inv = 1.0f / std::sqrt(len2); // RW 0x441C56 (the inverse square root)
		px *= inv;
		py *= inv;
		pz *= inv;
	}
	out[0] = px * 10000.0f; // RW 0xBDE8B8
	out[1] = py * 10000.0f;
	out[2] = pz * 10000.0f;
}

std::uint32_t CloudEffect::lightningShadowColor(const CloudEffectSettings &s)
{
	const float a = (float)clamp255(ftol((1.0 - (double)s.shadowIntensity) * 255.0));
	const int r = clamp255((int)(s.shadowColor.red * s.shadowIntensity * 255.0f + a));
	const int g = clamp255((int)(s.shadowColor.green * s.shadowIntensity * 255.0f + a));
	const int b = clamp255((int)(s.shadowColor.blue * s.shadowIntensity * 255.0f + a));
	return (((std::uint32_t)r + 0xFF00u) * 0x100u + (std::uint32_t)g) * 0x100u + (std::uint32_t)b; // 32-bit wrap as retail's imul / add
}

void CloudEffect::pickPosition()
{
	const int k = ftol((double)m_rand.randomFloat() * 100.0) % 3; // RW 0x615124: Random_Float * 100.0f, ftol, idiv 3
	if (k < 0 || k > 2)
	{
		return;
	}
	m_positionIndex = k;
	lightDirection(m_s.positions[k].x, m_s.positions[k].y, m_shadowDir);
}

void CloudEffect::stopLightning()
{
	m_lightningOn = false;
	m_lightningFrames = 0;
	m_intensity = 0.0f;
	m_hits = 0;
	m_dark[0] = m_dark[1] = m_dark[2] = 0.0f;
	if (m_s.lightningShadows)
	{
		m_positionIndex = -1; // the saved shadow light position and the saved shadow colour go back (no receiver: S-4891)
	}
	m_light[0] = m_light[1] = m_light[2] = 0.0f; // RW 0x46DEA9(scene, +0xEC, subtract), then +0xE0 .. +0xF4 = 0
}

CloudEffectFrame CloudEffect::update(std::uint32_t clientFrame, int w, const WeatherSettingStore &store, float loX, float loY, float hiX, float hiY,
	W3DDrawRandom &clientRandom, bool fxExists)
{
	CloudEffectFrame f;
	if (clientFrame == m_lastFrame)
	{
		f.active = m_phase != 0;
		for (int i = 0; i < 3; ++i)
		{
			f.darkening[i] = m_dark[i];
			f.lightColor[i] = m_light[i];
		}
		f.lightningIntensity = m_intensity;
		return f;
	}
	m_lastFrame = clientFrame;
	if (w == 1)
	{
		if (m_followed != 1)
		{
			refreshSettings(store.cloudEffect());
			m_phase = 1;
			m_followed = 1;
		}
	}
	else if (m_followed == 1)
	{
		m_phase = 3;
		m_followed = w;
	}
	const bool hasLightning = (w >= 0 && w < WEATHER_TYPE_COUNT) && store.weatherData(w).hasLightning; // RW 0x719BE4
	if (!hasLightning && m_lightningOn)
	{
		stopLightning();
	}
	// RW 0xD9989C == 2 (the render path; nothing writes it)
	if (m_phase == 1)
	{
		if (--m_counter < 1)
		{
			m_counter = 0;
			m_phase = 2;
		}
	}
	else if (m_phase == 3)
	{
		if (++m_counter > 254)
		{
			m_counter = 255;
			m_phase = 0;
		}
	}
	// ---- RW 0x494DBA
	if (m_phase != 0)
	{
		const RGBColor &c = m_followed == 2 ? m_s.darkeningRain : m_s.darkening;
		const float col[3] = { c.red, c.green, c.blue };
		if (m_phase != 2)
		{
			const float k = (float)(1.0 - (double)m_counter * 0.00392156862745098); // RW 0xBD2C98 - counter * RW 0xBDE190 (doubles)
			for (int i = 0; i < 3; ++i)
			{
				m_dark[i] = col[i] * k;
			}
			f.relightTerrain = clientFrame % 10u == 0u; // GameClient vtable +0x7C (the client frame) % 10
		}
		else
		{
			bool setColour = true;
			if (hasLightning)
			{
				if (m_lightningOn)
				{
					for (int i = 0; i < 3; ++i)
					{
						m_dark[i] = col[i];
					}
					setColour = false;
					if (--m_lightningFrames < 1)
					{
						stopLightning();
					}
					else
					{
						if (m_s.jitterIntensity)
						{
							float i = m_s.lightningIntensity.getValue(clientRandom);
							if ((double)i > 0.9) // RW 0xBDE1A0 (double 0.9)
							{
								i = 0.89999998f; // RW 0xBDE198
							}
							m_intensity = i;
							m_light[0] = m_light[1] = m_light[2] = i;
						}
						if (m_s.jitterPosition)
						{
							const float r = m_rand.randomFloat();
							if (r < m_s.lightningFrequency && m_s.lightningShadows)
							{
								pickPosition();
							}
						}
					}
				}
				else if (m_rand.randomFloat() < m_s.lightningChance && ++m_hits > 3)
				{
					m_lightningOn = true;
					m_lightningFrames = ftol((double)m_s.lightningDuration.getValue(clientRandom));
					float i = m_s.lightningIntensity.getValue(clientRandom);
					if ((double)i > 0.9)
					{
						i = 0.89999998f;
					}
					m_intensity = i;
					m_light[0] = m_light[1] = m_light[2] = i;
					if (m_s.lightningShadows)
					{
						pickPosition();
						m_shadowColor = lightningShadowColor(m_s);
					}
					// RW 0x494FA6 .. 0x495018: the FXList is looked up while +0xFC is still null (an empty name is not looked up); none found: no FX
					if (!m_s.lightningFX.empty() && fxExists)
					{
						f.playFX = true;
						f.fxName = m_s.lightningFX;
						f.fxPosition.x = (loX + hiX) * 0.5f; // RW 0xBD869C, the TerrainLogic extent's centre
						f.fxPosition.y = (loY + hiY) * 0.5f;
						f.fxPosition.z = 0.0f;
					}
				}
			}
			if (setColour)
			{
				for (int i = 0; i < 3; ++i)
				{
					m_dark[i] = col[i];
				}
			}
		}
	}
	f.active = m_phase != 0;
	for (int i = 0; i < 3; ++i)
	{
		f.darkening[i] = m_dark[i];
		f.lightColor[i] = m_light[i];
	}
	f.lightningIntensity = m_intensity;
	return f;
}

std::vector<std::string> CloudEffect::stopLines()
{
	return { "[S-4891] CloudEffect (RW 0x497D00 / 0x494DBA): the Darkness darkening and the lightning of a weather with HasLightning are ported; "
			 "Random_Float is an MSVCR71 rand() of the effect's own (retail shares the process's), the lightning shadow light position and colour "
			 "(LightningShadows) have no receiver (shadows are not drawn, S-390), and which terms of the object light environments the darkening "
			 "scales is inferred (ambient and diffuse); the cloud material pass (RW 0x495228) is never installed in RotWK and draws nothing" };
}

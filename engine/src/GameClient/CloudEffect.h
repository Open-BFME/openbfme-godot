// OpenBFME. GPL-3.0.
//
// Lane WEATHER-1: TheCloudEffect (RotWK W3D cloud effect, global RW 0xDE3C24, 0x108 bytes, ctor RW 0x494C65, vtable RW 0xBDE130): the darkening
// of the Darkness spell (TheGlobalWeatherSystem's weather CLOUDY) and the lightning of a weather whose WeatherData has HasLightning. Client only.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; there is no BFME1 / ZH donor for this class):
//   settings RW 0x614C71 (vtable slot 0x38, run by the CloudEffect block parse and on every entry into CLOUDY) from the INI data RW 0xDE3CC0:
//     +0x18 PropagateSpeed * 0.001f, +0x1C Angle * (pi / 180), +0x2C DissipateSpeed * 0.001, +0x30 DarkeningFactor, +0x3C DarkeningFactorRain,
//     +0x48 / +0x4C the rates, +0x5C LightningShadows, +0x90 / +0x91 the jitter flags, +0x60 LightningChance, +0x64 the shadow colour, +0x70 its
//     intensity, +0x74 LightningDuration, +0x80 LightningFrequency, +0x84 LightningIntensity, +0x94 .. +0xA8 the three positions, +0xAC
//     LightningFX, +0x50 / +0x54 = (cos(angle) * CloudScrollSpeed * -0.01f, sin(angle) * CloudScrollSpeed * 0.01f).
//   state: +0xBC the weather followed, +0xC0 the phase (0 off, 1 darkening, 2 dark, 3 lightening), +0xC4 the counter (255 at construction), +0xC8 the
//     lightning on, +0xCC its frames, +0xD0 its intensity, +0xD4 .. +0xDC the darkening colour, +0xE0 the shadow light position saved, +0xEC the
//     colour added to the scene's light 0, +0xF8 the shadow colour saved (0x555555 at construction), +0xFC the FXList found, +0x100 the last client
//     frame, +0x104 the lightning roll hits.
//   update RW 0x497D00 (vtable slot 0x28, GameClient::update RW 0x64849E, once per client frame; nothing when the client frame did not change):
//     weather w = TheGlobalWeatherSystem + 0x10; w CLOUDY: on entry (followed != 1) the settings refresh, phase 1, followed 1; else a followed CLOUDY
//     becomes phase 3 following w. Without lightning for w (RW 0x719BE4: the WeatherData of w, false outside 0..4) a lightning that is on stops
//     (all its state zeroed with the darkening colour, the shadow light position and colour restored when LightningShadows, the light colour taken
//     back off light 0). With the render path 2 (RW 0xD9989C, always 2: no writer): phase 1 counts the counter down to 0 (then phase 2), phase 3
//     up to 255 (then phase 0). Then RW 0x494DBA:
//       phase 0: nothing. colour = DarkeningFactor, DarkeningFactorRain when the followed weather is RAINY. Phase 1 / 3: darkening = colour *
//       (1.0 - counter * (1 / 255.0)), and on every client frame that is a multiple of 10 the terrain is lit again (TheTerrainRenderObject slot 0x224).
//       Phase 2 with lightning for w: the lightning on: darkening = colour, --frames, at < 1 it stops (as above, the darkening colour 0); else with
//       JitterLightningLightIntensity a new intensity (LightningIntensity, at most 0.9) replaces the added light colour, and with
//       JitterLightningLightPosition when Random_Float() < LightningFrequency and LightningShadows a new shadow light position. The lightning off:
//       when Random_Float() < LightningChance and then more than 3 such hits: on, frames = ftol(LightningDuration), intensity = LightningIntensity
//       (at most 0.9), the light colour (i, i, i); with LightningShadows the shadow light position saved and replaced (RW 0x615124) and the shadow
//       colour saved and replaced by RW 0x614AB6; the colour added to light 0 (RW 0x46DEA9: diffuse += c, ambient += c * 0.5); LightningFX (looked
//       up once) played at the centre of the map's extent (TerrainLogic slot 0x20: (lo + hi) * 0.5, z 0; RW 0x494615 = FXList::doFXPos). Then (and
//       in every other phase-2 case) darkening = colour.
//   readers: slot 0x3C isActive = phase != 0; slot 0x48 the darkening colour; slot 0x4C the lightning intensity. The terrain shader's LightningColor
//     (SAS RW 0x4E26D7) = (i, i, i) while active, else 0; DarkeningColor (RW 0x4E2659) is set but terrain.fxo never reads it. The light colours of the
//     terrain light environment (RW 0x470F44: light diffuse and ambient), the terrain vertex lighting (RW 0x468CC0: every global light's diffuse) and
//     the object light environments (RW 0x4E28F0 / 0x4E44FB / 0x4EDE13) are multiplied per channel by (1 - darkening) while active.
//   The material pass of the constructor (RW 0x4948AB, global RW 0xDCA2C0, Install_Materials RW 0x495228: the dark cloud texture over the terrain) is
//     never installed: only the cloud effect's own functions reference RW 0xDCA2C0 (dead in RotWK 2.01, nothing to draw).
// INFERENCE (stop S-4891): Random_Float is the shared CRT rand() (the effect owns an MSVCR71 generator, SnowManager.h); the shadow light position and
//   colour have no receiver (shadows are not drawn here, S-390); which object light environment terms the darkening scales (ambient and diffuse here).

#pragma once

#include "GameClient/SnowManager.h"
#include "GameClient/WeatherSetting.h"

#include <cstdint>
#include <string>
#include <vector>

class W3DDrawRandom;

struct CloudEffectSettings
{
	float propagateSpeed = 0.0f; ///< +0x18
	float angle = 0.0f;          ///< +0x1C radians
	float dissipateStartLevel = 0.0f, dissipateSpeed = 0.0f; ///< +0x28 / +0x2C
	RGBColor darkening{ 0, 0, 0 };     ///< +0x30
	RGBColor darkeningRain{ 0, 0, 0 }; ///< +0x3C
	bool lightningShadows = false;     ///< +0x5C
	bool jitterPosition = false;       ///< +0x90
	bool jitterIntensity = false;      ///< +0x91
	float lightningChance = 0.0f;      ///< +0x60
	RGBColor shadowColor{ 0, 0, 0 };   ///< +0x64
	float shadowIntensity = 0.0f;      ///< +0x70
	GameClientRandomVariable lightningDuration;  ///< +0x74
	float lightningFrequency = 0.0f;   ///< +0x80
	GameClientRandomVariable lightningIntensity; ///< +0x84
	Coord2D positions[3];              ///< +0x94 .. +0xA8
	std::string lightningFX;           ///< +0xAC
	float scrollX = 0.0f, scrollY = 0.0f; ///< +0x50 / +0x54
};

// What a client frame of the effect asks of the rest of the client
struct CloudEffectFrame
{
	bool active = false;            ///< slot 0x3C
	float darkening[3] = { 0, 0, 0 }; ///< slot 0x48
	float lightningIntensity = 0.0f;  ///< slot 0x4C (+0xD0)
	float lightColor[3] = { 0, 0, 0 }; ///< the colour added to the scene's light 0 (+0xEC)
	bool relightTerrain = false;    ///< a phase 1 / 3 frame that is a multiple of 10
	bool playFX = false;            ///< LightningFX starts this frame, at fxPosition
	std::string fxName;
	Coord3D fxPosition;
};

class CloudEffect
{
public:
	// RW 0x614C71 from the INI data
	void refreshSettings(const CloudEffectData &d);
	// RW 0x497D00 + RW 0x494DBA for client frame `clientFrame`; `weatherData` gives HasLightning (RW 0x719BE4); the map extent is TerrainLogic's.
	// `fxExists` answers whether LightningFX names an FXList (RW 0x5E20A2 on TheFXListStore)
	CloudEffectFrame update(std::uint32_t clientFrame, int globalWeather, const WeatherSettingStore &store, float extentLoX, float extentLoY, float extentHiX,
		float extentHiY, W3DDrawRandom &clientRandom, bool fxExists);

	CrtRand &crtRand() { return m_rand; }
	const CloudEffectSettings &settings() const { return m_s; }
	int phase() const { return m_phase; }
	int counter() const { return m_counter; }
	int followedWeather() const { return m_followed; }
	bool lightningOn() const { return m_lightningOn; }
	int lightningFrames() const { return m_lightningFrames; }
	int lightningHits() const { return m_hits; }
	float lightningIntensity() const { return m_intensity; }
	const float *darkening() const { return m_dark; }
	// the shadow light direction (RW 0x61501A of the chosen LightningLightPosition) and shadow colour the lightning set (no receiver here: S-4891)
	const float *shadowLightDirection() const { return m_shadowDir; }
	std::uint32_t shadowColor() const { return m_shadowColor; }
	int lightningPositionIndex() const { return m_positionIndex; }

	// RW 0x61501A: LightningLightPosition (x, y in degrees) to the light direction
	static void lightDirection(float x, float y, float out[3]);
	// RW 0x614AB6: the lightning shadow colour
	static std::uint32_t lightningShadowColor(const CloudEffectSettings &s);
	static std::vector<std::string> stopLines();

private:
	void stopLightning();
	void pickPosition(); ///< RW 0x615124
	CloudEffectSettings m_s;
	CrtRand m_rand;
	int m_followed = 0;       ///< +0xBC
	int m_phase = 0;          ///< +0xC0
	int m_counter = 255;      ///< +0xC4
	bool m_lightningOn = false; ///< +0xC8
	int m_lightningFrames = 0;  ///< +0xCC
	float m_intensity = 0.0f;   ///< +0xD0
	float m_dark[3] = { 0, 0, 0 };  ///< +0xD4
	float m_light[3] = { 0, 0, 0 }; ///< +0xEC
	std::uint32_t m_lastFrame = 0; ///< +0x100 (0 at construction: a client frame 0 is not run)
	int m_hits = 0;             ///< +0x104
	float m_shadowDir[3] = { 0, 0, 0 };
	std::uint32_t m_shadowColor = 0x555555u; ///< +0xF8 (the saved one)
	int m_positionIndex = -1;
};

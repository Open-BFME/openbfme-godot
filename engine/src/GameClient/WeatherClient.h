// OpenBFME. GPL-3.0.
//
// Lane WEATHER-1: the client's weather for one game: the WeatherSetting store of the map, TheSnowManager, TheCloudEffect and the weather sound of
// TheGlobalWeatherSystem (RW 0x719DEE), driven by the logic's weather (TheGlobalWeatherSystem + 0x10, read from the presented snapshot) at the
// client frame rate, with the snow's time advanced at every render frame. Client only: it never touches the simulation.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * GameClient::update (RW 0x64849E) updates TheSnowManager (vtable +0x28 -> RW 0x4943E1) and TheCloudEffect (+0x28 -> RW 0x497D00) once per call;
//     the port runs 30 client frames per second (FXPlayback, DrawableFade: RW 0x63CF0F's 30 / 5).
//   * the weather sound RW 0x719DEE, called by setWeather RW 0x71A024 when the weather changes: TheAudio->removeAudioEvent(+0x14), handle = 1, and
//     when the new weather's WeatherSound is set, handle = TheAudio->addAudioEvent(that event). GlobalWeatherSystem::reset (RW 0x719FDB) removes it.
// INFERENCE (stop S-4892): the sound follows the weather here on the client frame that first sees the new weather in the presented snapshot (retail
//   calls the audio inside the logic frame); the snow's time is integrated per render frame with the real elapsed milliseconds and its counters per
//   client frame (retail draws one render frame per client frame).

#pragma once

#include "GameClient/CloudEffect.h"
#include "GameClient/SnowManager.h"
#include "GameClient/WeatherSetting.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class W3DDrawRandom;

struct WeatherClientFrame
{
	SnowDrawParams snow;
	CloudEffectFrame cloud;          ///< the last client frame's cloud effect
	float terrainDarkening[3] = { 0, 0, 0 }; ///< the darkening the terrain vertex lighting was last computed with (RW 0x468CC0, relit every 10th frame)
	bool terrainDarkened = false;    ///< the last relight saw the effect active
	std::vector<CloudEffectFrame> fx; ///< the client frames of this advance that start LightningFX
	int clientFrames = 0;            ///< client frames run by this advance
};

class WeatherClient
{
public:
	// What the weather asks of the audio: start an event (a handle back, 0 for none) / stop a handle
	struct Audio
	{
		std::function<std::uint32_t(const std::string &eventName)> start;
		std::function<void(std::uint32_t handle)> stop;
	};

	// The map's weather (WeatherSettingStore::loadForMap), then TheSnowManager::init and the CloudEffect settings. The map's playable extent
	// (TerrainLogic slot 0x20) and its height map size (width, height, border in cells) are fixed for the game.
	bool load(ArchiveFileSystem &fs, const std::string &mapDir, WeatherSettingStore::SoundValidator validSound, int mapWidth, int mapHeight, int mapBorder,
		float extentHiX, float extentHiY, std::string *error);
	bool loaded() const { return m_loaded; }

	void setAudio(Audio a) { m_audio = std::move(a); }
	// does an FXList of this name exist (TheFXListStore)
	void setFXExists(std::function<bool(const std::string &)> f) { m_fxExists = std::move(f); }

	// One render frame: `seconds` of game time (already scaled; 0 while paused), the logic's weather of the presented frame. Runs the client frames
	// that are due (30 per second) and returns what to draw.
	WeatherClientFrame advance(double seconds, int globalWeather, W3DDrawRandom &clientRandom);

	// the end of the game: GlobalWeatherSystem::reset's sound removal and SnowManager::reset
	void shutdown();

	const WeatherSettingStore &store() const { return m_store; }
	const SnowManager &snow() const { return m_snow; }
	const CloudEffect &cloud() const { return m_cloud; }
	std::uint32_t clientFrame() const { return m_frame; }
	std::uint32_t soundHandle() const { return m_soundHandle; }
	int soundWeather() const { return m_soundWeather; }
	unsigned long long soundChanges() const { return m_soundChanges; }

	static std::vector<std::string> stopLines();

private:
	void followSound(int globalWeather);

	WeatherSettingStore m_store;
	SnowManager m_snow;
	CloudEffect m_cloud;
	Audio m_audio;
	std::function<bool(const std::string &)> m_fxExists;
	bool m_loaded = false;
	int m_mapWidth = 0, m_mapHeight = 0, m_mapBorder = 0;
	float m_extentHiX = 0.0f, m_extentHiY = 0.0f;
	double m_pendingSeconds = 0.0; ///< game time not yet given to a client frame
	double m_pendingMs = 0.0;      ///< milliseconds not yet given to the snow's integer sync time
	std::uint32_t m_frame = 0;     ///< the client frame (the first run is 1)
	std::uint32_t m_soundHandle = 1; ///< GlobalWeatherSystem + 0x14 (1 at construction)
	int m_soundWeather = 0;
	unsigned long long m_soundChanges = 0;
	float m_terrainDark[3] = { 0, 0, 0 };
	bool m_terrainDarkened = false;
	CloudEffectFrame m_lastCloud;
};

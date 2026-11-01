// OpenBFME. GPL-3.0.
// Lane WEATHER-1. See GameClient/WeatherClient.h.

#include "GameClient/WeatherClient.h"

#include <cmath>

namespace
{
constexpr double kClientFrameSeconds = 1.0 / 30.0; // 30 client frames per second (RW 0x63CF0F: 30 / 5 client frames per logic frame)
constexpr int kMaxClientFramesPerAdvance = 8;      // a long render frame does not replay seconds of weather at once (the rest is dropped)
}

bool WeatherClient::load(ArchiveFileSystem &fs, const std::string &mapDir, WeatherSettingStore::SoundValidator validSound, int mapWidth, int mapHeight,
	int mapBorder, float extentHiX, float extentHiY, std::string *error)
{
	m_loaded = false;
	m_store = WeatherSettingStore();
	m_store.setSoundValidator(std::move(validSound));
	if (!m_store.loadForMap(fs, mapDir, error))
	{
		return false;
	}
	m_mapWidth = mapWidth;
	m_mapHeight = mapHeight;
	m_mapBorder = mapBorder;
	m_extentHiX = extentHiX;
	m_extentHiY = extentHiY;
	m_snow = SnowManager();
	m_snow.init(m_store.finalSetting());
	m_cloud = CloudEffect();
	m_cloud.refreshSettings(m_store.cloudEffect()); // the CloudEffect block parse's slot 0x38 call (RW 0x615016)
	m_frame = 0;
	m_pendingSeconds = 0.0;
	m_pendingMs = 0.0;
	m_soundHandle = 1;
	m_soundWeather = 0;
	m_soundChanges = 0;
	m_terrainDark[0] = m_terrainDark[1] = m_terrainDark[2] = 0.0f;
	m_terrainDarkened = false;
	m_lastCloud = CloudEffectFrame();
	m_loaded = true;
	return true;
}

void WeatherClient::followSound(int w)
{
	if (w == m_soundWeather)
	{
		return;
	}
	// RW 0x719DEE
	m_soundWeather = w;
	++m_soundChanges;
	if (m_audio.stop)
	{
		m_audio.stop(m_soundHandle);
	}
	m_soundHandle = 1;
	const std::string &sound = m_store.weatherData(w).weatherSound;
	if (!sound.empty() && m_audio.start)
	{
		m_soundHandle = m_audio.start(sound);
	}
}

WeatherClientFrame WeatherClient::advance(double seconds, int globalWeather, W3DDrawRandom &clientRandom)
{
	WeatherClientFrame out;
	if (!m_loaded)
	{
		return out;
	}
	const WeatherSetting &s = m_store.finalSetting();
	if (seconds > 0.0)
	{
		// the snow's time: integer milliseconds as the WW3D sync time counts them, the fraction kept for the next render frame
		m_pendingMs += seconds * 1000.0;
		const double whole = std::floor(m_pendingMs);
		m_pendingMs -= whole;
		m_snow.advanceTime((std::uint32_t)whole);
		m_pendingSeconds += seconds;
	}
	int frames = 0;
	while (m_pendingSeconds >= kClientFrameSeconds && frames < kMaxClientFramesPerAdvance)
	{
		m_pendingSeconds -= kClientFrameSeconds;
		++frames;
		++m_frame;
		followSound(globalWeather);
		m_snow.clientFrame(s, globalWeather);
		const std::string &fxName = m_cloud.settings().lightningFX;
		const bool fxExists = !fxName.empty() && m_fxExists && m_fxExists(fxName);
		m_lastCloud = m_cloud.update(m_frame, globalWeather, m_store, 0.0f, 0.0f, m_extentHiX, m_extentHiY, clientRandom, fxExists);
		if (m_lastCloud.relightTerrain)
		{
			for (int i = 0; i < 3; ++i)
			{
				m_terrainDark[i] = m_lastCloud.active ? m_lastCloud.darkening[i] : 0.0f;
			}
			m_terrainDarkened = m_lastCloud.active;
		}
		if (m_lastCloud.playFX)
		{
			out.fx.push_back(m_lastCloud);
		}
	}
	if (frames == kMaxClientFramesPerAdvance)
	{
		m_pendingSeconds = 0.0;
	}
	out.clientFrames = frames;
	out.cloud = m_lastCloud;
	out.snow = m_snow.drawParams(s, m_mapWidth, m_mapHeight, m_mapBorder, clientRandom);
	for (int i = 0; i < 3; ++i)
	{
		out.terrainDarkening[i] = m_terrainDark[i];
	}
	out.terrainDarkened = m_terrainDarkened;
	return out;
}

void WeatherClient::shutdown()
{
	if (!m_loaded)
	{
		return;
	}
	// RW 0x719FDB: the sound goes
	if (m_audio.stop)
	{
		m_audio.stop(m_soundHandle);
	}
	m_soundHandle = 1;
	m_soundWeather = 0;
	// RW 0x493547 (SnowManager::reset) after RW 0x600AAD deleted the overrides
	m_store.deleteOverrides();
	m_snow.reset(m_store.finalSetting(), 0);
	m_loaded = false;
}

std::vector<std::string> WeatherClient::stopLines()
{
	std::vector<std::string> out = SnowManager::stopLines();
	for (const std::string &l : CloudEffect::stopLines())
	{
		out.push_back(l);
	}
	out.push_back("[S-4892] WeatherClient: the weather sound (RW 0x719DEE) follows the logic's weather on the first client frame that presents it "
				  "(retail calls the audio inside the logic frame); the client runs 30 frames per second, the snow's time is integrated per render "
				  "frame and its counters per client frame");
	out.push_back("[S-4893] the weather script actions are not ported (S-1180 counts them; no retail map script uses them): MAP_CHANGE_CLOUD_SPEED "
				  "(RW 0x7CAFA5 case 0x173: TheWritableGlobalData + 0xD40 = the real, read only by the cloud pass's wave RW 0x614AA7, which RotWK never "
				  "installs), DIM_WORLD_LIGHTS (case 0x1B5: TheDisplay vtable + 0xCC with four reals) and RESTORE_WORLD_LIGHTS (case 0x1B6: + 0xD0 "
				  "with one real); the display methods behind the two slots are not identified");
	return out;
}

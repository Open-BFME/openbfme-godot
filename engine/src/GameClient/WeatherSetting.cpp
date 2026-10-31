// OpenBFME. GPL-3.0.
// Lane WEATHER-1. See GameClient/WeatherSetting.h for the target facts.

#include "GameClient/WeatherSetting.h"

#include "Common/AsciiString.h"
#include "Common/INI.h"
#include "Common/INI/INIBlockStubs.h"
#include "Common/INIException.h"

#include <cstddef>
#include <stdexcept>

RGBColor defaultHardwareFogColor()
{
	return RGBColor{ 0x88 / 255.0f, 0x66 / 255.0f, 0x55 / 255.0f };
}

const char *const WeatherSettingStore::WeatherTypeNames[WEATHER_TYPE_COUNT] = { "NONE", "CLOUDY", "RAINY", "CLOUDYRAINY", "SUNNY" };

namespace
{
// the WeatherData record as it is parsed: the record and the audio validator its WeatherSound row needs (initFromINI hands this as the instance)
struct WeatherDataParse
{
	WeatherDataRecord record;
	const WeatherSettingStore::SoundValidator *valid = nullptr;
};

// RW 0x73B217 -> 0x73AA94 over the store's validator
void parseWeatherSound(INI *ini, void *instance, void *store, const void *)
{
	const WeatherSettingStore::SoundValidator &valid = *static_cast<const WeatherDataParse *>(instance)->valid;
	const std::string name = ini->getNextToken();
	if (AsciiStringUtil::compareNoCase(name, "NoSound") == 0)
	{
		static_cast<std::string *>(store)->clear();
		return;
	}
	if (!valid(name))
	{
		throw INIException(3, "Invalid Sound '%s'", name.c_str());
	}
	*static_cast<std::string *>(store) = name;
}

// RW 0xBF8D70 (token, parser, offset of the RW object; the offsets here are the C++ members')
#define WS(field) (int)offsetof(WeatherSetting, field)
const FieldParse kWeatherFields[] = {
	{ "SnowTexture", INI::parseAsciiString, nullptr, WS(snowTexture) },                // +0x18
	{ "SnowFrequencyScaleX", INI::parseReal, nullptr, WS(snowFrequencyScaleX) },       // +0x1C
	{ "SnowFrequencyScaleY", INI::parseReal, nullptr, WS(snowFrequencyScaleY) },       // +0x20
	{ "SnowAmplitude", INI::parseReal, nullptr, WS(snowAmplitude) },                   // +0x24
	{ "SnowPointSize", INI::parseReal, nullptr, WS(snowPointSize) },                   // +0x28
	{ "SnowMaxPointSize", INI::parseReal, nullptr, WS(snowMaxPointSize) },             // +0x2C
	{ "SnowMinPointSize", INI::parseReal, nullptr, WS(snowMinPointSize) },             // +0x30
	{ "SnowQuadSize", INI::parseReal, nullptr, WS(snowQuadSize) },                     // +0x34
	{ "SnowBoxHeight", INI::parseReal, nullptr, WS(snowBoxHeight) },                   // +0x38
	{ "SnowSpacing", INI::parseReal, nullptr, WS(snowSpacing) },                       // +0x3C
	{ "SnowSpeed", INI::parseReal, nullptr, WS(snowSpeed) },                           // +0x40
	{ "SnowPointSprites", INI::parseBool, nullptr, WS(snowPointSprites) },             // +0x44
	{ "SnowEnabled", INI::parseBool, nullptr, WS(snowEnabled) },                       // +0x45
	{ "IsSnowing", INI::parseBool, nullptr, WS(isSnowing) },                           // +0x46
	{ "NumberTiles", INI::parseInt, nullptr, WS(numberTiles) },                        // +0x48
	{ "SnowXYSpeed", INI::parseCoord2D, nullptr, WS(snowXYSpeed) },                    // +0x4C
	{ "LightningEnabled", INI::parseBool, nullptr, WS(lightningEnabled) },             // +0x54
	{ "LightningFactor", GameClientRandomVariable::parseRandomVariable, nullptr, WS(lightningFactor) }, // +0x58
	{ "LightningDuration", INI::parseInt, nullptr, WS(lightningDuration) },            // +0x64
	{ "LightningChance", INI::parseReal, nullptr, WS(lightningChance) },               // +0x68
	{ "SpellEnabled", INI::parseBool, nullptr, WS(spellEnabled) },                     // +0x6C
	{ "SpellDuration", INI::parseInt, nullptr, WS(spellDuration) },                    // +0x70
	{ "RampControl", INI::parseCoord2D, nullptr, WS(rampControl) },                    // +0x74
	{ "RampSpacing", INI::parseCoord2D, nullptr, WS(rampSpacing) },                    // +0x84
	{ "RampSpeed", INI::parseCoord2D, nullptr, WS(rampSpeed) },                        // +0x7C
	{ "HardwareFogColor", INI::parseRGBColor, nullptr, WS(hardwareFogColor) },         // +0x8C
	{ "HardwareFogEnable", INI::parseBool, nullptr, WS(hardwareFogEnable) },           // +0x98
	{ "HardwareFogStart", INI::parseReal, nullptr, WS(hardwareFogStart) },             // +0x9C
	{ "HardwareFogEnd", INI::parseReal, nullptr, WS(hardwareFogEnd) },                 // +0xA0
	{ "CloudTextureSize", INI::parseCoord2D, nullptr, WS(cloudTextureSize) },          // +0xA4
	{ "CloudOffsetPerSecond", INI::parseCoord2D, nullptr, WS(cloudOffsetPerSecond) },  // +0xAC
	{ nullptr, nullptr, nullptr, 0 },
};
#undef WS

// RW 0xC22D0C
const FieldParse kWeatherDataFields[] = {
	{ "WeatherSound", parseWeatherSound, nullptr, (int)(offsetof(WeatherDataParse, record) + offsetof(WeatherDataRecord, weatherSound)) },
	{ "HasLightning", INI::parseBool, nullptr, (int)(offsetof(WeatherDataParse, record) + offsetof(WeatherDataRecord, hasLightning)) },
	{ nullptr, nullptr, nullptr, 0 },
};

// RW 0xBFB6A8
#define CE(field) (int)offsetof(CloudEffectData, field)
const FieldParse kCloudEffectFields[] = {
	{ "CloudTexture", INI::parseAsciiString, nullptr, CE(cloudTexture) },
	{ "DarkCloudTexture", INI::parseAsciiString, nullptr, CE(darkCloudTexture) },
	{ "AlphaTexture", INI::parseAsciiString, nullptr, CE(alphaTexture) },
	{ "PropagateSpeed", INI::parseReal, nullptr, CE(propagateSpeed) },
	{ "Angle", INI::parseInt, nullptr, CE(angle) },
	{ "DissipateTexture", INI::parseAsciiString, nullptr, CE(dissipateTexture) },
	{ "DissipateStartLevel", INI::parseReal, nullptr, CE(dissipateStartLevel) },
	{ "DissipateSpeed", INI::parseReal, nullptr, CE(dissipateSpeed) },
	{ "DarkeningFactor", INI::parseRGBColor, nullptr, CE(darkeningFactor) },
	{ "DarkeningFactorRain", INI::parseRGBColor, nullptr, CE(darkeningFactorRain) },
	{ "DarkeningRate", INI::parseInt, nullptr, CE(darkeningRate) },
	{ "LighteningRate", INI::parseInt, nullptr, CE(lighteningRate) },
	{ "CloudScrollSpeed", INI::parseReal, nullptr, CE(cloudScrollSpeed) },
	{ "DissipateRateScale", INI::parseReal, nullptr, CE(dissipateRateScale) },
	{ "LightningShadows", INI::parseBool, nullptr, CE(lightningShadows) },
	{ "JitterLightningLightPosition", INI::parseBool, nullptr, CE(jitterLightningLightPosition) },
	{ "JitterLightningLightIntensity", INI::parseBool, nullptr, CE(jitterLightningLightIntensity) },
	{ "LightningChance", INI::parseReal, nullptr, CE(lightningChance) },
	{ "LightningShadowColor", INI::parseRGBColor, nullptr, CE(lightningShadowColor) },
	{ "LightningShadowIntensity", INI::parseReal, nullptr, CE(lightningShadowIntensity) },
	{ "LightningDuration", GameClientRandomVariable::parseRandomVariable, nullptr, CE(lightningDuration) },
	{ "LightningFrequency", INI::parseReal, nullptr, CE(lightningFrequency) },
	{ "LightningIntensity", GameClientRandomVariable::parseRandomVariable, nullptr, CE(lightningIntensity) },
	{ "LightningLightPosition1", INI::parseCoord2D, nullptr, CE(lightningLightPosition[0]) },
	{ "LightningLightPosition2", INI::parseCoord2D, nullptr, CE(lightningLightPosition[1]) },
	{ "LightningLightPosition3", INI::parseCoord2D, nullptr, CE(lightningLightPosition[2]) },
	{ "LightningFX", INI::parseAsciiString, nullptr, CE(lightningFX) },
	{ nullptr, nullptr, nullptr, 0 },
};
#undef CE
} // namespace

struct WeatherBlockParsers
{
	// RW 0x6011F3
	static void weather(WeatherSettingStore &s, INI *ini)
	{
		if (s.m_chain.empty())
		{
			s.m_chain.emplace_back(); // new WeatherSetting (RW 0x600F91)
		}
		else if (ini->getLoadType() == INI_LOAD_CREATE_OVERRIDES)
		{
			s.m_chain.push_back(s.m_chain.front()); // RW 0x600D8B(override, base): the override starts as a copy of the BASE
		}
		else
		{
			throw INIException(3, "WeatherSetting defined twice");
		}
		ini->initFromINI(&s.m_chain.back(), kWeatherFields);
		// RW 0x6012D8: TheSnowManager->updateIniSettings(); the client's SnowManager reads finalSetting() when it is (re)built (WeatherClient)
	}

	// RW 0x719D60
	static void weatherData(WeatherSettingStore &s, INI *ini)
	{
		if (ini->getLoadType() != INI_LOAD_OVERWRITE)
		{
			throw INIException(3, "Cannot define WeatherData in map.ini");
		}
		const std::string token = ini->getNextToken();
		int which = 0;
		for (; which < WEATHER_TYPE_COUNT; ++which)
		{
			if (AsciiStringUtil::compareNoCase(WeatherSettingStore::WeatherTypeNames[which], token) == 0)
			{
				break;
			}
		}
		if (which == WEATHER_TYPE_COUNT)
		{
			throw INIException(3, "Unknown weather type %s", token.c_str());
		}
		if (!s.m_validSound)
		{
			throw INIException(3, "WeatherData needs the audio event validator (WeatherSettingStore::setSoundValidator)"); // the port's wiring error, reported as the load's error
		}
		WeatherDataParse p;
		p.record = s.m_weatherData[which]; // RW 0x42DB80 fills the existing record in place
		p.valid = &s.m_validSound;
		ini->initFromINI(&p, kWeatherDataFields);
		s.m_weatherData[which] = p.record;
	}

	// RW 0x614FC9
	static void cloudEffect(WeatherSettingStore &s, INI *ini)
	{
		s.m_cloudCurrent = s.m_cloudDefaults; // RW 0x614B5F(current, defaults)
		ini->initFromINI(&s.m_cloudCurrent, kCloudEffectFields);
		const INILoadType t = ini->getLoadType();
		if (t != INI_LOAD_CREATE_OVERRIDES && t != INI_LOAD_CHILD_OBJECT)
		{
			s.m_cloudDefaults = s.m_cloudCurrent;
		}
		s.m_cloudEffectSeen = true;
		// RW 0x615008: TheCloudEffect's refresh (vtable slot 0x38, RW 0x614C71); the client's CloudEffect reads cloudEffect() when built
	}
};

namespace
{
// the recording stubs' lenient extent (INIBlockStubs.cpp): every line up to the next one that starts in column 0 with a registered block keyword
void skipBlock(INI *ini)
{
	for (;;)
	{
		const std::string *next = ini->peekNextLine();
		if (!next)
		{
			return;
		}
		const std::string &l = *next;
		if (!l.empty() && l[0] != ' ' && l[0] != '\t' && l[0] != '\v' && l[0] != '\f')
		{
			const size_t b = l.find_first_not_of(" \n\r\t=");
			if (b != std::string::npos)
			{
				size_t e = l.find_first_of(" \n\r\t=", b);
				if (e == std::string::npos)
				{
					e = l.size();
				}
				if (ini->environment().blocks.contains(l.substr(b, e - b)))
				{
					return;
				}
			}
		}
		ini->readLine();
	}
}

class Load
{
public:
	Load(ArchiveFileSystem *fs, WeatherSettingStore &store) : m_store(store)
	{
		m_env.fileSystem = fs;
		// the blocks of a map.ini that the shared stub table parses for real into the world's stores (RetailObjectWorld::applyMapIni parses them
		// there): here they are skipped like the other recording stubs (lenient extent), they are not the weather's
		static const char *const kWorldBlocks[] = { "Locomotor", "FXList", "FXParticleSystem", "Weapon", "Armor", "DamageFX" };
		std::vector<std::string> skip = { "Weather", "WeatherData", "CloudEffect" };
		skip.insert(skip.end(), std::begin(kWorldBlocks), std::end(kWorldBlocks));
		RegisterRecordingBlockStubs(m_env.blocks, m_recorder, skip, StubExtent::Lenient);
		for (const char *k : kWorldBlocks)
		{
			m_env.blocks.registerBlock(k, [](INI *ini) { skipBlock(ini); });
		}
		m_env.blocks.registerBlock("Weather", [this](INI *ini) { WeatherBlockParsers::weather(m_store, ini); });
		m_env.blocks.registerBlock("WeatherData", [this](INI *ini) { WeatherBlockParsers::weatherData(m_store, ini); });
		m_env.blocks.registerBlock("CloudEffect", [this](INI *ini) { WeatherBlockParsers::cloudEffect(m_store, ini); });
	}
	bool run(const std::string &name, const std::vector<std::uint8_t> *bytes, int loadType, std::string *error)
	{
		INI ini(m_env);
		try
		{
			if (bytes)
			{
				ini.loadMemory(name, *bytes, (INILoadType)loadType);
			}
			else
			{
				ini.load(name, (INILoadType)loadType);
			}
		}
		catch (const std::exception &e)
		{
			if (error)
			{
				*error = name + ": " + e.what();
			}
			return false;
		}
		return true;
	}

private:
	WeatherSettingStore &m_store;
	INIEnvironment m_env;
	INIBlockRecorder m_recorder;
};

std::string mapDirectoryOf(const std::string &mapDir)
{
	std::string d = mapDir;
	for (char &c : d)
	{
		if (c == '/')
		{
			c = '\\';
		}
	}
	if (AsciiStringUtil::compareNoCase(d.substr(0, 5), "maps\\") != 0)
	{
		d = "maps\\" + d;
	}
	return d;
}
} // namespace

bool WeatherSettingStore::loadFile(ArchiveFileSystem &fs, const std::string &path, int loadType, std::string *error)
{
	Load l(&fs, *this);
	if (!l.run(path, nullptr, loadType, error))
	{
		return false;
	}
	m_files.push_back(path);
	return true;
}

bool WeatherSettingStore::loadText(ArchiveFileSystem *fs, const std::string &name, const std::string &text, int loadType, std::string *error)
{
	Load l(fs, *this);
	const std::vector<std::uint8_t> bytes(text.begin(), text.end());
	return l.run(name, &bytes, loadType, error);
}

bool WeatherSettingStore::loadForMap(ArchiveFileSystem &fs, const std::string &mapDir, std::string *error)
{
	for (const char *f : { "Data\\INI\\Default\\Weather.ini", "Data\\INI\\Weather.ini", "Data\\INI\\Default\\Environment.ini", "Data\\INI\\Environment.ini" })
	{
		if (!fs.doesFileExist(f))
		{
			if (error)
			{
				*error = std::string(f) + " not found";
			}
			return false;
		}
		if (!loadFile(fs, f, INI_LOAD_OVERWRITE, error))
		{
			return false;
		}
	}
	if (!hasWeatherSetting())
	{
		if (error)
		{
			*error = "Data\\INI\\Weather.ini defines no Weather block";
		}
		return false;
	}
	const std::string dir = mapDirectoryOf(mapDir);
	for (const char *file : { "map.ini", "solo.ini" }) // RW 0x627224, 0x627290
	{
		const std::string path = dir + "\\" + file;
		if (fs.doesFileExist(path) && !loadFile(fs, path, INI_LOAD_CREATE_OVERRIDES, error))
		{
			return false;
		}
	}
	return true;
}

void WeatherSettingStore::deleteOverrides()
{
	if (m_chain.size() > 1)
	{
		m_chain.resize(1);
	}
}

const WeatherSetting &WeatherSettingStore::finalSetting() const
{
	if (m_chain.empty())
	{
		throw std::logic_error("WeatherSettingStore: no Weather block was loaded");
	}
	return m_chain.back();
}

const WeatherSetting &WeatherSettingStore::baseSetting() const
{
	if (m_chain.empty())
	{
		throw std::logic_error("WeatherSettingStore: no Weather block was loaded");
	}
	return m_chain.front();
}

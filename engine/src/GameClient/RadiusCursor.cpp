// OpenBFME. GPL-3.0.
// See RadiusCursor.h (lane SPELL-4). Client-only: the aiming marker reads nothing of the logic and never changes it.

#include "GameClient/RadiusCursor.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/AsciiString.h"
#include "Common/INI.h"
#include "Common/INIException.h"
#include "Common/INI/INIBlockStubs.h"
#include "GameClient/GlobalLanguage.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace
{
// RW 0xDA37F8: the shadow type bits of a RadiusDecalTemplate's Style (parseBitString32 RW 0x42E840), as GameLogic/ExperienceLevels.cpp reads them
const char *const kShadowTypeNames[] = { "SHADOW_DECAL", "SHADOW_VOLUME", "SHADOW_VOLUME_NEW", "SHADOW_DYNAMIC_PROJECTION", "SHADOW_DIRECTIONAL_PROJECTION",
	"SHADOW_ALPHA_DECAL", "SHADOW_ADDITIVE_DECAL", "SHADOW_VOLUME_NON_SELF_1", "SHADOW_VOLUME_NON_SELF_2", "SHADOW_VOLUME_NON_SELF_3",
	"SHADOW_ALPHA_DECAL_DYNAMIC", "SHADOW_ADDITIVE_DECAL_DYNAMIC", "SHADOW_MERGE_DECAL", "SHADOW_DECAL_TERRAIN_CLAIM", "SHADOW_VOLUME_OR_DECAL",
	"SHADOW_VOLUME_NEW_OR_DECAL", "SHADOW_SUBTRACT_DECAL_DYNAMIC", nullptr };

#define RD_OFF(f) (int)offsetof(RadiusDecalTemplate, f)
// RW 0xC24278 (13 rows; parseAsciiString RW 0x42EE5E, parseBitString32 RW 0x42E840, parsePercentToReal RW 0x42EEFA, parseReal RW 0x42ED00,
// parseColorInt RW 0x42F13E, parseBool RW 0x42E558, parseUnsignedInt RW 0x42ECB2)
const FieldParse kRadiusDecalFieldParse[] = {
	{ "Texture", INI::parseAsciiString, nullptr, RD_OFF(texture) },
	{ "Texture2", INI::parseAsciiString, nullptr, RD_OFF(texture2) },
	{ "Style", INI::parseBitString32, kShadowTypeNames, RD_OFF(style) },
	{ "OpacityMin", INI::parsePercentToReal, nullptr, RD_OFF(opacityMin) },
	{ "OpacityMax", INI::parsePercentToReal, nullptr, RD_OFF(opacityMax) },
	{ "OpacityThrobTime", INI::parseReal, nullptr, RD_OFF(opacityThrobTime) },
	{ "RotationsPerMinute", INI::parseReal, nullptr, RD_OFF(rotationsPerMinute) },
	{ "Color", INI::parseColorInt, nullptr, RD_OFF(color) },
	{ "OnlyVisibleToOwningPlayer", INI::parseBool, nullptr, RD_OFF(onlyVisibleToOwningPlayer) },
	{ "MaxRadius", INI::parseReal, nullptr, RD_OFF(maxRadius) },
	{ "MinRadius", INI::parseReal, nullptr, RD_OFF(minRadius) },
	{ "MaxSelectedUnits", INI::parseUnsignedInt, nullptr, RD_OFF(maxSelectedUnits) },
	{ "SpiralAcceleration", INI::parseReal, nullptr, RD_OFF(spiralAcceleration) },
	{ nullptr, nullptr, nullptr, 0 },
};
#undef RD_OFF

std::string lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return s;
}

std::string withoutComment(const std::string &line)
{
	size_t cut = line.find(';');
	const size_t slashes = line.find("//");
	if (slashes != std::string::npos && (cut == std::string::npos || slashes < cut))
	{
		cut = slashes;
	}
	return cut == std::string::npos ? line : line.substr(0, cut);
}

bool firstTokenIsEnd(const std::string &line)
{
	const std::string l = withoutComment(line);
	const size_t b = l.find_first_not_of(" \t");
	if (b == std::string::npos)
	{
		return false;
	}
	const size_t e = l.find_first_of(" \t=", b);
	return AsciiStringUtil::compareNoCase(l.substr(b, e == std::string::npos ? std::string::npos : e - b), "End") == 0;
}

// the InGameUI fields this file does not read: a `Field = value` line is consumed; a line without `=` opens a block that ends at its End
void skipField(INI *ini, void *, void *, const void *)
{
	if (withoutComment(ini->currentLineText()).find('=') != std::string::npos)
	{
		return;
	}
	for (;;)
	{
		const std::string *next = ini->peekNextLine();
		if (!next)
		{
			throw INIException(4, "Missing 'END' token.\n\nError parsing nested block in InGameUI, file '%s', line %i.\n", ini->getFilename().c_str(),
				ini->currentSourceLine());
		}
		const std::string line = *next;
		ini->readLine();
		if (firstTokenIsEnd(line))
		{
			return;
		}
	}
}

struct LoadState
{
	std::map<std::string, RadiusDecalTemplate> *out = nullptr;
	TerrainResourceUiSettings *resourceUi = nullptr; // lane OWNERFIX-2
	PlayerMessageSettings *messages = nullptr;
	bool inGameUi = false;
};

// lane OWNERFIX-2: the resource claim rows (RW 0xC12860 .. 0xC12890)
void parseResourceClaimDecal(INI *ini, void *instance, void *, const void *)
{
	INI::parseAsciiString(ini, nullptr, &static_cast<LoadState *>(instance)->resourceUi->claimDecal, nullptr); // RW 0x69C374 -> 0x42EE5E
}
void parseResourcePlaceDecal(INI *ini, void *instance, void *, const void *)
{
	INI::parseAsciiString(ini, nullptr, &static_cast<LoadState *>(instance)->resourceUi->placeDecal, nullptr); // RW 0x42EE5E
}
void parseResourcePlaceFont(INI *ini, void *instance, void *, const void *)
{
	FontDesc f;
	GlobalLanguage::parseFontDesc(ini, nullptr, &f, nullptr); // RW 0x778C93
	TerrainResourceUiSettings &ui = *static_cast<LoadState *>(instance)->resourceUi;
	ui.fontName = f.name;
	ui.fontSize = f.size;
	ui.fontBold = f.bold;
}
void parseResourcePlaceFontColor(INI *ini, void *instance, void *, const void *)
{
	INI::parseColorInt(ini, nullptr, &static_cast<LoadState *>(instance)->resourceUi->fontColor, nullptr); // RW 0x42F13E
}

// RW 0x6A4E27 (InGameUI::parseNamedRadiusDecalTemplate): the name, a default template (RW 0x732888) parsed with RW 0xC24278; a duplicate replaces the earlier
void parseNamedRadiusDecalTemplate(INI *ini, void *instance, void *, const void *)
{
	const std::string name = ini->getNextToken();
	RadiusDecalTemplate t;
	ini->initFromINI(&t, kRadiusDecalFieldParse);
	(*static_cast<LoadState *>(instance)->out)[lower(name)] = t;
}

// Reuse this InGameUI pass for the message fields; retain the native INI parsers.
template <void (*Parse)(INI *, void *, void *, const void *), size_t Offset>
void parseMessageField(INI *ini, void *instance, void *, const void *)
{
	auto *settings = static_cast<LoadState *>(instance)->messages;
	Parse(ini, settings, reinterpret_cast<char *>(settings) + Offset, nullptr);
}
#define MESSAGE_FIELD(name, parser, member) \
	{ name, parseMessageField<INI::parser, offsetof(PlayerMessageSettings, member)>, nullptr, 0 }
const FieldParse kInGameUiFields[] = {
	MESSAGE_FIELD("MessageColor1", parseColorInt, color1),
	MESSAGE_FIELD("MessageColor2", parseColorInt, color2),
	MESSAGE_FIELD("MessagePosition", parseICoord2D, position),
	MESSAGE_FIELD("MessageFont", parseAsciiString, font),
	MESSAGE_FIELD("MessagePointSize", parseInt, pointSize),
	MESSAGE_FIELD("MessageBold", parseBool, bold),
	MESSAGE_FIELD("MessageDelayMS", parseInt, delayMs),
	{ "RadiusCursorTemplate", parseNamedRadiusDecalTemplate, nullptr, 0 },
	{ "TerrainResourceClaimDecal", parseResourceClaimDecal, nullptr, 0 },
	{ "PlaceTerrainResourceClaimantDecal", parseResourcePlaceDecal, nullptr, 0 },
	{ "PlaceTerrainResourceClaimantFont", parseResourcePlaceFont, nullptr, 0 },
	{ "PlaceTerrainResourceClaimantFontColor", parseResourcePlaceFontColor, nullptr, 0 },
	{ nullptr, skipField, nullptr, 0 },
};
#undef MESSAGE_FIELD

bool run(ArchiveFileSystem *fs, const std::string &path, const std::string *memory, std::map<std::string, RadiusDecalTemplate> &out, TerrainResourceUiSettings &ui, PlayerMessageSettings &messages,
	std::string *error)
{
	INIEnvironment env;
	env.fileSystem = fs;
	INIBlockRecorder recorder;
	LoadState state;
	state.out = &out;
	ui = TerrainResourceUiSettings();
	state.resourceUi = &ui;
	messages = PlayerMessageSettings();
	state.messages = &messages;
	RegisterRecordingBlockStubs(env.blocks, recorder, { "InGameUI" }, StubExtent::LenientAfterEnd);
	env.blocks.registerBlock("InGameUI", [&state](INI *ini) {
		state.inGameUi = true;
		ini->initFromINI(&state, kInGameUiFields);
	});
	INI ini(env);
	try
	{
		if (memory)
		{
			ini.loadMemory(path, std::vector<std::uint8_t>(memory->begin(), memory->end()), INI_LOAD_OVERWRITE);
		}
		else
		{
			ini.load(path, INI_LOAD_OVERWRITE);
		}
	}
	catch (const std::exception &e)
	{
		if (error)
		{
			*error = path + ": " + e.what();
		}
		return false;
	}
	if (!state.inGameUi)
	{
		if (error)
		{
			*error = path + ": no InGameUI block";
		}
		return false;
	}
	return true;
}
} // namespace

bool RadiusCursorTemplates::load(ArchiveFileSystem &fs, std::string *error)
{
	const char *file = "data\\ini\\ingameui.ini";
	if (!fs.doesFileExist(file))
	{
		if (error)
		{
			*error = std::string(file) + " is not in the mount";
		}
		return false;
	}
	m_templates.clear();
	return run(&fs, file, nullptr, m_templates, m_resourceUi, m_messageUi, error);
}

bool RadiusCursorTemplates::scan(const std::string &inGameUiIni, std::string *error)
{
	m_templates.clear();
	return run(nullptr, "ingameui.ini", &inGameUiIni, m_templates, m_resourceUi, m_messageUi, error);
}

const RadiusDecalTemplate *RadiusCursorTemplates::find(const std::string &name) const
{
	auto it = m_templates.find(lower(name));
	return it == m_templates.end() ? nullptr : &it->second;
}

int RadiusCursor::throbOpacity(const RadiusDecalTemplate &t, unsigned clientFrame, bool drawIconUI)
{
	// RW 0x732926: n = ceil(OpacityThrobTime * 0.03) (BfmeClientFramesPerMillisecond), rounded to an integer (fistp), at least 1
	const double framesD = std::ceil((double)(t.opacityThrobTime * 0.03f));
	const unsigned frames = framesD > 1.0 ? (unsigned)std::lround(framesD) : 1u;
	const unsigned phase = clientFrame % frames;
	const float percent = 0.5f * (std::sin((float)phase * 6.28318548f / (float)frames) + 1.0f);
	if (!drawIconUI)
	{
		return 0;
	}
	// RotWK evaluates the expression on the x87 stack (extended precision) and truncates: double here
	return (int)((((double)t.opacityMax - (double)t.opacityMin) * (double)percent + (double)t.opacityMin) * 255.0);
}

std::uint32_t RadiusCursor::packedColor(std::uint32_t style, std::uint32_t color, int opacity)
{
	// RW 0x73232B (Shadow::setColor)
	const std::uint32_t rgb = color & 0xFFFFFFu;
	if (style & 0x1420u)
	{
		return rgb | ((std::uint32_t)opacity << 24);
	}
	if (style & 0x10840u)
	{
		const float f = (float)(std::uint32_t)opacity / 255.0f;
		return ((std::uint32_t)(int)((float)((rgb >> 16) & 0xFF) * f) << 16) | ((std::uint32_t)(int)((float)((rgb >> 8) & 0xFF) * f) << 8)
			| (std::uint32_t)(int)((float)(rgb & 0xFF) * f);
	}
	return 0; // the decal keeps its earlier diffuse (none was set): not a decal style
}

void RadiusCursor::clear()
{
	// RW 0x69B0E1 -> RadiusDecal::clear (RW 0x7327E5)
	m_type.clear();
	m_template = nullptr;
	m_decal = RadiusCursorDecal();
}

void RadiusCursor::set(const std::string &type, const RadiusDecalTemplate *tmpl, float radius, bool havePlayer, bool playerIsLocal, std::uint32_t playerColor,
	bool valid)
{
	// RW 0x6A077A
	if (!active() || AsciiStringUtil::compareNoCase(type, m_type) != 0)
	{
		clear();
		if (!tmpl || !havePlayer || !(radius > 0.0f))
		{
			return;
		}
		// RW 0x732DE7 (createRadiusDecal)
		if (tmpl->texture.empty())
		{
			return;
		}
		m_type = type;
		m_template = tmpl;
		m_decal.type = type;
		if (tmpl->onlyVisibleToOwningPlayer && !playerIsLocal)
		{
			return; // the RadiusDecal is not empty but has no decal
		}
		m_decal.visible = true;
		m_decal.texture = tmpl->texture;
		m_decal.texture2 = tmpl->texture2;
		m_decal.style = tmpl->style;
		m_decal.size = radius * 2.0f;
		m_decal.angle = 0.0f;
		m_decal.rgb = (tmpl->color ? tmpl->color : playerColor) & 0xFFFFFFu;
		m_decal.packed = packedColor(m_decal.style, m_decal.rgb, m_decal.opacity);
	}
	// the hint's colour (RW 0x6A08D5 .. : Shadow::setColor when the decal was made)
	if (m_decal.visible)
	{
		m_decal.valid = valid;
		m_decal.rgb = (valid ? kValidColor : kInvalidColor) & 0xFFFFFFu;
		m_decal.packed = packedColor(m_decal.style, m_decal.rgb, m_decal.opacity);
	}
}

void RadiusCursor::update(const Coord3D &position, unsigned clientFrame, bool drawIconUI)
{
	if (!m_template || !m_decal.visible)
	{
		return;
	}
	m_decal.position = position;
	// RW 0x732926: the throb, then Shadow::setOpacity (the packed colour follows)
	m_decal.opacity = throbOpacity(*m_template, clientFrame, drawIconUI);
	m_decal.packed = packedColor(m_decal.style, m_decal.rgb, m_decal.opacity);
	if (m_template->rotationsPerMinute != 0.0f)
	{
		m_decal.angle = (float)clientFrame * ((1.0f / 30.0f) * 0.0174532924f * m_template->rotationsPerMinute * 60.0f);
	}
}

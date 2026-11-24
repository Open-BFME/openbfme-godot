// OpenBFME. GPL-3.0.
// See GameClient/ControlBarScheme.h for details.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameClient/ControlBarScheme.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/AsciiString.h"
#include "Common/INIException.h"

#include <algorithm>
#include <cstddef>

thread_local ControlBarSchemeStore *TheControlBarSchemeStore = nullptr;

ControlBarScheme::ControlBarScheme() = default;

namespace
{

#define SCHEME_OFF(member) (int)offsetof(ControlBarScheme, member)

// RW 0x721AF4 / 0x721B45: nested ImagePart and AnimatingPart blocks
void parseSubBlock(INI *ini, void *, void *, const void *)
{
	int depth = 1;
	while (depth > 0 && !ini->isEOF())
	{
		ini->readLine();
		const char *token = ini->firstToken(ini->getSeps());
		if (!token)
		{
			continue;
		}
		if (AsciiStringUtil::compareNoCase(token, "ImagePart") == 0 ||
			AsciiStringUtil::compareNoCase(token, "AnimatingPart") == 0)
		{
			++depth;
		}
		else if (AsciiStringUtil::compareNoCase(token, "End") == 0)
		{
			--depth;
		}
	}
}

// Dummy ignore parser for unneeded visual image rows
void parseIgnore(INI *, void *, void *, const void *)
{
}

// RW 0xC23400 (68 rows in retail game.dat)
const FieldParse kControlBarSchemeFieldParse[] = {
	{ "ImagePart", parseSubBlock, nullptr, 0 },
	{ "AnimatingPart", parseSubBlock, nullptr, 0 },
	{ "ScreenCreationRes", INI::parseICoord2D, nullptr, SCHEME_OFF(m_screenCreationRes) },
	{ "Side", INI::parseAsciiString, nullptr, SCHEME_OFF(m_side) },
	{ "QueueButtonImage", parseIgnore, nullptr, 0 },
	{ "RightHUDImage", parseIgnore, nullptr, 0 },
	{ "BuildUpClockColor", INI::parseColorInt, nullptr, SCHEME_OFF(m_buildUpClockColor) },
	{ "ButtonBorderBuildColor", INI::parseColorInt, nullptr, SCHEME_OFF(m_buttonBorderBuildColor) },
	{ "CommandBarBorderColor", INI::parseColorInt, nullptr, SCHEME_OFF(m_commandBarBorderColor) },
	{ "ButtonBorderActionColor", INI::parseColorInt, nullptr, SCHEME_OFF(m_buttonBorderActionColor) },
	{ "ButtonBorderUpgradeColor", INI::parseColorInt, nullptr, SCHEME_OFF(m_buttonBorderUpgradeColor) },
	{ "ButtonBorderSystemColor", INI::parseColorInt, nullptr, SCHEME_OFF(m_buttonBorderSystemColor) },
	{ "ButtonBorderAlteredColor", INI::parseColorInt, nullptr, SCHEME_OFF(m_buttonBorderAlteredColor) },
	{ "OptionsButtonEnable", parseIgnore, nullptr, 0 },
	{ "OptionsButtonHightlited", parseIgnore, nullptr, 0 },
	{ "OptionsButtonPushed", parseIgnore, nullptr, 0 },
	{ "OptionsButtonDisabled", parseIgnore, nullptr, 0 },
	{ "IdleWorkerButtonEnable", parseIgnore, nullptr, 0 },
	{ "IdleWorkerButtonHightlited", parseIgnore, nullptr, 0 },
	{ "IdleWorkerButtonPushed", parseIgnore, nullptr, 0 },
	{ "IdleWorkerButtonDisabled", parseIgnore, nullptr, 0 },
	{ "BuddyButtonEnable", parseIgnore, nullptr, 0 },
	{ "BuddyButtonHightlited", parseIgnore, nullptr, 0 },
	{ "BuddyButtonPushed", parseIgnore, nullptr, 0 },
	{ "BuddyButtonDisabled", parseIgnore, nullptr, 0 },
	{ "BeaconButtonEnable", parseIgnore, nullptr, 0 },
	{ "BeaconButtonHightlited", parseIgnore, nullptr, 0 },
	{ "BeaconButtonPushed", parseIgnore, nullptr, 0 },
	{ "BeaconButtonDisabled", parseIgnore, nullptr, 0 },
	{ "GenBarButtonIn", parseIgnore, nullptr, 0 },
	{ "GenBarButtonOn", parseIgnore, nullptr, 0 },
	{ "ToggleButtonUpIn", parseIgnore, nullptr, 0 },
	{ "ToggleButtonUpOn", parseIgnore, nullptr, 0 },
	{ "ToggleButtonUpPushed", parseIgnore, nullptr, 0 },
	{ "ToggleButtonDownIn", parseIgnore, nullptr, 0 },
	{ "ToggleButtonDownOn", parseIgnore, nullptr, 0 },
	{ "ToggleButtonDownPushed", parseIgnore, nullptr, 0 },
	{ "GeneralButtonEnable", parseIgnore, nullptr, 0 },
	{ "GeneralButtonHightlited", parseIgnore, nullptr, 0 },
	{ "GeneralButtonPushed", parseIgnore, nullptr, 0 },
	{ "GeneralButtonDisabled", parseIgnore, nullptr, 0 },
	{ "UAttackButtonEnable", parseIgnore, nullptr, 0 },
	{ "UAttackButtonHightlited", parseIgnore, nullptr, 0 },
	{ "UAttackButtonPushed", parseIgnore, nullptr, 0 },
	{ "GenArrow", parseIgnore, nullptr, 0 },
	{ "MinMaxButtonEnable", parseIgnore, nullptr, 0 },
	{ "MinMaxButtonHightlited", parseIgnore, nullptr, 0 },
	{ "MinMaxButtonPushed", parseIgnore, nullptr, 0 },
	{ "MinMaxUL", parseIgnore, nullptr, 0 },
	{ "MinMaxLR", parseIgnore, nullptr, 0 },
	{ "GeneralUL", parseIgnore, nullptr, 0 },
	{ "GeneralLR", parseIgnore, nullptr, 0 },
	{ "UAttackUL", parseIgnore, nullptr, 0 },
	{ "UAttackLR", parseIgnore, nullptr, 0 },
	{ "OptionsUL", parseIgnore, nullptr, 0 },
	{ "OptionsLR", parseIgnore, nullptr, 0 },
	{ "WorkerUL", parseIgnore, nullptr, 0 },
	{ "WorkerLR", parseIgnore, nullptr, 0 },
	{ "ChatUL", parseIgnore, nullptr, 0 },
	{ "ChatLR", parseIgnore, nullptr, 0 },
	{ "BeaconUL", parseIgnore, nullptr, 0 },
	{ "BeaconLR", parseIgnore, nullptr, 0 },
	{ "PowerBarUL", parseIgnore, nullptr, 0 },
	{ "PowerBarLR", parseIgnore, nullptr, 0 },
	{ "MoneyUL", parseIgnore, nullptr, 0 },
	{ "MoneyLR", parseIgnore, nullptr, 0 },
	{ "CommandMarkerImage", parseIgnore, nullptr, 0 },
	{ "ExpBarForegroundImage", parseIgnore, nullptr, 0 },
	{ nullptr, nullptr, nullptr, 0 }
};

} // namespace

void ControlBarSchemeStore::parseSchemeDefinition(INI *ini)
{
	const char *name = ini->getNextToken();
	if (!name || !*name)
	{
		throw INIException(1, "ControlBarScheme without a name");
	}
	auto it = m_schemes.find(name);
	ControlBarScheme *scheme = nullptr;
	if (it != m_schemes.end())
	{
		scheme = it->second.get();
	}
	else
	{
		auto created = std::make_unique<ControlBarScheme>();
		created->m_name = name;
		scheme = created.get();
		m_schemes.emplace(name, std::move(created));
	}
	ini->initFromINI(scheme, kControlBarSchemeFieldParse);
}

void ControlBarSchemeStore::parseSchemeDefinitionGlobal(INI *ini)
{
	if (!TheControlBarSchemeStore)
	{
		throw INIPlainIntError("TheControlBar==NULL");
	}
	TheControlBarSchemeStore->parseSchemeDefinition(ini);
}

const ControlBarScheme *ControlBarSchemeStore::findScheme(const std::string &name) const
{
	for (const auto &kv : m_schemes)
	{
		if (AsciiStringUtil::compareNoCase(kv.first, name) == 0)
		{
			return kv.second.get();
		}
	}
	return nullptr;
}

const ControlBarScheme *ControlBarSchemeStore::findSchemeBySide(const std::string &side) const
{
	for (const auto &kv : m_schemes)
	{
		if (AsciiStringUtil::compareNoCase(kv.second->m_side, side) == 0)
		{
			return kv.second.get();
		}
	}
	return findScheme("Default");
}

void ControlBarSchemeStore::registerBlock(INIBlockRegistry &registry)
{
	registry.registerBlock("ControlBarScheme", [this](INI *ini) {
		parseSchemeDefinition(ini);
	});
}

// RW 0x721BA1
void ControlBarSchemeStore::loadFromFiles(INIEnvironment &env, std::vector<std::string> *errors)
{
	static const char *const files[] = {
		"Data\\INI\\Default\\ControlBarScheme.ini",
		"Data\\INI\\ControlBarScheme.ini"
	};
	for (const char *file : files)
	{
		if (!env.fileSystem || !env.fileSystem->doesFileExist(file))
		{
			continue;
		}
		INI ini(env);
		try
		{
			ini.load(file, INI_LOAD_OVERWRITE);
		}
		catch (const std::exception &e)
		{
			if (errors)
			{
				errors->push_back(std::string(file) + ": " + e.what());
			}
		}
	}
}

// OpenBFME. GPL-3.0.
//
// SubsystemLegend, loadIniFilesFromLegend and the RotWK subsystem INI load order.
// Citations are in SubsystemLegend.h and on each function.

#include "Common/SubsystemLegend.h"

#include "Common/AsciiString.h"

#include <algorithm>

const char *const kSubsystemLegendFile = "Data\\INI\\Default\\SubsystemLegendExpansion1.ini";

namespace
{
// B1 SubsystemLegend.cpp: TheSubsystemLegendLoaderNames, retail 0x012D7758. Only "INI".
const LookupListRec kLoaderNames[] = { { "INI", 0 }, { nullptr, 0 } };

SubsystemLegendEntry *asEntry(void *instance)
{
	return static_cast<SubsystemLegendEntry *>(instance);
}

void parseInitFile(INI *ini, void *instance, void *, const void *)
{
	INI::parseAsciiStringVectorAppend(ini, instance, &asEntry(instance)->initFile, nullptr);
}
void parseInitPath(INI *ini, void *instance, void *, const void *)
{
	INI::parseAsciiStringVectorAppend(ini, instance, &asEntry(instance)->initPath, nullptr);
}
void parseExtension(INI *ini, void *instance, void *, const void *)
{
	INI::parseAsciiStringVectorAppend(ini, instance, &asEntry(instance)->extension, nullptr);
}
void parseExcludePath(INI *ini, void *instance, void *, const void *)
{
	INI::parseAsciiStringVectorAppend(ini, instance, &asEntry(instance)->excludePath, nullptr);
}
void parseIncludePathCinematics(INI *ini, void *instance, void *, const void *)
{
	INI::parseAsciiStringVectorAppend(ini, instance, &asEntry(instance)->includePathCinematics, nullptr);
}
void parseLoader(INI *ini, void *instance, void *, const void *)
{
	INI::parseLookupList(ini, instance, &asEntry(instance)->loader, kLoaderNames);
}
void parseInitFileDebug(INI *ini, void *instance, void *, const void *)
{
	INI::parseAsciiString(ini, instance, &asEntry(instance)->initFileDebug, nullptr);
}

// RW LoadSubsystem field table at 0xBED7D0 (spec 3.4 item 1); B1 has the subset
// InitFile/InitPath/Extension/Loader/InitFileDebug (SubsystemLegend.cpp:44-57).
const FieldParse kLegendFieldParse[] = {
	{ "InitFile", parseInitFile, nullptr, 0 },
	{ "InitPath", parseInitPath, nullptr, 0 },
	{ "Extension", parseExtension, nullptr, 0 },
	{ "ExcludePath", parseExcludePath, nullptr, 0 },
	{ "IncludePathCinematics", parseIncludePathCinematics, nullptr, 0 },
	{ "Loader", parseLoader, nullptr, 0 },
	{ "InitFileDebug", parseInitFileDebug, nullptr, 0 },
	{ nullptr, nullptr, nullptr, 0 },
};

std::string normalizedSlashes(std::string s)
{
	std::replace(s.begin(), s.end(), '/', '\\');
	return s;
}

bool startsWithNoCase(const std::string &s, const std::string &prefix)
{
	return s.size() >= prefix.size() && AsciiStringUtil::compareNoCase(s.substr(0, prefix.size()), prefix) == 0;
}

bool isUnderCinematicsPath(const SubsystemLegendEntry &entry, const std::string &file)
{
	const std::string f = normalizedSlashes(file);
	for (const std::string &p : entry.includePathCinematics)
	{
		if (startsWithNoCase(f, normalizedSlashes(p)))
		{
			return true;
		}
	}
	return false;
}

void loadFileChecked(INI &ini, const std::string &file, const SubsystemLoadOptions &options, SubsystemLoadReport *report)
{
	if (!options.collectErrors)
	{
		ini.load(file, INI_LOAD_OVERWRITE);
		return;
	}
	try
	{
		ini.load(file, INI_LOAD_OVERWRITE);
	}
	catch (const INIException &e)
	{
		if (report)
		{
			SubsystemLoadReport::FileError err;
			err.file = file;
			err.code = e.code();
			err.message = e.message();
			report->errors.push_back(err);
		}
	}
}
}

// B1 SubsystemLegend.cpp parseLoadSubsystem / parseSubsystemLegendDefinition.
void SubsystemLegend::registerBlock(INIBlockRegistry &registry)
{
	registry.registerBlock("LoadSubsystem", [this](INI *ini) { parseLoadSubsystem(ini); });
}

void SubsystemLegend::parseLoadSubsystem(INI *ini)
{
	SubsystemLegendEntry entry;
	entry.name = ini->getNextToken(nullptr);
	ini->initFromINI(&entry, kLegendFieldParse);
	m_entries.push_back(entry);
}

const SubsystemLegendEntry *SubsystemLegend::findEntry(const std::string &name) const
{
	for (const SubsystemLegendEntry &e : m_entries)
	{
		if (e.name == name)
		{
			return &e;
		}
	}
	return nullptr;
}

// B1 Libraries/Source/subsystem/SubsystemInterface.cpp:40-74.
bool SubsystemLegend::loadIniFilesFromLegend(const std::string &subsystemName, INI &ini, const SubsystemLoadOptions &options, SubsystemLoadReport *report) const
{
	const SubsystemLegendEntry *entry = findEntry(subsystemName);
	if (!entry)
	{
		return false;
	}

	bool loadedAny = false;
	for (const std::string &f : entry->initFile)
	{
		loadedAny = true;
		if (!options.cinematics && isUnderCinematicsPath(*entry, f))
		{
			if (report)
			{
				report->skippedCinematics.push_back(entry->name + ": " + f);
			}
			continue;
		}
		loadFileChecked(ini, f, options, report);
	}

	for (const std::string &d : entry->initPath)
	{
		loadedAny = true;
		INILoadDirectoryOptions dirOptions;
		dirOptions.excludePaths = entry->excludePath;
		if (!options.cinematics)
		{
			for (const std::string &p : entry->includePathCinematics)
			{
				dirOptions.excludePaths.push_back(p);
				if (report)
				{
					report->skippedCinematics.push_back(entry->name + ": directory " + p);
				}
			}
		}
		for (std::string &p : dirOptions.excludePaths)
		{
			p = normalizedSlashes(p);
		}
		if (options.collectErrors)
		{
			dirOptions.onFileError = [report](const std::string &file, const INIException &e) {
				if (report)
				{
					SubsystemLoadReport::FileError err;
					err.file = file;
					err.code = e.code();
					err.message = e.message();
					report->errors.push_back(err);
				}
			};
		}
		ini.loadDirectory(d, true, INI_LOAD_OVERWRITE, dirOptions);
	}
	return loadedAny;
}

void LoadSubsystemFile(INI &ini, const std::string &file, const SubsystemLoadOptions &options, SubsystemLoadReport *report)
{
	loadFileChecked(ini, file, options, report);
}


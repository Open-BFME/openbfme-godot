// OpenBFME. GPL-3.0.
//
// A name-level scan of the INI files: which Object / ChildObject / ObjectReskin names (and which
// roads.ini Road / Bridge names) exist. It exists so the map lane can report, for every object
// template name a map places, whether the retail INI defines it, without the INI object model
// (another lane). It is NOT an INI parser: it matches the three definition keywords at the start of
// a line and takes the next token, exactly the scan the spec author used to establish that every
// retail map object name resolves (spec maps-and-terrain.md 2.7, [CORPUS objres.py]).
//
// Source of the file set: every *.ini / *.inc under data\ini\ in the mounted (winning) archives,
// plus every maps\<map>\map.ini, whose definitions are scoped to that map (spec 2.15: loaded with
// INI_LOAD_CREATE_OVERRIDES before the map's objects are created).

#pragma once

#include "Common/ArchiveFileSystem.h"
#include "Common/MapObject.h"

#include <map>
#include <set>
#include <string>
#include <vector>

struct ObjectNameIndex
{
	std::set<std::string> objectNames;      // lower-case Object / ChildObject / ObjectReskin names
	std::set<std::string> roadNames;        // lower-case Road / Bridge names from roads.ini
	std::map<std::string, std::set<std::string>> mapIniObjectNames; // "maps\\<dir>\\map.ini" -> names
	size_t iniFilesScanned = 0;
	size_t mapIniFilesScanned = 0;

	enum Resolution
	{
		Unresolved,
		SpecialName,  // "*Waypoints/Waypoint", "*GenericAIObjects/GenericAIObject": handled by TerrainLogic, no template
		GlobalObject, // defined by the game INI
		RoadName,     // a roads.ini road or bridge
		MapIniObject  // defined only by this map's map.ini
	};

	// The "*" template names a retail map places that have no INI template (the world builder's own
	// pseudo-templates). EVIDENCED: exactly the "*" names the 181 retail maps use (asserted by the corpus
	// test). Any other "*Name" is Unresolved, not exempt: `*TypoMissing` must be reported.
	static const std::vector<std::string> &specialNames();

	// mapDirKey: lower-case "maps\\<dir>\\map.ini" of the map being checked ("" = none).
	Resolution resolve(const std::string &templateName, const std::string &mapIniKey) const;
	// Like resolve(), but an object ZH classifies by its PROPERTIES rather than by a template (a
	// waypoint carries waypointID, a scorch decal carries scorchType, a light carries
	// lightHeightAboveTerrain: ZH WorldHeightMap.cpp:1289-1296) is SpecialName too. The retail
	// "Scorch" objects are such decals and have no INI template.
	Resolution resolveObject(const MapObject &object, const std::string &mapIniKey) const;
};

namespace ObjectNameScan
{
// Scans the INI text for definition lines. Exposed for tests.
void scanText(const std::string &text, std::set<std::string> &objectNames);
void scanRoadText(const std::string &text, std::set<std::string> &roadNames);

bool build(ArchiveFileSystem &fs, ObjectNameIndex &out, std::string *error);
} // namespace ObjectNameScan

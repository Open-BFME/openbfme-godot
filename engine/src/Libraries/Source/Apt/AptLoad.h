// OpenBFME. GPL-3.0.
//
// Apt movie loading: reads movie files through a file source (the mounted retail archives in the
// game, an in-memory map in unit tests), parses them, and resolves imports across movies.
//
// A movie X is the files X.apt, X.const, X.dat and X_geometry/<id>.ru (spec menus-apt.md 2.1); retail
// ships one BIG per movie, apt/<x>.big, with those files at the archive root.  BFME1 loads
// "<dir><file>.big" with dir "Apt\\" (BFME1 decomp GameEngine/Source/GameClient/GUI/
// WindowManager_loadAptWindow.cpp:155-213); once mounted the paths are flat, which is what this
// loader asks the file source for.
//
// The `.swf` suffix handling mirrors BFME1 game/Libraries/Source/EA/Apt/DispatchLiteral008C5840.cpp:
// a url ending in ".swf" (any case) names a movie, minus the suffix.

#pragma once

#include "Libraries/Source/Apt/AptFile.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

class ArchiveFileSystem;

class AptFileSource
{
public:
	virtual ~AptFileSource() = default;
	virtual bool readFile(const std::string &virtualPath, std::vector<std::uint8_t> &out, std::string *error) = 0;
	virtual bool fileExists(const std::string &virtualPath) = 0;
	// Names of every movie available ("MainMenu" etc. as stored, no extension).
	virtual std::vector<std::string> listMovies() = 0;
};

// File source over the retail archive mount (ArchiveFileSystem, RetailArchivePolicy order).
class AptArchiveFileSource : public AptFileSource
{
public:
	explicit AptArchiveFileSource(ArchiveFileSystem &fileSystem) : m_fs(fileSystem) {}
	bool readFile(const std::string &virtualPath, std::vector<std::uint8_t> &out, std::string *error) override;
	bool fileExists(const std::string &virtualPath) override;
	std::vector<std::string> listMovies() override;

private:
	ArchiveFileSystem &m_fs;
};

struct AptResolvedImport
{
	std::shared_ptr<const AptFile> movie;  // the exporting movie
	std::uint32_t characterId = 0;         // character id inside `movie`
	std::uint32_t localSlot = 0;           // the importer's character slot it fills
};

class AptLoader
{
public:
	explicit AptLoader(AptFileSource &source) : m_source(source) {}

	// Load and cache a movie by name (ASCII case-insensitive).  Null + *error on failure.
	std::shared_ptr<const AptFile> loadMovie(const std::string &movieName, std::string *error);

	// Resolve one import of `importer` to the exporting movie's character.  Fails when the target
	// movie or export is missing, or when the export name is ambiguous (several different ids).
	bool resolveImport(const AptFile &importer, const AptImport &import, AptResolvedImport &out, std::string *error);

	// Resolve every import of a movie, in file order.  Stops at the first failure.
	bool resolveImports(const AptFile &importer, std::vector<AptResolvedImport> &out, std::string *error);

	bool loadImageMap(const std::string &movieName, AptImageMap &out, std::string *error);
	bool loadGeometry(const std::string &movieName, std::uint32_t geometryId, AptGeometry &out, std::string *error);

	// "Y.swf" -> "Y" (returns true); anything else returns false.
	static bool movieNameFromSwf(const std::string &url, std::string &movieName);

private:
	AptFileSource &m_source;
	std::map<std::string, std::shared_ptr<const AptFile>> m_movies; // key: lower-cased name
};

// OpenBFME unit tests. GPL-3.0.
// Shared fixtures for the INI tests: an in-memory archive file system plus an INIEnvironment.

#pragma once

#include "BigTestUtil.h"

#include "Common/ArchiveFile.h"
#include "Common/INI.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace initest
{

typedef std::vector<std::pair<std::string, std::string>> FileList;

inline std::vector<std::uint8_t> toBytes(const std::string &s)
{
	return std::vector<std::uint8_t>(s.begin(), s.end());
}

// An INI environment backed by BIG archives built from synthetic files.
struct Fixture
{
	Win32BIGFileSystem fsys;
	INIEnvironment env;
	int archives = 0;

	Fixture() { env.fileSystem = &fsys; }

	explicit Fixture(const FileList &files)
	{
		env.fileSystem = &fsys;
		mount(files);
	}

	// Later mounts have lower precedence (first mounted wins).
	void mount(const FileList &files)
	{
		const std::string name = "fx" + std::to_string(archives++) + ".big";
		std::string error;
		auto archive = fsys.openArchiveFromMemory(name, std::make_shared<const std::vector<std::uint8_t>>(bigtest::makeBig("BIGF", files)), &error);
		if (!archive)
		{
			throw std::runtime_error("test archive: " + error);
		}
		fsys.mountArchive(std::move(archive), name, false);
	}
};

// Loads `text` as file `name` through a fresh INI and returns the thrown exception message, or
// an empty string when the load succeeded.
inline std::string loadError(INIEnvironment &env, const std::string &name, const std::string &text, INILoadType type = INI_LOAD_OVERWRITE, int *codeOut = nullptr)
{
	INI ini(env);
	try
	{
		ini.loadMemory(name, toBytes(text), type);
	}
	catch (const INIException &e)
	{
		if (codeOut)
		{
			*codeOut = e.code();
		}
		return e.message();
	}
	return std::string();
}

} // namespace initest

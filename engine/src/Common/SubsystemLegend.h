// OpenBFME. GPL-3.0.
//
// SubsystemLegend: the manifest that says which INI files and directories each engine
// subsystem loads, plus the load driver and the RotWK subsystem init order.
//
// Sources:
//   B1 Source/Common/System/SubsystemLegend.cpp:44-57 (entry layout, field table, findEntry,
//      parseLoadSubsystem)
//   B1 Libraries/Source/subsystem/SubsystemInterface.cpp:40-102 (loadIniFilesFromLegend,
//      initSubsystem: every InitFile in order, then every InitPath via
//      loadDirectory(dir, true, INI_LOAD_OVERWRITE))
//   B1 Source/Common/INI/ini_subsystem.cpp:27-48 (INI::loadSubsystemFiles swallows errors)
//   spec ini-and-object-model.md section 3.4: RW LoadSubsystem field table at 0xBED7D0:
//      InitFile, InitPath, Extension, ExcludePath, IncludePathCinematics (string vectors,
//      appended), Loader (lookup list), InitFileDebug (string).
//   RW GameEngine::init subsystem-name order (0x63AF2D onward), see SubsystemInitOrder.cpp.
//
// IncludePathCinematics: the legend header comment says "these items are only loaded if the
// -cinematics command argument is set"; RW 0x5B4C25-0x5B4C42 adds the cinematic paths to the
// exclusion list when cinematics are off (Sol review of INI-1). Files under such a path are
// skipped unless SubsystemLoadOptions::cinematics is set. ExcludePath is a plain prefix match
// (INILoadDirectoryOptions).

#pragma once

#include "Common/INI.h"

#include <string>
#include <vector>

struct SubsystemLegendEntry
{
	std::string name;
	std::vector<std::string> initFile;
	std::vector<std::string> initPath;
	std::vector<std::string> extension;
	std::vector<std::string> excludePath;
	std::vector<std::string> includePathCinematics;
	int loader = 0; ///< lookup list {"INI", 0}; "INI" is the only loader name in retail
	std::string initFileDebug;
};

struct SubsystemLoadOptions
{
	// The -cinematics command-line switch. Off by default, like a normal launch.
	bool cinematics = false;
	// false: an INI error propagates (retail SubsystemInterface::loadIniFilesFromLegend does not
	// catch). true: errors are recorded in the report and loading continues with the next file
	// (the test harness).
	bool collectErrors = false;
};

struct SubsystemLoadReport
{
	struct FileError
	{
		std::string file;
		int code = 0;
		std::string message;
	};

	std::vector<std::string> subsystemsLoaded;   ///< in the order they ran, "<name>" or "<name> [phase]"
	std::vector<std::string> noLegendEntry;      ///< order-table names with no legend entry (retail uses hard-coded paths we do not have)
	std::vector<std::string> unverifiedOrder;   ///< legend entries GameEngine::init never names (acceptance stop), loaded last in legend order
	std::vector<std::string> neverLoaded;        ///< legend entries RotWK code never requests (spec 3.5)
	std::vector<std::string> skippedCinematics;  ///< InitFile entries skipped by IncludePathCinematics
	std::vector<FileError> errors;
};

class SubsystemLegend
{
public:
	// B1 SubsystemLegend.cpp init/reset: empties the entry list.
	void reset() { m_entries.clear(); }

	// B1 SubsystemLegend.cpp parseSubsystemLegendDefinition: registers the "LoadSubsystem"
	// block keyword against this legend.
	void registerBlock(INIBlockRegistry &registry);

	// B1 SubsystemLegend.cpp parseLoadSubsystem: LoadSubsystem <name> ... End.
	void parseLoadSubsystem(INI *ini);

	// B1 SubsystemLegend.cpp findEntry: AsciiString compare, i.e. case-sensitive.
	const SubsystemLegendEntry *findEntry(const std::string &name) const;
	const std::vector<SubsystemLegendEntry> &entries() const { return m_entries; }

	// B1 SubsystemInterface.cpp loadIniFilesFromLegend. Returns true when the legend named any
	// file or path (the hard-coded paths are then skipped by the caller).
	bool loadIniFilesFromLegend(const std::string &subsystemName, INI &ini, const SubsystemLoadOptions &options, SubsystemLoadReport *report) const;

private:
	std::vector<SubsystemLegendEntry> m_entries;
};

// One step of GameEngine::init: the subsystem name and any file loaded right after it that is
// not in the legend.
struct SubsystemInitStep
{
	const char *name;
	std::vector<const char *> extraFiles;
};

// The RotWK subsystem order, read from RW GameEngine::init (0x63AF2D-0x63C7D5), a straight-line
// function whose name pushes precede their initSubsystem calls. See SubsystemInitOrder.cpp for
// the hard-coded loads and the one acceptance stop (six legend entries initialised elsewhere).
const std::vector<SubsystemInitStep> &RotwkSubsystemInitOrder();

// Legend entries RotWK never requests (spec 3.5): Credits, InGameUI, Animation2D and
// TheParticleSystemManager. Their keywords are not in the block table; loading them would throw
// "Unknown block", and the spec says not to fix that by adding blocks.
const std::vector<std::string> &RotwkNeverLoadedSubsystems();

// Loads one file with the load driver's error policy (propagate, or record in the report when
// options.collectErrors is set). Used for the hard-coded loads that are not in the legend.
void LoadSubsystemFile(INI &ini, const std::string &file, const SubsystemLoadOptions &options, SubsystemLoadReport *report);

// Where the legend itself lives (spec 3.4 item 1).
extern const char *const kSubsystemLegendFile;

// Runs the whole subsystem INI load, as the engine does at startup:
//   1. load kSubsystemLegendFile (needs a "LoadSubsystem" handler registered in env.blocks)
//   2. for each step of RotwkSubsystemInitOrder(): loadIniFilesFromLegend(name) then the step's
//      extra files
//   3. every legend entry GameEngine::init never names, in legend order (reported in
//      unverifiedOrder: ACCEPTANCE STOP, their real position was not recovered)
// `legend` must be the one whose registerBlock() was called on ini's registry.
void RunSubsystemIniLoad(SubsystemLegend &legend, INI &ini, const SubsystemLoadOptions &options, SubsystemLoadReport &report);

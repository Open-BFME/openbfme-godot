// OpenBFME unit tests. GPL-3.0.
// Retail scan of every W3D draw module declaration. The Object parser is not merged yet (lane OBJ-1), so a small scanner stands in
// for it: the Object / ChildObject / ObjectReskin block handler walks the INI cursor to the end of its file and parses every
// "Draw = <class> <tag>" line it meets with the draw module parser. The INI pipeline (subsystem legend order, #include, #define
// macros) is the real one; nothing else about an Object is interpreted.

#pragma once

#include "Common/INI.h"
#include "Common/INI/INIBlockStubs.h"
#include "Common/SubsystemLegend.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDraw.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace drawtest
{

struct RetailDrawModule
{
	std::string object;       ///< the most recent Object / ChildObject / ObjectReskin header name
	std::string file;
	int line = 0;             ///< line of the "Draw =" header
	std::string className;
	std::string tag;
	std::unique_ptr<W3DModelDrawModuleData> data; ///< Scripted / Horde; null when the parse failed or the class is W3DDefaultDraw
	std::unique_ptr<W3DDefaultDrawModuleData> defaultData; ///< W3DDefaultDraw only
	std::string error;
};

struct RetailDrawScan
{
	bool available = false;
	std::string mountError;
	std::vector<RetailDrawModule> modules;
	std::map<std::string, int> declarationsByClass;
	std::vector<std::string> otherDrawClasses; ///< "Draw = X" lines whose class is not one of the three (counted per class name)
	std::map<std::string, int> otherDrawCounts;
	SubsystemLoadReport report;
	size_t macroCount = 0;
};

// Runs the subsystem INI load once per process and returns the scan.
RetailDrawScan &retailDrawScan();

} // namespace drawtest

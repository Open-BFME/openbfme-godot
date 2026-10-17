// OpenBFME enhanced build-palette policy, not a retail availability check. GPL-3.0.
#pragma once
#include <map>
#include <string>
#include <vector>
class ThingFactory;
class CommandStore;
class ModuleFactory;
namespace BuildModeFilter
{
struct Entry
{
 bool building=false,unit=false,plot=false,marker=false,placeholder=false,scenery=false;
 bool builderBuildable=false,plotBuildable=false,enhancedPlotBuildable=false;
 bool unfinishedContent=false,unsupported=false,recommended=false;
 std::vector<std::string> builders,plots,enhancedPlots,reasons,warnings;
};
// Resolve only commands in a producer's actual or upgrade-switched sets; orphan
// CommandButtons do not prove that content is normally available.
std::map<std::string,Entry> classify(const ThingFactory &,const CommandStore &,ModuleFactory &);
}

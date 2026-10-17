// OpenBFME enhanced content. GPL-3.0. Generated from mounted faction data, not a retail rule.
#pragma once
#include <string>
#include <vector>
class ThingFactory;
class CommandStore;
class PlayerTemplateStore;
namespace FactionBuildPlots
{
struct Definition { std::string name, faction, category, foundation; std::vector<std::string> buildings; };
struct Generated { std::string ini; std::vector<Definition> plots; };
Generated generate(const ThingFactory &, const CommandStore &, const PlayerTemplateStore &);
void copyButtons(const Generated &, CommandStore &);
bool isPlot(const std::string &templateName);
}

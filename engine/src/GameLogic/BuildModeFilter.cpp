// OpenBFME enhanced build-palette policy. GPL-3.0.
#include "GameLogic/BuildModeFilter.h"
#include "GameLogic/FactionBuildPlots.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/KindOfTokens.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/Module/UpgradeModuleClasses.h"
#include <array>
#include <set>
#include <stdexcept>

namespace
{
struct Flags
{
 std::array<std::uint32_t,7> mask{};
 explicit Flags(const ThingTemplate &tt)
 {
  std::string error;KindOfTokens::parseTemplate(tt,mask,error);
  if(!error.empty())throw std::runtime_error(error);
 }
 bool has(const char *name)const
 {
  const int bit=KindOfTokens::indexOf(name);
  return bit>=0 && (mask[bit/32] & (1u<<(bit%32)));
 }
 bool any(std::initializer_list<const char *> names)const
 {
  for(const auto name:names)if(has(name))return true;
  return false;
 }
};
void unique(std::vector<std::string> &values,const std::string &value)
{
 for(const auto &existing:values)if(existing==value)return;
 values.push_back(value);
}
bool module(const ThingTemplate &tt,const char *name)
{
 for(const auto &m:tt.behaviorModules().nuggets())if(m.name==name)return true;
 return false;
}
std::set<std::string> sets(const ThingTemplate &tt)
{
 std::set<std::string> result;
 if(const auto v=tt.findField("CommandSet"))if(const auto text=std::get_if<std::string>(v))result.insert(*text);
 for(const auto &m:tt.behaviorModules().nuggets())
  if(const auto upgrade=dynamic_cast<const CommandSetUpgradeModuleData *>(m.data.get()))result.insert(upgrade->m_commandSet);
 return result;
}
}

std::map<std::string,BuildModeFilter::Entry> BuildModeFilter::classify(const ThingFactory &things,const CommandStore &commands,ModuleFactory &modules)
{
 std::map<std::string,Entry> result;
 for(const auto master:things.templates())
 {
  const auto tt=master->getFinalOverride();const Flags flags(*tt);
  auto &e=result[tt->getName()];e.building=flags.has("STRUCTURE");
  e.plot=flags.any({"BASE_FOUNDATION","BASE_DEFENSE_FOUNDATION","EXPANSION_PAD"}) || module(*tt,"FoundationAIUpdate");
  e.unit=!e.building && flags.any({"INFANTRY","CAVALRY","MONSTER","HORDE_MONSTER","MACHINE","AIRCRAFT","DOZER","SWARM_DOZER","HARVESTER","HORDE","HERO","BOAT","SHIP","TROLL"});
  e.marker=flags.any({"TACTICAL_MARKER","BASE_SITE","INERT_SHROUD_REVEALER"});
  e.placeholder=flags.any({"REBUILD_HOLE","SPELL_BOOK","OCL_BIT"});
  e.scenery=flags.any({"TREE","SHRUB","SHRUBBERY","ENVIRONMENT"});
  e.unfinishedContent=flags.any({"DEPRECATED","OBSOLETE"});
  if(e.plot)e.reasons.push_back("foundation_or_expansion_pad");
  if(e.marker)e.reasons.push_back("marker_kindof");
  if(e.placeholder)e.reasons.push_back("helper_or_rebuild_placeholder_kindof");
  if(e.scenery)e.reasons.push_back("scenery_kindof");
  if(e.unfinishedContent)e.reasons.push_back("deprecated_or_obsolete_kindof");
  if(tt->drawModules().size()==0)e.warnings.push_back("no_draw_module_not_proof_of_unfinished_content");
  for(const auto &m:tt->behaviorModules().nuggets())
  {
   const auto implementation=modules.findModuleTemplate(m.name,MODULETYPE_BEHAVIOR);
   if(!implementation || !implementation->portedModule)
   {e.unsupported=true;unique(e.warnings,"unsupported_behavior:"+m.name);}
  }
 }
 for(const auto master:things.templates())
 {
  const auto tt=master->getFinalOverride();const Flags flags(*tt);
  const bool builder=flags.has("DOZER") || flags.has("SWARM_DOZER");
  const bool plot=result.at(tt->getName()).plot;
  if(!builder && !plot)continue;
  for(const auto &name:sets(*tt))
  {
   if(name.empty() || name=="None")continue;
   const auto set=commands.findCommandSet(name);
   if(!set){result.at(tt->getName()).warnings.push_back("missing_command_set:"+name);continue;}
   for(int i=0;i<CommandSet::MAX_BUTTONS;++i)
   {
    const auto button=set->getCommandButton(i);if(!button)continue;
    const bool byBuilder=builder && button->m_command==GUI_COMMAND_DOZER_CONSTRUCT;
    const bool byPlot=plot && button->m_command==GUI_COMMAND_FOUNDATION_CONSTRUCT;
    if(!byBuilder && !byPlot)continue;
    const auto target=button->getThingTemplate();
    if(!target){result.at(tt->getName()).warnings.push_back("unresolved_build_command:"+button->m_name);continue;}
    auto &e=result.at(target->getName());
    if(byBuilder){e.builderBuildable=true;unique(e.builders,tt->getName());}
    if(byPlot && FactionBuildPlots::isPlot(tt->getName())){e.enhancedPlotBuildable=true;unique(e.enhancedPlots,tt->getName());}
    else if(byPlot){e.plotBuildable=true;unique(e.plots,tt->getName());}
   }
  }
 }
 for(auto &pair:result)
 {
  auto &e=pair.second;
  const bool normal=e.builderBuildable || e.plotBuildable;
  e.recommended=e.building && normal && !e.plot && !e.marker && !e.placeholder && !e.scenery && !e.unfinishedContent;
  if(e.builderBuildable)e.reasons.push_back("offered_by_builder_command");
  if(e.plotBuildable)e.reasons.push_back("offered_by_native_plot_command");
  if(e.enhancedPlotBuildable)e.reasons.push_back("offered_by_enhanced_plot_command");
  if(e.building && !normal && !e.plot)e.warnings.push_back("not_listed_by_builder_or_native_plot");
 }
 return result;
}

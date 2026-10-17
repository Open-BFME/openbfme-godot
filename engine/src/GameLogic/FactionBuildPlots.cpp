// OpenBFME enhanced content. GPL-3.0. User-requested free-standing faction plots.
// Uses the existing FoundationAIUpdate/CastleMemberBehavior construction lifecycle;
// category classification and generated templates are OpenBFME policy, not a retail port.
#include "GameLogic/FactionBuildPlots.h"
#include "Common/PlayerTemplate.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/KindOfTokens.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/Module/UpgradeModuleClasses.h"
#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace
{
std::string field(const ThingTemplate &tt, const char *name)
{
 const auto value=tt.findField(name);
 const auto text=value ? std::get_if<std::string>(value) : nullptr;
 return text ? *text : std::string();
}
bool kind(const ThingTemplate &tt, const char *name)
{
 std::array<std::uint32_t,7> mask{};std::string error;
 KindOfTokens::parseTemplate(tt,mask,error);
 if (!error.empty()) throw std::runtime_error(error);
 const int bit=KindOfTokens::indexOf(name);
 return bit>=0 && (mask[bit/32] & (1u<<(bit%32)));
}
bool module(const ThingTemplate &tt,const char *name)
{
 for (const auto &m:tt.behaviorModules().nuggets()) if(m.name==name) return true;
 return false;
}
std::vector<const CommandSet *> sets(const ThingTemplate &tt,const CommandStore &commands)
{
 std::set<std::string> names{field(tt,"CommandSet")};
 for(const auto &m:tt.behaviorModules().nuggets())
  if(const auto upgrade=dynamic_cast<const CommandSetUpgradeModuleData *>(m.data.get())) names.insert(upgrade->m_commandSet);
 std::vector<const CommandSet *> result;
 for(const auto &name:names) if(const auto set=commands.findCommandSet(name)) result.push_back(set);
 return result;
}
std::array<bool,5> categories(const ThingTemplate &tt,const CommandStore &commands)
{
 bool siege=false,units=false,upgrades=false;
 for(const auto set:sets(tt,commands)) for(int i=0;i<CommandSet::MAX_BUTTONS;++i)
 {
  const auto button=set->getCommandButton(i);
  if(!button)continue;
  if(button->m_command==GUI_COMMAND_PLAYER_UPGRADE)upgrades=true;
  if(button->m_command!=GUI_COMMAND_UNIT_BUILD && button->m_command!=GUI_COMMAND_SECONDARY_UNIT_BUILD)continue;
  const auto unit=button->getThingTemplate();if(!unit)throw std::runtime_error("Build plots: unresolved unit in "+button->m_name);
  if(kind(*unit,"DOZER") || kind(*unit,"STRUCTURE"))continue;
  if(kind(*unit,"SIEGEENGINE") || kind(*unit,"MONSTER") || kind(*unit,"HORDE_MONSTER") || kind(*unit,"TROLL"))siege=true;
  else units=true;
 }
 return {true,siege,units,kind(tt,"ECONOMY_STRUCTURE") || module(tt,"TerrainResourceBehavior") || module(tt,"AutoDepositUpdate"),upgrades && !units};
}
}

bool FactionBuildPlots::isPlot(const std::string &name) { return name.compare(0,19,"OpenBFME_BuildPlot_")==0; }
FactionBuildPlots::Generated FactionBuildPlots::generate(const ThingFactory &things,const CommandStore &commands,const PlayerTemplateStore &players)
{
 Generated result;std::ostringstream ini;
 const char *names[]={"General","Siege","Military","Economy","Upgrades"};
 for(const int index:players.playableSideIndices())
 {
  const auto faction=players.getNthPlayerTemplate(index);
  std::map<std::string,const CommandButton *> offers;
  std::set<std::string> sides{faction->m_side};
  for(const auto &name:faction->m_startingUnit)
  {
   const auto master=things.findTemplate(name);
   const auto builder=master ? master->getFinalOverride() : nullptr;
   if(!builder || !kind(*builder,"DOZER"))continue;
   const auto builderSide=field(*builder,"Side");
   if(!builderSide.empty())sides.insert(builderSide);
   for(const auto set:sets(*builder,commands))for(int i=0;i<CommandSet::MAX_BUTTONS;++i)
   {
    const auto button=set->getCommandButton(i);if(!button || button->m_command!=GUI_COMMAND_DOZER_CONSTRUCT)continue;
    const auto building=button->getThingTemplate();
    if(!building)throw std::runtime_error("Build plots: unresolved building in "+button->m_name);
    if(!kind(*building,"STRUCTURE") || kind(*building,"WALL_HUB") || kind(*building,"CASTLE_KEEP") || kind(*building,"CASTLE_CENTER"))continue;
    offers.emplace(building->getName(),button);
   }
  }
  if(offers.empty())continue; // Playable mission factions without a builder have no builder menu to mirror.
  const ThingTemplate *foundation=nullptr;
  for(const auto candidate:things.templates())
  {
   const auto tt=candidate->getFinalOverride();
   if(!isPlot(tt->getName()) && sides.count(field(*tt,"Side")) && module(*tt,"FoundationAIUpdate") && module(*tt,"CastleMemberBehavior"))
   {foundation=tt;break;}
  }
  if(!foundation)throw std::runtime_error("Build plots: no faction foundation for "+faction->getName());
  std::array<std::vector<const CommandButton *>,5> menus;
  for(const auto &offer:offers)
  {
   const auto membership=categories(*offer.second->getThingTemplate(),commands);
   for(size_t type=0;type<menus.size();++type)if(membership[type])menus[type].push_back(offer.second);
  }
  for(size_t type=0;type<menus.size();++type)
  {
   Definition plot;plot.name="OpenBFME_BuildPlot_"+faction->getName()+"_"+names[type];
   plot.faction=faction->getName();plot.category=names[type];plot.foundation=foundation->getName();
   if(things.findTemplate(plot.name))throw std::runtime_error("Build plots: reserved template already exists: "+plot.name);
   if(menus[type].size()>CommandSet::MAX_BUTTONS)throw std::runtime_error("Build plots: menu exceeds 33 slots: "+plot.name);
   ini<<"ChildObject "<<plot.name<<" "<<plot.foundation<<"\n"
      <<" DisplayName = OPENBFME:BuildPlot"<<names[type]<<"\n KindOf = STRUCTURE SELECTABLE IMMOBILE BASE_FOUNDATION\n CommandSet = "<<plot.name<<"Set\n BuildCost = 0\n";
   // Keep the pad art/body and occupancy lifecycle, discard fortress-linked upgrades,
   // auto-unpack, death-spawns and expansion-selection restrictions.
   for(const auto &m:foundation->behaviorModules().nuggets())
    if(m.name!="FoundationAIUpdate" && m.name!="CastleMemberBehavior" && m.name.find("Body")==std::string::npos)ini<<" RemoveModule = "<<m.tag<<"\n";
   ini<<"End\n";
   int slot=0;
   for(const auto source:menus[type])
   {
    const auto buttonName=plot.name+"Button"+std::to_string(++slot);
    // Button parser has no inheritance. Preserve all source presentation and
    // prerequisite fields by copying the native button after parsing below.
    ini<<"CommandButton "<<buttonName<<"\n Command = FOUNDATION_CONSTRUCT\n Object = "<<source->getThingTemplate()->getName()<<"\nEnd\n";
    plot.buildings.push_back(source->m_name);
   }
   ini<<"CommandSet "<<plot.name<<"Set\n";
   for(int i=1;i<=slot;++i)ini<<" "<<i<<" = "<<plot.name<<"Button"<<i<<"\n";
   ini<<"End\n";result.plots.push_back(std::move(plot));
  }
 }
 result.ini=ini.str();return result;
}

void FactionBuildPlots::copyButtons(const Generated &generated,CommandStore &commands)
{
 for(const auto &plot:generated.plots)for(size_t i=0;i<plot.buildings.size();++i)
 {
  auto button=commands.findNonConstCommandButton(plot.name+"Button"+std::to_string(i+1));
  const auto source=commands.findCommandButton(plot.buildings[i]);
  if(!button || !source)throw std::runtime_error("Build plots: missing generated/source button: "+plot.name);
  const auto name=button->m_name;
  *button=*source;
  button->m_name=name;button->m_nextOverride=nullptr;button->m_isOverride=false;
  button->m_command=GUI_COMMAND_FOUNDATION_CONSTRUCT;
  button->m_radial=true;button->m_inPalantir=false;
  button->m_options &= ~COMMAND_OPTION_NEED_TARGET_POS;
 }
}

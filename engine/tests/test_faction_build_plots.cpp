// OpenBFME enhanced plot menus and ordinary-dozer-building construction. GPL-3.0.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "ProdTestUtil.h"
#include "GameLogic/FactionBuildPlots.h"
#include "GameClient/GameText.h"
#include "GameLogic/Construction.h"
#include "GameLogic/BuildCommands.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/Module/ConstructionModules.h"
#include "GameLogic/Module/CastleModules.h"
using namespace prodtest;

namespace
{
const char objects[] = R"ini(
Object Pad
 Side = Alpha
 KindOf = STRUCTURE SELECTABLE IMMOBILE BASE_FOUNDATION
 Behavior = FoundationAIUpdate ModuleTag_Foundation
 End
 Behavior = CastleMemberBehavior ModuleTag_Member
 End
End
ChildObject BetaPad Pad
 Side = Beta
End
Object Builder
 Side = Alpha
 KindOf = DOZER
 CommandSet = BuilderSet
End
ChildObject BetaBuilder Builder
 Side = Beta
 CommandSet = BetaBuilderSet
End
Object Soldier
 KindOf = INFANTRY
End
Object Trebuchet
 KindOf = SIEGEENGINE
End
Object Ent
 KindOf = MONSTER
End
Object Troll
 KindOf = TROLL
End
Object Barracks
 KindOf = STRUCTURE
 BuildCost = 300
 BuildTime = 2
 CommandSet = BarracksSet
 Body = ActiveBody ModuleTag_Body
  MaxHealth = 1000
  MaxHealthDamaged = 500
  MaxHealthReallyDamaged = 100
 End
 Behavior = GettingBuiltBehavior ModuleTag_Build
 End
End
ChildObject SiegeWorks Barracks
 CommandSet = SiegeSet
End
ChildObject EntMoot Barracks
 CommandSet = EntSet
End
ChildObject TrollCage Barracks
 CommandSet = TrollSet
End
ChildObject Farm Barracks
 KindOf = STRUCTURE ECONOMY_STRUCTURE
 CommandSet = EmptySet
End
ChildObject Blacksmith Barracks
 CommandSet = SmithSet
End
ChildObject Tower Barracks
 CommandSet = EmptySet
End
ChildObject Fort Barracks
 KindOf = STRUCTURE CASTLE_KEEP
End
ChildObject WallHub Barracks
 KindOf = STRUCTURE WALL_HUB
End
CommandButton TrainSoldier
 Command = UNIT_BUILD
 Object = Soldier
End
CommandButton TrainSiege
 Command = UNIT_BUILD
 Object = Trebuchet
End
CommandButton TrainEnt
 Command = UNIT_BUILD
 Object = Ent
End
CommandButton TrainTroll
 Command = UNIT_BUILD
 Object = Troll
End
CommandButton Research
 Command = PLAYER_UPGRADE
 Upgrade = Upgrade_Basic
End
CommandSet BarracksSet
 1 = TrainSoldier
End
CommandSet SiegeSet
 1 = TrainSiege
 2 = Research
End
CommandSet EntSet
 1 = TrainEnt
End
CommandSet TrollSet
 1 = TrainTroll
End
CommandSet SmithSet
 1 = Research
End
CommandSet EmptySet
End
PlayerTemplate FactionA
 Side = Alpha
 PlayableSide = Yes
 StartingUnit0 = Builder
End
PlayerTemplate FactionB
 Side = Beta
 PlayableSide = Yes
 StartingUnit0 = BetaBuilder
End
)ini";
struct Fixture:ProdWorld
{
 FactionBuildPlots::Generated generated;
 Fixture()
 {
  load(objects);
  std::string ini="CommandSet BuilderSet\n";
  const char *names[]={"Barracks","SiegeWorks","EntMoot","TrollCage","Farm","Blacksmith","Tower","Fort","WallHub"};
  for(int i=0;i<9;++i)ini+=" "+std::to_string(i+1)+" = Build"+names[i]+"\n";
  ini+="End\nCommandSet BetaBuilderSet\n 1 = BuildFarm\nEnd\n";
  std::string buttons;
  for(const auto name:names)buttons+="CommandButton Build"+std::string(name)+"\n Command = DOZER_CONSTRUCT\n Object = "+name+"\nEnd\n";
  load(buttons+ini);
  auto source=commands.findNonConstCommandButton("BuildBarracks");
  source->m_buttonImageName={"BarracksIcon"};source->m_textLabel={"CONTROLBAR:Barracks"};
  source->m_options=COMMAND_OPTION_NEED_TARGET_POS | COMMAND_OPTION_NEED_UPGRADE;
  source->m_neededUpgrade={"Upgrade_Basic"};
  generated=FactionBuildPlots::generate(w.things,commands,templates);
  load(generated.ini);
  FactionBuildPlots::copyButtons(generated,commands);
 }
 const FactionBuildPlots::Definition &plot(const std::string &faction,const std::string &category)
 {
  for(const auto &p:generated.plots)if(p.faction==faction && p.category==category)return p;
  throw std::runtime_error("missing plot");
 }
};
}
TEST_CASE("faction plots: five variants per faction with exact roles and no forts or wall hubs")
{
 Fixture f;
 REQUIRE(f.generated.plots.size()==10);
 const auto &general=f.plot("FactionA","General");
 CHECK(general.buildings.size()==7);
 for(const auto &name:general.buildings){CHECK(name!="BuildFort");CHECK(name!="BuildWallHub");}
 const auto &siege=f.plot("FactionA","Siege");
 CHECK(siege.buildings==std::vector<std::string>{"BuildEntMoot","BuildSiegeWorks","BuildTrollCage"});
 CHECK(f.plot("FactionA","Military").buildings==std::vector<std::string>{"BuildBarracks"});
 CHECK(f.plot("FactionA","Economy").buildings==std::vector<std::string>{"BuildFarm"});
 CHECK(f.plot("FactionA","Upgrades").buildings==std::vector<std::string>{"BuildBlacksmith","BuildSiegeWorks"});
 CHECK(f.plot("FactionB","General").buildings==std::vector<std::string>{"BuildFarm"});
 CHECK(f.plot("FactionB","Siege").buildings.empty());
 CHECK(f.plot("FactionB","General").foundation=="BetaPad");
 for(const auto &p:f.generated.plots)
 {
  CHECK(FactionBuildPlots::isPlot(p.name));
  const auto set=f.commands.findCommandSet(p.name+"Set");REQUIRE(set);
  for(size_t i=0;i<p.buildings.size();++i)
  {
   const auto button=set->getCommandButton((int)i);REQUIRE(button);
   CHECK(button->m_command==GUI_COMMAND_FOUNDATION_CONSTRUCT);
   CHECK(button->m_radial);CHECK_FALSE(button->m_inPalantir);
   CHECK_FALSE(button->hasOption(COMMAND_OPTION_NEED_TARGET_POS));
   const auto original=f.commands.findCommandButton(p.buildings[i]);
   CHECK(button->m_buttonImageName==original->m_buttonImageName);
   CHECK(button->m_textLabel==original->m_textLabel);
   CHECK(button->m_neededUpgrade==original->m_neededUpgrade);
   CHECK(button->getThingTemplate()==f.commands.findCommandButton(p.buildings[i])->getThingTemplate());
  }
 }
 CHECK_FALSE(FactionBuildPlots::isPlot("OpenBFME_BuildPlotImposter"));
 CHECK(f.commands.findCommandButton("BuildBarracks")->m_command==GUI_COMMAND_DOZER_CONSTRUCT);
}
TEST_CASE("faction plots: ordinary builder buildings pay, self-build, and free their plot on death")
{
 Fixture f;
 const auto name=f.plot("FactionA","Military").name;
 Object *plot=f.make(name.c_str(),f.teamOf("Alice"));REQUIRE(plot);
 const auto money=f.alice()->getMoney()->countMoney();
 Object *building=Construction::constructOnPlot(*plot,*f.w.get("Barracks"),*plot->getPosition(),0,*f.alice(),false);
 REQUIRE(building);
 CHECK(building->isUnderConstruction());
 CHECK(building->getBuildCostPaid()==300);
 CHECK(f.alice()->getMoney()->countMoney()==money-300);
 CHECK(plot->isDrawableHidden());
 CHECK(Construction::constructOnPlot(*plot,*f.w.get("Barracks"),*plot->getPosition(),0,*f.alice(),false)==nullptr);
 auto gb=dynamic_cast<GettingBuiltBehavior *>(building->findModule("GettingBuiltBehavior"));REQUIRE(gb);
 CHECK(gb->isConstructing());
 for(int i=0;i<40 && building->isUnderConstruction();++i)f.logic->runLogicFrame();
 CHECK_FALSE(building->isUnderConstruction());
 f.logic->destroyObject(building);
 for(int i=0;i<3;++i)f.logic->runLogicFrame();
 CHECK_FALSE(plot->isDrawableHidden());
 CHECK(Construction::constructOnPlot(*plot,*f.w.get("Barracks"),*plot->getPosition(),0,*f.alice(),false)!=nullptr);
}

TEST_CASE("faction plots: authored category labels and mounted translations")
{
 GameTextTable text;std::string value,error;
 CHECK(text.lookup("OPENBFME:BuildPlotSiege",value));CHECK(value=="Siege build plot");
 const std::string translated="OPENBFME:BuildPlotSiege\n\"Translated siege plot\"\nEND\n";
 REQUIRE(text.parse(reinterpret_cast<const std::uint8_t *>(translated.data()),translated.size(),&error));
 CHECK(text.lookup("OPENBFME:BuildPlotSiege",value));CHECK(value=="Translated siege plot");
 CHECK_FALSE(text.lookup("OPENBFME:UnknownLabel",value));
}

TEST_CASE("faction plots: siege offered only after a command-set upgrade is classified")
{
 ProdWorld f;f.load(objects);
 f.load(R"ini(
ChildObject LateSiege Barracks
 CommandSet = EmptySet
 Behavior = CommandSetUpgrade ModuleTag_LevelTwo
  TriggeredBy = Upgrade_Barracks2
  CommandSet = SiegeSet
 End
End
CommandButton BuildLateSiege
 Command = DOZER_CONSTRUCT
 Object = LateSiege
End
CommandSet BuilderSet
 1 = BuildLateSiege
End
)ini");
 const auto generated=FactionBuildPlots::generate(f.w.things,f.commands,f.templates);
 REQUIRE(generated.plots.size()==5);
 CHECK(generated.plots[1].category=="Siege");
 CHECK(generated.plots[1].buildings==std::vector<std::string>{"BuildLateSiege"});
 CHECK(generated.plots[2].buildings.empty());
 CHECK(generated.plots[4].buildings==std::vector<std::string>{"BuildLateSiege"});
}

TEST_CASE("faction plots: native menu dispatch pays once and cancellation refunds and restores the pad")
{
 Fixture f;f.logic->settings().buildRulesLoaded=true;
 auto plot=f.make(f.plot("FactionA","General").name,f.teamOf("Alice"));REQUIRE(plot);
 GameLogicDispatch dispatch(*f.logic);BuildCommands commands;commands.registerHandlers(dispatch);
 f.alice()->selection()={plot->getID()};
 const auto money=f.alice()->getMoney()->countMoney();
 GameMessage build(MSG_FOUNDATION_CONSTRUCT,f.alice()->getPlayerIndex());
 build.appendIntegerArgument(f.w.get("Tower")->getTemplateID());
 build.appendLocationArgument(*plot->getPosition());build.appendRealArgument(0);
 dispatch.dispatch(build);REQUIRE(commands.stats().foundations==1);
 auto member=dynamic_cast<CastleMemberBehavior *>(plot->findModule("CastleMemberBehavior"));REQUIRE(member);
 auto building=f.logic->findObjectByID(member->occupantId());REQUIRE(building);
 CHECK(f.alice()->getMoney()->countMoney()==money-300);
 f.alice()->selection()={building->getID()};
 dispatch.dispatch(GameMessage(MSG_DOZER_CANCEL_CONSTRUCT,f.alice()->getPlayerIndex()));
 CHECK(commands.stats().cancels==1);CHECK(f.alice()->getMoney()->countMoney()==money);
 for(int i=0;i<3;++i)f.logic->runLogicFrame();
 CHECK_FALSE(plot->isDrawableHidden());CHECK_FALSE(member->plotIsTaken());
}

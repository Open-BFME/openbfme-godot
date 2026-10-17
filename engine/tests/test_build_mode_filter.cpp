// OpenBFME build palette classification policy. GPL-3.0.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "ProdTestUtil.h"
#include "GameLogic/BuildModeFilter.h"
using namespace prodtest;

TEST_CASE("build filter: commands prove availability; names and selectability do not")
{
 ProdWorld f;
 f.load(R"ini(
Object Builder
 KindOf = DOZER
 CommandSet = BuilderSet
End
Object Plot
 KindOf = STRUCTURE BASE_FOUNDATION
 CommandSet = PlotSet
End
ChildObject OpenBFME_BuildPlot_Test_General Plot
 CommandSet = EnhancedPlotSet
End
Object FakeMarkerNameBarracks
 KindOf = STRUCTURE
End
Object Expansion
 KindOf = STRUCTURE
End
Object EnhancedOnly
 KindOf = STRUCTURE
End
Object Orphan
 KindOf = STRUCTURE
End
Object Tactical
 KindOf = STRUCTURE TACTICAL_MARKER
End
Object Soldier
 KindOf = INFANTRY SELECTABLE
End
Object Horde
 KindOf = HORDE
End
Object SelectableScenery
 KindOf = SELECTABLE TREE
End
Object Hole
 KindOf = STRUCTURE REBUILD_HOLE
End
Object Old
 KindOf = STRUCTURE OBSOLETE
End
CommandButton Build
 Command = DOZER_CONSTRUCT
 Object = FakeMarkerNameBarracks
End
CommandButton PlotBuild
 Command = FOUNDATION_CONSTRUCT
 Object = Expansion
End
CommandButton EnhancedBuild
 Command = FOUNDATION_CONSTRUCT
 Object = EnhancedOnly
End
CommandButton Unused
 Command = DOZER_CONSTRUCT
 Object = Orphan
End
CommandSet BuilderSet
 1 = Build
End
CommandSet PlotSet
 1 = PlotBuild
End
CommandSet EnhancedPlotSet
 1 = EnhancedBuild
End
)ini");
 const auto result=BuildModeFilter::classify(f.w.things,f.commands,f.w.modules);
 const auto &builder=result.at("FakeMarkerNameBarracks");
 CHECK(builder.builderBuildable);CHECK(builder.recommended);CHECK_FALSE(builder.marker);CHECK_FALSE(builder.placeholder);
 CHECK(builder.builders==std::vector<std::string>{"Builder"});
 CHECK(result.at("Expansion").plotBuildable);CHECK(result.at("Expansion").recommended);
 CHECK(result.at("EnhancedOnly").enhancedPlotBuildable);CHECK_FALSE(result.at("EnhancedOnly").plotBuildable);
 CHECK_FALSE(result.at("EnhancedOnly").recommended);CHECK_FALSE(result.at("Orphan").builderBuildable);
 CHECK(result.at("Tactical").marker);CHECK_FALSE(result.at("Tactical").recommended);
 CHECK(result.at("Soldier").unit);CHECK(result.at("Horde").unit);
 CHECK(result.at("SelectableScenery").scenery);CHECK_FALSE(result.at("SelectableScenery").unit);
 CHECK(result.at("Hole").placeholder);CHECK(result.at("Old").unfinishedContent);
 CHECK(result.at("Plot").plot);CHECK_FALSE(result.at("Plot").recommended);
 CHECK_FALSE(builder.unfinishedContent);CHECK_FALSE(builder.warnings.empty()); // no art is a warning, not proof
}

TEST_CASE("build filter: upgraded menus and obsolete content retain independent evidence")
{
 ProdWorld f;
 f.load(R"ini(
Object Builder
 KindOf = DOZER
 CommandSet = Empty
 Behavior = CommandSetUpgrade ModuleTag_Later
  TriggeredBy = Upgrade_Barracks2
  CommandSet = Later
 End
End
Object OldBuilding
 KindOf = STRUCTURE DEPRECATED
End
CommandButton BuildOld
 Command = DOZER_CONSTRUCT
 Object = OldBuilding
End
CommandSet Empty
End
CommandSet Later
 1 = BuildOld
End
)ini");
 const auto result=BuildModeFilter::classify(f.w.things,f.commands,f.w.modules);
 const auto &entry=result.at("OldBuilding");
 CHECK(entry.builderBuildable);CHECK(entry.unfinishedContent);CHECK_FALSE(entry.recommended);
 CHECK(entry.builders==std::vector<std::string>{"Builder"});
}

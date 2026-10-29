// OpenBFME. GPL-3.0. BFME2 source wall-library and planning regressions.
#include "doctest.h"
#include "CombatTestUtil.h"
#include "Common/MapObject.h"
#include "Common/StateHash.h"
#include "Common/XferSave.h"
#include "Common/XferLoad.h"
#include "GameLogic/SkirmishAI/AIWallBuilder.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"
using namespace AIWallBuilder;
namespace {
MapObject marker(const char *name,int index,float x,float y=0,float z=0) {
 MapObject object; object.m_location={x,y,z};
 object.m_properties.setInt("GenericAIObjectID",23);
 object.m_properties.setInt("GenericAIObjectType",0);
 object.m_properties.setAsciiString("GenericAIObjectName",name);
 object.m_properties.setInt("GenericAIObjectWallHubNumber",index);
 return object;
}
}
TEST_CASE("BFME2 AI wall library: typed markers, unsigned node order, first duplicate and upper median")
{
 auto second=marker("West",2,200), first=marker("West",0,0), middle=marker("West",1,100);
 auto duplicate=marker("West",1,999);
 auto east0=marker("East",0,400), east1=marker("East",1,500);
 auto rawBridge=marker("NotWall",0,0); rawBridge.m_properties=Dict(); rawBridge.m_flags=FLAG_BRIDGE_POINT2;
 auto wrongType=marker("WrongType",0,0); wrongType.m_properties.setReal("GenericAIObjectID",23);
 auto otherKind=marker("OtherAI",0,0); otherKind.m_properties.setInt("GenericAIObjectType",1);
 auto library=loadWallLibrary({second,east0,first,middle,duplicate,east1,rawBridge,wrongType,otherKind});
 REQUIRE(library.size()==2);
 CHECK(library[0].name=="West"); CHECK(library[1].name=="East");
 REQUIRE(library[0].nodes.size()==3);
 CHECK(library[0].nodes[0].index==0); CHECK(library[0].nodes[1].index==1); CHECK(library[0].nodes[2].index==2);
 CHECK(library[0].nodes[1].position.x==100);
 CHECK(library[0].selected==1); CHECK(library[1].selected==1);
}
TEST_CASE("BFME2 AI wall library: single nodes, gaps and negative hub indices are rejected")
{
 auto library=loadWallLibrary({marker("Single",0,0),marker("Gap",0,0),marker("Gap",2,200),
  marker("Negative",-1,0),marker("Negative",0,100),marker("Good",1,100),marker("Good",0,0)});
 REQUIRE(library.size()==1); CHECK(library[0].name=="Good");
}
TEST_CASE("BFME2 AI wall eligibility: every node needs a command center strictly within 1500 in 3D")
{
 const auto plan=loadWallLibrary({marker("Wall",0,0),marker("Wall",1,1000)})[0];
 CHECK_FALSE(nearCommandCenters(plan,{}));
 CHECK(nearCommandCenters(plan,{{0,0,1499}})==false); // second node is farther in 3D
 CHECK_FALSE(nearCommandCenters(plan,{{0,0,1500},{1000,0,1500}}));
 CHECK(nearCommandCenters(plan,{{0,0,1499},{1000,0,1499}}));
 CHECK_FALSE(nearCommandCenters(plan,{{-1499,0,0}})); // only first node is covered
 CHECK(nearCommandCenters(plan,{{-1499,0,0},{2499,0,0}}));
}
TEST_CASE("BFME2 AI wall choice: zero and one skip selection draw; multiple draw before priority")
{
 combattest::CombatWorld w;
 auto &random=w.logic->random(); random.seedRandom(1234); random.enableCallLog(true); random.clearCallLog();
 CHECK(choosePlan(random,{}).index==-1); CHECK(random.callLog().empty());
 GameLogicRandom oracle=random;
 const float singlePriority=oracle.getValueReal(70,110,"AIWallBuilder.cpp",180);
 auto single=choosePlan(random,{7}); CHECK(single.index==7); CHECK(single.priority==singlePriority);
 REQUIRE(random.callLog().size()==1); CHECK(random.callLog()[0].line==180);
 oracle=random;
 const int picked=oracle.getValue(0,1,"AIWallBuilder.cpp",165);
 const float priority=oracle.getValueReal(70,110,"AIWallBuilder.cpp",180);
 random.clearCallLog();
 auto multiple=choosePlan(random,{4,9}); CHECK(multiple.index==(picked==0 ? 4 : 9)); CHECK(multiple.priority==priority);
 REQUIRE(random.callLog().size()==2);
 CHECK(random.callLog()[0].line==165); CHECK(random.callLog()[0].lo==0); CHECK(random.callLog()[0].hi==1);
 CHECK(random.callLog()[1].line==180); CHECK(random.callLog()[1].rlo==70); CHECK(random.callLog()[1].rhi==110);
}
TEST_CASE("BFME2 AI wall gate: endpoint neighbor and interior real draw give the midpoint")
{
 combattest::CombatWorld w;
 auto &random=w.logic->random(); random.seedRandom(51); random.enableCallLog(true); random.clearCallLog();
 auto pair=loadWallLibrary({marker("Wall",0,0,10,20),marker("Wall",1,100,30,40)})[0];
 const auto endpoint=calcGatePosition(random,pair);
 CHECK(endpoint.x==50); CHECK(endpoint.y==20); CHECK(endpoint.z==30); CHECK(random.callLog().empty());
 const auto triple=loadWallLibrary({marker("Wall",0,0),marker("Wall",1,100),marker("Wall",2,200)})[0];
 GameLogicRandom oracle=random; const float roll=oracle.getValueReal(0,1,"AIWall.cpp",560);
 const auto interior=calcGatePosition(random,triple);
 CHECK(interior.x==(roll>0.5f ? 150 : 50)); // tolerance: exact native side branch, not an approximate result.
 REQUIRE(random.callLog().size()==1); CHECK(random.callLog()[0].file=="AIWall.cpp"); CHECK(random.callLog()[0].line==560);
}
TEST_CASE("BFME2 AI wall map identity: save mismatch is rejected transactionally and valid library remains loaded")
{
 combattest::CombatWorld w;
 SkirmishAIStore store(w.w.keys); store.registerBlocks(w.w.fx.env.blocks);
 REQUIRE(w.w.load("SkirmishAIData Walls\nEnd\nChildObject WallCenter Swordsman\n KindOf = STRUCTURE COMMANDCENTER IMMOBILE\nEnd\n").empty());
 auto *owner=w.players.findPlayerWithName("Alice"); owner->setPlayerType(PLAYER_COMPUTER,true);
 w.unit("WallCenter",'A',100,500); w.unit("WallCenter",'B',200,500); w.unit("Swordsman",'A',300,500);
 const auto centers=commandCenters(*w.logic,*owner); REQUIRE(centers.size()==1); CHECK(centers[0].x==100);
 auto &manager=w.logic->skirmishAI(); manager.setStore(&store);
 const std::vector<MapObject> original{marker("MapWall",0,0),marker("MapWall",1,100)};
 manager.loadWallLibrary(original); manager.newGame(true);
 REQUIRE(manager.wallLibrary().size()==1);
 StateHasher savedHash; manager.crc(savedHash);
 XferSave saved; saved.beginBlock(); manager.xfer(saved); saved.endBlock();
 manager.loadWallLibrary({marker("OtherMapWall",0,0),marker("OtherMapWall",1,200)});
 StateHasher before; manager.crc(before); CHECK_FALSE(before.value()==savedHash.value());
 XferLoad bad(saved.data()); bad.beginBlock();
 CHECK_THROWS_WITH(manager.xfer(bad),"skirmish ai: wall library does not match the loaded map");
 StateHasher after; manager.crc(after); CHECK(after.value()==before.value());
 manager.loadWallLibrary(original);
 const auto identity=manager.wallLibraryCrc();
 XferLoad good(saved.data()); good.beginBlock(); manager.xfer(good); good.endBlock();
 CHECK(manager.wallLibraryCrc()==identity); CHECK(manager.wallLibrary()[0].name=="MapWall");
 StateHasher restoredHash; manager.crc(restoredHash); CHECK(restoredHash.value()==savedHash.value());
 manager.reset(); CHECK(manager.wallLibrary().empty());
}

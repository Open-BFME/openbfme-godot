// OpenBFME. GPL-3.0. BFME2 source wall-library and planning regressions.
#include <ostream> // libstdc++ shared_ptr diagnostics need the pointer insertion overload
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

#include "Common/Money.h"
#include "GameLogic/SkirmishAI/AIBaseBuilder.h"
#include "GameLogic/WallSpan.h"
TEST_CASE("BFME2 AI wall construction: median starts alone, both directions wait and hand off terminal hubs")
{
 combattest::CombatWorld w;
 const auto plan=loadWallLibrary({marker("Wall",0,100),marker("Wall",1,200),marker("Wall",2,300)})[0];
 AIBuildState builder;
 auto wall=activate(plan,1,80,"Swordsman",builder);
 CHECK(wall.owner==1); CHECK(wall.gatePriority==90);
 CHECK(wall.leftState==1); CHECK(wall.rightState==1);
 REQUIRE(builder.pending.size()==1); CHECK(builder.pending[0]==wall.orders[1]);
 CHECK(builder.nextSerial==4);
 for(const auto &r:wall.orders) { CHECK(r->wall); CHECK(r->plain); CHECK(r->priority==80); CHECK(r->templateName=="Swordsman"); }
 CHECK_FALSE(readyForGate(wall,plan,false));
 updateDirection(*w.logic,wall,plan,builder,true); CHECK(wall.leftState==1);
 auto *middle=w.unit("Swordsman",'A',200,0); middle->setConstructionPercent(50);
 wall.orders[1]->state=1; wall.orders[1]->built=middle->getID();
 updateDirection(*w.logic,wall,plan,builder,true); updateDirection(*w.logic,wall,plan,builder,false);
 CHECK(wall.leftState==2); CHECK(wall.rightState==2); CHECK(builder.pending.size()==1);
 updateDirection(*w.logic,wall,plan,builder,true); CHECK(wall.leftIndex==1);
 middle->setConstructionPercent(-1);
 updateDirection(*w.logic,wall,plan,builder,true); updateDirection(*w.logic,wall,plan,builder,false);
 CHECK(wall.leftIndex==0); CHECK(wall.rightIndex==2); CHECK(wall.orders[1]->state==2);
 REQUIRE(builder.pending.size()==3);
 CHECK(builder.pending[1]==wall.orders[0]); CHECK(builder.pending[2]==wall.orders[2]);
 CHECK(wall.orders[0]->wallStartNode==middle->getID()); CHECK(wall.orders[2]->wallStartNode==middle->getID());
 CHECK_FALSE(readyForGate(wall,plan,false));
 for(int index:{0,2}) {
  auto *hub=w.unit("Swordsman",'A',100.0f+index*100.0f,0);
  wall.orders[index]->built=hub->getID(); wall.orders[index]->state=1;
 }
 updateDirection(*w.logic,wall,plan,builder,true); updateDirection(*w.logic,wall,plan,builder,false);
 CHECK(wall.leftState==4); CHECK(wall.rightState==4); CHECK(wall.leftIndex==-1); CHECK(wall.rightIndex==3);
 CHECK(readyForGate(wall,plan,false)); CHECK_FALSE(readyForGate(wall,plan,true));
}
TEST_CASE("BFME2 AI wall construction: failed median stops both ends and missing product requeues once")
{
 combattest::CombatWorld w;
 const auto plan=loadWallLibrary({marker("Wall",0,100),marker("Wall",1,200)})[0];
 AIBuildState builder;
 auto wall=activate(plan,1,70,"Swordsman",builder);
 wall.orders[1]->state=3;
 updateDirection(*w.logic,wall,plan,builder,true); updateDirection(*w.logic,wall,plan,builder,false);
 CHECK(wall.leftState==4); CHECK(wall.rightState==4); CHECK(readyForGate(wall,plan,false));
 builder=AIBuildState(); wall=activate(plan,1,70,"Swordsman",builder);
 auto median=wall.orders[1]; median->state=1;
 updateDirection(*w.logic,wall,plan,builder,true); updateDirection(*w.logic,wall,plan,builder,false);
 updateDirection(*w.logic,wall,plan,builder,true); updateDirection(*w.logic,wall,plan,builder,false);
 CHECK(median->state==0); REQUIRE(builder.pending.size()==1); CHECK(builder.pending[0]==median); CHECK(builder.inProgress.empty());
 // Native endpoint unsigned underflow is retained rather than replaced by signed arithmetic.
 auto endpoint=plan; endpoint.selected=0; wall=Construction(); wall.leftIndex=0; wall.rightIndex=2;
 CHECK(readyForGate(wall,endpoint,false));
}
namespace {
const char wallDefinitions[]=
 "Object AIWallHub\n KindOf = STRUCTURE IMMOBILE WALL_HUB\n BuildCost = 20\n Geometry = BOX\n GeometryMajorRadius = 4\n GeometryMinorRadius = 10\n GeometryHeight = 20\n Behavior = WallHubBehavior Hub\n SegmentTemplateName = AIWallTile\n DefaultSegmentTemplateName = AIWallTile\n HubCapTemplateName = AIWallHub\n BuilderRadius = 10\n End\nEnd\n"
 "Object AIWallTile\n KindOf = STRUCTURE IMMOBILE WALL_SEGMENT\n BuildCost = 10\n Geometry = BOX\n GeometryMajorRadius = 4\n GeometryMinorRadius = 10\n GeometryHeight = 20\nEnd\n";
}
TEST_CASE("BFME2 AI wall segment: quote caches cost only on success and build returns terminal hub")
{
 combattest::CombatWorld w(wallDefinitions);
 w.logic->settings().buildRulesLoaded=true; w.logic->settings().allowedHeightVariationForBuilding=10; w.logic->settings().maxLineBuildObjects=50;
 auto *hub=w.unit("AIWallHub",'A',300,500); auto &owner=*hub->getControllingPlayer();
 owner.getMoney()->withdraw(owner.getMoney()->countMoney(),false); owner.getMoney()->deposit(1000,false);
 AIBuildRequest request; request.wall=true; request.plain=true; request.wallStartNode=hub->getID(); request.offset={700,500,0}; request.cost=17;
 WallSpan::Plan quote; REQUIRE(WallSpan::plan(*w.logic,*hub,*hub->getPosition(),request.offset,0,quote));
 REQUIRE(quote.worstCode==0);
 CHECK(checkSegment(*w.logic,request,owner)==0); CHECK(request.cost==quote.cost);
 const auto money=owner.getMoney()->countMoney();
 REQUIRE(buildSegment(*w.logic,request,owner));
 auto *terminal=w.logic->findObjectByID(request.built); REQUIRE(terminal);
 CHECK(terminal->isKindOfName("WALL_HUB")); CHECK_FALSE(terminal==hub);
 CHECK(owner.getMoney()->countMoney()==money-(std::uint32_t)quote.cost);
 CHECK(terminal->getConstructionPercent()==0);
 request.wallStartNode=INVALID_ID;
 CHECK_THROWS_WITH(checkSegment(*w.logic,request,owner),"AIWall: missing start hub for span quote");
 CHECK_THROWS_WITH(buildSegment(*w.logic,request,owner),"AIWall: missing start hub for span build");
}
TEST_CASE("BFME2 AI wall segment: insufficient money waits, invalid quote fails without spending")
{
 combattest::CombatWorld w(wallDefinitions);
 w.logic->settings().buildRulesLoaded=true; w.logic->settings().allowedHeightVariationForBuilding=10; w.logic->settings().maxLineBuildObjects=50;
 auto *hub=w.unit("AIWallHub",'A',300,500); auto &owner=*hub->getControllingPlayer();
 owner.getMoney()->withdraw(owner.getMoney()->countMoney(),false);
 AIBuildRequest request; request.wall=true; request.plain=true; request.wallStartNode=hub->getID(); request.offset={700,500,0}; request.cost=17;
 CHECK(checkSegment(*w.logic,request,owner)==2); CHECK(request.cost==17); CHECK(owner.getMoney()->countMoney()==0);
 CHECK_FALSE(buildSegment(*w.logic,request,owner)); CHECK(request.state==3); CHECK(request.built==INVALID_ID);
 owner.getMoney()->deposit(1000,false);
 request.offset=*hub->getPosition();
 CHECK(checkSegment(*w.logic,request,owner)==9); CHECK(request.cost==17); CHECK(owner.getMoney()->countMoney()==1000);
}
TEST_CASE("BFME2 AI wall requests: save keeps start hub and cached quote, malformed kind rolls back")
{
 combattest::CombatWorld w;
 SkirmishAIStore store(w.w.keys); store.registerBlocks(w.w.fx.env.blocks);
 REQUIRE(w.w.load("SkirmishAIData Walls\nEnd\n").empty());
 auto *owner=w.players.findPlayerWithName("Alice"); owner->setPlayerType(PLAYER_COMPUTER,true);
 auto *hub=w.unit("Swordsman",'A',100,100);
 auto &manager=w.logic->skirmishAI(); manager.setStore(&store); manager.newGame(true);
 auto *ai=const_cast<AISkirmishPlayer *>(manager.findAI(owner->getPlayerIndex())); REQUIRE(ai);
 auto r=std::make_shared<AIBuildRequest>(); r->plain=true; r->wall=true; r->wallStartNode=hub->getID(); r->cost=123; r->serial=ai->build.nextSerial++; r->templateName="Swordsman";
 AIBaseBuilder::startPlainRequest(ai->build,r);
 StateHasher before; manager.crc(before);
 XferSave saved; saved.beginBlock(); manager.xfer(saved); saved.endBlock();
 r->wallStartNode=INVALID_ID; r->cost=0;
 XferLoad load(saved.data()); load.beginBlock(); manager.xfer(load); load.endBlock();
 ai=const_cast<AISkirmishPlayer *>(manager.findAI(owner->getPlayerIndex())); REQUIRE(ai);
 REQUIRE(ai->build.pending.size()==1); r=ai->build.pending[0];
 CHECK(r->wall); CHECK(r->wallStartNode==hub->getID()); CHECK(r->cost==123);
 StateHasher restored; manager.crc(restored); CHECK(restored.value()==before.value());
 r->plain=false;
 XferSave malformed; malformed.beginBlock(); manager.xfer(malformed); malformed.endBlock();
 r->plain=true;
 XferLoad bad(malformed.data()); bad.beginBlock();
 CHECK_THROWS_WITH(manager.xfer(bad),"skirmish ai: invalid wall segment request");
 StateHasher after; manager.crc(after); CHECK(after.value()==before.value());
}
TEST_CASE("BFME2 AI wall queue: all difficulties wait for funds then execute a span through the request processor")
{
 for(int difficulty=0;difficulty<4;++difficulty) {
  CAPTURE(difficulty);
  combattest::CombatWorld w(wallDefinitions);
  SkirmishAIStore store(w.w.keys); store.registerBlocks(w.w.fx.env.blocks);
  REQUIRE(w.w.load("SkirmishAIData Walls\n DisableTeamBuilding = Yes\nEnd\n").empty());
  w.logic->settings().buildRulesLoaded=true; w.logic->settings().allowedHeightVariationForBuilding=10; w.logic->settings().maxLineBuildObjects=50;
  auto *hub=w.unit("AIWallHub",'A',300,500); auto &owner=*hub->getControllingPlayer(); owner.setSkirmishDifficulty(difficulty);
  owner.getMoney()->withdraw(owner.getMoney()->countMoney(),false);
  AISkirmishPlayer bot; bot.playerIndex=owner.getPlayerIndex(); bot.store=&store; bot.build.baseBuilderDisabled=true;
  auto r=std::make_shared<AIBuildRequest>(); r->wall=true; r->plain=true; r->wallStartNode=hub->getID(); r->offset={700,500,0}; r->templateName="AIWallHub"; r->priority=80;
  AIBaseBuilder::startPlainRequest(bot.build,r);
  r->offset=*hub->getPosition();
  AIBaseBuilder::update(*w.logic,bot);
  CHECK(r->lastCheck==9); CHECK(r->state==0); CHECK(bot.build.failed==0); CHECK(r->producer==INVALID_ID);
  r->offset={700,500,0};
  AIBaseBuilder::update(*w.logic,bot);
  CHECK(r->lastCheck==2); CHECK(r->state==0); CHECK(bot.build.waitedForMoney==1); REQUIRE(bot.build.pending.size()==1);
  owner.getMoney()->deposit(1000,false);
  AIBaseBuilder::update(*w.logic,bot);
  CHECK(r->lastCheck==0); CHECK(r->state==1); CHECK(bot.build.pending.empty()); REQUIRE(bot.build.inProgress.size()==1);
  CHECK(bot.build.inProgress[0]==r); CHECK(bot.build.executed==1); CHECK(bot.build.failed==0);
  auto *terminal=w.logic->findObjectByID(r->built); REQUIRE(terminal); CHECK(terminal->isKindOfName("WALL_HUB"));
  CHECK(owner.getMoney()->countMoney()==1000u-(std::uint32_t)r->cost);
 }
}

#include "GameClient/MapChunks.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameLogic/ScriptEngine/ScriptConditions.h"
#include <cmath>
TEST_CASE("BFME2 AI wall triggers: median-first geometry preserves aliased cross product and native corner order")
{
 auto vertical=loadWallLibrary({marker("Wall",0,100,100,900),marker("Wall",1,100,300,-900)})[0];
 auto areas=occupancyTriggers(vertical); REQUIRE(areas.size()==2); REQUIRE(areas[0].points.size()==4);
 const float xy[4][2]={{140,260},{140,60},{60,340},{60,140}};
 for(size_t i=0;i<4;++i) {CHECK(areas[0].points[i].x==xy[i][0]);CHECK(areas[0].points[i].y==xy[i][1]);}
 CHECK(ScriptConditions::pointInTrigger(areas[0],130,100));
 CHECK_FALSE(ScriptConditions::pointInTrigger(areas[0],100,100));
 // A horizontal source segment collapses: aliased new Y reads the new X=0.
 auto horizontal=occupancyTriggers(loadWallLibrary({marker("Wall",0,100,100),marker("Wall",1,300,100)})[0]);
 CHECK(horizontal[0].points[0].x==300);CHECK(horizontal[0].points[0].y==100);
 CHECK(horizontal[0].points[1].x==100);CHECK(horizontal[0].points[1].y==100);
 CHECK_FALSE(ScriptConditions::pointInTrigger(horizontal[0],200,100));
 auto coincident=occupancyTriggers(loadWallLibrary({marker("Wall",0,100,100),marker("Wall",1,100,100)})[0]);
 CHECK(std::isnan(coincident[0].points[0].x)); CHECK(std::isnan(coincident[0].points[0].y));
 CHECK_FALSE(ScriptConditions::pointInTrigger(coincident[0],100,100));
}
TEST_CASE("BFME2 AI wall triggers: counters append in first-entry order, ignore exits and wrap")
{
 std::vector<EntryCount> counts;
 recordEntry(counts,4,false); CHECK(counts.empty());
 recordEntry(counts,4,true); recordEntry(counts,2,true); recordEntry(counts,4,true); recordEntry(counts,4,false);
 REQUIRE(counts.size()==2); CHECK(counts[0].player==4); CHECK(counts[0].count==2); CHECK(counts[1].player==2); CHECK(counts[1].count==1);
 counts[0].count=0xFFFFFFFFu; recordEntry(counts,4,true); CHECK(counts[0].count==0);
}
TEST_CASE("BFME2 AI wall triggers: real movement counts each overlapping trigger and reset retires callbacks")
{
 combattest::CombatWorld w;
 auto &manager=w.logic->skirmishAI(); auto &scripts=w.logic->scriptEngine();
 const size_t first=scripts.triggerAreaCount();
 manager.loadWallLibrary({marker("Wall",0,100,100),marker("Wall",1,100,300)});
 REQUIRE(scripts.triggerAreaCount()==first+2);
 CHECK(scripts.triggerAreaAt(first).name.find("AIWallTrigger_")==0);
 CHECK(manager.wallEntryCounts()[0].empty());
 w.frames(1); // object trigger flags use zero as the no-event-frame sentinel
 auto *unit=w.unit("Swordsman",'A',400,400);
 Coord3D inside{130,100,0}, outside{400,400,0}, outside2{401,400,0};
 unit->setPosition(&inside); unit->updateTriggerAreaFlags();
 REQUIRE(manager.wallEntryCounts()[0].size()==1); CHECK(manager.wallEntryCounts()[0][0].count==2);
 unit->updateTriggerAreaFlags(); CHECK(manager.wallEntryCounts()[0][0].count==2);
 unit->setPosition(&outside); unit->updateTriggerAreaFlags();
 unit->setPosition(&outside2); unit->updateTriggerAreaFlags(); // native exit uses previous integer position
 CHECK(manager.wallEntryCounts()[0][0].count==2);
 w.frames(1); // exited trigger slots clear on a later frame's cell movement
 unit->setPosition(&inside); unit->updateTriggerAreaFlags(); CHECK(manager.wallEntryCounts()[0][0].count==4);
 auto *enemy=w.unit("Swordsman",'B',400,400); enemy->setPosition(&inside); enemy->updateTriggerAreaFlags();
 REQUIRE(manager.wallEntryCounts()[0].size()==2); CHECK(manager.wallEntryCounts()[0][1].count==2);
 CHECK(manager.wallEntryCounts()[0][0].player==unit->getControllingPlayer()->getPlayerIndex());
 CHECK(manager.wallEntryCounts()[0][1].player==enemy->getControllingPlayer()->getPlayerIndex());
 manager.reset(); CHECK(manager.wallEntryCounts().empty());
 CHECK(scripts.triggerAreaCount()==first+2); CHECK(scripts.triggerAreaAt(first).points.empty());
 scripts.triggerCallback(first,*unit,true); // disabled callback cannot touch cleared manager state
 manager.loadWallLibrary({marker("NewWall",0,100,100),marker("NewWall",1,100,300)});
 scripts.triggerCallback(first,*unit,true); CHECK(manager.wallEntryCounts()[0].empty());
}
TEST_CASE("BFME2 AI wall triggers: native save restores counters without registering duplicate polygons")
{
 combattest::CombatWorld w;
 auto &manager=w.logic->skirmishAI(); auto &scripts=w.logic->scriptEngine();
 manager.loadWallLibrary({marker("Wall",0,100,100),marker("Wall",1,100,300)});
 auto *unit=w.unit("Swordsman",'A',400,400);
 scripts.triggerCallback(0,*unit,true);
 const size_t triggerCount=scripts.triggerAreaCount();
 StateHasher before; manager.crc(before);
 XferSave saved; saved.beginBlock(); manager.xfer(saved); saved.endBlock();
 scripts.triggerCallback(0,*unit,true);
 StateHasher changed; manager.crc(changed); CHECK_FALSE(changed.value()==before.value());
 // The objects block replaces object-owned polygons before the AI block loads.
 // Map-owned wall callbacks and their indices must survive that phase.
 const int objectTrigger=scripts.addPolygonTrigger(scripts.triggerAreaAt(0), [](Object &, bool) {});
 scripts.beginRuntimeTriggerLoad(); scripts.endRuntimeTriggerLoad();
 CHECK(scripts.triggerAreaCount()==triggerCount);
 scripts.triggerCallback(objectTrigger,*unit,true); // retired object-owned slot is no longer callable
 CHECK_FALSE(scripts.triggerAreaAt(0).points.empty());
 XferLoad load(saved.data()); load.beginBlock(); manager.xfer(load); load.endBlock();
 CHECK(scripts.triggerAreaCount()==triggerCount); CHECK(manager.wallEntryCounts()[0][0].count==1);
 StateHasher restored; manager.crc(restored); CHECK(restored.value()==before.value());
 auto &counts=const_cast<std::vector<std::vector<EntryCount>>&>(manager.wallEntryCounts());
 counts[0].push_back(counts[0][0]);
 XferSave malformed; malformed.beginBlock(); manager.xfer(malformed); malformed.endBlock();
 counts[0].pop_back();
 XferLoad bad(malformed.data()); bad.beginBlock(); CHECK_THROWS_WITH(manager.xfer(bad),"skirmish ai: repeated wall entry player");
 StateHasher after; manager.crc(after); CHECK(after.value()==before.value());
 scripts.triggerCallback(0,*unit,true); CHECK(manager.wallEntryCounts()[0][0].count==2);
}

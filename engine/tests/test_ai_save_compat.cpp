// OpenBFME. GPL-3.0. Historical writers generate these synthetic internal
// snapshots independently of the current reader; see the fixture generator.
#include "doctest.h"
#include "LegacyAISaveTestUtil.h"
#include "LegacyAISaveFixtures.h"
#include "Common/StateHash.h"
namespace {
std::vector<std::uint8_t> decode(std::string_view hex)
{
 std::vector<std::uint8_t> bytes;
 auto digit=[](char c) { return c<='9' ? c-'0' : c-'a'+10; };
 for(size_t i=0;i<hex.size();i+=2) bytes.push_back((std::uint8_t)(digit(hex[i])*16+digit(hex[i+1])));
 return bytes;
}
void load(legacy_ai_save::Scenario &s,const std::vector<std::uint8_t> &bytes)
{
 XferLoad x(bytes); x.beginBlock(); s.manager().xfer(x); x.endBlock(); s.refresh();
}
}
TEST_CASE("AI save compatibility: historical v1 through v9 writers load populated state and default only absent fields")
{
 for(const auto &fixture:legacy_ai_save::fixtures) {
  CAPTURE(fixture.version);
  const bool hasUpgrades=fixture.version>=2;
  const bool hasCaptureHistory=fixture.version>=3;
  const bool hasPlainRequests=fixture.version>=4;
  const bool hasSpatialState=fixture.version>=5;
  const bool hasRawThreat=fixture.version>=6;
  const bool hasPredefinedUnits=fixture.version>=7;
  const bool hasWallRequests=fixture.version>=9;
  legacy_ai_save::Scenario s;
  auto bytes=decode(fixture.hex);
  REQUIRE(bytes[8]==fixture.version);
  load(s,bytes);
  auto &manager=s.manager(); const auto &ai=*s.ai;
  REQUIRE(ai.build.pending.size()==1); const auto &request=*ai.build.pending[0];
  CHECK(request.name=="LegacyRequest"); CHECK(request.priority==17); CHECK(request.offset.x==231); CHECK(request.serial==1);
  CHECK(request.cost==(hasUpgrades ? 99 : 0));
  CHECK(request.plain==(hasPlainRequests)); CHECK(request.wall==(hasWallRequests));
  CHECK(request.wallStartNode==(hasWallRequests ? s.unit->getID() : INVALID_ID));
  CHECK(ai.build.scienceBuilder.lastExpansionFrame==(hasUpgrades ? 4321u : 0u));
  CHECK(manager.record(s.owner->getPlayerIndex())->reserve()==(hasUpgrades ? 77u : 0u));
  CHECK(manager.record(s.owner->getPlayerIndex())->positions().members().size()==(hasSpatialState ? 1u : 0u));
  REQUIRE(manager.spellBookAssistTimers().size()==1); CHECK(manager.spellBookAssistTimers()[0].second==111);
  CHECK(manager.spellBookArmyBreakerTimers().size()==(hasSpatialState ? 1u : 0u));
  CHECK(manager.captureFlags().size()==(hasCaptureHistory ? 1u : 0u));
  REQUIRE(ai.brain.targets.size()==1); CHECK(ai.brain.targets[0].frame==37); CHECK(ai.brain.targets[0].position.y==220);
  CHECK(ai.brain.targets[0].defenseSize==(hasSpatialState ? 2 : 0));
  REQUIRE(ai.brain.tactics.size()==1); REQUIRE(ai.brain.tactics[0].teams.size()==1);
  const auto &team=ai.brain.tactics[0].teams[0]; CHECK(team.name=="LegacyTeam"); CHECK(team.members==std::vector<ObjectID>{s.unit->getID()});
  CHECK(team.hasRecruitOrigin==(hasSpatialState)); CHECK(team.recruitOrigin.x==(hasSpatialState ? 250 : 0));
  CHECK(team.remainingUnits==(hasPredefinedUnits ? std::vector<std::string>{"Swordsman"} : std::vector<std::string>{}));
  REQUIRE(ai.brain.attackedAt.size()==1); CHECK(ai.brain.attackedAt[0].frame==91); CHECK(ai.brain.attackedAt[0].enemies.v[0]==123);
  CHECK(ai.brain.attackedAt[0].nativeThreat==(hasRawThreat ? 456 : 0));
  REQUIRE(manager.wallEntryCounts().size()==1); CHECK(manager.wallEntryCounts()[0].empty());
  // Convert to the current writer and reload: every preserved/defaulted field,
  // shared request reference and spatial registration must hash identically.
  StateHasher before; manager.crc(before);
  XferSave current; current.beginBlock(); manager.xfer(current); current.endBlock(); CHECK(current.data()[8]==10);
  s.ai->build.pending[0]->priority=999;
  load(s,current.data());
  StateHasher after; manager.crc(after); CHECK(after.value()==before.value());
 }
}
TEST_CASE("AI save compatibility: farm squad storage aliases retain active and cooldown guards")
{
 for (bool running : {false, true}) {
  CAPTURE(running);
  legacy_ai_save::Scenario s;
  // The named-counter map has the same encoding in every supported layout.
  // Exercise historical key text independently of the current runtime names.
  s.ai->brain.namedCounters = {{"FarmKillSquad::IsRunning", running ? 1 : 0},
   {"FarmKillSquad::NextLogicFrame", 12345}};
  XferSave old; old.beginBlock(); s.manager().xfer(old); old.endBlock();
  s.ai->brain.namedCounters.clear();
  load(s, old.data());
  CHECK(s.ai->brain.namedCounters.at("AIFarmKillSquad_IsRunning") == (running ? 1 : 0));
  CHECK(s.ai->brain.namedCounters.at("AIFarmKillSquad_FrameNextRun") == 12345);
  CHECK(s.ai->brain.namedCounters.count("FarmKillSquad::IsRunning") == 0);
  CHECK(s.ai->brain.namedCounters.count("FarmKillSquad::NextLogicFrame") == 0);
  StateHasher before; s.manager().crc(before);
  XferSave current; current.beginBlock(); s.manager().xfer(current); current.endBlock();
  load(s, current.data());
  StateHasher after; s.manager().crc(after);
  CHECK(after.value() == before.value());
 }
}
TEST_CASE("AI save compatibility: conflicting farm squad aliases fail transactionally")
{
 legacy_ai_save::Scenario s;
 s.ai->brain.namedCounters = {{"FarmKillSquad::IsRunning", 1}, {"AIFarmKillSquad_IsRunning", 0}};
 XferSave invalid; invalid.beginBlock(); s.manager().xfer(invalid); invalid.endBlock();
 StateHasher before; s.manager().crc(before);
 CHECK_THROWS_WITH(load(s, invalid.data()), "skirmish ai: conflicting farm squad counter aliases");
 StateHasher after; s.manager().crc(after);
 CHECK(after.value() == before.value());
}

TEST_CASE("AI save compatibility: truncated legacy snapshots fail transactionally")
{
 for(const auto &fixture:legacy_ai_save::fixtures) {
  CAPTURE(fixture.version);
  legacy_ai_save::Scenario s;
  auto bytes=decode(fixture.hex); bytes.pop_back();
  for(int i=0;i<4;++i) bytes[4+i]=(std::uint8_t)(bytes.size()>>(8*i)); // keep framing valid so the reader reaches the truncated payload
  StateHasher before; s.manager().crc(before);
  CHECK_THROWS_AS(load(s,bytes),XferException);
  StateHasher after; s.manager().crc(after); CHECK(after.value()==before.value());
 }
}

#include "GameLogic/Map/TerrainTrees.h"
TEST_CASE("AI save compatibility: pre-overhaul terrain v1 reconstructs unselected groups and round-trips as v2")
{
 combattest::CombatWorld world;
 REQUIRE(world.w.load("Object LegacyTree\nEnd\n").empty());
 auto *tt=const_cast<ThingTemplate *>(world.w.things.findTemplate("LegacyTree")); REQUIRE(tt);
 ThingTemplate::Nugget draw; draw.name="W3DTreeDraw"; tt->moduleList(MODULETYPE_DRAW).push_back(draw);
 auto &trees=world.logic->trees(); trees.setExtent(0,0,1000,1000);
 REQUIRE(trees.addTree(*tt,{100,200,0},1)); REQUIRE(trees.addTree(*tt,{110,200,0},2));
 // Independent historical v1 schema: version, map count/next index, frame,
 // topple speed and changed-record count. No spatial-group trailer existed.
 XferSave legacy; legacy.beginBlock(); XferVersion v{1,1}; legacy.xferVersion(v);
 unsigned count=2; int next=2; unsigned frame=55; float speed=2.5f; unsigned changed=0;
 legacy.xferUnsignedInt(&count); legacy.xferInt(&next); legacy.xferUnsignedInt(&frame); legacy.xferReal(&speed); legacy.xferUnsignedInt(&changed); legacy.endBlock();
 trees.setToppleSpeed(9);
 XferLoad old(legacy.data()); old.beginBlock(); trees.xfer(old); old.endBlock();
 REQUIRE(trees.records().size()==2); CHECK(trees.records()[0].position.x==100); CHECK(trees.records()[1].position.x==110);
 XferSave current; current.beginBlock(); trees.xfer(current); current.endBlock();
 XferLoad fields(current.data()); fields.beginBlock(); XferVersion currentVersion{1,2}; fields.xferVersion(currentVersion); CHECK(currentVersion.current==2);
 fields.xferUnsignedInt(&count); fields.xferInt(&next); fields.xferUnsignedInt(&frame); fields.xferReal(&speed); fields.xferUnsignedInt(&changed);
 CHECK(count==2); CHECK(next==2); CHECK(frame==55); CHECK(speed==2.5f); CHECK(changed==0);
 AIPositionSampler groups; groups.xfer(fields); bool selected=true; fields.xferBool(&selected); fields.endBlock();
 CHECK(groups.members()==std::vector<ObjectID>{0,1}); REQUIRE(groups.groups().size()==1); CHECK_FALSE(groups.groups()[0].selected); CHECK_FALSE(selected);
 XferLoad again(current.data()); again.beginBlock(); trees.xfer(again); again.endBlock();
 XferSave roundTrip; roundTrip.beginBlock(); trees.xfer(roundTrip); roundTrip.endBlock(); CHECK(roundTrip.data()==current.data());
}

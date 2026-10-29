// OpenBFME. GPL-3.0. Synthetic state shared by the historical-writer fixture
// generator and compatibility tests. No game data or retail save bytes.
#pragma once
#include "CombatTestUtil.h"
#include "Common/MapObject.h"
#include "Common/XferSave.h"
#include "Common/XferLoad.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"
namespace legacy_ai_save {
struct Scenario {
 combattest::CombatWorld world;
 SkirmishAIStore store{world.w.keys};
 Player *owner=nullptr;
 AISkirmishPlayer *ai=nullptr;
 Object *unit=nullptr;
 SkirmishAIManager &manager() { return world.logic->skirmishAI(); }
 Scenario() {
  store.registerBlocks(world.w.fx.env.blocks);
  REQUIRE(world.w.load("SkirmishAIData Legacy\n DisableTeamBuilding = Yes\nEnd\nChildObject LegacySite Swordsman\n KindOf = STRUCTURE IMMOBILE CAPTUREFLAG\nEnd\n").empty());
  owner=world.players.findPlayerWithName("Alice"); owner->setPlayerType(PLAYER_COMPUTER,true);
  unit=world.unit("Swordsman",'A',100,200);
  world.unit("LegacySite",'A',400,500);
  manager().setStore(&store);
  std::vector<MapObject> markers;
  for(int i=0;i<2;++i) {
   MapObject m; m.m_location={100.0f,100.0f+200.0f*i,0};
   m.m_properties.setInt("GenericAIObjectID",23); m.m_properties.setInt("GenericAIObjectType",0);
   m.m_properties.setAsciiString("GenericAIObjectName","LegacyWall"); m.m_properties.setInt("GenericAIObjectWallHubNumber",i);
   markers.push_back(m);
  }
  manager().loadWallLibrary(markers); manager().newGame(true);
  refresh();
  auto r=std::make_shared<AIBuildRequest>();
  r->templateName="Swordsman"; r->name="LegacyRequest"; r->priority=17; r->offset={231,233,0}; r->serial=ai->build.nextSerial++;
  r->plain=true; r->wall=true; r->wallStartNode=unit->getID(); r->cost=99;
  AIBaseBuilder::startPlainRequest(ai->build,r);
  ai->build.scienceBuilder.lastExpansionFrame=4321;
  manager().record(owner->getPlayerIndex())->setReserve(77);
  manager().spellBookAssistTimers().push_back({owner->getPlayerIndex(),111});
  manager().spellBookArmyBreakerTimers().push_back({owner->getPlayerIndex(),222});
  AITarget target; target.id=1; target.type=0; target.frame=37; target.position={110,220,0}; target.defenseSize=2;
  ai->brain.targets.push_back(target); ai->brain.nextTargetId=2;
  AITactic tactic; tactic.kind="SimpleAttack"; tactic.kindIndex=0; tactic.id=1;
  AITacticTeam team; team.name="LegacyTeam"; team.type=0; team.minimum=1; team.maximum=3; team.priority=20;
  team.members.push_back(unit->getID()); team.hasRecruitOrigin=true; team.recruitOrigin={250,350,0}; team.remainingUnits={"Swordsman"};
  tactic.teams.push_back(team); ai->brain.tactics.push_back(tactic); ai->brain.nextTacticId=2;
  AIAttackedAt attack; attack.id=1; attack.position={120,240,0}; attack.frame=91; attack.radius=300; attack.nativeThreat=456;
  attack.enemies.v[0]=123; ai->brain.attackedAt.push_back(attack); manager().attackedAtIds()=2;
 }
 void refresh() { ai=const_cast<AISkirmishPlayer *>(manager().findAI(owner->getPlayerIndex())); REQUIRE(ai); }
};
}

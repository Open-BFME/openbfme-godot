// OpenBFME. GPL-3.0. Ported policy from BFME2Lua siege_damage/live_data.
#include "GameLogic/WallBalance.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/WallNavigationRegions.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/BuildPlacement.h"
#include "GameLogic/WallSpan.h"
#include "GameLogic/Construction.h"
#include "GameLogic/Module/TerrainResourceBehavior.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameClient/ControlBarCommands.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Player.h"
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <set>
namespace {
bool kind(const ThingTemplate &tt, const char *name) {
 const int bit = ObjectTemplateInfoBuilder::kindOfIndex(name);
 return bit >= 0 && MaskTest(ObjectTemplateInfoBuilder::build(tt).kindOf, (unsigned)bit);
}
}
bool WallBalance::wall(const Object &o) {
 return o.isKindOfName("WALL_SEGMENT") || o.isKindOfName("WALL_HUB") || o.isKindOfName("WALL_UPGRADE") || o.isKindOfName("DEFENSIVE_WALL") || o.isKindOfName("WALL_GATE");
}
int WallBalance::healthCategory(const Object &o) {
 if (!wall(o)) return -1;
 if (o.isKindOfName("WALL_GATE")) return 2;
 return o.isKindOfName("WALL_HUB") ? 1 : 0;
}
int WallBalance::costCategory(const ThingTemplate &tt) {
 if (kind(tt,"WALL_HUB")) {
  if (tt.getName().find("Expansion") != std::string::npos) return 3;
  return tt.getName().find("Outer") != std::string::npos ? 2 : 1;
 }
 if (kind(tt,"WALL_UPGRADE")) return 3;
 return kind(tt,"WALL_SEGMENT") || kind(tt,"DEFENSIVE_WALL") || kind(tt,"WALL_GATE") ? 0 : -1;
}
bool WallBalance::attackerAllowed(const Object &target, const Object &attacker) {
 const auto &r = target.logic().settings().wallBalance;
 if (!r.enabled) return true;
 const bool fortress = target.isKindOfName("COMMANDCENTER") && target.isKindOfName("STRUCTURE");
 const bool expansion = target.isKindOfName("STRUCTURE") && (target.isKindOfName("BASE_DEFENSE_FOUNDATION") || (target.isKindOfName("FS_BASE_DEFENSE") && target.getTemplate()->getName().find("Expansion") != std::string::npos));
 const int mode = fortress || expansion ? r.fortressDamage : wall(target) ? r.wallDamage : 0;
 return mode == 0 || attacker.isKindOfName(mode == 1 ? "CAN_ATTACK_WALLS" : "SIEGEENGINE");
}

struct WallEnclosureCache {
 std::uint64_t signature = 0;
 std::map<int,WallNavigationRegions::Regions> regions;
 int width = 0, height = 0;
 std::set<ObjectID> startingFortresses;
};
namespace {
bool resource(const ThingTemplate &tt) {
 for (const auto &n : tt.behaviorModules().nuggets())
  if (auto *d = dynamic_cast<const TerrainResourceBehaviorModuleData *>(n.data.get()); d && d->m_maxIncome > 0) return true;
 return false;
}
bool footprintContains(const BuildPlacement::Footprint &f,float x,float y) {
 const float dx=x-f.x,dy=y-f.y;
 if(f.type!=0) return dx*dx+dy*dy<=f.major*f.major;
 const float c=std::cos(f.angle),s=std::sin(f.angle);
 return std::abs(dx*c+dy*s)<=f.major && std::abs(-dx*s+dy*c)<=f.minor;
}
}
bool WallBalance::enclosed(GameLogic &logic,const Player &owner,const Coord3D &point,bool perimeter) {
 auto &r = logic.settings().wallBalance;
 auto *ai=logic.aiWorld(); if(!ai || !ai->pathfinder().terrainView()) return false;
 auto &pf=ai->pathfinder(); const auto *extent=pf.getExtent();
 const int w=extent->x+1,h=extent->y+1;
 if(w<=0||h<=0||(long long)w*h>4000000) return false;
 if(!r.enclosureCache) r.enclosureCache=std::make_shared<WallEnclosureCache>();
 auto &cache=*r.enclosureCache;
 std::uint64_t signature=1469598103934665603ULL;
 auto hash=[&](std::uint64_t n){signature=(signature^n)*1099511628211ULL;};
 hash(w);hash(h);hash(logic.getFrame());
 bool ownWall=false;
 for(Object *o=logic.getFirstObject();o;o=o->getNextObject()) {
  if(!o->isKindOfName("STRUCTURE") && !wall(*o)) continue;
  ownWall |= wall(*o) && !o->isUnderConstruction() && !o->isEffectivelyDead() && !o->isDestroyed() && o->getControllingPlayer()==&owner;
  hash(o->getID());hash(o->isDestroyed());hash(o->isInWorld());hash(o->isEffectivelyDead());hash(o->isUnderConstruction());
  hash(o->getControllingPlayer()?o->getControllingPlayer()->getPlayerIndex()+1:0);
  hash((std::int64_t)(o->getPosition()->x*100));hash((std::int64_t)(o->getPosition()->y*100));hash((std::int64_t)(o->getOrientation()*10000));
 }
 if(!ownWall)return false;
 if(cache.signature!=signature){cache.signature=signature;cache.regions.clear();cache.width=w;cache.height=h;}
 const int player=owner.getPlayerIndex();
 auto found=cache.regions.find(player);
 if(found==cache.regions.end()) {
  std::vector<unsigned char> passable(w*h),natural(w*h),own(w*h);
  for(int y=0;y<h;++y)for(int x=0;x<w;++x){
   const auto *c=pf.getCell(LAYER_GROUND,x,y);
   passable[y*w+x]=c && (c->getType()==PathfindCell::CELL_CLEAR||c->getType()==PathfindCell::CELL_RUBBLE);
  }
  natural=passable; bool haveOwn=false;
  const auto *terrain=pf.terrainView();
  for(Object *o=logic.getFirstObject();o;o=o->getNextObject()) {
   if(!wall(*o)||o->isDestroyed()||o->isEffectivelyDead()||o->isUnderConstruction()||!o->isInWorld())continue;
   std::vector<BuildPlacement::Footprint> footprints;
   BuildPlacement::footprintsOf(logic,*o->getTemplate(),*o->getPosition(),o->getOrientation(),footprints);
   const bool owned=o->getControllingPlayer()==&owner; haveOwn|=owned;
   for(const auto &f:footprints){
    const float radius=std::max(f.major,f.minor)*1.5f;
    for(int y=std::max(0,(int)std::floor((f.y-radius)/10));y<=std::min(h-1,(int)std::ceil((f.y+radius)/10));++y)
     for(int x=std::max(0,(int)std::floor((f.x-radius)/10));x<=std::min(w-1,(int)std::ceil((f.x+radius)/10));++x){
      const float wx=(x+.5f)*10,wy=(y+.5f)*10;
      if(!footprintContains(f,wx,wy))continue;
      passable[y*w+x]=0;own[y*w+x]|=owned;
      float water=0,ground=0;
      natural[y*w+x]=!terrain->isCliffCell(wx,wy)&&!terrain->impassableToPlayers(wx,wy)&&!terrain->isUnderwater(wx,wy,&water,&ground);
     }
   }
  }
  if(!haveOwn)return false;
  WallNavigationRegions::Regions regions;regions.build(w,h,passable,natural,own);
  found=cache.regions.emplace(player,std::move(regions)).first;
 }
 const auto &regions=found->second;const int x=(int)std::floor(point.x/10),y=(int)std::floor(point.y/10);
 if(!perimeter)return regions.enclosed(x,y);
 for(int radius=0;radius<=16;++radius){
  bool any=false,inside=false,outside=false;
  for(int dy=-radius;dy<=radius;++dy)for(int dx=-radius;dx<=radius;++dx){
   if(std::max(std::abs(dx),std::abs(dy))!=radius)continue;
   const int px=x+dx,py=y+dy;if(px<0||py<0||px>=w||py>=h)continue;
   if(!regions.labels[py*w+px])continue;
   any=true;if(regions.enclosed(px,py))inside=true;else outside=true;
  }
  if(any)return inside&&!outside;
 }
 return false;
}
bool WallBalance::placementAllowed(GameLogic &logic,const ThingTemplate &tt,const Coord3D &point,const Object *builder,const Player *owner) {
 const auto &r=logic.settings().wallBalance;
 if(!r.enabled||!r.requireEnclosed||!kind(tt,"STRUCTURE")||costCategory(tt)>=0||resource(tt)||kind(tt,"COMMANDCENTER")||kind(tt,"BASE_FOUNDATION")||kind(tt,"BASE_DEFENSE_FOUNDATION")||(kind(tt,"FS_BASE_DEFENSE")&&tt.getName().find("Expansion")!=std::string::npos))return true;
 if(!owner && builder)owner=builder->getControllingPlayer();
 if(!owner)return false;
 if(owner->getPlayerType()==PLAYER_COMPUTER || owner->getSide()=="Wild" || owner->getSide()=="Goblins")return true;
 return enclosed(logic,*owner,point);
}
void WallBalance::createStartingWalls(GameLogic &logic) {
 const auto &r=logic.settings().wallBalance;if(!r.enabled||(!r.startingWalls&&!r.requireEnclosed)||logic.getFrame()>300)return;
 if(!r.enclosureCache)r.enclosureCache=std::make_shared<WallEnclosureCache>();
 std::vector<ObjectID> fortresses;
 for(Object *o=logic.getFirstObject();o;o=o->getNextObject())
  if(o->isKindOfName("COMMANDCENTER")&&o->isKindOfName("STRUCTURE")&&!o->isUnderConstruction()&&o->getControllingPlayer())fortresses.push_back(o->getID());
 for(ObjectID id:fortresses){
  Object *fort=logic.findObjectByID(id);auto *owner=fort->getControllingPlayer();
  if(r.enclosureCache->startingFortresses.count(id))continue;
  if(!r.startingWalls && owner->getPlayerType()!=PLAYER_COMPUTER)continue;
  auto definition=r.startingHubs.find(owner->getSide());if(definition==r.startingHubs.end())continue;
  r.enclosureCache->startingFortresses.insert(id);
  const ThingTemplate *hubTemplate=logic.things().findTemplate(definition->second);
  if(!hubTemplate){logic.reportError("Starting walls: unknown hub "+definition->second);continue;}
  float radius=r.startingRadius;float lx,ly,hx,hy;
  if(logic.aiWorld()&&logic.aiWorld()->pathfinder().terrainView()){
   logic.aiWorld()->pathfinder().terrainView()->getExtent(lx,ly,hx,hy);
   radius=std::min(radius,std::min({fort->getPosition()->x-lx,hx-fort->getPosition()->x,fort->getPosition()->y-ly,hy-fort->getPosition()->y})-60);
  }
  if(radius<150){logic.reportError("Starting walls: insufficient map space");continue;}
  const float half=std::asin(std::min(.9f,r.startingSpacing/(2*radius)));
  std::vector<float> angles;for(int side=0;side<4;++side)for(float sign:{-1.0f,1.0f}){
   float a=side*1.57079632679f+sign*half;if(a<0)a+=6.28318530718f;angles.push_back(a);
  }
  std::sort(angles.begin(),angles.end());std::vector<ObjectID> hubs;
  for(float a:angles){a+=fort->getOrientation();Coord3D p{fort->getPosition()->x+radius*std::cos(a),fort->getPosition()->y+radius*std::sin(a),0};
   auto *hub=Construction::buildObjectNow(logic,fort,*hubTemplate,p,a,*owner);if(hub){hub->setBuildCostPaid(0);hubs.push_back(hub->getID());}
  }
  if(hubs.size()!=8){logic.reportError("Starting walls: unable to place eight hubs");continue;}
  std::vector<ObjectID> pieces;
  for(size_t i=0;i<hubs.size();++i){auto *a=logic.findObjectByID(hubs[i]),*b=logic.findObjectByID(hubs[(i+1)%hubs.size()]);if(!a||!b)continue;
   auto *wh=WallHubBehavior::find(*a,0);if(!wh)continue;
   std::vector<ObjectID> made; if(!WallSpan::build(logic,*a,*a->getPosition(),*b->getPosition(),*owner,wh->data()->m_options,&made,true))logic.reportError("Starting walls: span failed");
   pieces.insert(pieces.end(),made.begin(),made.end());
  }
  auto gate=r.startingGates.find(owner->getSide());if(gate==r.startingGates.end()||!TheCommandStore)continue;
  const auto *button=TheCommandStore->findCommandButton(gate->second);if(!button||button->m_upgradeName.empty())continue;
  for(float offset:{0.0f,3.14159265359f}){
   const float angle=fort->getOrientation()+offset;const float x=fort->getPosition()->x+radius*std::cos(half)*std::cos(angle),y=fort->getPosition()->y+radius*std::cos(half)*std::sin(angle);
   Object *nearest=nullptr;float best=radius*radius;
   for(ObjectID piece:pieces)if(Object *o=logic.findObjectByID(piece);o&&!o->isDestroyed()&&o->isKindOfName("WALL_SEGMENT")){
    const float dx=o->getPosition()->x-x,dy=o->getPosition()->y-y;if(dx*dx+dy*dy<best){best=dx*dx+dy*dy;nearest=o;}
   }
   if(nearest)nearest->giveUpgrade(button->m_upgradeName);
  }
 }
}

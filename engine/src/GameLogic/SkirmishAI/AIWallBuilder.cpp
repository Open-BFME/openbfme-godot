// OpenBFME. GPL-3.0.
#include "GameLogic/SkirmishAI/AIWallBuilder.h"
#include "Common/MapObject.h"
#include "GameClient/MapChunks.h"
#include "Common/Player.h"
#include "Common/Money.h"
#include "GameLogic/WallSpan.h"
#include "Common/RandomValue.h"
#include "Common/StateHash.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SkirmishAI/AIBaseBuilder.h"
#include <algorithm>
#include <stdexcept>
namespace AIWallBuilder {
std::vector<Plan> loadWallLibrary(const std::vector<MapObject> &objects)
{
 // BFME2 MapObjectReadRva0030DC85.cpp derives runtime flag 0x20 from the
 // typed GenericAIObjectID property. It is NOT the serialized bridge-point flag 0x20.
 // AIWallBuilder::loadWallLibrary RVA 0x4E9C13; node insertion RVA 0x4EB794
 // retains the first duplicate index (read-only BFME2 binary confirmation).
 std::vector<Plan> plans;
 for(const auto &object:objects) {
  const auto *marker=object.m_properties.find("GenericAIObjectID");
  if(!marker || marker->type!=Dict::DICT_INT || object.m_properties.getInt("GenericAIObjectType")!=0) continue;
  const std::string name=object.m_properties.getAsciiString("GenericAIObjectName");
  auto plan=std::find_if(plans.begin(),plans.end(),[&](const Plan &p){return p.name==name;});
  if(plan==plans.end()) { plans.push_back(Plan{}); plan=plans.end()-1; plan->name=name; }
  const auto index=(std::uint32_t)object.m_properties.getInt("GenericAIObjectWallHubNumber");
  if(std::any_of(plan->nodes.begin(),plan->nodes.end(),[&](const Node &node){return node.index==index;})) continue;
  plan->nodes.push_back(Node{index,object.m_location});
 }
 // AIWall::init RVA 0x4EBEB7: unsigned key order, upper median and exact 0..n-1 sequence.
 plans.erase(std::remove_if(plans.begin(),plans.end(),[](Plan &plan){
  if(plan.nodes.size()<2) return true;
  std::sort(plan.nodes.begin(),plan.nodes.end(),[](const Node &a,const Node &b){return a.index<b.index;});
  for(size_t i=0;i<plan.nodes.size();++i) if(plan.nodes[i].index!=i) return true;
  plan.selected=(std::uint32_t)(plan.nodes.size()/2);
  return false;
 }),plans.end());
 return plans;
}

std::vector<TriggerArea> occupancyTriggers(const Plan &plan)
{
 // AIWall::rva004EBB58, 0x4EBB58..4EBDF9. Normalize has no zero-length
 // guard. The native cross product aliases its input: new Y reads new X.
 if(plan.selected>=plan.nodes.size()) throw std::logic_error("AIWall: invalid trigger plan");
 std::vector<TriggerArea> triggers;
 Coord3D previous=plan.nodes[plan.selected].position;
 for(const auto &node:plan.nodes) {
  const auto &current=node.position;
  float x=SimMath::subf32(previous.x,current.x), y=SimMath::subf32(previous.y,current.y);
  const float squared=SimMath::addf32(SimMath::addf32(SimMath::mulf32(x,x),SimMath::mulf32(y,y)),0.0f);
  const float length=SimMath::sqrtPC24(squared);
  const float scale=SimMath::divf32(1.0f,length);
  x=SimMath::mulf32(x,scale); y=SimMath::mulf32(y,scale);
  float z=SimMath::mulf32(0.0f,scale);
  x=SimMath::subf32(SimMath::mulf32(y,1.0f),SimMath::mulf32(z,0.0f));
  y=SimMath::subf32(SimMath::mulf32(z,0.0f),SimMath::mulf32(x,1.0f));
  x=SimMath::mulf32(x,40.0f); y=SimMath::mulf32(y,40.0f);
  TriggerArea area;
  area.points.push_back({SimMath::addf32(previous.x,x),SimMath::addf32(previous.y,y)});
  area.points.push_back({SimMath::addf32(current.x,x),SimMath::addf32(current.y,y)});
  x=SimMath::mulf32(x,-1.0f); y=SimMath::mulf32(y,-1.0f);
  area.points.push_back({SimMath::addf32(previous.x,x),SimMath::addf32(previous.y,y)});
  area.points.push_back({SimMath::addf32(current.x,x),SimMath::addf32(current.y,y)});
  triggers.push_back(std::move(area));
  previous=current;
 }
 return triggers;
}
void recordEntry(std::vector<EntryCount> &counts,int player,bool entered)
{
 // AIWall::informInstanceOnTriggerEntered 0x4EBAF7: exits do nothing;
 // append each player's first entry and increment with native 32-bit wrapping.
 if(!entered) return;
 auto entry=std::find_if(counts.begin(),counts.end(),[&](const EntryCount &e){return e.player==player;});
 if(entry==counts.end()) { counts.push_back({player,0}); entry=counts.end()-1; }
 ++entry->count;
}

bool nearCommandCenters(const Plan &plan,const std::vector<Coord3D> &centers)
{
 // AIWall::rva004EB7CC, source SSE order z*z + y*y + x*x; all centers are visited.
 if(centers.empty()) return false;
 for(const auto &node:plan.nodes) {
  bool far=true;
  for(const auto &center:centers) {
   const float x=SimMath::subf32(center.x,node.position.x), y=SimMath::subf32(center.y,node.position.y), z=SimMath::subf32(center.z,node.position.z);
   const float squared=SimMath::addf32(SimMath::addf32(SimMath::mulf32(z,z),SimMath::mulf32(y,y)),SimMath::mulf32(x,x));
   if(squared<2250000.0f) far=false;
  }
  if(far) return false;
 }
 return true;
}
std::vector<Coord3D> commandCenters(GameLogic &logic,const Player &owner)
{
 // PlayerRva002AF614.cpp / callback 0x2AF5EF: COMMANDCENTER only, no alive filter.
 std::vector<Coord3D> positions;
 for(Object *object=logic.getFirstObject();object;object=object->getNextObject())
  if(object->getControllingPlayer()==&owner && object->isKindOfName("COMMANDCENTER")) positions.push_back(*object->getPosition());
 return positions;
}
Choice choosePlan(GameLogicRandom &random,const std::vector<int> &eligible)
{
 // AIWallBuilder.cpp RVA 0x4E9DA8 / 0x4E98B4: single candidate skips the integer draw.
 if(eligible.empty()) return Choice{};
 const auto pick=eligible.size()>1 ? (size_t)random.getValue(0,(int)eligible.size()-1,"AIWallBuilder.cpp",165) : 0;
 return Choice{eligible[pick],random.getValueReal(70,110,"AIWallBuilder.cpp",180)};
}
Coord3D calcGatePosition(GameLogicRandom &random,const Plan &plan)
{
 // AIWall.cpp RVA 0x4EAFBD: endpoints deterministic; interior uses real >0.5 at line 560.
 if(plan.nodes.size()<2 || plan.selected>=plan.nodes.size()) throw std::logic_error("AIWall: invalid gate plan");
 const auto index=plan.selected;
 const auto other=index==0 ? 1 : index==plan.nodes.size()-1 ? index-1 :
  random.getValueReal(0,1,"AIWall.cpp",560)>0.5f ? index+1 : index-1;
 const Coord3D &a=plan.nodes[index].position, &b=plan.nodes[other].position;
 return Coord3D{SimMath::mulf32(SimMath::addf32(a.x,b.x),0.5f),SimMath::mulf32(SimMath::addf32(a.y,b.y),0.5f),SimMath::mulf32(SimMath::addf32(a.z,b.z),0.5f)};
}
Construction activate(const Plan &plan,int owner,float priority,const std::string &hubTemplate,AIBuildState &builder)
{
 // AIWall::activate RVA 0x4EAF18: set both states to 1, set every order's
 // priority/template, register only the median, and create gate priority +10.
 if(plan.nodes.size()<2 || plan.selected>=plan.nodes.size()) throw std::logic_error("AIWall: invalid construction plan");
 Construction wall;
 wall.owner=owner;
 wall.leftState=wall.rightState=1;
 wall.leftIndex=wall.rightIndex=(int)plan.selected;
 wall.gatePriority=SimMath::addf32(priority,10.0f);
 for(const auto &node:plan.nodes) {
  auto request=std::make_shared<AIBuildRequest>();
  request->plain=true;
  request->wall=true;
  request->templateName=hubTemplate;
  request->priority=priority;
  request->offset=node.position;
  request->serial=builder.nextSerial++;
  wall.orders.push_back(request);
 }
 AIBaseBuilder::startPlainRequest(builder,wall.orders[plan.selected]);
 return wall;
}
void updateDirection(GameLogic &logic,Construction &wall,const Plan &plan,AIBuildState &builder,bool left)
{
 // AIWall::updateState RVA 0x4EB405. State 1 only observes the median;
 // it does not also perform state 2's work during the same update.
 if(wall.orders.size()!=plan.nodes.size() || plan.selected>=wall.orders.size()) throw std::logic_error("AIWall: invalid construction orders");
 int &state=left ? wall.leftState : wall.rightState;
 int &index=left ? wall.leftIndex : wall.rightIndex;
 if(state==1) {
  const int selectedState=wall.orders[plan.selected]->state;
  if(selectedState==1) state=2;
  else if(selectedState==3) state=4;
 } else if(state==2) {
  if(index>=(int)wall.orders.size()) throw std::logic_error("AIWall: invalid construction cursor");
  auto &current=wall.orders[index<0 ? plan.selected : (size_t)index];
  if(current->state==3) state=4;
  else if(current->state!=0) {
   Object *built=logic.findObjectByID(current->built);
   if(built) {
    if(built->getConstructionPercent()==-1.0f) {
     current->state=2;
     index+=left ? -1 : 1;
     if(index>=0 && (size_t)index<wall.orders.size()) {
      auto &next=wall.orders[(size_t)index];
      next->wallStartNode=built->getID();
      AIBaseBuilder::startPlainRequest(builder,next);
     } else state=4;
    }
   } else {
    AIBaseBuilder::leaveQueues(builder,current.get());
    AIBaseBuilder::startPlainRequest(builder,current);
   }
  }
 }
}
bool readyForGate(const Construction &wall,const Plan &plan,bool gateHasProducer)
{
 // AIWall::update RVA 0x4EBA96 uses unsigned comparisons, including
 // selected==0's wrapping selected-1. Keep that observable endpoint behavior.
 return !gateHasProducer &&
  ((std::uint32_t)wall.leftIndex<plan.selected-1u || wall.leftState==4) &&
  ((std::uint32_t)wall.rightIndex>plan.selected+1u || wall.rightState==4);
}
int checkSegment(GameLogic &logic,AIBuildRequest &request,Player &owner)
{
 // AIBuildableWallSegment::canMake RVA 0x596E15. Caller handles unset
 // startNode with the base structure path; the quote uses option=false.
 Object *start=logic.findObjectByID(request.wallStartNode);
 if(!start) throw std::logic_error("AIWall: missing start hub for span quote");
 WallSpan::Plan quote;
 if(!WallSpan::plan(logic,*start,*start->getPosition(),request.sitePosition(logic),0,quote)) return 9;
 if(quote.worstCode==7) return 2;
 if(quote.worstCode!=0) return 9;
 if(owner.getMoney()->countMoney()<(std::uint32_t)quote.cost) return 2;
 request.cost=quote.cost;
 return 0;
}
bool buildSegment(GameLogic &logic,AIBuildRequest &request,Player &owner)
{
 // AIBuildableWallSegment::build RVA 0x596D99: store the returned terminal
 // hub, not the first span tile. Shared WallSpan legality/topology remains S-657.
 Object *start=logic.findObjectByID(request.wallStartNode);
 if(!start) throw std::logic_error("AIWall: missing start hub for span build");
 Object *terminal=nullptr;
 if(!WallSpan::build(logic,*start,*start->getPosition(),request.sitePosition(logic),owner,0,nullptr,&terminal)) {
  request.state=3;
  return false;
 }
 if(!terminal) throw std::logic_error("AIWall: span build returned no terminal hub");
 request.built=terminal->getID();
 return true;
}
void crc(const std::vector<Plan> &plans,StateHasher &h)
{
 h.addU32((std::uint32_t)plans.size());
 for(const auto &plan:plans) {
  h.addString(plan.name); h.addU32(plan.selected); h.addU32((std::uint32_t)plan.nodes.size());
  for(const auto &node:plan.nodes) {h.addU32(node.index);h.addFloat(node.position.x);h.addFloat(node.position.y);h.addFloat(node.position.z);}
 }
}
}

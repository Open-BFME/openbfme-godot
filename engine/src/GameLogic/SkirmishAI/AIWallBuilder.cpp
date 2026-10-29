// OpenBFME. GPL-3.0.
#include "GameLogic/SkirmishAI/AIWallBuilder.h"
#include "Common/MapObject.h"
#include "Common/Player.h"
#include "Common/RandomValue.h"
#include "Common/StateHash.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
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
void crc(const std::vector<Plan> &plans,StateHasher &h)
{
 h.addU32((std::uint32_t)plans.size());
 for(const auto &plan:plans) {
  h.addString(plan.name); h.addU32(plan.selected); h.addU32((std::uint32_t)plan.nodes.size());
  for(const auto &node:plan.nodes) {h.addU32(node.index);h.addFloat(node.position.x);h.addFloat(node.position.y);h.addFloat(node.position.z);}
 }
}
}

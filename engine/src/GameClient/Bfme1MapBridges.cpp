// OpenBFME. GPL-3.0. BFME1 only. Retail 1.03: loadBridges 0x6DA8A0,
// W3DBridge::load 0x6D8C10, getBridgeInfo 0x6D7CD0, Bridge ctor 0x1A98A0.
#include "GameClient/Bfme1MapBridges.h"
#include "Common/AsciiString.h"
#include "Common/INI.h"
#include "Common/Thing/ThingFactory.h"
#include "GameClient/DrawableLaunchBones.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/MapObjectLoop.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PristinePose.h"
#include "GameLogic/SimMath.h"
#include "Libraries/WWVegas/WWMath/quat.h"
#include <algorithm>
#include <map>
#include <stdexcept>

namespace {
struct Definition { std::string model;float scale=1; };
std::map<std::string,Definition> definitions(ArchiveFileSystem &fs)
{
 INIEnvironment env;env.fileSystem=&fs;std::map<std::string,Definition> out;
 env.blocks.registerBlock("Road",[](INI *ini){while(!ini->isEOF()){ini->readLine();const auto *k=ini->firstToken();if(k && AsciiStringUtil::compareNoCase(k,"End")==0)return;}throw INIException(4,"Road: missing End");});
 env.blocks.registerBlock("Bridge",[&](INI *ini){
  const auto name=AsciiStringUtil::lowered(ini->getNextAsciiString());
  auto found=out.find(name);Definition d;
  if(found!=out.end())d=found->second;
  else if(auto base=out.find("defaultbridge");base!=out.end())d=base->second;
  while(!ini->isEOF()){
   ini->readLine();const auto *token=ini->firstToken();if(!token)continue;
   const auto key=AsciiStringUtil::lowered(token);
   if(key=="end"){out[name]=std::move(d);return;}
   if(key=="bridgescale")d.scale=ini->scanReal(ini->getNextTokenPreprocess());
   else if(key=="bridgemodelname")d.model=ini->getNextAsciiString();
   else {
    // The remaining native rows belong to Bridge damage/presentation, S-5017.
    static const std::vector<std::string> fields={"texture","scaffoldobjectname","scaffoldsupportobjectname","radarcolor","transitioneffectsheight","numfxpertype","bridgemodelnamedamaged","texturedamaged","bridgemodelnamereallydamaged","texturereallydamaged","bridgemodelnamebroken","texturebroken","towerobjectnamefromleft","towerobjectnamefromright","towerobjectnametoleft","towerobjectnametoright","damagedtosound","repairedtosound","transitiontoocl","transitiontofx"};
    if(std::find(fields.begin(),fields.end(),key)==fields.end())throw INIException(5,"Bridge: unknown field %s",token);
   }
  }throw INIException(4,"Bridge: missing End");
 });
 INI ini(env);ini.load("data\\ini\\roads.ini",INI_LOAD_OVERWRITE);return out;
}
float transformedY(const Matrix3D &m,const W3dVectorStruct &v)
{
 // 0x6D8C10 vertex loop: X + (Y + Z), then translation; only the final value is stored.
 return SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24AddW(SimMath::pc24MulW(m.Row[1][0],v.X),SimMath::pc24AddW(SimMath::pc24MulW(m.Row[1][1],v.Y),SimMath::pc24MulW(m.Row[1][2],v.Z))),m.Row[1][3]));
}
Bfme1BridgeGeometry geometry(const MapObject &a,const MapObject &b,const Definition &def,WW3DDrawAssets &assets,TerrainLogic &terrain)
{
 std::string error;const auto *model=assets.model(def.model,&error);
 if(!model)throw std::runtime_error("BFME1 Bridge pristine model '"+def.model+"': "+error);
 const RenderSubObject *left=nullptr;
 for(const auto &sub:model->SubObjects){
  const auto dot=sub.Name.find('.');const auto suffix=AsciiStringUtil::lowered(sub.Name.substr(dot==std::string::npos?0:dot+1));
  if(suffix.rfind("bridge_left",0)==0){left=&sub;break;}
 }
 if(!left || !left->Mesh || left->Mesh->Vertices.empty())throw std::runtime_error("BFME1 Bridge pristine model has no BRIDGE_LEFT vertices: "+def.model);
 Matrix3D transform;transform.Make_Identity();
 if(model->Tree){
  std::vector<Matrix3D> pose;if(!PristinePose::evaluate(*model->Tree,nullptr,0,1,pose,&error))throw std::runtime_error(error);
  if(left->BoneIndex<0 || left->BoneIndex>=(int)pose.size())throw std::runtime_error("BFME1 Bridge BRIDGE_LEFT has an invalid bone");
  transform=pose[(size_t)left->BoneIndex];
 }
 float low=transformedY(transform,left->Mesh->Vertices.front()),high=low;
 for(const auto &v:left->Mesh->Vertices){const auto y=transformedY(transform,v);low=std::min(low,y);high=std::max(high,y);}
 Bfme1BridgeGeometry g;g.from=a.m_location;g.to=b.m_location;
 g.from.z=SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24AddW(terrain.getGroundHeight(g.from.x,g.from.y),g.from.z),0.25f));
 g.to.z=SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24AddW(terrain.getGroundHeight(g.to.x,g.to.y),g.to.z),0.25f));
 float nx=SimMath::pc24Sub(g.from.y,g.to.y),ny=SimMath::pc24Sub(g.to.x,g.from.x);
 const float sq=SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24MulW(nx,nx),SimMath::pc24MulW(ny,ny)));
 // getBridgeInfo uses WWMath::Inv_Sqrt, not sqrt/divide.
 double ywide=ny;if(sq!=0){const float inv=BFME2_Inverse_Sqrt(sq);nx=SimMath::pc24Mul(inv,nx);ywide=SimMath::pc24MulW(inv,ny);}
 auto edge=[&](const Coord3D &p,float y){return Coord3D{SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24MulW(SimMath::pc24MulW(nx,y),def.scale),p.x)),SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24MulW(SimMath::pc24MulW(ywide,y),def.scale),p.y)),p.z};};
 g.fromLeft=edge(g.from,high);g.fromRight=edge(g.from,low);g.toLeft=edge(g.to,high);g.toRight=edge(g.to,low);return g;
}
}
bool Bfme1MapBridges::create(ArchiveFileSystem &fs,WW3DAssetManager &manager,GameLogic &logic,TerrainLogic &terrain,const std::vector<MapObject> &objects,std::vector<std::string> &stops,std::string *error)
{
 try {
  const bool any=std::any_of(objects.begin(),objects.end(),[](const auto &o){return o.getFlag(FLAG_BRIDGE_POINT1);});if(!any)return true;
  const auto defs=definitions(fs);WW3DDrawAssets assets(manager);size_t count=0;
  for(size_t i=0;i<objects.size();++i){
   const auto &a=objects[i];if(!a.getFlag(FLAG_BRIDGE_POINT1))continue;
   if(i+1==objects.size())break;if(!objects[i+1].getFlag(FLAG_BRIDGE_POINT2))continue;
   const auto &b=objects[++i];if(count==200)break; // W3DBridgeBuffer's native capacity
   const auto found=defs.find(AsciiStringUtil::lowered(a.m_objectName));
   if(found==defs.end())throw std::runtime_error("BFME1 Bridge definition not found: "+a.m_objectName);
   auto g=geometry(a,b,found->second,assets,terrain);
   const auto *tt=logic.things().findTemplate("GenericBridge");if(!tt)throw std::runtime_error("BFME1 GenericBridge template not found");
   auto *object=logic.newObject(tt,nullptr,ObjectStatusMaskType{});if(!object)throw std::runtime_error("BFME1 GenericBridge creation failed");
   Coord3D center{SimMath::pc24Mul(SimMath::pc24Add(g.fromLeft.x,g.toRight.x),0.5f),SimMath::pc24Mul(SimMath::pc24Add(g.fromLeft.y,g.toRight.y),0.5f),SimMath::pc24Mul(SimMath::pc24Add(g.fromLeft.z,g.toRight.z),0.5f)};
   object->setPosition(&center);MapObjectLoopResult properties;MapObjectLoop::applyProperties(*object,a.m_properties,properties);
   object->setOrientation(SimMath::fstpDword(SimMath::atan2d(SimMath::pc24Sub(g.toLeft.y,g.fromLeft.y),SimMath::pc24Sub(g.toLeft.x,g.fromLeft.x))));
   g.objectID=object->getID();g.layer=(int)count+2;terrain.addBfme1Bridge(g);++count;
   for(const auto &key:properties.unportedKeys)stops.push_back("[S-5017] BFME1 Bridge map property unverified: "+key.first);
  }
  if(count)stops.push_back("[S-5017] BFME1 retail Bridge creation/classification runs; damage-state changes, repair and client bridge rendering remain unported");
  return true;
 }catch(const std::exception &e){if(error)*error=e.what();return false;}
}

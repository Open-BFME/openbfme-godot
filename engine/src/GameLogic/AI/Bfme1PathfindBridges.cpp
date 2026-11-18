// OpenBFME. GPL-3.0. BFME1 only. Retail 1.03 Bridge layers: addBridge
// 0x3D5850, AllocateCells 0x3FBED0, classifyCells 0x3FC790, destination layer
// 0x1A7C20. The raised-wall variant remains AIPathfindRaised.cpp (RW 0x768832).
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/SimMath.h"
#include <algorithm>
#include <stdexcept>

PathfindLayerEnum Pathfinder::addBfme1Bridge(const Bfme1BridgeGeometry &b)
{
 for(int l=LAYER_TOP;l<=LAYER_RAISED_LAST;++l){
  auto &r=m_raised[(size_t)l];if(r.used())continue;
  r.bridge=std::make_shared<Bfme1BridgeGeometry>(b);r.bridge->layer=l;m_anyRaised=true;
  rebuildBfme1Bridge((PathfindLayerEnum)l);return (PathfindLayerEnum)l;
 }
 throw std::runtime_error("[S-5017] BFME1 Bridge has no free pathfinder layer (2..15)");
}

PathfindLayerEnum Pathfinder::bfme1BridgeAt(float x,float y,float z) const
{
 // 0x1A7C20: traverse the terrain list newest first, choosing a STRICTLY
 // closer height than ground / the previously selected Bridge. No cell test.
 float distance=SimMath::fstpDword(SimMath::absD(SimMath::pc24SubW(z,m_terrain?m_terrain->getGroundHeight(x,y):0)));
 auto result=LAYER_GROUND;
 for(int l=LAYER_RAISED_LAST;l>=LAYER_TOP;--l){
  const auto &b=m_raised[(size_t)l].bridge;if(!b || !b->contains(x,y))continue;
  const auto d=SimMath::pc24SubW(z,b->heightWide(x,y));const auto absolute=d<0?-d:d;
  if(absolute<distance){distance=SimMath::fstpDword(absolute);result=(PathfindLayerEnum)l;}
 }
 return result;
}

void Pathfinder::rebuildBfme1Bridge(PathfindLayerEnum layer)
{
 auto &r=m_raised[(size_t)layer];const auto &b=*r.bridge;
 for(auto &c:r.cells)c.reset(&m_pool);
 r.cells.clear();r.connect[0]=r.connect[1]={-1,-1};r.width=r.depth=0;
 const auto lx=std::min({b.fromLeft.x,b.fromRight.x,b.toLeft.x,b.toRight.x}),hx=std::max({b.fromLeft.x,b.fromRight.x,b.toLeft.x,b.toRight.x});
 const auto ly=std::min({b.fromLeft.y,b.fromRight.y,b.toLeft.y,b.toRight.y}),hy=std::max({b.fromLeft.y,b.fromRight.y,b.toLeft.y,b.toRight.y});
 auto low=[](float v){return SimMath::fistp32(SimMath::floorD(SimMath::pc24MulW(SimMath::pc24SubW(v,0.1f),0.1f)))-1;};
 auto high=[](float v){return SimMath::fistp32(SimMath::ceilD(SimMath::pc24MulW(SimMath::pc24AddW(v,0.1f),0.1f)))+1;};
 const int x0=std::max(low(lx),m_extent.lo.x),y0=std::max(low(ly),m_extent.lo.y);
 const int x1=std::min(high(hx),m_extent.hi.x),y1=std::min(high(hy),m_extent.hi.y);
 r.origin={x0,y0};if(x1<=x0 || y1<=y0)return;
 r.width=x1-x0;r.depth=y1-y0;r.cells.assign((size_t)r.width*r.depth,PathfindCell());
 for(int x=x0;x<x1;++x)for(int y=y0;y<y1;++y){
  auto &c=r.cells[(size_t)(x-x0)*r.depth+y-y0];
  const float ax=SimMath::sseFromInt32(x*10),ay=SimMath::sseFromInt32(y*10);
  const float bx=SimMath::pc24Add(ax,10),by=SimMath::pc24Add(ay,10);
  const int inside=(int)b.contains(ax,ay)+(int)b.contains(ax,by)+(int)b.contains(bx,by)+(int)b.contains(bx,ay);
  // Native names are reversed relative to this port's RW-derived enum:
  // BFME1 IMPASSABLE=5 (GetCell falls back to ground), BRIDGE_IMPASSABLE=6.
  c.setLayer(layer);c.setType(PathfindCell::CELL_BRIDGE_IMPASSABLE);
  if(inside==4)c.setType(PathfindCell::CELL_CLEAR);
  else {
   if(inside)c.setType(PathfindCell::CELL_IMPASSABLE);
   if(b.sideMeetsBounds(ax,ay,bx,by))c.setType(PathfindCell::CELL_IMPASSABLE);
   else {
    if(b.endMeetsBounds(ax,ay,bx,by))c.setType(PathfindCell::CELL_CLEAR);
    if(b.entryMeetsBounds(ax,ay,bx,by)){
     c.setType(PathfindCell::CELL_CLEAR);c.setConnectLayer(LAYER_GROUND);
     getCell(LAYER_GROUND,x,y)->setConnectLayer(layer);
    }
   }
  }
  if(c.getType()!=PathfindCell::CELL_BRIDGE_IMPASSABLE && c.getConnectLayer()!=LAYER_GROUND){
   const float cx=SimMath::pc24Add(ax,5),cy=SimMath::pc24Add(ay,5);
   if(SimMath::pc24AddW(m_terrain->getGroundHeight(cx,cy),10)>b.height(cx,cy)){
    auto *ground=getCell(LAYER_GROUND,x,y);
    if(ground->getType()!=PathfindCell::CELL_OBSTACLE)ground->setType(PathfindCell::CELL_IMPASSABLE);
   }
  }
 }
 // 0x3FCB0A..0x3FCB7C: normalize the full 3D span; offset its XY by seven.
 const auto dx=SimMath::pc24SubW(b.to.x,b.from.x),dy=SimMath::pc24SubW(b.to.y,b.from.y),dz=SimMath::pc24SubW(b.to.z,b.from.z);
 const auto len=SimMath::sqrtPC24(SimMath::pc24AddW(SimMath::pc24AddW(SimMath::pc24MulW(dz,dz),SimMath::pc24MulW(dy,dy)),SimMath::pc24MulW(dx,dx)));
 const auto inv=len?SimMath::pc24DivW(1,len):1;
 const float ox=SimMath::fstpDword(SimMath::pc24MulW(SimMath::pc24MulW(dx,inv),7)),oy=SimMath::fstpDword(SimMath::pc24MulW(SimMath::pc24MulW(dy,inv),7));
 auto cell=[](double v){return SimMath::fistp32(SimMath::floorD(SimMath::pc24MulW(v,0.1f)));};
 r.connect[0]={cell(SimMath::pc24SubW(b.from.x,ox)),cell(SimMath::pc24SubW(b.from.y,oy))};
 r.connect[1]={cell(SimMath::pc24AddW(b.to.x,ox)),cell(SimMath::pc24AddW(b.to.y,oy))};
}

// OpenBFME. GPL-3.0. BFME1 only; later editions retain their terrain rules.
#include "GameLogic/Map/Bfme1BridgeGeometry.h"
#include "GameLogic/SimMath.h"
#include <algorithm>
#include <stdexcept>
#include "Libraries/WWVegas/WWMath/quat.h"
namespace {
// Native LineInRegion 0x1A2020: classify once, clip first then second;
// each interpolation divides before multiplying, with one coordinate store.
bool segment(float ax,float ay,float bx,float by,float lx,float ly,float hx,float hy)
{
 auto code=[&](float x,float y){return (x<lx?1:x>hx?2:0)|(y<ly?8:y>hy?4:0);};
 const int first=code(ax,ay),second=code(bx,by);
 if((first|second)==0)return true;if(first&second)return false;
 auto clip=[&](float &x,float &y,float otherX,float otherY,int outside) {
  if(outside&(8|4)) {
   const auto dy=SimMath::pc24SubW(by,ay);if(dy==0)return false;
   const float edge=outside&8?ly:hy;
   const auto ratio=SimMath::pc24DivW(SimMath::pc24SubW(edge,y),dy);
   x=SimMath::fstpDword(SimMath::pc24AddW(x,SimMath::pc24MulW(SimMath::pc24SubW(bx,ax),ratio)));y=edge;
  }
  if(x>hx || x<lx) {
   const auto dx=SimMath::pc24SubW(bx,ax);if(dx==0)return false;
   const float edge=x>hx?hx:lx;
   const auto ratio=SimMath::pc24DivW(SimMath::pc24SubW(edge,x),dx);
   // Re-read original endpoints after the preceding Y clip, as retail.
   const auto dy=SimMath::pc24SubW(by,ay);
   y=SimMath::fstpDword(SimMath::pc24AddW(y,SimMath::pc24MulW(dy,ratio)));x=edge;
  }
  (void)otherX;(void)otherY;return true;
 };
 if(first && !clip(ax,ay,bx,by,first))return false;
 if(second && !clip(bx,by,ax,ay,second))return false;
 return ax>=lx && ax<=hx && ay>=ly && ay<=hy && bx>=lx && bx<=hx && by>=ly && by<=hy;
}

}
bool Bfme1BridgeGeometry::endMeetsBounds(float lx,float ly,float hx,float hy) const
{
 // 0x1A24B0: normalize the full 3D near edge, then shorten both end
 // segments by ten using its XY direction. A plain Bridge reuses it.
 const auto x=SimMath::pc24SubW(fromRight.x,fromLeft.x),y=SimMath::pc24SubW(fromRight.y,fromLeft.y),z=SimMath::pc24SubW(fromRight.z,fromLeft.z);
 const auto len=SimMath::sqrtPC24(SimMath::pc24AddW(SimMath::pc24AddW(SimMath::pc24MulW(z,z),SimMath::pc24MulW(y,y)),SimMath::pc24MulW(x,x)));
 auto nx=x,ny=y;if(len!=0){const auto inv=SimMath::pc24DivW(1,len);nx=SimMath::pc24MulW(nx,inv);ny=SimMath::pc24MulW(ny,inv);}
 const float ex=SimMath::fstpDword(SimMath::pc24MulW(nx,10)),ey=SimMath::fstpDword(SimMath::pc24MulW(ny,10));
 for(const auto &ends:{std::pair<Coord3D,Coord3D>{fromLeft,fromRight},{toLeft,toRight}})
  if(segment(SimMath::pc24Add(ends.first.x,ex),SimMath::pc24Add(ends.first.y,ey),SimMath::pc24Sub(ends.second.x,ex),SimMath::pc24Sub(ends.second.y,ey),lx,ly,hx,hy))return true;
 return false;
}
namespace {
double planeHeight(const Bfme1BridgeGeometry &b,const Coord3D &p)
{
 // Native plane 0x1A5DC0 (third-first cross second-first), then
 // intersection 0x1A1CF0 and its stored fraction times 1000 (0x1A77AB).
 const auto ux=SimMath::pc24SubW(b.toLeft.x,b.fromLeft.x),uy=SimMath::pc24SubW(b.toLeft.y,b.fromLeft.y),uz=SimMath::pc24SubW(b.toLeft.z,b.fromLeft.z);
 const float vx=SimMath::pc24Sub(b.fromRight.x,b.fromLeft.x),vy=SimMath::pc24Sub(b.fromRight.y,b.fromLeft.y);
 const auto vz=SimMath::pc24SubW(b.fromRight.z,b.fromLeft.z);
 float nx=SimMath::fstpDword(SimMath::pc24SubW(SimMath::pc24MulW(vy,uz),SimMath::pc24MulW(vz,uy)));
 float ny=SimMath::fstpDword(SimMath::pc24SubW(SimMath::pc24MulW(vz,ux),SimMath::pc24MulW(vx,uz)));
 const auto nzWide=SimMath::pc24SubW(SimMath::pc24MulW(vx,uy),SimMath::pc24MulW(vy,ux));
 float nz=SimMath::fstpDword(nzWide),distance=0;
 if(nx==0 && ny==0 && nzWide==0){nz=1;}
 else {
  const float lengthSq=SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24AddW(SimMath::pc24MulW(nzWide,nzWide),SimMath::pc24MulW(ny,ny)),SimMath::pc24MulW(nx,nx)));
  if(lengthSq!=0){const float inv=BFME2_Inverse_Sqrt(lengthSq);nx=SimMath::pc24Mul(inv,nx);ny=SimMath::pc24Mul(inv,ny);nz=SimMath::pc24Mul(inv,nz);}
  distance=SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24AddW(SimMath::pc24MulW(b.fromLeft.x,nx),SimMath::pc24MulW(ny,b.fromLeft.y)),SimMath::pc24MulW(b.fromLeft.z,nz)));
 }
 const auto denominator=SimMath::pc24AddW(SimMath::pc24AddW(SimMath::pc24MulW(1000,nz),SimMath::pc24MulW(0,ny)),SimMath::pc24MulW(0,nx));
 if(denominator==0)throw std::runtime_error("[S-5017] BFME1 Bridge has a vertical plane with undefined native height");
 const auto dot=SimMath::pc24AddW(SimMath::pc24AddW(SimMath::pc24MulW(p.x,nx),SimMath::pc24MulW(ny,p.y)),SimMath::pc24MulW(0,nz));
 const float fraction=SimMath::fstpDword(SimMath::pc24DivW(SimMath::pc24SubW(distance,dot),denominator));
 return SimMath::pc24MulW(fraction,1000);

}
}
bool Bfme1BridgeGeometry::interactsWithEnd(const Coord3D &p,float radius) const
{
 if(layer==1)return false;
 const auto extent=SimMath::pc24AddW(radius,5);
 if(!endMeetsBounds(SimMath::fstpDword(SimMath::pc24SubW(p.x,extent)),SimMath::fstpDword(SimMath::pc24SubW(p.y,extent)),SimMath::fstpDword(SimMath::pc24AddW(p.x,extent)),SimMath::fstpDword(SimMath::pc24AddW(p.y,extent))))return false;
 const auto value=planeHeight(*this,p);
 const auto dz=SimMath::pc24SubW(p.z,value);return (dz<0?-dz:dz)<=10;
}

float Bfme1BridgeGeometry::height(float x,float y) const
{
 return SimMath::fstpDword(heightWide(x,y));
}
double Bfme1BridgeGeometry::heightWide(float x,float y) const {return planeHeight(*this,{x,y,0});}
namespace {
Coord3D scaledVector(const Coord3D &a,const Coord3D &b,float scale)
{
 const auto x=SimMath::pc24SubW(a.x,b.x),y=SimMath::pc24SubW(a.y,b.y),z=SimMath::pc24SubW(a.z,b.z);
 const auto len=SimMath::sqrtPC24(SimMath::pc24AddW(SimMath::pc24AddW(SimMath::pc24MulW(z,z),SimMath::pc24MulW(y,y)),SimMath::pc24MulW(x,x)));
 const auto inv=len!=0?SimMath::pc24DivW(1,len):1;
 return {SimMath::fstpDword(SimMath::pc24MulW(SimMath::pc24MulW(x,inv),scale)),SimMath::fstpDword(SimMath::pc24MulW(SimMath::pc24MulW(y,inv),scale)),0};
}
Coord3D shift(const Coord3D &a,const Coord3D &v,float scale)
{return {SimMath::pc24Add(a.x,SimMath::pc24Mul(v.x,scale)),SimMath::pc24Add(a.y,SimMath::pc24Mul(v.y,scale)),a.z};}
bool triangle(const Coord3D &a,const Coord3D &b,const Coord3D &c,float x,float y)
{
 const float dx[3]={SimMath::pc24Sub(b.x,a.x),SimMath::pc24Sub(c.x,b.x),SimMath::pc24Sub(a.x,c.x)};
 const float dy[3]={SimMath::pc24Sub(b.y,a.y),SimMath::pc24Sub(c.y,b.y),SimMath::pc24Sub(a.y,c.y)};
 const auto determinant=SimMath::pc24SubW(SimMath::pc24MulW(SimMath::pc24SubW(c.x,a.x),dy[0]),SimMath::pc24MulW(SimMath::pc24SubW(c.y,a.y),dx[0]));
 if(determinant!=0){
  const double sign=determinant>0?1:-1;const Coord3D vertices[3]={a,b,c};
  for(int i=0;i<3;++i){
   const float cross=SimMath::fstpDword(SimMath::pc24SubW(SimMath::pc24MulW(dy[i],SimMath::pc24SubW(x,vertices[i].x)),SimMath::pc24MulW(SimMath::pc24SubW(y,vertices[i].y),dx[i])));
   if(SimMath::pc24MulW(cross,sign)<0)return false;
  }return true;
 }
 // 0x1A106A: degenerate triangles use the longest edge. Retail's second
 // squared length is also copied to the third length slot; preserve that tie.
 const float ab=SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24MulW(dy[0],dy[0]),SimMath::pc24MulW(dx[0],dx[0])));
 const float bc=SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24MulW(dy[1],dy[1]),SimMath::pc24MulW(dx[1],dx[1])));
 const int i=ab>bc?0:2;const Coord3D vertices[3]={a,b,c};const auto px=SimMath::pc24SubW(x,vertices[i].x),py=SimMath::pc24SubW(y,vertices[i].y);const float length=ab>bc?ab:bc;
 if(length==0)return SimMath::pc24AddW(SimMath::pc24MulW(py,py),SimMath::pc24MulW(px,px))==0;
 if(SimMath::pc24SubW(SimMath::pc24MulW(dy[i],px),SimMath::pc24MulW(py,dx[i]))!=0)return false;
 if(SimMath::pc24AddW(SimMath::pc24MulW(py,py),SimMath::pc24MulW(px,px))>=length)return false;
 const float ex=SimMath::fstpDword(SimMath::pc24SubW(px,dx[i])),ey=SimMath::fstpDword(SimMath::pc24SubW(py,dy[i]));
 return SimMath::pc24AddW(SimMath::pc24MulW(ey,ey),SimMath::pc24MulW(ex,ex))<length;
}
}
bool Bfme1BridgeGeometry::contains(float x,float y) const
{
 // Bridge::isPointOnBridge, native triangle order (0x1A5F40).
 const float lx=std::min({fromLeft.x,fromRight.x,toLeft.x,toRight.x}),hx=std::max({fromLeft.x,fromRight.x,toLeft.x,toRight.x});
 const float ly=std::min({fromLeft.y,fromRight.y,toLeft.y,toRight.y}),hy=std::max({fromLeft.y,fromRight.y,toLeft.y,toRight.y});
 if(x<lx || x>hx || y<ly || y>hy)return false;
 return triangle(fromLeft,fromRight,toLeft,x,y)||triangle(fromRight,toLeft,toRight,x,y);
}
bool Bfme1BridgeGeometry::sideMeetsBounds(float lx,float ly,float hx,float hy) const
{
 // BFME1 0x1A26D0: two side lines, first 5.1 out then 10.2 out.
 const auto side=scaledVector(fromRight,fromLeft,5.1f);
 auto fl=fromLeft,fr=fromRight,tl=toLeft,tr=toRight;
 for(int pass=0;pass<2;++pass){fl=shift(fl,side,-1);fr=shift(fr,side,1);tl=shift(tl,side,-1);tr=shift(tr,side,1);
  if(segment(fl.x,fl.y,tl.x,tl.y,lx,ly,hx,hy)||segment(fr.x,fr.y,tr.x,tr.y,lx,ly,hx,hy))return true;
 }return false;
}
bool Bfme1BridgeGeometry::entryMeetsBounds(float lx,float ly,float hx,float hy) const
{
 // BFME1 0x1A2950: ends shifted five outward and inset ten sideways.
 const auto side=scaledVector(fromRight,fromLeft,10),along=scaledVector(to,from,5);
 const auto fl=shift(shift(fromLeft,along,-1),side,1),fr=shift(shift(fromRight,along,-1),side,-1);
 const auto tl=shift(shift(toLeft,along,1),side,1),tr=shift(shift(toRight,along,1),side,-1);
 return segment(fl.x,fl.y,fr.x,fr.y,lx,ly,hx,hy)||segment(tl.x,tl.y,tr.x,tr.y,lx,ly,hx,hy);
}

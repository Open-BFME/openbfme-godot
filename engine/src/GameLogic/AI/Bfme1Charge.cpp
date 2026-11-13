// OpenBFME. GPL-3.0. BFME1 only. Attack kind 6 (0x1846D7), machine
// 0x1818D0, state 0x17ECB0 / 0x185060 / 0x17EE60. Later editions retain
// their existing attack machines (RW 0x74CED6; charge remains S-860).
#include "GameLogic/AI/AIAttack.h"
#include "GameLogic/AI/AIMove.h"
#include "GameLogic/AI/AIXfer.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponState.h"
#include "Common/StateHash.h"
#include <algorithm>
#include <stdexcept>
namespace {
int charging(){return CombatNames::modelCondition("CHARGING");}
double relativeAngle(Object &o,const Coord3D &p)
{
 // Thing::relativeAngleTo 0x150510: normalized XY dot -> clamped acos,
 // signed by the cross product. The acos result stays wide in ST0.
 const auto dx=SimMath::pc24SubW(p.x,o.getPosition()->x),dy=SimMath::pc24SubW(p.y,o.getPosition()->y);
 const auto length=SimMath::sqrtPC24(SimMath::pc24AddW(SimMath::pc24MulW(dy,dy),SimMath::pc24MulW(dx,dx)));
 if(length==0)return 0;
 const auto inverse=SimMath::pc24DivW(1,length);
 const float nx=SimMath::fstpDword(SimMath::pc24MulW(dx,inverse)),ny=SimMath::fstpDword(SimMath::pc24MulW(dy,inverse));
 const float fx=SimMath::cosf32(o.getOrientation()),fy=SimMath::sinf32(o.getOrientation());
 const auto wide=SimMath::pc24AddW(SimMath::pc24MulW(nx,fx),SimMath::pc24MulW(ny,fy));
 float dot=SimMath::fstpDword(wide);if(wide<-1)dot=-1;else if(dot>1)dot=1;
 const auto angle=SimMath::acosDet(dot); // CRT equality remains the explicit S-167 boundary.
 return SimMath::pc24SubW(SimMath::pc24MulW(ny,fx),SimMath::pc24MulW(nx,fy))<0?-angle:angle;
}
bool triggerRange(Object &o,Object &v,const WeaponTemplate &w)
{
 // 0x16FD30: strict 3D range, UNMODIFIED weapon range, signed angle.
 const float dx=SimMath::pc24Sub(o.getPosition()->x,v.getPosition()->x),dy=SimMath::pc24Sub(o.getPosition()->y,v.getPosition()->y),dz=SimMath::pc24Sub(o.getPosition()->z,v.getPosition()->z);
 const auto sq=SimMath::pc24AddW(SimMath::pc24AddW(SimMath::pc24MulW(dx,dx),SimMath::pc24MulW(dy,dy)),SimMath::pc24MulW(dz,dz));
 return sq<SimMath::pc24MulW(w.m_attackRange,w.m_attackRange) && relativeAngle(o,*v.getPosition())<std::max(w.m_aimDelta,0.035f);
}
class Charge : public AIState {
 std::unique_ptr<AIMoveToState> move;
 bool done=false;int budget=1;
 void path(const Coord3D &p){ai().world().pathfinder().removeGoal(ai().adapter());move->setGoal(p);move->setAdjustsDestination(false);ai().mover().requestPath(p,true);}
public:
 explicit Charge(AIStateMachine &m):AIState(m,"AIChargeTargetState"){}
 StateReturnType onEnter() override {
  Object *v=machine().goalObject();if(!v)return STATE_FAILURE;
  if(auto *w=owner().getWeapons())w->setWeaponLock(0,LOCKED_TEMPORARILY);
  move=std::make_unique<AIMoveToState>(ai().mover(),*v->getPosition(),false);
  return move->onEnter();
 }
 StateReturnType update() override {
  Object *v=machine().goalObject();auto &o=owner();if(!move || (!v && !o.testModelCondition(charging())) || budget<=0)return STATE_FAILURE;
  if(v && !o.testModelCondition(charging())){
   const auto dx=SimMath::pc24SubW(v->getPosition()->x,move->goal().x),dy=SimMath::pc24SubW(v->getPosition()->y,move->goal().y);
   if(SimMath::pc24AddW(SimMath::pc24MulW(dx,dx),SimMath::pc24MulW(dy,dy))>2500)path(*v->getPosition());
  }
  auto *w=o.getWeapons();auto *weapon=w?w->currentWeapon():nullptr;
  if(weapon){
   if(v && !o.testModelCondition(charging()) && weapon->getTemplate() && triggerRange(o,*v,*weapon->getTemplate()) && w->weaponStatus(*weapon)==WEAPON_READY_TO_FIRE){
    // 0x16F4F0: fifty past the victim along the full 3D direction.
    const auto &p=*o.getPosition(),&q=*v->getPosition();const float dx=SimMath::pc24Sub(q.x,p.x);
    const auto dy=SimMath::pc24SubW(q.y,p.y),dz=SimMath::pc24SubW(q.z,p.z);
    const auto len=SimMath::sqrtPC24(SimMath::pc24AddW(SimMath::pc24AddW(SimMath::pc24MulW(dz,dz),SimMath::pc24MulW(dy,dy)),SimMath::pc24MulW(dx,dx)));
    if(len==0)throw std::runtime_error("[S-5017] BFME1 charge-through has zero native direction");
    const auto inv=SimMath::pc24DivW(1,len);
    const float ux=SimMath::fstpDword(SimMath::pc24MulW(dx,inv)),uy=SimMath::fstpDword(SimMath::pc24MulW(dy,inv));
    const auto uz=SimMath::pc24MulW(dz,inv);
    const float oy=SimMath::pc24Mul(uy,50),oz=SimMath::fstpDword(SimMath::pc24MulW(uz,50));
    Coord3D goal{SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24MulW(ux,50),q.x)),SimMath::pc24Add(oy,q.y),SimMath::pc24Add(oz,q.z)};
    machine().setGoalObject(INVALID_ID);path(goal);o.setModelConditionState(charging(),true);ai().chooseLocomotorSet(4);
    o.setStatus((unsigned)CombatNames::status("IS_FIRING_WEAPON"),true);w->fireCurrentWeapon(nullptr,o.getPosition());
   }
   if(done)return STATE_SUCCESS;
   return move->update();
  }
  return STATE_FAILURE;
 }
 void onExit(StateExitType) override {
  owner().setModelConditionState(charging(),false);ai().chooseLocomotorSet(0);
  if(auto *w=owner().getWeapons())w->releaseWeaponLock(LOCKED_TEMPORARILY);
  if(move)move->onExit();
 }
 void crc(StateHasher &h) const override {h.addBool(done);h.addI32(budget);h.addBool(bool(move));if(move)move->crc(h);}
 void xfer(Xfer &x) override {
  XferVersion version{1,1};x.xferVersion(version);x.xferBool(&done);x.xferInt(&budget);bool has=bool(move);x.xferBool(&has);
  if(x.isLoading()){move.reset();if(has)move=std::make_unique<AIMoveToState>(ai().mover(),Coord3D{},false);}
  if(move)move->xfer(x);
 }
};
}
std::unique_ptr<AIStateMachine> makeBfme1ChargeMachine(AIUpdateInterface &ai)
{
 if(!ai.getObject()->getTemplate()->bfme1Data())throw std::logic_error("BFME1 charge machine requested for another edition");
 ai.getObject()->logic().noteStop("[S-5017] BFME1 charge: native state control runs; VoiceStartCharging and CRT angle equivalence remain unverified");
 auto machine=std::make_unique<AIStateMachine>(ai,"AttackFireWhileChargingMachine");
 machine->defineState(0,std::make_unique<Charge>(*machine),MACHINE_DONE_SUCCESS,MACHINE_DONE_FAILURE);return machine;
}

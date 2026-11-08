// OpenBFME. GPL-3.0.
// BFME1 1.03: formation 0x00242A30/0x002458B0, order 0x00245420,
// hub 0x002417E0, freeze 0x00243EA0, unfreeze 0x0023D850,
// melee update 0x002440E0 and destination search 0x00238D10.
// Member acquisition: Object 0x001CBDC0(false), ground footprint views
// 0x003E4330/0x003E49F0/0x003EA570 and recipient 0x001CFF30.
// The separate HordeContain keeps RW 0x875847/0x877A7A/0x87468B and
// the RW melee strategies. Low-level pathfinder/CRT acceptance stops apply.
#include "GameLogic/Object/Contain/Bfme1HordeRuntime.h"
#include "GameLogic/Object/Contain/Bfme1MeleeRuntime.h"
#include "GameLogic/Object/Contain/HordeContainCore.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/PhysicsBehavior.h"
#include "GameLogic/System/InvisibilityManager.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SnapshotXfer.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Player.h"
#include <algorithm>
#include <cmath>

namespace {
float add(float a,float b){return SimMath::pc24Add(a,b);}
float sub(float a,float b){return SimMath::pc24Sub(a,b);}
float mul(float a,float b){return SimMath::pc24Mul(a,b);}
float distance(const Coord3D &a,const Coord3D &b)
{
	double x=SimMath::pc24SubW(a.x,b.x),y=SimMath::pc24SubW(a.y,b.y);
	return SimMath::fstpDword(SimMath::sqrtPC24(SimMath::pc24AddW(SimMath::pc24MulW(x,x),SimMath::pc24MulW(y,y))));
}
float normalize(float a)
{
	while(a>3.14159274f)a=sub(a,6.28318548f);
	while(a<=-3.14159274f)a=add(a,6.28318548f);
	return a;
}
int moving(){return CombatNames::modelCondition("MOVING");}
void stopMove(Object &o,AIUpdateInterface &ai)
{
	if(ai.isMoving())ai.mover().endingMove();
	o.setModelConditionState(moving(),false);ai.setLocomotorGoalNone();
}
void reserve(Object &o,AIUpdateInterface &ai,const Coord3D &p)
{
	if(ai.world().mapReady())ai.world().pathfinder().updateGoal(ai.adapter(),&p,ai.adapter().getLayer());
}
}
void Bfme1HordeData::buildFieldParse(MultiIniFieldParse &p)
{
	HordeContainBehaviorData::buildFieldParse(p);
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif
	static const FieldParse fields[]={
		{"NarrowPassageScale",INI::parsePercentToReal,nullptr,(int)offsetof(Bfme1HordeData,narrowScale)},
		{nullptr,nullptr,nullptr,0}};
	p.add(fields);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
}
Bfme1HordeContain::Bfme1HordeContain(Thing *t,const HordeContainBehaviorData *d):HordeContain(t,d),
	m_narrowScale(dynamic_cast<const Bfme1HordeData *>(d)?static_cast<const Bfme1HordeData *>(d)->narrowScale:0.4f)
{
	getObject()->logic().noteStop("[S-5017] BFME1 formation/melee: native formation, mood/target search, emotion/quarrel, collision-ignore guards, retail plain-Bridge creation/classification and charge attack paths run; general pathfinder equivalence, mesh-backed Bridge heights/damage, contested-building and assault-transport mood callbacks and CRT equivalence remain unverified");
}
UpdateSleepTime Bfme1HordeContain::update()
{
	const auto sleep=HordeContain::update();
	// BFME1 0x0023FA80: the force-refresh byte clears after a member
	// pass completes without marking more work, not when melee merely ends.
	if(!dirty())m_force=false;
	return sleep;
}
Coord3D Bfme1HordeContain::nativeRotatedOffset(float x,float y) const
{
	// 0x00233F30: length/atan2 polar conversion, not RW's Cartesian rotation.
	float radius=SimMath::fstpDword(SimMath::sqrtPC24(SimMath::pc24AddW(SimMath::pc24MulW(x,x),SimMath::pc24MulW(y,y))));
	if(auto *world=getObject()->logic().aiWorld())
		if(auto *terrain=world->pathfinder().terrainView();terrain && terrain->passagePlane(*getObject()->getPosition()))radius=mul(radius,m_narrowScale);
	float a=SimMath::fstpDword(SimMath::pc24AddW(SimMath::atan2d(y,x),getObject()->getOrientation()));
	return {mul(SimMath::cosf32(a),radius),mul(SimMath::sinf32(a),radius),0};
}
Coord3D Bfme1HordeContain::getMemberFormationPosition(const Object *member) const
{
	int i=core().slotIndexOrZero(member->getID());
	if(m_frozen)
		return (size_t)i<m_formation.size() && !m_formation[i].invalid?m_formation[i].position:*member->getPosition();
	Coord3D p=*getObject()->getPosition();
	if((size_t)i>=core().slots().size())return p;
	const auto &s=core().slots()[i];Coord3D r=nativeRotatedOffset(s.x,s.y);
	p.x=add(p.x,r.x);p.y=add(p.y,r.y);return p;
}
Bfme1HordeContain::MemberState *Bfme1HordeContain::record(const Object &member)
{
	int i=core().slotIndexOrZero(member.getID());
	return i>=0 && (size_t)i<m_formation.size()?&m_formation[i]:nullptr;
}
bool Bfme1HordeContain::beginNativeMelee(Object &)
{
	m_frozen=true;m_force=false;updateFormation();m_formation.resize(core().slots().size());
	if(auto *ai=getObject()->getAIUpdateInterface();ai && ai->world().mapReady())ai->world().pathfinder().removeGoal(ai->adapter());
	for(Object *m:*getContainedItemsList())
	{
		auto *a=m->getAIUpdateInterface();if(!a)continue;
		if(a->mover().goalType()==AIGOAL_EXPLICIT_WITH_PATH && a->mover().path())continue;
		if(!m->testStatus((unsigned)CombatNames::status("IS_MELEE_ATTACKING")) && !a->isIdle())a->aiIdle(CMD_FROM_AI);
		if(auto *r=record(*m)){r->position=*m->getPosition();r->invalid=true;reserve(*m,*a,r->position);a->setLocomotorGoalNone();}
	}
	return true;
}
void Bfme1HordeContain::removeFromContain(Object *member)
{
	// BFME1 0x002408E0, tails 0x00240A4F/0x00240A89: a lost
	// member permits the next melee pass to bypass outstanding retry delays.
	const auto count=getContainedItemsList()->size();
	HordeContain::removeFromContain(member);
	if(getContainedItemsList()->size()!=count)m_retry=true;
}
bool Bfme1HordeContain::endNativeMelee()
{
	m_frozen=false;m_force=true;
	for(Object *m:*getContainedItemsList())
	{
		if(auto *a=m->getAIUpdateInterface();a && a->world().mapReady())a->world().pathfinder().removeGoal(a->adapter());
		if(m->isKindOfName("MACHINE")){getObject()->setOrientation(m->getOrientation());getObject()->setPosition(m->getPosition());}
	}
	updateFormation();return true;
}
void Bfme1HordeContain::memberOrder(Object &member,const Coord3D &position,float orientation,bool force)
{
	auto *a=member.getAIUpdateInterface(),*b=getObject()->getAIUpdateInterface();
	if(!a || !b || member.isEffectivelyDead() || a->locomotorHost().physicsMotionDisabled())return;
	if(b->isMoving() && !m_frozen)
	{
		const auto &p=*member.getPosition();float x=sub(p.x,position.x),y=sub(p.y,position.y);
		float d=CombatQueries::edgeDistanceSquared2D(*getObject(),*getObject()->getPosition(),member,p);
		if(std::min(d,add(mul(x,x),mul(y,y)))>mul(hordeData().m_meleeAttackLeashDistance,hordeData().m_meleeAttackLeashDistance) && !a->isBusy())a->aiBusy(CMD_FROM_AI);
	}
	if(a->isStateActive() && (!b->isMoving() || !member.testStatus((unsigned)CombatNames::status("RUNNING_DOWN_FROM_BEHIND")))){stopMove(member,*a);return;}
	if(member.isKindOfName("MACHINE"))
	{
		if(!m_frozen || b->isMoving())
		{
			member.setPosition(getObject()->getPosition());member.setOrientation(getObject()->getOrientation());
			const auto flags=getObject()->getModelConditionBits();
			for(size_t w=0;w<flags.size();++w)for(int bit=0;bit<32;++bit)member.setModelConditionState((int)w*32+bit,(flags[w]>>bit)&1u);
			if(auto *l=a->curLocomotor())l->setPreferredHeight(b->adapter().getHeightAboveTerrain());
			if(!b->isMoving())reserve(member,*a,*member.getPosition());
		}
		return;
	}
	if(a->mover().goalType()==AIGOAL_EXPLICIT_WITH_PATH && a->mover().path() && (!m_frozen || a->mover().path()->hasExplicitZ())){markMemberWork();return;}
	if(m_frozen){moveHub(member,position,orientation);return;}
	if(!force && !m_force && isMemberAheadOfSlot(member,position) && hordeData().m_useSlowHordeMovement && (b->isMoving() || slotCellClear(member)))
	{stopMove(member,*a);markMemberWork();return;}
	Coord3D dest=position;if(m_gather)dest=*getObject()->getPosition();else adjustMemberDestination(member,dest);moveHub(member,dest,orientation);
}
void Bfme1HordeContain::moveHub(Object &member,const Coord3D &destination,float orientation)
{
	auto *a=member.getAIUpdateInterface(),*b=getObject()->getAIUpdateInterface();if(!a||!b)return;
	Locomotor *l=a->curLocomotor(),*k=b->curLocomotor();if(!l||!k)return;
	if(a->locomotorHost().physicsMotionDisabled()){member.setModelConditionState(moving(),false);return;}
	Coord3D dest=destination;auto &pf=a->world().pathfinder();
	if(!b->isMoving() && !variantHasQuarrel() && a->world().mapReady())
	{
		const auto current=pf.curPathfindCell(member.getID()),goal=pf.pathfindGoalCell(member.getID());
		if(current.x>=0 && current.x==goal.x && current.y==goal.y)dest=*member.getPosition();
	}
	const float d=distance(dest,*member.getPosition());float adjusted=d;
	if(auto *path=b->mover().path())
	{
		const auto ahead=path->computePointAhead(k->speed());
		adjusted=SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24SubW(path->remainingDistanceFrom(ahead),k->speed()),d));
	}
	if(!m_frozen && (!a->isStateActive()||!b->isStateActive()) && (b->isMoving()||a->isIdle()) && !a->isBusy())a->aiBusy(CMD_FROM_AI);
	else if(m_frozen && !a->isIdle() && !a->isStateActive())a->aiIdle(CMD_FROM_AI);
	float speed=l->getMaxSpeedForCondition(a->locomotorHost());
	float threshold=std::min(l->getMaxAccelerationForSpeed(speed),mul(speed,0.2f));
	if(d<threshold)
	{
		Coord3D near=dest;
		if(l->getTemplate().m_zAxisBehavior!=Z_FLOATING_Z)
		{
			near.z=member.getPosition()->z;
			if(a->world().mapReady() && pf.validMovementPosition(&a->adapter(),a->locomotorInfo(),a->adapter().getLayer(),&near))near.z=a->layerHeightAt(near.x,near.y);
		}
		member.setPosition(&near);a->locomotorHost().setPendingPosition(near);
		float diff=SimMath::fstpDword(SimMath::absD(normalize(sub(orientation,member.getOrientation()))));
		bool turn=diff>0.17453294f;MemberState *r=m_frozen?record(member):nullptr;
		if(r)
		{
			turn=(r->phase==1 && (diff>1.57079637f || r->until>member.logic().getFrame())) || (r->phase==2 && diff>0.17453294f);
			if(turn)r->phase=2;
		}
		if(turn)
		{
			if(member.isKindOfName("DOZER")){member.setOrientation(orientation);a->setLocomotorGoalNone();}
			else{a->setLocomotorGoalOrientation(orientation);a->wakeUpNow();}
			markMemberWork();return;
		}
		stopMove(member,*a);if(r && (r->phase==1 || r->phase==2))r->phase=3;
		if(!b->isMoving() && a->world().mapReady())
		{
			if(!slotCellClear(member))
			{
				if(!pf.adjustToPossibleDestination(a->adapter(),a->locomotorInfo(),&near))return;
				a->hordeMemberMoveExplicit(near);reserve(member,*a,near);
			}
			else{reserve(member,*a,*member.getPosition());return;}
		}
		markMemberWork();return;
	}
	markMemberWork();if(!m_frozen && b->isMoving() && a->world().mapReady())pf.removeGoal(a->adapter());
	if(m_gather)a->hordeMemberMoveExplicit(dest);else a->hordeMemberMoveTo(dest);
	a->setPathExtraDistance(adjusted);member.setModelConditionState(moving(),true);
}
namespace {
Object *targetHorde(Object &o)
{
	if(auto *c=o.getContain();c && c->getHordeContainInterface())return &o;
	Object *p=o.getContainedBy();return p && p->getContain() && p->getContain()->getHordeContainInterface()?p:nullptr;
}

}
void Bfme1HordeContain::endReform()
{
	// BFME1 0x002435F0. Unlike RW 0x877E12, pair exchanges in list order;
	// the first slot coordinate stays fixed after an exchange.
	auto *members=getContainedItemsList();
	auto slot=[&](int i){auto &s=core().slots().at(i);auto p=nativeRotatedOffset(s.x,s.y);p.x=add(p.x,getObject()->getPosition()->x);p.y=add(p.y,getObject()->getPosition()->y);return p;};
	for(auto i=members->begin();i!=members->end();++i)
	{
		Object &a=**i;int sa=core().slotIndexOrZero(a.getID());if((size_t)sa>=core().slots().size())return;
		Coord3D p=slot(sa);
		for(auto j=std::next(i);j!=members->end();++j)
		{
			Object &b=**j;if(a.getTemplate()->getFinalOverride()!=b.getTemplate()->getFinalOverride())continue;
			auto *wa=a.getWeapons()?a.getWeapons()->currentWeapon():nullptr,*wb=b.getWeapons()?b.getWeapons()->currentWeapon():nullptr;
			if(wa && wb && wa->getTemplate()!=wb->getTemplate())continue;
			int sb=core().slotIndexOrZero(b.getID());if((size_t)sb>=core().slots().size())continue;
			float da=distance(*a.getPosition(),p),db=distance(*b.getPosition(),p);if(!(db<da))continue;
			Coord3D q=slot(sb);
			if(distance(*a.getPosition(),q)<add(mul(sub(da,db),0.3f),distance(*b.getPosition(),q)))
			{variantCore().setMemberSlot(b.getID(),sa);variantCore().setMemberSlot(a.getID(),sb);sa=sb;}
		}
	}
	updateFormation();
}
bool Bfme1HordeContain::nativeTargetReady(Object &input)
{
	// BFME1 0x002439F0: fixed squared edge threshold (10000), active-state
	// and current-victim tests. No RW Amoeba weapon-range / height gate.
	const bool close=CombatQueries::edgeDistanceSquared2D(input,*input.getPosition(),*getObject(),*getObject()->getPosition())<10000.0f;
	for(Object *m:*getContainedItemsList())
	{
		auto *a=m->getAIUpdateInterface();if(!a)continue;bool ready=false;
		if(a->isStateActive())
		{
			Object *v=a->currentVictim();auto *c=v?v->getContainedBy():nullptr;
			ready=close || (v && (v==&input || c==&input || (input.getContainedBy() && c==input.getContainedBy())));
		}
		else if(close)
		{
			if(a->mover().path() && a->mover().path()->hasExplicitZ())continue; // BFME1 Path 0x001B3FE0.
			if(a->isBusy())a->aiIdle(CMD_FROM_AI);
			Bfme1Melee::acquire(*m);
			ready=a->isStateActive();
		}
		if(ready)return true;
	}
	return false;
}
bool Bfme1HordeContain::findMeleeDestination(Object &member,Coord3D &out,Object &target,const Coord3D &targetPosition,unsigned &delay,bool allowOccupied)
{
	// BFME1 0x00238D10: square perimeter rings, x then y, strict score
	// improvement. The two ratio gates deliberately use different constants.
	auto *a=member.getAIUpdateInterface();if(!a)return false;auto &pf=a->world().pathfinder();
	delay=0;if(!a->world().mapReady()){delay=20;return false;}
	Coord3D start=*member.getPosition();pf.snapPosition(a->adapter(),&start);
	const float original=distance(targetPosition,start);float best=add(original,100),step=0,improvement=0;
	int radius;bool center;pf.getRadiusAndCenter(&a->adapter(),radius,center);int rings=2*(radius*2+(center?1:0));
	const bool far=mul((float)rings,20)<original,blocked=!Bfme1Melee::footprintGoals(member,start).empty();
	Coord3D alternate=start;auto layer=a->adapter().getLayer();auto surfaces=a->locomotorInfo().validSurfaces;
	const bool badLine=!pf.isHordeMeleeLinePassable(&a->adapter(),surfaces,layer,start,start);
	if(badLine)
	{
		bool found=false;
		for(int ring=1;ring<=2 && !found;++ring)for(int x=-ring;x<=ring && !found;++x)for(int y=-ring;y<=ring;++y)
		{
			if(x!=-ring && x!=ring && y!=-ring && y!=ring)continue;
			// Native 0x238F0E..0x238F3C loads x for BOTH coordinates.
			Coord3D p{add(start.x,mul((float)x,10)),add(start.y,mul((float)x,10)),start.z};
			if(pf.isHordeMeleeLinePassable(&a->adapter(),surfaces,layer,p,p)){alternate=p;found=true;break;}
		}
	}
	bool found=false;Coord3D result=start;
	for(int ring=1;ring<=rings;++ring)
	{
		for(int x=-ring;x<=ring;++x)for(int y=-ring;y<=ring;++y)
		{
			if(x!=-ring && x!=ring && y!=-ring && y!=ring)continue;
			Coord3D p{add(start.x,mul((float)x,10)),add(start.y,mul((float)y,10)),start.z};float score=distance(p,targetPosition);
			if(score>original && !blocked)continue;
			auto destLayer=pf.objectLayerAt(a->adapter(),p);
			if(destLayer!=layer && !((destLayer==LAYER_RAMP || layer==LAYER_RAMP) && SimMath::absD(sub(pf.getLayerHeight(p.x,p.y,destLayer),pf.getLayerHeight(p.x,p.y,layer)))<40))continue;
			if(!Bfme1Melee::goalFootprintFree(member,p))
			{
				if(!allowOccupied || !member.getWeapons() || !member.getWeapons()->currentWeapon())continue;
				auto ids=Bfme1Melee::footprintGoals(member,p);if(ids.size()!=2)continue;
				Object *one=member.logic().findObjectByID(ids[0]),*two=member.logic().findObjectByID(ids[1]);
				if(!one||!two||one->getContainedBy()!=member.getContainedBy()||two->getContainedBy()!=member.getContainedBy() || !a->attackRangeFromTo(p,target.getID(),targetPosition,0))continue;
				score=sub(original,5);
			}
			if(!badLine && !pf.isHordeMeleeLinePassable(&a->adapter(),surfaces,layer,alternate,p))continue;
			if(score<best){found=true;result=p;best=score;step=distance(p,start);improvement=sub(original,score);}
		}
		if(!far && found && SimMath::pc24DivW(improvement,step)>0.7071f)break;
	}
	if(found){out=result;if(SimMath::pc24DivW(improvement,step)<0.5f)delay=10;}
	else delay=20;
	return found;
}
bool Bfme1HordeContain::tickNativeMelee(Object &input)
{
	if(!m_frozen || m_force)beginNativeMelee(input);
	// 0x002440E0 -> 0x002372C0, same centroid/member choice as RW 0x86F18F.
	if(!getObject()->testStatus((unsigned)CombatNames::status("STAND_GROUND")))recentreOnMembers(getObject());
	Object *parent=targetHorde(input);Object &target=parent?*parent:input;
	const unsigned now=getObject()->logic().getFrame();int budget=std::max((int)getContainedItemsList()->size()/2,2);
	for(Object *m:*getContainedItemsList())
	{
		auto *a=m->getAIUpdateInterface();if(!a || (a->mover().goalType()==AIGOAL_EXPLICIT_WITH_PATH && a->mover().path()))continue;
		auto *r=record(*m);if(!r)continue;
		if(m->testStatus((unsigned)CombatNames::status("IS_MELEE_ATTACKING")) || a->isStateActive())
		{r->invalid=true;r->last=now;reserve(*m,*a,*m->getPosition());continue;}
		auto *w=m->getWeapons();const auto goals=Bfme1Melee::footprintGoals(*m,*m->getPosition());
		if(((target.isKindOfName("STRUCTURE") && (goals.empty()||goals.size()==2)) || (r->phase==3 && target.isKindOfName("SIEGE_TOWER"))) && w && w->currentWeapon() && w->isWithinAttackRange(target))
		{Bfme1Melee::receive(*m,target);reserve(*m,*a,*m->getPosition());continue;}
		if(r->until>now && !m_retry)continue;
		if(r->phase==1 || r->phase==2){updateFormation();continue;}
		if(r->phase==3)
		{
			Bfme1Melee::acquire(*m);
			if(m->testStatus((unsigned)CombatNames::status("IS_MELEE_ATTACKING")))continue;
		}
		if(r->phase==0 && budget>0)r->invalid=true;
		if(!r->invalid && r->phase!=3)continue;
		r->position=*m->getPosition();Coord3D toward=*target.getPosition();
		if(auto *h=dynamic_cast<HordeContain *>(target.getContain()))if(auto *near=h->pickMemberNear(false,r->position,0,nullptr))toward=*near->getPosition();
		unsigned delay=0;bool success=findMeleeDestination(*m,r->position,target,toward,delay,r->until>0 && target.isKindOfName("STRUCTURE"));
		r->until=delay?now+delay:0;
		if(success){--budget;updateFormation();r->phase=1;}
		else{if(r->until)Bfme1Melee::acquire(*m);r->phase=0;}
		reserve(*m,*a,r->position);r->invalid=false;if(budget<1)break;
	}
	m_retry=false;return true;
}

void Bfme1HordeContain::crc(StateHasher &h) const
{
	HordeContain::crc(h);h.addBool(m_frozen);h.addBool(m_retry);h.addBool(m_gather);h.addBool(m_force);h.addU32((unsigned)m_formation.size());
	for(const auto &r:m_formation){h.addI32(r.phase);h.addFloat(r.position.x);h.addFloat(r.position.y);h.addFloat(r.position.z);h.addBool(r.invalid);h.addU32(r.until);h.addU32(r.last);}
}
void Bfme1HordeContain::xfer(Xfer &x)
{
	HordeContain::xfer(x);XferVersion v{1,1};x.xferVersion(v);x.xferBool(&m_frozen);x.xferBool(&m_retry);x.xferBool(&m_gather);x.xferBool(&m_force);
	unsigned n=(unsigned)m_formation.size();x.xferUnsignedInt(&n);if(n>core().slots().size())throw XferException("BFME1 formation exceeds slot count");
	m_formation.resize(n);for(auto &r:m_formation){x.xferInt(&r.phase);x.xferReal(&r.position.x);x.xferReal(&r.position.y);x.xferReal(&r.position.z);x.xferBool(&r.invalid);x.xferUnsignedInt(&r.until);x.xferUnsignedInt(&r.last);if(r.phase<0||r.phase>3)throw XferException("BFME1 formation phase outside 0..3");}
}

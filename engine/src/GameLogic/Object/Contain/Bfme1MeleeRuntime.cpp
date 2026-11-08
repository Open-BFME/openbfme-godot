// OpenBFME. GPL-3.0.
// BFME1 1.03 only. Object acquisition 0x001CBDC0, recipient 0x001CFF30,
// target handoff 0x0027D420; footprint queries 0x003E4330/0x003E4680/
// 0x003E49F0, movement 0x003E5E40, view 0x003EA570, wall rim 0x003E4DA0.
// RW keeps its existing pathfinder/melee and attack-command runtime.
#include "GameLogic/Object/Contain/Bfme1MeleeRuntime.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/PhysicsBehavior.h"
#include "GameLogic/Module/SiegeModules.h"
#include "GameLogic/Module/SquishCollide.h"
#include "GameLogic/System/InvisibilityManager.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/SimMath.h"
#include "Common/Player.h"
#include "Common/Thing/ThingTemplate.h"
#include "Libraries/WWVegas/WWMath/quat.h"
#include <algorithm>

namespace {
float add(float a,float b){return SimMath::pc24Add(a,b);}
float sub(float a,float b){return SimMath::pc24Sub(a,b);}
float mul(float a,float b){return SimMath::pc24Mul(a,b);}
bool status(const Object &o,const char *name){return o.testStatus((unsigned)CombatNames::status(name));}
bool stunned(const Object &o){return o.physicsModule() && o.physicsModule()->isStunned();}
Object *horde(Object &o)
{
	if(o.isKindOfName("HORDE"))return &o;
	auto *p=o.getContainedBy();return p && p->isKindOfName("HORDE")?p:nullptr;
}
Object *attackingVictim(Object &o)
{
	auto *a=o.getAIUpdateInterface();return a && a->isStateActive()?a->currentVictim():nullptr;
}
struct Footprint { int x,y,r,above; };
Footprint footprint(Object &o,const Coord3D &at)
{
	// 0x003DEE30. Preserve x87 PC24 and the unclipped cell coordinates.
	auto &world=*o.logic().aiWorld();const auto &adapter=world.adapterFor(o);
	float diameter=mul(CombatQueries::boundingCircleRadius(o),2);
	if(diameter>10 && diameter<20)diameter=20;
	if(adapter.getPathfindDiameter()>0)diameter=adapter.getPathfindDiameter();
	const int size=std::max(1,SimMath::fistp32(SimMath::floorD(SimMath::pc24AddW(SimMath::pc24MulW(diameter,0.1f),0.3f))));
	const int cap=o.isKindOfName("HORDE") || o.isKindOfName("MONSTER")?4:2;
	const int r=std::min(size/2,cap);const bool center=(size&1)!=0 || size/2>cap;
	const int x=SimMath::fistp32(SimMath::floorD(SimMath::pc24AddW(SimMath::pc24MulW(at.x,0.1f),center?0.0f:0.5f)));
	const int y=SimMath::fistp32(SimMath::floorD(SimMath::pc24AddW(SimMath::pc24MulW(at.y,0.1f),center?0.0f:0.5f)));
	return {x,y,r,r+(center?1:0)};
}
// Native getCell selects a passable raised cell, otherwise the ground cell.
// Bridge-backed layers also query the ground near bridge ends (TerrainLogic
// 0x001A7F70); Bridge creation itself remains the separate S-161 boundary.
template<class Visit> bool cells(Object &o,const Coord3D &at,bool perimeter,Visit visit,PathfindLayerEnum overrideLayer=LAYER_INVALID)
{
	auto *world=o.logic().aiWorld();if(!world || !world->mapReady())return false;
	auto &pf=world->pathfinder();auto layer=overrideLayer==LAYER_INVALID?world->adapterFor(o).getLayer():overrideLayer;const auto f=footprint(o,at);const int extra=perimeter?1:0;
	const int x0=f.x-f.r-extra,y0=f.y-f.r-extra,x1=f.x+f.above+extra,y1=f.y+f.above+extra;
 // Native 0x3E47F3..0x3E48ED: for each cell visit the requested
 // layer first, then the extra ground branch, retaining each branch's
 // owner-field semantics and the global sixteen-result limit.
 const auto *terrain=o.logic().terrain();
 const bool extraGround=isRaisedLayer(layer) && terrain && terrain->bfme1InteractsWithBridgeEnd(layer,*o.getPosition(),CombatQueries::boundingCircleRadius(o));
 for(int x=x0;x<x1;++x)for(int y=y0;y<y1;++y) {
  if(perimeter && x!=x0 && y!=y0 && x!=x1-1 && y!=y1-1)continue;
  if(!visit(pf.getCell(layer,x,y),x,y,isRaisedLayer(layer)))return false;
  if(extraGround && !visit(pf.getCell(LAYER_GROUND,x,y),x,y,false))return false;
 }
 return true;
}
enum class Owners { Positions, Goals, Perimeter };
std::vector<ObjectID> neighbors(Object &o,const Coord3D &at,Owners owners)
{
	std::vector<ObjectID> ids;
	cells(o,at,owners==Owners::Perimeter,[&](const PathfindCell *c,int,int,bool raisedBranch){
		if(!c)return true;const auto *p=c->occupants(owners==Owners::Goals?OCC_GROUND_GOAL:OCC_POSITION);ObjectID id=p?p->owner:0;
		if(!id && owners==Owners::Perimeter)id=c->getObstacleID();
		if(id && id!=o.getID() && std::find(ids.begin(),ids.end(),id)==ids.end())
		{
			// 0x003E487E: the raised-layer branch tests/deduplicates the
			// goal owner (+0x14) but appends the position owner (+0x18),
			// even when getCell fell back to ground. Preserve that native
			// asymmetry, including a zero or repeated returned position.
			if(owners==Owners::Goals && raisedBranch)
			{
				const auto *position=c->occupants(OCC_POSITION);ids.push_back(position?position->owner:0);
			}
			else ids.push_back(id);
		}
		return ids.size()<16;
	});return ids;
}
bool has(const std::vector<ObjectID> &ids,ObjectID id){return std::find(ids.begin(),ids.end(),id)!=ids.end();}
bool wallRim(Object &o,const Coord3D &from)
{
	// 0x003E4DA0: the one-cell rim around the attacker's footprint on the
	// ground grid, looking for a different wall layer of type 5.
	auto &pf=o.logic().aiWorld()->pathfinder();const auto f=footprint(o,from);const auto *center=pf.getCell(LAYER_GROUND,f.x,f.y);
	const auto layer=center?center->getLayer():LAYER_GROUND;
	for(int x=f.x-f.r-1;x<=f.x+f.above;++x)for(int y=f.y-f.r-1;y<=f.y+f.above;++y)
	{
		if(x!=f.x-f.r-1 && y!=f.y-f.r-1 && x!=f.x+f.above && y!=f.y+f.above)continue;
		const auto *c=pf.getCell(LAYER_GROUND,x,y);
		if(c && c->getLayer()!=layer && isWallLayer(c->getLayer()) && c->getType()==PathfindCell::CELL_BRIDGE_IMPASSABLE)return true;
	}
	return false;
}
float shapeGapSquared(Object &victim,Object &source,const Coord3D &from)
{
	// Object 0x001C2380: minimum over active shapes, each shape's projected
	// rectangle. Off a corner use the support extent along the normalized
	// direction, not the closest point on an AABB. Inv_Sqrt 0x00131C90 has
	// the same bit seed/Newton steps as BFME2_Inverse_Sqrt (RW 0x441C56).
	const auto *m=victim.getBasis();float best=9999999.0f;bool any=false;
	for(const auto &s:victim.geometryShapes())
	{
		if(!s.active)continue;
		const float angle=victim.getOrientation(),co=SimMath::cosf32(angle),si=SimMath::sinf32(angle);
		const float cx=SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24SubW(SimMath::pc24MulW(co,s.offsetX),SimMath::pc24MulW(si,s.offsetY)),victim.getPosition()->x));
		const float cy=SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24AddW(SimMath::pc24MulW(si,s.offsetX),SimMath::pc24MulW(co,s.offsetY)),victim.getPosition()->y));
		const float dx=sub(from.x,cx),dy=sub(from.y,cy);
		const float x=SimMath::fstpDword(SimMath::absD(SimMath::pc24AddW(SimMath::pc24MulW(m[0],dx),SimMath::pc24MulW(m[3],dy))));
		const float y=SimMath::fstpDword(SimMath::absD(SimMath::pc24AddW(SimMath::pc24MulW(m[1],dx),SimMath::pc24MulW(m[4],dy))));
		if(x<=s.majorRadius && y<=s.minorRadius)return 0;
		float extent,dist2;
		if(x<=s.majorRadius){extent=s.minorRadius;dist2=mul(y,y);}
		else if(y<=s.minorRadius){extent=s.majorRadius;dist2=mul(x,x);}
		else
		{
			dist2=add(mul(dx,dx),mul(dy,dy));float nx=dx,ny=dy;
			if(dist2!=0){const float inv=BFME2_Inverse_Sqrt(dist2);nx=mul(dx,inv);ny=mul(dy,inv);}
			const auto onX=SimMath::absD(SimMath::pc24AddW(SimMath::pc24MulW(mul(m[0],s.majorRadius),nx),SimMath::pc24MulW(mul(m[3],s.majorRadius),ny)));
			const auto onY=SimMath::absD(SimMath::pc24AddW(SimMath::pc24MulW(mul(m[1],s.minorRadius),nx),SimMath::pc24MulW(mul(m[4],s.minorRadius),ny)));
			extent=SimMath::fstpDword(SimMath::pc24AddW(onX,onY));
		}
		float gap=SimMath::fstpDword(SimMath::pc24SubW(SimMath::sqrtPC24(dist2),SimMath::pc24AddW(extent,CombatQueries::boundingCircleRadius(source))));
		if(!any || gap<best){best=gap;any=true;}
	}
	return best<=0?0:mul(best,best);
}
int crushLevel(Object &o,bool crusher)
{
	// BFME1 0x001C7600 / 0x001C74E0: signed bytes, mounted override,
	// without RW's additional attribute-level sums.
	const auto &t=CrushTemplateInfo::cached(o);const bool mounted=o.testModelCondition(CombatNames::modelCondition("MOUNTED"));
	const int alt=crusher?t.mountedCrusherLevel:t.mountedCrushableLevel;
	return (int)(signed char)(unsigned char)(mounted && alt!=255?alt:(crusher?t.crusherLevel:t.crushableLevel));
}
bool canCrushTarget(Object &o,Object &victim)
{
	if(crushLevel(o,true)<=0 || crushLevel(o,true)<=crushLevel(victim,false))return false;
	auto *a=o.getAIUpdateInterface();if(!a)return false;
	// 0x001C7600: a moving special/portal path bypasses the zone query.
	if(!(a->isMoving() && a->mover().path() && a->mover().path()->hasExplicitZ()))
		if(!a->world().pathfinder().quickDoesPathExist(a->adapter(),o.getPosition(),victim.getPosition(),nullptr))return false;
	if(status(o,"RAMPAGING") || status(o,"FLEE_OFF_MAP") || o.testModelCondition(CombatNames::modelCondition("CHARGING")))return true;
	if(o.getRelationship(victim)==ALLIES)return false;
	const auto *w=o.getWeapons();const auto *weapon=w?w->currentWeapon():nullptr;return !weapon || weapon->getTemplate()->m_meleeWeapon;
}
bool crushSpeed(Object &o)
{
	Object *root=&o;while(root->getContainedBy() && root->getContainedBy()->isKindOfName("HORDE"))root=root->getContainedBy();
	auto *a=root->getAIUpdateInterface();if(!a)return false;
	const float pct=CrushTemplateInfo::cached(*root).minCrushVelocityPercent;if(pct<=0)return true;
	auto *l=a->curLocomotor();const float current=l?std::max(0.0f,l->speed()):0;
	return (l?mul(l->getMaxSpeedForCondition(a->locomotorHost()),pct):0)<=current;
}
void reserve(Object &o)
{
	// Object 0x001C7200: evict other stationary goal owners before taking
	// the footprint, then clear the locomotor goal. Its virtual +0x60 is
	// Object's verified no-op 0x001BE0C0.
	auto *a=o.getAIUpdateInterface();if(!a || !a->world().mapReady())return;
	for(auto id:Bfme1Melee::footprintGoals(o,*o.getPosition()))if(auto *other=o.logic().findObjectByID(id);other && !other->isEffectivelyDead() && !stunned(*other) && !status(*other,"IS_MELEE_ATTACKING"))
		if(auto *ai=other->getAIUpdateInterface())a->world().pathfinder().removeGoal(ai->adapter());
	a->world().pathfinder().updateGoal(a->adapter(),o.getPosition(),a->adapter().getLayer());a->setLocomotorGoalNone();
}
}

std::vector<ObjectID> Bfme1Melee::footprintGoals(Object &o,const Coord3D &at)
{
	return neighbors(o,at,Owners::Goals);
}
bool Bfme1Melee::goalFootprintFree(Object &o,const Coord3D &at)
{
	// BFME1 0x003DF250: type 4/5/6, human extra-pass restriction,
	// PATH_THROUGH_INFANTRY and HORDE goal exceptions.
	const bool through=o.isKindOfName("PATH_THROUGH_INFANTRY");
	auto *world=o.logic().aiWorld();if(!world || !world->mapReady())return false;
	const auto layer=world->pathfinder().objectLayerAt(world->adapterFor(o),at);
	return cells(o,at,false,[&](const PathfindCell *c,int,int,bool){
		if(!c || c->getType()==PathfindCell::CELL_OBSTACLE || c->getType()==PathfindCell::CELL_BRIDGE_IMPASSABLE || c->getType()==PathfindCell::CELL_IMPASSABLE || (c->getExtraPass() && o.getControllingPlayer() && o.getControllingPlayer()->getPlayerType()==PLAYER_HUMAN))return false;
		if(auto *p=c->occupants(OCC_POSITION);p && p->owner!=o.getID())
		{auto *other=o.logic().findObjectByID(p->owner);if(!through || (other && !other->isKindOfName("INFANTRY")))return false;}
		if(auto *p=c->occupants(OCC_GROUND_GOAL);p && p->owner!=o.getID())
		{auto *other=o.logic().findObjectByID(p->owner);if(other && !(other->isKindOfName("HORDE") && !o.isKindOfName("HORDE")) && (!through || !other->isKindOfName("INFANTRY")))return false;}
		return true;
	},layer);
}
bool Bfme1Melee::movementClear(Object &o)
{
	// BFME1 0x003E5E40: terrain and a goal owner that is also the
	// position owner block acquisition. A moving unit's separate goal does not.
	auto *world=o.logic().aiWorld();if(!world || !world->mapReady())return false;
	const auto layer=world->pathfinder().objectLayerAt(world->adapterFor(o),*o.getPosition());
	return cells(o,*o.getPosition(),false,[&](const PathfindCell *c,int,int,bool){
		if(!c || c->getType()==PathfindCell::CELL_OBSTACLE || c->getType()==PathfindCell::CELL_BRIDGE_IMPASSABLE || (c->getExtraPass() && o.getControllingPlayer() && o.getControllingPlayer()->getPlayerType()==PLAYER_HUMAN))return false;
		auto *g=c->occupants(OCC_GROUND_GOAL),*p=c->occupants(OCC_POSITION);
		return !g || g->owner==o.getID() || !p || g->owner!=p->owner;
	},layer);
}
bool Bfme1Melee::view(Object &o,const Coord3D &from,Object &victim)
{
	auto *world=o.logic().aiWorld();if(!world || !world->mapReady())return false;
	// 0x003EA570. Perimeter identity takes priority over every special case.
	if(has(neighbors(o,from,Owners::Perimeter),victim.getID()))return !has(neighbors(o,from,Owners::Positions),victim.getID());
	const float dx=sub(o.getPosition()->x,from.x),dy=sub(o.getPosition()->y,from.y),dz=sub(o.getPosition()->z,from.z);
	if(SimMath::pc24AddW(SimMath::pc24AddW(SimMath::pc24MulW(dz,dz),SimMath::pc24MulW(dy,dy)),SimMath::pc24MulW(dx,dx))<1 && !victim.isKindOfName("STRUCTURE") && has(neighbors(victim,*victim.getPosition(),Owners::Positions),o.getID()))return true;
	if(victim.isKindOfName("WALK_ON_TOP_OF_WALL"))
	{
		const bool upgrade=victim.isKindOfName("WALL_UPGRADE");const float reach=add(upgrade?0:CombatQueries::boundingCircleRadius(o),20);
		if(shapeGapSquared(victim,o,from)<mul(reach,reach) && (upgrade || wallRim(o,from)))return true;
	}
	if(!victim.isKindOfName("SIEGE_TOWER"))return false;
	auto *portal=findSiegePortal(victim);auto *deploy=SiegeDeploySpecialPower::of(victim);
	if(!portal || !deploy || !deploy->isDeployed())return false;
	const Coord3D top=portal->topAttackPosition();const float reach=add(portal->topAttackRadius(),CombatQueries::boundingCircleRadius(o));
	return SimMath::absD(sub(top.z,from.z))<20 && SimMath::absD(sub(top.x,from.x))<reach && SimMath::absD(sub(top.y,from.y))<reach;
}
bool Bfme1Melee::targetMovingAway(Object &o,Object *victim)
{
	if(!victim || victim->isEffectivelyDead())return false;
	if(status(*victim,"HORDE_MEMBER"))if(auto *root=horde(*victim))victim=root;
	const float co=SimMath::cosf32(victim->getOrientation()),si=SimMath::sinf32(victim->getOrientation());const float dx=sub(victim->getPosition()->x,o.getPosition()->x),dy=sub(victim->getPosition()->y,o.getPosition()->y);
	if(SimMath::pc24AddW(SimMath::pc24MulW(co,dx),SimMath::pc24MulW(si,dy))<0)return false;
	auto *a=victim->getAIUpdateInterface();auto *l=a?a->curLocomotor():nullptr;
	return l && l->speed()>SimMath::pc24MulW(l->getMaxSpeedForCondition(a->locomotorHost()),0.25f);
}
void Bfme1Melee::autoAcquired(AIUpdateInterface &a)
{
	if(a.getObject()->getTemplate()->bfme1Data())a.nativeMeleeState().autoTarget=true;
}
bool Bfme1Melee::receive(Object &o,Object &other)
{
	auto *a=o.getAIUpdateInterface();if(!a || a->nativeMeleeState().responding || targetMovingAway(o,&other))return false;
	Object *current=attackingVictim(o);bool same=current==&other;
	if(status(other,"HORDE_MEMBER") && current && horde(other)==horde(*current))same=true;
	if(!a->nativeMeleeState().autoTarget && current && (!status(o,"HORDE_MEMBER") || other.isKindOfName("STRUCTURE")) && !same)return false;
	const auto *w=o.getWeapons();const auto *weapon=w?w->currentWeapon():nullptr;
	if(a->isBusy() || !weapon || !weapon->getTemplate()->m_meleeWeapon)return false;
	if(canCrushTarget(o,other) && crushSpeed(o))return false;
	if(status(o,"HORDE_MEMBER"))if(auto *root=horde(o);root && canCrushTarget(*root,other) && crushSpeed(*root))return false;
	if(a->moodMatrixValue()&0x2000)return false;
	// Object 0x001C14E0 -> HordeContain 0x00243990. The member gate
	// asks idle/busy, not general weapon permission or the target's range.
	if(auto *parent=o.getContainedBy();parent && parent->getContain() && parent->getContain()->getHordeContainInterface())
	{
		if(a->isBusy())a->aiIdle(CMD_FROM_AI);
		else if(!a->isIdle())return false;
	}
	if((!a->isIdle() && !a->isStateActive()) || status(o,"UNCONTROLLABLY_SCARED") || status(o,"IS_MELEE_ATTACKING"))return false;
	if(InvisibilityManager::isStealthedAndUndetected(other,o.getControllingPlayer()) || (status(o,"STEALTHED") && !status(o,"DETECTED")))return false;
	// AI 0x0027D420: State vslot +0x20 means ATTACKING (AIIdleState's
	// slot is false; its idle predicate is +0x1C). Keep an attack/attack-move
	// or attack-follow order and its lock, replacing only the goal.
	auto &machine=a->stateMachine();const unsigned state=machine.currentStateId();
	if(a->isStateActive() || state==0x21 || state==0x3D)
	{
		const bool locked=machine.isLocked();machine.unlock();machine.setGoalObject(other.getID());if(locked)machine.lock();
		if(!targetMovingAway(o,&other))a->mover().destroyPath();
	}
	else if(!a->hasTemporaryStateMachine())a->aiAttackObject(&other,CMD_FROM_AI);
	reserve(o);
	struct Responding { bool &flag;explicit Responding(bool &f):flag(f){flag=true;}~Responding(){flag=false;} } responding(a->nativeMeleeState().responding);
	if(other.getRelationship(o)==ENEMIES && !other.isKindOfName("MINE"))receive(other,o);
	return true;
}
void Bfme1Melee::acquire(Object &o,bool includeCurrent)
{
	auto *a=o.getAIUpdateInterface();if((a && a->isBusy()) || o.isEffectivelyDead() || (!includeCurrent && status(o,"IS_MELEE_ATTACKING")) || stunned(o))return;
	Object *root=horde(o);
	if(root && root->getAIUpdateInterface() && root->getAIUpdateInterface()->stateMachine().temporaryStateLocked())return;
	if(!movementClear(o) && !(root && root->isKindOfName("COMBO_HORDE") && !root->isKindOfName("MELEE_HORDE")))return;
	Object *wanted=attackingVictim(o),*ownerTarget=status(o,"HORDE_MEMBER") && o.getContainedBy()?attackingVictim(*o.getContainedBy()):nullptr;
	Object *wantedHorde=wanted && status(*wanted,"HORDE_MEMBER")?wanted->getContainedBy():nullptr,*best=nullptr;
	auto eligible=[](Object &v){return !v.isEffectivelyDead() && !stunned(v);};
	auto restricted=[](Object &v){return v.isKindOfName("STRUCTURE") || v.isKindOfName("NOT_AUTOACQUIRABLE");};
	for(auto id:neighbors(o,*o.getPosition(),Owners::Positions))
		if(auto *v=o.logic().findObjectByID(id);v && eligible(*v) && o.getRelationship(*v)==ENEMIES && (!restricted(*v) || v==wanted || v==ownerTarget))if(best!=wanted || !best)best=v;
	for(auto id:neighbors(o,*o.getPosition(),Owners::Perimeter))
	{
		auto *v=o.logic().findObjectByID(id);if(!v)continue;
		if(v==ownerTarget || (ownerTarget && horde(*v)==ownerTarget)){best=v;break;}
		if(wantedHorde && v->getContainedBy()==wantedHorde)best=v;
		if(!eligible(*v) || o.getRelationship(*v)!=ENEMIES || !view(o,*o.getPosition(),*v))continue;
		if((!best || best!=wanted) && (!restricted(*v) || v==wanted)){best=v;if(includeCurrent)acquire(*v,false);}
	}
	if(best && view(o,*o.getPosition(),*best))receive(o,*best);
}

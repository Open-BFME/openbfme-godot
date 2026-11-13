// OpenBFME. GPL-3.0.
// BFME1 1.03 only. Authority: AOD ctor 0x00230580, data ctor
// 0x0022F7E0 / table 0x00CAE090, build 0x00231C50, update 0x00230AE0,
// flow 0x0022F960, position 0x002310F0, motion 0x00230BF0, large-unit
// tracking 0x00231980. BFME2/RotWK retain their registered implementations.
#include "GameLogic/Object/Contain/Bfme1HordeRuntime.h"
#include "GameLogic/Object/Contain/HordeContainBehaviorData.h"
#include "GameLogic/Object/Contain/HordeContainCore.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/StateHash.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/PhysicsBehavior.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameLogic/SnapshotXfer.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/Module/CollideModule.h"
#include "GameLogic/Module/MoneyEventModules.h"
#include "GameLogic/Damage.h"
#include "GameLogic/FXEvents.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/ObjectFilterMatch.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif
#include <algorithm>
#include <array>
#include <stdexcept>

namespace
{
constexpr float pi = 3.14159274f, twoPi = 6.28318548f;
float add(float a, float b) { return SimMath::pc24Add(a,b); }
float sub(float a, float b) { return SimMath::pc24Sub(a,b); }
float mul(float a, float b) { return SimMath::pc24Mul(a,b); }
float div(float a, float b) { return SimMath::fstpDword(SimMath::pc24DivW(a,b)); }
float angle(float a)
{
	while (a > pi) a = sub(a, twoPi);
	while (a <= -pi) a = add(a, twoPi);
	return a;
}
float length(const Coord3D &p)
{
	return SimMath::fstpDword(SimMath::sqrtPC24(SimMath::pc24AddW(SimMath::pc24AddW(SimMath::pc24MulW(p.x,p.x),SimMath::pc24MulW(p.z,p.z)),SimMath::pc24MulW(p.y,p.y))));
}
Coord3D delta(const Coord3D &a, const Coord3D &b) { return {sub(a.x,b.x),sub(a.y,b.y),sub(a.z,b.z)}; }
void xferPoint(Xfer &x, Coord3D &p) { x.xferReal(&p.x); x.xferReal(&p.y); x.xferReal(&p.z); }
void hashPoint(StateHasher &h, const Coord3D &p) { h.addFloat(p.x); h.addFloat(p.y); h.addFloat(p.z); }
int moving() { return CombatNames::modelCondition("MOVING"); }
int oath() { return CombatNames::modelCondition("OATH_FULLFILLED"); }

// BFME1 Thing::relativeAngleTo 0x00150510. Unlike the shared AI helper,
// this is acos of a normalized dot with the cross-product sign (zero
// separation returns zero). acos retains the facade's existing S-167 limit.
float relativeAngle(const Object &object,const Coord3D &point)
{
	const auto &p=*object.getPosition();
	double dx=SimMath::pc24SubW(point.x,p.x),dy=SimMath::pc24SubW(point.y,p.y);
	double norm=SimMath::sqrtPC24(SimMath::pc24AddW(SimMath::pc24MulW(dy,dy),SimMath::pc24MulW(dx,dx)));
	if(norm==0) return 0;
	double inv=SimMath::pc24DivW(1,norm);
	float x=SimMath::fstpDword(SimMath::pc24MulW(inv,dx)),y=SimMath::fstpDword(SimMath::pc24MulW(inv,dy));
	float c=SimMath::cosf32(object.getOrientation()),sn=SimMath::sinf32(object.getOrientation());
	float dot=SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24MulW(x,c),SimMath::pc24MulW(y,sn)));
	float result=SimMath::fstpDword(SimMath::acosDet(std::clamp((double)dot,-1.0,1.0)));
	if(SimMath::pc24SubW(SimMath::pc24MulW(y,c),SimMath::pc24MulW(x,sn))<0) result=sub(0,result);
	return result;
}

// ThingTemplate ctor initializes byte +0x495 to SMALL (0); the shared
// object parser already types this byte-sized enum as a long long slot.
int deadCollideSize(const Object &object)
{
	const FieldValue *value=object.getTemplate()->findField("DeadCollideSize");
	if (!value) return 0;
	if (auto *index=std::get_if<long long>(value);index && *index>=0 && *index<=2) return (int)*index;
	throw std::logic_error("[S-5017] BFME1 AOD: invalid DeadCollideSize field");
}

// Horse ctor 0x0024D220 has its own member order 0x0024E310.
// Its slot 4 is a thunk to the ordinary attack function, NOT an empty method.
class HorseContain final : public Bfme1HordeContain
{
public:
	HorseContain(Thing *t,const HordeContainBehaviorData *d):Bfme1HordeContain(t,d)
	{
		getObject()->logic().noteStop("[S-5017] BFME1 HorseHordeContain: native member order and paired reform assignment; shared low-level pathfinder and locomotor boundaries apply");
	}
	const std::type_info &xferClass() const override {return typeid(HorseContain);}
	void beginReform() override
	{
		// 0x0024D4E0: zero positive current speed, no ordinary reform setup.
		if (auto *ai=getObject()->getAIUpdateInterface();ai && ai->curLocomotor() && ai->curLocomotor()->speed()>0)
			ai->curLocomotor()->setSpeed(0);
	}
	void endReform() override
	{
		// 0x0024D610: template-equal pairs exchange slots when the
		// second member is nearer the first slot and the first's walk
		// to the second slot improves by the native 0.3 distance margin.
		// One RankInfo row restricts the exchange to odd/even neighbours.
		const bool paired=hordeData().m_rankInfo.size()<2;
		const auto *members=getContainedItemsList();
		auto slotPosition=[&](int index) {
			const auto &s=core().slots().at(index);
			const Coord3D offset=nativeRotatedOffset(s.x,s.y);
			const auto &p=*getObject()->getPosition();return Coord3D{add(offset.x,p.x),add(offset.y,p.y),p.z};
		};
		auto distance=[](const Coord3D &a,const Coord3D &b) {Coord3D d=delta(a,b);d.z=0;return length(d);};
		for(auto first=members->begin();first!=members->end();++first)
		{
			Object *a=*first;int sa=core().slotIndexOrZero(a->getID());
			if ((size_t)sa>=core().slots().size() || (paired && !(sa&1)))continue;
			const Coord3D target=slotPosition(sa);
			auto second=paired?members->begin():std::next(first);
			for(;second!=members->end();++second)
			{
				Object *b=*second;if(a->getTemplate()->getFinalOverride()!=b->getTemplate()->getFinalOverride())continue;
				int sb=core().slotIndexOrZero(b->getID());
				if ((size_t)sb>=core().slots().size() || (paired && ((sb&1) || sb!=sa+1)))continue;
				const Coord3D otherTarget=slotPosition(sb);float da=distance(*a->getPosition(),target),db=distance(*b->getPosition(),target);
				if(db>da)continue;
				const Coord3D ar=delta(*a->getPosition(),otherTarget),br=delta(*b->getPosition(),otherTarget);
				double desired=SimMath::sqrtPC24(SimMath::pc24AddW(SimMath::pc24MulW(ar.x,ar.x),SimMath::pc24MulW(ar.y,ar.y)));
				double current=SimMath::sqrtPC24(SimMath::pc24AddW(SimMath::pc24MulW(br.x,br.x),SimMath::pc24MulW(br.y,br.y)));
				if(SimMath::pc24AddW(current,SimMath::pc24MulW(SimMath::pc24SubW(da,db),0.3f))>desired)
				{variantCore().setMemberSlot(b->getID(),sa);variantCore().setMemberSlot(a->getID(),sb);sa=sb;}
			}
		}
	}

protected:
	void memberOrder(Object &member,const Coord3D &slot,float orientation,bool force) override
	{
		auto *ai=member.getAIUpdateInterface(),*owner=getObject()->getAIUpdateInterface();
		if (!ai || !owner || member.isEffectivelyDead() || ai->locomotorHost().physicsMotionDisabled()) return;
		if (ai->isStateActive() && !owner->isMoving())
		{
			member.setModelConditionState(moving(),false);ai->setLocomotorGoalNone();return;
		}
		if (frozenFormation()) { moveHub(member,slot,orientation);return; }
		if (ai->mover().goalType()==AIGOAL_EXPLICIT_WITH_PATH && ai->mover().path())
		{
			if (ai->mover().path()->hasExplicitZ() || !ai->world().pathfinder().isLinePassable(&ai->adapter(),ai->locomotorInfo().validSurfaces,ai->adapter().getLayer(),*member.getPosition(),slot,false,true))
			{markMemberWork();return;}
			ai->setLocomotorGoalNone();
		}
		Locomotor *l=ai->curLocomotor(),*k=owner->curLocomotor();
		if (!l || !k) return;
		if (!k->isFasterThanQuarterSpeed(owner->locomotorHost()))
		{
			float current=member.getOrientation(),turn=l->getMaxTurnRate(ai->locomotorHost());
			float diff=angle(sub(orientation,current));
			if (SimMath::absD(diff)>mul(turn,0.5f)) {ai->setLocomotorGoalOrientation(add(current,std::clamp(diff,-turn,turn)));return;}
		}
		if (!meleeEngaged() && isMemberAheadOfSlot(member,slot) && (owner->isMoving() || slotCellClear(member)))
		{
			member.setModelConditionState(moving(),false);ai->setLocomotorGoalNone();return;
		}
		Coord3D dest=slot;adjustMemberDestination(member,dest);moveHub(member,dest,orientation);
		(void)force;
	}
};

class AodData final : public Bfme1HordeData
{
public:
	DieMuxData deathFilter;
	float frequency=0, frequencyRandom=0, amplitude=0, amplitudeRandom=0, still=0;
	float frequencyZ=0, frequencyRandomZ=0, amplitudeZ=0, amplitudeRandomZ=0, stillZ=0;
	float oathZ=1, heightFactor=0.45f, minHeight=0, maxHeight=999999, tailOff=1;
	unsigned timeout=2;
	float scatterSpeed=0.3f, scatterRandom=0.4f;
	static void buildFieldParse(MultiIniFieldParse &p)
	{
		Bfme1HordeData::buildFieldParse(p);
#define AOD_REAL(name, field) {name, INI::parseReal, nullptr, (int)offsetof(AodData, field)}
		static const FieldParse fields[] = {
			// BFME1 0x22F8A0 -> 0x2469A0 -> 0x22DB70 -> 0x2284A0:
			// the inherited OpenContain chain adds DieMux table 0xCB2E70.
			// Retain its data; AOD death-filter execution remains S-5017.
			{"DeathTypes",ParseDeathTypeFlags,nullptr,(int)offsetof(AodData,deathFilter.m_deathTypes)},
			{"ExemptStatus",ParseObjectStatusMask,nullptr,(int)offsetof(AodData,deathFilter.m_exemptStatus)},
			{"RequiredStatus",ParseObjectStatusMask,nullptr,(int)offsetof(AodData,deathFilter.m_requiredStatus)},
			{"DamageAmountRequired",INI::parseReal,nullptr,(int)offsetof(AodData,deathFilter.m_damageAmountRequired)},
			{"MinKillerAngle",INI::parseAngleReal,nullptr,(int)offsetof(AodData,deathFilter.m_minKillerAngle)},
			{"MaxKillerAngle",INI::parseAngleReal,nullptr,(int)offsetof(AodData,deathFilter.m_maxKillerAngle)},
			AOD_REAL("FrequencyScale",frequency), AOD_REAL("FrequencyRandomness",frequencyRandom),
			AOD_REAL("AmplitudeScale",amplitude), AOD_REAL("AmplitudeRandomness",amplitudeRandom), AOD_REAL("StillAmplitude",still),
			AOD_REAL("FrequencyScaleZ",frequencyZ), AOD_REAL("FrequencyRandomnessZ",frequencyRandomZ),
			AOD_REAL("AmplitudeScaleZ",amplitudeZ), AOD_REAL("AmplitudeRandomnessZ",amplitudeRandomZ), AOD_REAL("StillAmplitudeZ",stillZ),
			AOD_REAL("OathFulfilledZFactor",oathZ), AOD_REAL("LargeUnitHeightFactor",heightFactor),
			AOD_REAL("LargeUnitMinHeight",minHeight), AOD_REAL("LargeUnitMaxHeight",maxHeight),
			{"LargeUnitTimeout",INI::parseDurationUnsignedInt,nullptr,(int)offsetof(AodData,timeout)},
			AOD_REAL("LargeUnitTailOff",tailOff), AOD_REAL("ScatterSpeedFactor",scatterSpeed), AOD_REAL("ScatterRandomness",scatterRandom),
			{nullptr,nullptr,nullptr,0}
		};
#undef AOD_REAL
		p.add(fields);
	}
};

class AodContain final : public Bfme1HordeContain
{
	struct History { Coord3D position{}; float angle=0; };
	struct Flow { float x=0, angle=0; Coord3D position{}; };
	const AodData *m_aod;
	std::vector<std::array<float,6>> m_waves;
	std::vector<History> m_history;
	std::vector<Flow> m_flow;
	mutable ObjectID m_large=INVALID_ID;
	unsigned m_largeFrame=0;
	Coord3D m_largePosition{};
	float m_largeHeight=0, m_scatter=0;
	Coord3D rotated(float x,float y) const
	{
		float radius=SimMath::fstpDword(SimMath::sqrtPC24(SimMath::pc24AddW(SimMath::pc24MulW(x,x),SimMath::pc24MulW(y,y))));
		if (auto *world=getObject()->logic().aiWorld())
			if (const auto *terrain=world->pathfinder().terrainView(); terrain && terrain->passagePlane(*getObject()->getPosition()))
				radius=mul(radius,m_aod->narrowScale);
		float a=SimMath::fstpDword(SimMath::pc24AddW(SimMath::atan2d(y,x),getObject()->getOrientation()));
		return {mul(SimMath::cosf32(a),radius),mul(SimMath::sinf32(a),radius),0};
	}
	void pushHistory(const Coord3D &p,float a)
	{
		m_history.insert(m_history.begin(),{p,a});
		if (m_history.size()>60) m_history.pop_back();
	}
	const Flow &flowFor(int slot) const
	{
		float x=core().slots().at(slot).x;
		for (size_t i=m_flow.size(); i>1; --i) if (m_flow[i-1].x==x) return m_flow[i-1];
		return m_flow.front();
	}
	void updateFlow()
	{
		// 0x0022F960: march backwards along the bounded history, then
		// limit each row's angular change to ten degrees per logic frame.
		float traversed=0; size_t index=0; Coord3D d{-1,0,0};
		for (auto &f:m_flow)
		{
			float remaining=sub(sub(m_flow.front().x,f.x),traversed), len;
			for (;;)
			{
				if (index+1<m_history.size()) { d=delta(m_history[index+1].position,m_history[index].position); d.z=0; len=length(d); }
				else len=99999.9f;
				if (len>remaining) break;
				++index; remaining=sub(remaining,len); traversed=add(traversed,len);
			}
			float norm=length(d);
			Coord3D dir=d;
			if (norm!=0) { dir.x=div(dir.x,norm); dir.y=div(dir.y,norm); }
			f.position={add(mul(dir.x,remaining),m_history[index].position.x),add(mul(dir.y,remaining),m_history[index].position.y),m_history[index].position.z};
			float current=m_history[index].angle, next=index+1<m_history.size()?m_history[index+1].angle:current;
			float desired=angle(add(mul(div(remaining,len),angle(sub(next,current))),current));
			f.angle=angle(add(f.angle,std::clamp(angle(sub(desired,f.angle)),-0.17453294f,0.17453294f)));
		}
	}
public:
	AodContain(Thing *t,const AodData *d):Bfme1HordeContain(t,d),m_aod(d)
	{
		getObject()->logic().noteStop("[S-5017] BFME1 AOD: native formation/melee and contact scheduling; low-level pathfinder, contact-coordinate and CRT boundaries apply");
		getObject()->logic().noteStop("[S-5017] BFME1 AOD: inherited death-filter fields are retained; death-filter execution remains unported");
	}
	const std::type_info &xferClass() const override { return typeid(AodContain); }
	void onObjectCreated() override
	{
		HordeContain::onObjectCreated(); m_waves.clear(); m_flow.clear(); m_history.clear();
		auto &rng=getObject()->logic().random();
		const char *file="F:\\bfme\\Code\\gameengine\\Source\\GameLogic\\Object\\Contain\\HordeContain\\AODHordeContain.cpp";
		for (const auto &s:core().slots())
		{
			std::array<float,6> w;
			w[1]=rng.getValueReal(sub(1,m_aod->amplitudeRandom),add(1,m_aod->amplitudeRandom),file,0x209);
			w[0]=rng.getValueReal(0,mul(m_aod->frequencyRandom,twoPi),file,0x20A);
			w[2]=rng.getValueReal(sub(1,m_aod->frequencyRandom),add(1,m_aod->frequencyRandom),file,0x20B);
			w[4]=rng.getValueReal(sub(1,m_aod->amplitudeRandomZ),add(1,m_aod->amplitudeRandomZ),file,0x20E);
			w[3]=rng.getValueReal(0,mul(m_aod->frequencyRandomZ,twoPi),file,0x20F);
			w[5]=rng.getValueReal(sub(1,m_aod->frequencyRandomZ),add(1,m_aod->frequencyRandomZ),file,0x210);
			m_waves.push_back(w);
		}
		seedFlow();
	}
	void onBuildComplete() override
	{
		// The port constructs modules before the creator places the object.
		// Seed position-dependent history at the existing placed/build-complete
		// boundary; the six per-slot RNG calls are not repeated.
		if (!payloadCreated()) seedFlow();
		HordeContain::onBuildComplete();
	}
	void seedFlow()
	{
		m_flow.clear();m_history.clear();
		for (const auto &s:core().slots())
		{
			if (std::any_of(m_flow.begin(),m_flow.end(),[&](const Flow &f){return f.x==s.x;})) continue;
			if (m_flow.size()==20) break; // native fixed capacity
			Coord3D p=rotated(sub(s.x,core().slots().front().x),0);
			const Coord3D &o=*getObject()->getPosition(); p={add(p.x,o.x),add(p.y,o.y),o.z};
			m_flow.push_back({s.x,getObject()->getOrientation(),p});
		}
		for (auto i=m_flow.rbegin();i!=m_flow.rend();++i) pushHistory(i->position,i->angle);
		if (!m_flow.empty()) pushHistory(m_flow.front().position,m_flow.front().angle);
	}

	bool runMemberPass() override
	{
		if (m_flow.empty()) return false;
		Object &o=*getObject(); float speed=20, acceleration=2;
		if (auto *ai=o.getAIUpdateInterface();ai && ai->curLocomotor())
		{
			speed=ai->curLocomotor()->getMaxSpeedForCondition(ai->locomotorHost());
			acceleration=ai->curLocomotor()->getMaxAcceleration(ai->locomotorHost());
		}
		const auto &p=*o.getPosition();const auto &front=m_flow.front().position;
		double dx=SimMath::pc24SubW(p.x,front.x),dy=SimMath::pc24SubW(p.y,front.y),dz=SimMath::pc24SubW(p.z,front.z);
		double distance=SimMath::sqrtPC24(SimMath::pc24AddW(SimMath::pc24AddW(SimMath::pc24MulW(dz,dz),SimMath::pc24MulW(dy,dy)),SimMath::pc24MulW(dx,dx)));
		if (distance>acceleration) pushHistory(p,o.getOrientation());
		updateFlow();
		if (o.testModelCondition(oath())) m_scatter=add(m_scatter,speed);
		const bool work=HordeContain::runMemberPass();
		return work || m_aod->still>0.01f || m_aod->stillZ>0.01f;
	}
	Coord3D getMemberFormationPosition(const Object *member) const override
	{
		if (frozenFormation()) return Bfme1HordeContain::getMemberFormationPosition(member);
		int index=core().slotOf(member->getID()); if (index<0) index=0;
		if (m_flow.empty() || core().slots().empty()) return *getObject()->getPosition();
		const auto &s=core().slots().at(index); const auto &f=flowFor(index);
		Coord3D p={SimMath::fstpDword(SimMath::pc24SubW(f.position.x,SimMath::pc24MulW(SimMath::sind(f.angle),s.y))),
			SimMath::fstpDword(SimMath::pc24AddW(f.position.y,SimMath::pc24MulW(SimMath::cosd(f.angle),s.y))),f.position.z};
		if ((size_t)index>=m_waves.size()) return p;
		float ratio=0;
		if (auto *ai=getObject()->getAIUpdateInterface();ai && ai->curLocomotor()) ratio=div(ai->curLocomotor()->speed(),ai->curLocomotor()->getMaxSpeedForCondition(ai->locomotorHost()));
		const auto &w=m_waves[index]; const auto &d=*m_aod;
		double frame=SimMath::fildU32(getObject()->logic().getFrame());
		auto wave=[&](float frequency,float frequencyFactor,float phase,float amplitude) {
			return SimMath::pc24MulW(SimMath::sinDD(SimMath::pc24AddW(SimMath::pc24MulW(SimMath::pc24MulW(frame,frequency),frequencyFactor),phase)),amplitude);
		};
		auto blend=[&](float still) { still=std::clamp(still,0.0f,1.0f); return SimMath::pc24AddW(SimMath::pc24MulW(SimMath::pc24SubW(1,still),ratio),still); };
		float xy=SimMath::fstpDword(SimMath::pc24MulW(SimMath::pc24MulW(blend(d.still),d.amplitude),SimMath::fstpDword(wave(d.frequency,w[2],w[0],w[1]))));
		Coord3D r=rotated(0,xy); float radius=length(r);
		float a=SimMath::fstpDword(SimMath::pc24AddW(SimMath::atan2d(r.y,r.x),f.angle));
		r={mul(SimMath::cosf32(a),radius),mul(SimMath::sinf32(a),radius),0};
		float z=SimMath::fstpDword(SimMath::pc24MulW(SimMath::pc24MulW(SimMath::pc24AddW(wave(d.frequencyZ,w[5],w[3],w[4]),0.95f),blend(d.stillZ)),d.amplitudeZ));
		if (member->testModelCondition(oath()))
		{
			float norm=length({s.x,s.y,0}); float x=s.x,y=s.y; if (norm!=0) {x=div(x,norm);y=div(y,norm);}
			float scatter=SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24MulW(d.scatterSpeed,m_scatter),SimMath::pc24MulW(SimMath::pc24MulW(SimMath::pc24MulW(index%5,d.scatterRandom),m_scatter),0.25f)));
			r.x=add(r.x,mul(x,scatter)); r.y=add(r.y,mul(y,scatter));
		}
		p.x=add(p.x,r.x);p.y=add(p.y,r.y);
		float surface=getObject()->logic().getGroundHeight(p.x,p.y);
		if (auto *ai=getObject()->getAIUpdateInterface()) {float water=0;if (ai->locomotorHost().isUnderwater(p.x,p.y,&water)) surface=water;}
		p.z=add(surface,z);
		if (m_large)
		{
			unsigned age=getObject()->logic().getFrame()-m_largeFrame;
			if (age>=d.timeout) {m_large=INVALID_ID;return p;} // native early return precedes oath Z
			float height=std::clamp(mul(d.heightFactor,m_largeHeight),d.minHeight,d.maxHeight);
			unsigned half=d.timeout>>1;float fade=age>half?sub(1,div((float)(age-half),(float)half)):1;
			Coord3D diff=delta(p,m_largePosition);float c=SimMath::cosf32(getObject()->getOrientation()),sn=SimMath::sinf32(getObject()->getOrientation());
			float forward=SimMath::fstpDword(SimMath::absD(SimMath::pc24AddW(SimMath::pc24MulW(c,diff.x),SimMath::pc24MulW(sn,diff.y))));
			float lateral=SimMath::fstpDword(SimMath::pc24SubW(SimMath::absD(SimMath::pc24AddW(SimMath::pc24MulW(-sn,diff.x),SimMath::pc24MulW(c,diff.y))),17));
			double distance=SimMath::pc24AddW(std::max(lateral,0.0f),forward),extent=SimMath::pc24MulW(height,d.tailOff);
			if (distance<extent)
			{
				double t=SimMath::pc24DivW(distance,extent);
				double hump=SimMath::pc24SubW(1,SimMath::pc24AddW(SimMath::pc24MulW(SimMath::sqrtPC24(t),t),SimMath::pc24MulW(SimMath::pc24MulW(t,t),SimMath::pc24SubW(1,t))));
				p.z=SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24MulW(SimMath::pc24MulW(hump,height),fade),p.z));
			}
		}
		if (member->testModelCondition(oath()))
			p.z=SimMath::fstpDword(SimMath::pc24AddW(p.z,SimMath::pc24AddW(SimMath::pc24MulW(SimMath::pc24MulW(d.scatterSpeed,m_scatter),d.oathZ),SimMath::pc24MulW(SimMath::pc24MulW(SimMath::pc24MulW(SimMath::pc24MulW((index+2)%5,d.scatterRandom),m_scatter),d.oathZ),0.25f))));
		return p;
	}
	void trackLargeUnit(Object *other) override
	{
		if (!other || other->isEffectivelyDead()) return;
		if (deadCollideSize(*other)!=2 || m_large==other->getID()) return;
		m_large=other->getID();m_largeFrame=getObject()->logic().getFrame();m_largePosition=*other->getPosition();
		m_largeHeight=ObjectGeometry::maxHeightAbovePosition(other->geometryShapes());
	}
protected:
	bool hasVariantMemberPositions() const override { return true; }
	void memberOrder(Object &member,const Coord3D &dest,float orientation,bool force) override
	{
		int justBuilt=CombatNames::modelCondition("JUST_BUILT");
		if (member.testModelCondition(justBuilt)) {Bfme1HordeContain::memberOrder(member,dest,orientation,force);member.setModelConditionState(justBuilt,false);return;}
		auto *ai=member.getAIUpdateInterface();
		if (ai && ai->isStateActive()) {member.setModelConditionState(moving(),false);return;}
		if (frozenFormation()) { moveHub(member,dest,orientation); return; }
		int index=core().slotOf(member.getID());if (index<0 || m_flow.empty()) return;
		orientation=flowFor(index).angle;float step=9999,turn=pi;
		if (ai && ai->curLocomotor()) {step=mul(ai->curLocomotor()->getMaxSpeedForCondition(ai->locomotorHost()),1.25f);turn=ai->curLocomotor()->getMaxTurnRate(ai->locomotorHost());}
		member.setModelConditionState(moving(),getObject()->testModelCondition(moving()));
		member.setModelConditionState(oath(),getObject()->testModelCondition(oath()));
		if (member.testModelCondition(oath()))
		{
			float relative=relativeAngle(member,dest);
			orientation=add(member.getOrientation(),std::clamp(relative,-turn,turn));
		}
		Coord3D d=delta(dest,*member.getPosition()),p=dest;float len=length(d);
		if (len>step) {d={mul(div(d.x,len),step),mul(div(d.y,len),step),mul(div(d.z,len),step)};const auto &old=*member.getPosition();p={add(old.x,d.x),add(old.y,d.y),add(old.z,d.z)};}
		member.setOrientation(orientation);member.setPosition(&p);
		if (auto *physics=PhysicsBehavior::find(member)) physics->setStunned(false);
	}
public:
	void crc(StateHasher &h) const override
	{
		Bfme1HordeContain::crc(h);h.addU32(m_large);h.addU32(m_largeFrame);hashPoint(h,m_largePosition);h.addFloat(m_largeHeight);h.addFloat(m_scatter);
		h.addU32((unsigned)m_waves.size());for (const auto &w:m_waves) for(float v:w) h.addFloat(v);
		h.addU32((unsigned)m_history.size());for(const auto &s:m_history){hashPoint(h,s.position);h.addFloat(s.angle);}
		h.addU32((unsigned)m_flow.size());for(const auto &s:m_flow){h.addFloat(s.x);h.addFloat(s.angle);hashPoint(h,s.position);}
	}
	void xfer(Xfer &x) override
	{
		Bfme1HordeContain::xfer(x);XferVersion v{1,1};x.xferVersion(v);x.xferUnsignedInt(&m_large);x.xferUnsignedInt(&m_largeFrame);xferPoint(x,m_largePosition);x.xferReal(&m_largeHeight);x.xferReal(&m_scatter);
		unsigned n=(unsigned)m_waves.size();x.xferUnsignedInt(&n);if(n>core().slots().size())throw XferException("BFME1 AOD wave count exceeds slots");m_waves.resize(n);for(auto &w:m_waves)for(float &f:w)x.xferReal(&f);
		n=(unsigned)m_history.size();x.xferUnsignedInt(&n);if(n>60)throw XferException("BFME1 AOD history exceeds 60");m_history.resize(n);for(auto &s:m_history){xferPoint(x,s.position);x.xferReal(&s.angle);}
		n=(unsigned)m_flow.size();x.xferUnsignedInt(&n);if(n>20)throw XferException("BFME1 AOD flow exceeds 20");m_flow.resize(n);for(auto &s:m_flow){x.xferReal(&s.x);x.xferReal(&s.angle);xferPoint(x,s.position);}
		if (!m_flow.empty() && m_history.empty())throw XferException("BFME1 AOD flow has no history");
	}
};
// AODCrushCollide, BFME1 ctor 0x00215C50 / data 0x00215B90,
// table 0x00CA98A8; callback 0x00215E50, update 0x00216100.
class AodCrushData final : public ModuleData
{
public:
	std::string smallFX,mediumFX,largeFX,smallOCL,mediumOCL,largeOCL;
	int damageType=DAMAGE_CRUSH,deathType=DEATH_NORMAL,specialType=DAMAGE_CRUSH,specialDeath=DEATH_NORMAL,selfType=DAMAGE_CRUSH,selfDeath=DEATH_NORMAL;
	float damage=0,specialDamage=0,selfDamage=0;
	ObjectFilter special;
	static void buildFieldParse(MultiIniFieldParse &p)
	{
#define AOD_FIELD(name,proc,table,field) {name,proc,table,(int)offsetof(AodCrushData,field)}
		static const FieldParse fields[]={
			AOD_FIELD("SmallFXList",INI::parseAsciiString,nullptr,smallFX), AOD_FIELD("MediumFXList",INI::parseAsciiString,nullptr,mediumFX), AOD_FIELD("LargeFXList",INI::parseAsciiString,nullptr,largeFX),
			AOD_FIELD("SmallObjectCreationList",INI::parseAsciiString,nullptr,smallOCL), AOD_FIELD("MediumObjectCreationList",INI::parseAsciiString,nullptr,mediumOCL), AOD_FIELD("LargeObjectCreationList",INI::parseAsciiString,nullptr,largeOCL),
			AOD_FIELD("Damage",INI::parseReal,nullptr,damage), AOD_FIELD("DamageType",INI::parseIndexList,TheDamageNames,damageType), AOD_FIELD("DeathType",INI::parseIndexList,TheWeaponDeathNames,deathType),
			AOD_FIELD("SpecialDamage",INI::parseReal,nullptr,specialDamage), AOD_FIELD("SpecialDamageType",INI::parseIndexList,TheDamageNames,specialType), AOD_FIELD("SpecialDeathType",INI::parseIndexList,TheWeaponDeathNames,specialDeath),
			AOD_FIELD("SelfDamage",INI::parseReal,nullptr,selfDamage), AOD_FIELD("SelfDamageType",INI::parseIndexList,TheDamageNames,selfType), AOD_FIELD("SelfDeathType",INI::parseIndexList,TheWeaponDeathNames,selfDeath),
			AOD_FIELD("SpecialObject",ParseObjectFilter,nullptr,special),{nullptr,nullptr,nullptr,0}
		};
#undef AOD_FIELD
		p.add(fields);
	}
};
class AodCrush final : public UpdateModule, public CollideModuleInterface
{
	const AodCrushData *m_crush;
	unsigned m_expire=0;
	ObjectID m_last=INVALID_ID;
	bool m_active=false;
public:
	AodCrush(Thing *t,const AodCrushData *d):UpdateModule(t,d),m_crush(d)
	{
		friend_setNextCallFrame((unsigned)UPDATE_SLEEP_FOREVER);
		getObject()->logic().noteStop("[S-5017] BFME1 AODCrushCollide: native damage, LARGE tracking and firing deadline; contact generation retains S-581's planar overlap scheduling");
	}
	const std::type_info &xferClass() const override {return typeid(AodCrush);}
	CollideModuleInterface *getCollide() override {return this;}
	bool wouldLikeToCollideWith(const Object &) const override {return false;}
	void onCollide(Object *other,const Coord3D *,const Coord3D *) override
	{
		Object &self=*getObject();
		auto *physics=PhysicsBehavior::find(self),*otherPhysics=other?PhysicsBehavior::find(*other):nullptr;
		if (!other || (otherPhysics && otherPhysics->isStunned()) || self.isEffectivelyDead() || self.testModelCondition(oath()) || (physics && physics->isStunned()) || other->getRelationship(self)==ALLIES) return;
		if (Object *parent=self.getContainedBy()) if (auto *contain=parent->getContain()) if (auto *horde=contain->getHordeContainInterface()) horde->trackLargeUnit(other);
		if (other->isEffectivelyDead() || !otherPhysics || otherPhysics->isStunned()) return;
		auto &logic=self.logic();
		if (!m_active) {m_active=true;self.setModelConditionState(CombatNames::modelCondition("FIRING_A"),true);setWakeFrame(&self,(UpdateSleepTime)10);}
		m_expire=logic.getFrame()+10;
		bool special=ObjectFilterMatch::allows(logic,m_crush->special,*other,self.getControllingPlayer());
		DamageInfo info;info.m_input.m_sourceID=self.getID();
		info.m_input.m_damageType=special?m_crush->specialType:m_crush->damageType;
		info.m_input.m_deathType=special?m_crush->specialDeath:m_crush->deathType;
		info.m_input.m_amount=special?m_crush->specialDamage:m_crush->damage;
		other->attemptDamage(info);m_last=other->getID();
		if (special) {DamageInfo hit;hit.m_input.m_sourceID=other->getID();hit.m_input.m_damageType=m_crush->selfType;hit.m_input.m_deathType=m_crush->selfDeath;hit.m_input.m_amount=m_crush->selfDamage;self.attemptDamage(hit);}
		const int size=deadCollideSize(*other);
		const std::string &fx=size==0?m_crush->smallFX:size==1?m_crush->mediumFX:m_crush->largeFX;
		const std::string &ocl=size==0?m_crush->smallOCL:size==1?m_crush->mediumOCL:m_crush->largeOCL;
		auto event=FXEventLog::objectEvent(FXEvent::OBJECT_FX,"AODCrushCollide",logic.getFrame(),fx,*other);event.secondary=self.getID();logic.fxEvents().emit(event);
		if (!ocl.empty() && ocl!="None")
		{
			const auto *list=TheObjectCreationListStore?TheObjectCreationListStore->findObjectCreationList(ocl):nullptr;
			if (!list) logic.reportError("[S-5017] BFME1 AODCrushCollide: unresolved object creation list "+ocl);
			else {const Coord3D second=*self.getPosition();list->create(logic,other,*other->getPosition(),&second);}
		}
	}
	UpdateSleepTime update() override
	{
		if (!m_active) return UPDATE_SLEEP_FOREVER;
		if (m_expire<getObject()->logic().getFrame()) {getObject()->setModelConditionState(CombatNames::modelCondition("FIRING_A"),false);m_active=false;}
		return UPDATE_SLEEP_NONE;
	}
	void crc(StateHasher &h) const override {UpdateModule::crc(h);h.addU32(m_expire);h.addU32(m_last);h.addBool(m_active);}
	void xfer(Xfer &x) override {xferUpdateModule(x);XferVersion v{1,1};x.xferVersion(v);x.xferUnsignedInt(&m_expire);x.xferUnsignedInt(&m_last);x.xferBool(&m_active);}
};

}

void registerBfme1HordeVariants(ModuleFactory &modules)
{
	for(const char *name:{"HordeContain","HorseHordeContain"})
	{
		const auto info=*modules.findModuleTemplate(name,MODULETYPE_BEHAVIOR);
		modules.bindDataProc(name,MODULETYPE_BEHAVIOR,[fallback=info.newModuleData](INI *ini)->std::shared_ptr<ModuleData>{
			if(!ini || !ini->bfme1Data())return fallback(ini);
			auto data=std::make_shared<Bfme1HordeData>();ini->initFromINIMultiProc(data.get(),&Bfme1HordeData::buildFieldParse);return data;
		});
	}
	const auto ordinary=*modules.findModuleTemplate("HordeContain",MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("HordeContain",MODULETYPE_BEHAVIOR,[fallback=ordinary.newModule](Thing *t,const ModuleData *d,const ModuleFactory::ModuleTemplate &info)->std::unique_ptr<Module>{
		if(auto *o=dynamic_cast<Object *>(t);o && o->getTemplate()->bfme1Data())return std::make_unique<Bfme1HordeContain>(t,dynamic_cast<const HordeContainBehaviorData *>(d));
		return fallback(t,d,info);
	});

	const auto crush=*modules.findModuleTemplate("AODCrushCollide",MODULETYPE_BEHAVIOR);
	modules.bindDataProc("AODCrushCollide",MODULETYPE_BEHAVIOR,[fallback=crush.newModuleData](INI *ini)->std::shared_ptr<ModuleData>{
		if (!ini || !ini->bfme1Data())return fallback(ini);
		auto data=std::make_shared<AodCrushData>();ini->initFromINIMultiProc(data.get(),&AodCrushData::buildFieldParse);return data;
	});
	modules.bindModuleProc("AODCrushCollide",MODULETYPE_BEHAVIOR,[fallback=crush.newModule](Thing *t,const ModuleData *d,const ModuleFactory::ModuleTemplate &info)->std::unique_ptr<Module>{
		if (auto *typed=dynamic_cast<const AodCrushData *>(d))return std::make_unique<AodCrush>(t,typed);
		return fallback(t,d,info);
	});
	const auto horse=*modules.findModuleTemplate("HorseHordeContain",MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("HorseHordeContain",MODULETYPE_BEHAVIOR,[fallback=horse.newModule](Thing *thing,const ModuleData *data,const ModuleFactory::ModuleTemplate &info)->std::unique_ptr<Module>{
		if (auto *object=dynamic_cast<Object *>(thing);object && object->getTemplate()->bfme1Data())return std::make_unique<HorseContain>(thing,dynamic_cast<const HordeContainBehaviorData *>(data));
		return fallback(thing,data,info);
	});
	const auto original=*modules.findModuleTemplate("AODHordeContain",MODULETYPE_BEHAVIOR);
	modules.bindDataProc("AODHordeContain",MODULETYPE_BEHAVIOR,[fallback=original.newModuleData](INI *ini)->std::shared_ptr<ModuleData>{
		if (!ini || !ini->bfme1Data()) return fallback(ini);
		auto data=std::make_shared<AodData>();ini->initFromINIMultiProc(data.get(),&AodData::buildFieldParse);return data;
	});
	modules.bindModuleProc("AODHordeContain",MODULETYPE_BEHAVIOR,[fallback=original.newModule](Thing *thing,const ModuleData *data,const ModuleFactory::ModuleTemplate &info)->std::unique_ptr<Module>{
		if (auto *typed=dynamic_cast<const AodData *>(data))return std::make_unique<AodContain>(thing,typed);
		return fallback(thing,data,info);
	});
}

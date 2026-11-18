// OpenBFME. GPL-3.0.
// Original BFME1 1.03: dirty list 0x009A2350, invalidation 0x009A2FE0,
// movement score 0x009A2420, sweep 0x009A3AD0, sort 0x009A2750,
// overlap 0x009A3960, contact hash 0x009A3770, dispatch 0x009A30B0.
// Source interface: Object 0x001BFC20/80/CE0, group 0x001CAB00,
// response 0x001CAB50. RW uses 0xB6C700/0xB6D060/0x68C7B2;
// its established runtime is deliberately separate. Geometry/CRT stops remain.
#include "GameLogic/AI/Bfme1ContactManager.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/NotifyCrushModules.h"
#include "GameLogic/Module/ProjectileModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameLogic/SnapshotXfer.h"
#include "GameLogic/SimMath.h"
#include "Common/StateHash.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace {
using Key=std::pair<ObjectID,ObjectID>;
Key key(ObjectID a,ObjectID b){return a<b?Key{a,b}:Key{b,a};}
unsigned bucket(Key k){return ((k.first<<16)+k.second)%0x493u;}
bool registered(const Object &o)
{
	// BFME1 0x001CA2E0..0x001CA430: NO_COLLIDE / HORDE have no source record.
	return o.isInWorld() && !o.isDestroyed() && !o.isOutOfCollisionManager() && !o.isKindOfName("NO_COLLIDE") && !o.isKindOfName("HORDE");
}
std::uint64_t group(const Object &o)
{
	// Native KindOf bit 2 is IMMOBILE (table 0x00EAA068), not STRUCTURE.
	if(o.isKindOfName("IMMOBILE"))return 1;
	return (std::uint64_t)(o.getContainedBy()?o.getContainedBy()->getID():o.getID())+2;
}
}
void Bfme1ContactManager::dirty(Node &n)
{
	if(!n.dirty){n.dirty=true;m_dirty.insert(m_dirty.begin(),n.id);}
}
void Bfme1ContactManager::changed(Object &o)
{
	auto i=m_nodes.find(o.getID());
	if(!registered(o)){if(i!=m_nodes.end()){i->second.source=false;dirty(i->second);}return;}
	if(i==m_nodes.end())
	{
		Node n;n.id=o.getID();n.source=true;n.group=group(o);i=m_nodes.emplace(n.id,n).first;
	}
	i->second.source=true;dirty(i->second);
}
void Bfme1ContactManager::removed(Object &o)
{
	if(auto i=m_nodes.find(o.getID());i!=m_nodes.end()){i->second.source=false;dirty(i->second);}
}
Object *Bfme1ContactManager::source(ObjectID id) const
{
	auto i=m_nodes.find(id);if(i==m_nodes.end() || !i->second.source)return nullptr;
	Object *o=m_logic.findObjectByID(id);return o && registered(*o)?o:nullptr;
}
float Bfme1ContactManager::value(const Endpoint &e,unsigned axis) const
{
	const auto &n=m_nodes.at(e.id);return e.maximum?n.hi[axis]:n.lo[axis];
}
void Bfme1ContactManager::eraseContact(Key k)
{
	if(!m_contacts.erase(k))return;
	auto &b=m_buckets[bucket(k)];b.erase(std::remove(b.begin(),b.end(),k),b.end());
}
void Bfme1ContactManager::add(ObjectID a,ObjectID b)
{
	if(a==b || m_nodes.at(a).group==m_nodes.at(b).group)return;
	Key k=key(a,b);unsigned &count=m_pairs[k];
	if(++count==3){m_contacts.emplace(k,Contact{});m_buckets[bucket(k)].insert(m_buckets[bucket(k)].begin(),k);}
}
void Bfme1ContactManager::drop(ObjectID a,ObjectID b)
{
	auto i=m_pairs.find(key(a,b));if(i==m_pairs.end())return;
	if(i->second==3)eraseContact(i->first);
	if(--i->second==0)m_pairs.erase(i);
}
void Bfme1ContactManager::unlink(Node &n)
{
	for(auto i=m_pairs.begin();i!=m_pairs.end();)
		if(i->first.first==n.id || i->first.second==n.id){eraseContact(i->first);i=m_pairs.erase(i);}else ++i;
	for(auto &axis:m_axes)axis.erase(std::remove_if(axis.begin(),axis.end(),[&](auto &e){return e.id==n.id;}),axis.end());
}
void Bfme1ContactManager::insert(Node &n,unsigned axis)
{
	// 0x009A3AD0 inserts the LOWER endpoint first, after equal values,
	// then opens overlaps until the UPPER endpoint (before equal values).
	auto &v=m_axes[axis];std::vector<ObjectID> open;size_t at=0;
	while(at<v.size() && !(value(v[at],axis)>n.lo[axis]))
	{
		auto e=v[at++];if(!e.maximum)open.insert(open.begin(),e.id);else open.erase(std::remove(open.begin(),open.end(),e.id),open.end());
	}
	v.insert(v.begin()+at,{n.id,false});++at;
	if(at<v.size())
	{
		for(auto id:open)add(id,n.id);
		while(at<v.size() && value(v[at],axis)<n.hi[axis]){if(!v[at].maximum)add(v[at].id,n.id);++at;}
	}
	v.insert(v.begin()+at,{n.id,true});
}
unsigned Bfme1ContactManager::refresh(Node &n,bool first)
{
	Object *o=source(n.id);if(!o)return 1;
	auto g=group(*o);bool groupChanged=g!=n.group;n.group=g;
	const auto &axis=m_axes[0];auto max=std::find_if(axis.begin(),axis.end(),[&](auto &e){return e.id==n.id && e.maximum;});
	if(!groupChanged && first && max!=axis.end() && max!=axis.begin())return 0;
	const auto &p=*o->getPosition();float r=CombatQueries::boundingCircleRadius(*o);float lo[3]{},hi[3]{};
	lo[0]=SimMath::pc24Sub(p.x,r);lo[1]=SimMath::pc24Sub(p.y,r);hi[0]=SimMath::pc24Add(p.x,r);hi[1]=SimMath::pc24Add(p.y,r);
	float below=0;for(const auto &s:o->geometryShapes())if(s.active)
		below=std::max(below,SimMath::pc24Sub(s.type==ObjectGeometry::SHAPE_SPHERE?s.majorRadius:0,s.offsetZ));
	lo[2]=SimMath::pc24Sub(p.z,below);hi[2]=SimMath::pc24Add(p.z,ObjectGeometry::maxHeightAbovePosition(o->geometryShapes()));
	float d=std::max((float)SimMath::absD(SimMath::pc24Sub(n.hi[0],hi[0])),(float)SimMath::absD(SimMath::pc24Sub(n.hi[1],hi[1])));
	unsigned score=d>50?std::min(10000u,(unsigned)(11-SimMath::truncToInt32(SimMath::pc24Mul(SimMath::pc24Sub(d,50),-0.01f)))):1;
	std::copy(lo,lo+3,n.lo);std::copy(hi,hi+3,n.hi);return score;
}
void Bfme1ContactManager::mergeSort(unsigned axis)
{
	// 0x009A2750 splits into two alternating REVERSED lists before its
	// bottom-up merges. std::stable_sort would change equal endpoint order.
	std::array<std::deque<Endpoint>,4> lists;int out=0;
	for(auto e:m_axes[axis]){lists[out].push_front(e);out^=1;}
	int src=0;size_t run=1;
	while(!lists[src+1].empty())
	{
		auto &a=lists[src],&b=lists[src+1];out=src^2;lists[out].clear();lists[out+1].clear();
		while(!a.empty())
		{
			size_t na=run,nb=run;
			while((na && !a.empty()) || (nb && !b.empty()))
			{
				bool takeB=(!na||a.empty()) || (nb && !b.empty() && value(a.front(),axis)>value(b.front(),axis));
				auto &from=takeB?b:a;lists[out].push_back(from.front());from.pop_front();if(takeB)--nb;else --na;
			}
			out^=1;
		}
		run<<=1;src^=2;
	}
	m_axes[axis].assign(lists[src].begin(),lists[src].end());
}
void Bfme1ContactManager::respond(Object &self,Object &other)
{
	// Object::onCollide 0x001C8A80. The second comparison really tests
	// this object's nonzero ID, not equality to the other ignore ID.
	if(self.bfme1CollisionIgnoreID()==other.getID() || (other.bfme1CollisionIgnoreID()!=0 && self.getID()!=0))return;
	// Native Object response stops on NO_COLLISIONS before each module.
	// Its contact source does not write point/normal: use explicit zeroes
	// rather than retail's uninitialized storage (reported boundary S-5017).
	const Coord3D zero{};
	if(self.testStatus((unsigned)CombatNames::statuses().noCollisions))return;
	if(auto *ai=self.getAIUpdateInterface())if(auto *adapter=m_logic.aiWorld()->findAdapter(other.getID()))ai->onCollide(*adapter);
	for(auto *c:self.collideModules())
	{
		if(!source(self.getID()) || self.testStatus((unsigned)CombatNames::statuses().noCollisions))break;
		c->onCollide(&other,&zero,&zero);
	}
	// Bezier's collision interface is represented separately in this runtime.
	if(source(self.getID()))if(auto *b=dynamic_cast<BezierProjectileBehavior *>(self.findModule("BezierProjectileBehavior"));b && b->projectileCanCollideWith(&other))b->projectileHandleCollision(&other);
	++m_responses;
}
void Bfme1ContactManager::update()
{
	m_tests=m_responses=0;
	for(auto i=m_contacts.begin();i!=m_contacts.end();)
	{
		Key k=i->first;++i;
		if(m_nodes.at(k.first).dirty || m_nodes.at(k.second).dirty)
		{if(!source(k.first)||!source(k.second))eraseContact(k);else m_contacts.at(k).state=0;}
	}
	unsigned score=0;std::vector<ObjectID> deferred;
	for(auto id:m_dirty)
	{
		auto &n=m_nodes.at(id);unsigned s=refresh(n,true);
		if(!s){deferred.push_back(id);continue;}
		score+=s;unlink(n);
		if(source(id)){for(unsigned axis=0;axis<3;++axis)insert(n,axis);n.dirty=false;}
		else m_nodes.erase(id);
	}
	for(auto id:deferred){auto &n=m_nodes.at(id);score+=refresh(n,false);n.dirty=false;}
	m_dirty.clear();
	if(m_nodes.empty())return;
	if(score<=m_nodes.size())
	{
		for(unsigned axis=0;axis<3;++axis)
		{
			auto &v=m_axes[axis];for(size_t i=1;i<v.size();++i)
			{
				Endpoint e=v[i];size_t j=i;
				while(j && value(v[j-1],axis)>value(e,axis))
				{
					Endpoint before=v[j-1];if(before.maximum && !e.maximum)add(before.id,e.id);else if(!before.maximum && e.maximum)drop(before.id,e.id);
					v[j]=before;--j;
				}
				v[j]=e;
			}
		}
	}
	else
	{
		m_pairs.clear();m_contacts.clear();for(auto &b:m_buckets)b.clear();
		for(unsigned axis=0;axis<3;++axis)
		{
			mergeSort(axis);std::vector<ObjectID> open;
			for(auto e:m_axes[axis])if(!e.maximum){for(auto id:open)add(id,e.id);open.insert(open.begin(),e.id);}else open.erase(std::remove(open.begin(),open.end(),e.id),open.end());
		}
	}
	for(auto &bucket:m_buckets)
	{
		// Source removal during response leaves nodes queued until next update.
		// New records cannot create contacts until the next sweep.
		const auto order=bucket;
		for(Key k:order)
		{
			auto it=m_contacts.find(k);if(it==m_contacts.end())continue;
			Object *a=source(k.first),*b=source(k.second);if(!a||!b)continue;
			if(it->second.state==0){++m_tests;it->second.state=NotifyCrushModules::geometriesOverlap(a->geometryShapes(),*a->getPosition(),a->getOrientation(),b->geometryShapes(),*b->getPosition(),b->getOrientation())?1:2;}
			if(it->second.state!=1)continue;
			respond(*a,*b);a=source(k.first);b=source(k.second);if(a&&b)respond(*b,*a);
		}
	}
}
void Bfme1ContactManager::crc(StateHasher &h) const
{
	h.addU32((unsigned)m_nodes.size());for(auto &[id,n]:m_nodes){h.addU32(id);h.addU64(n.group);h.addBool(n.source);h.addBool(n.dirty);for(int a=0;a<3;++a){h.addFloat(n.lo[a]);h.addFloat(n.hi[a]);}}
	h.addU32((unsigned)m_dirty.size());for(auto id:m_dirty)h.addU32(id);
	for(auto &axis:m_axes){h.addU32((unsigned)axis.size());for(auto e:axis){h.addU32(e.id);h.addBool(e.maximum);}}
	h.addU32((unsigned)m_pairs.size());for(auto &[k,n]:m_pairs){h.addU32(k.first);h.addU32(k.second);h.addU32(n);}
	for(auto &b:m_buckets){h.addU32((unsigned)b.size());for(auto k:b){h.addU32(k.first);h.addU32(k.second);h.addU32(m_contacts.at(k).state);}}
}
void Bfme1ContactManager::xfer(Xfer &x)
{
	XferVersion v{1,1};x.xferVersion(v);
	auto size=[&](unsigned n){x.xferUnsignedInt(&n);if(n>1000000)throw XferException("BFME1 contact count exceeds limit");return n;};
	unsigned n=size((unsigned)m_nodes.size());if(x.isLoading())m_nodes.clear();
	std::vector<ObjectID> ids;for(auto &[id,_]:m_nodes)ids.push_back(id);
	for(unsigned i=0;i<n;++i)
	{
		ObjectID id=x.isLoading()?0:ids[i];x.xferObjectID(&id);if(!id || (x.isLoading() && m_nodes.count(id)))throw XferException("BFME1 duplicate contact source");
		auto &node=m_nodes[id];node.id=id;unsigned low=(unsigned)node.group,high=(unsigned)(node.group>>32);x.xferUnsignedInt(&low);x.xferUnsignedInt(&high);node.group=((std::uint64_t)high<<32)|low;
		x.xferBool(&node.source);x.xferBool(&node.dirty);for(int a=0;a<3;++a){x.xferReal(&node.lo[a]);x.xferReal(&node.hi[a]);}
	}
	auto id=[&](ObjectID &id){x.xferObjectID(&id);if(!m_nodes.count(id))throw XferException("BFME1 contact references absent source");};
	n=size((unsigned)m_dirty.size());m_dirty.resize(n);std::set<ObjectID> dirtyIds;for(auto &d:m_dirty){id(d);if(!dirtyIds.insert(d).second || !m_nodes.at(d).dirty)throw XferException("BFME1 invalid contact dirty order");}
	for(auto &axis:m_axes){n=size((unsigned)axis.size());axis.resize(n);std::set<std::pair<ObjectID,bool>> seen;for(auto &e:axis){id(e.id);x.xferBool(&e.maximum);if(!seen.emplace(e.id,e.maximum).second)throw XferException("BFME1 duplicate endpoint");}}
	n=size((unsigned)m_pairs.size());std::vector<std::pair<Key,unsigned>> pairs(m_pairs.begin(),m_pairs.end());if(x.isLoading())m_pairs.clear();
	for(unsigned i=0;i<n;++i){Key k=x.isLoading()?Key{}:pairs[i].first;unsigned count=x.isLoading()?0:pairs[i].second;id(k.first);id(k.second);x.xferUnsignedInt(&count);if(k.first>=k.second || count<1||count>3 || (x.isLoading()&&m_pairs.count(k)))throw XferException("BFME1 invalid overlap pair");m_pairs[k]=count;}
	if(x.isLoading())m_contacts.clear();
	for(unsigned bi=0;bi<m_buckets.size();++bi){auto &b=m_buckets[bi];n=size((unsigned)b.size());b.resize(n);for(auto &k:b){id(k.first);id(k.second);unsigned state=x.isLoading()?0:m_contacts.at(k).state;x.xferUnsignedInt(&state);if(bucket(k)!=bi || state>2 || m_pairs.find(k)==m_pairs.end()||m_pairs.at(k)!=3 || (x.isLoading()&&m_contacts.count(k)))throw XferException("BFME1 invalid cached contact");m_contacts[k]={state};}}
	if(x.isLoading())
	{
		for(auto &[sourceId,node]:m_nodes)
		{
			if(node.dirty!=bool(dirtyIds.count(sourceId)) || (node.source && !m_logic.findObjectByID(sourceId)))throw XferException("BFME1 inconsistent contact source");
			int previous=-1;
			for(unsigned axis=0;axis<3;++axis)
			{
				if(!std::isfinite(node.lo[axis]) || !std::isfinite(node.hi[axis]))throw XferException("BFME1 nonfinite contact bounds");
				int count=0;for(auto &e:m_axes[axis])if(e.id==sourceId)++count;
				if((count!=0 && count!=2) || (previous>=0 && previous!=count) || (count==0 && !node.dirty))throw XferException("BFME1 incomplete contact endpoints");
				previous=count;
			}
		}
		for(auto &[k,count]:m_pairs)if(count==3 && !m_contacts.count(k))throw XferException("BFME1 missing cached contact");
	}

}

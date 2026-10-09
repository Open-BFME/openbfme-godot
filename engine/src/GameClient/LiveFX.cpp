// OpenBFME. GPL-3.0. See GameClient/LiveFX.h.

#include "GameClient/LiveFX.h"

#include "Common/AsciiString.h"
#include "Common/Audio/AudioEntryPoints.h"
#include "Common/Audio/AudioEventRTS.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/LiveGame.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDrawBones.h"
#include "GameLogic/System/InvisibilityManager.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/ObjectTemplateInfo.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <iterator>

namespace
{
Matrix3D thingMatrix(const Coord3D &pos, const float *basis)
{
	Matrix3D m;
	for (int r = 0; r < 3; ++r)
	{
		m.Row[r][0] = basis[r * 3 + 0];
		m.Row[r][1] = basis[r * 3 + 1];
		m.Row[r][2] = basis[r * 3 + 2];
	}
	m.Row[0][3] = pos.x;
	m.Row[1][3] = pos.y;
	m.Row[2][3] = pos.z;
	return m;
}

ModelConditionFlags toFlags(const std::array<std::uint32_t, 19> &bits)
{
	ModelConditionFlags f;
	for (int w = 0; w < 19; ++w)
	{
		std::uint32_t v = bits[(size_t)w];
		for (int b = 0; v != 0; ++b, v >>= 1)
		{
			if (v & 1u)
			{
				f.set(w * 32 + b);
			}
		}
	}
	return f;
}

const char *kNoneBone = "none";

// An object as the FXList nuggets see it: the values of the call (the snapshot of the event) and the live object for the questions only it can answer.
class LiveFXObject : public FXObject
{
public:
	LiveFXObject(GameLogic &logic, Object *obj, ObjectID id, const Coord3D &pos, const Matrix3D &mtx, const ModelConditionFlags &flags)
		: m_logic(logic), m_obj(obj), m_id(id), m_pos(pos), m_mtx(mtx), m_flags(flags)
	{
	}
	static LiveFXObject of(GameLogic &logic, Object &obj)
	{
		return LiveFXObject(logic, &obj, obj.getID(), *obj.getPosition(), thingMatrix(*obj.getPosition(), obj.getBasis()), toFlags(obj.getModelConditionBits()));
	}
	std::uint32_t objectId() const override { return m_id; }
	Coord3D position() const override { return m_pos; }
	Matrix3D transform() const override { return m_mtx; }
	ModelConditionFlags modelConditions() const override { return m_flags; }
	bool passesObjectFilter(const ObjectFilter &filter) const override
	{
		return m_obj && ObjectFilterMatch::allows(m_logic, filter, *m_obj, m_logic.players().getLocalPlayer());
	}
	bool hasDrawable() const override { return m_obj && m_obj->clientHooks() != nullptr; } // SMOOTH-1: a client-bound object has its drawable
	float boundingCircleRadius() const override { return m_obj ? CombatQueries::boundingCircleRadius(*m_obj) : 0.0f; }
	int controllingPlayerIndex() const override
	{
		const Player *p = m_obj ? m_obj->getControllingPlayer() : nullptr;
		return p ? p->getPlayerIndex() : -1;
	}
	bool isControlledByLocalPlayer() const override
	{
		const Player *p = m_obj ? m_obj->getControllingPlayer() : nullptr;
		return p && p == m_logic.players().getLocalPlayer();
	}
	int relationshipToLocalPlayer() const override
	{
		const Player *local = m_logic.players().getLocalPlayer();
		return (m_obj && local) ? (int)local->getRelationship(m_obj->getTeam()) : (int)NEUTRAL;
	}
	bool hasKindOf(const char *kindOfName) const override { return m_obj && m_obj->isKindOfName(kindOfName); }
	bool isHorde() const override { return m_obj && m_obj->isKindOfName("HORDE"); }

private:
	GameLogic &m_logic;
	Object *m_obj;
	ObjectID m_id;
	Coord3D m_pos;
	Matrix3D m_mtx;
	ModelConditionFlags m_flags;
};

// SMOOTH-1 (merge with FX-2): what a nugget reads of an object, captured at the logic's call on the simulation owner. The object filters are evaluated for
// the filters of the FXLists the call can play (the list, or every choice of a list call); a filter that was not captured fails and is counted.
struct CapturedFXObject : public FXObject
{
	std::uint32_t id = 0;
	Coord3D pos;
	Matrix3D mtx;
	ModelConditionFlags flags;
	bool drawable = false;
	float radius = 0.0f;
	int owner = -1;
	bool localControlled = false;
	int relationship = (int)NEUTRAL;
	KindOfMaskType kindOf{};
	bool horde = false;
	std::map<const ObjectFilter *, bool> filters;
	mutable unsigned long long *uncapturedFilters = nullptr;

	std::uint32_t objectId() const override { return id; }
	Coord3D position() const override { return pos; }
	Matrix3D transform() const override { return mtx; }
	ModelConditionFlags modelConditions() const override { return flags; }
	bool passesObjectFilter(const ObjectFilter &filter) const override
	{
		auto it = filters.find(&filter);
		if (it == filters.end())
		{
			if (uncapturedFilters)
			{
				++*uncapturedFilters;
			}
			return false;
		}
		return it->second;
	}
	bool hasDrawable() const override { return drawable; }
	float boundingCircleRadius() const override { return radius; }
	int controllingPlayerIndex() const override { return owner; }
	bool isControlledByLocalPlayer() const override { return localControlled; }
	int relationshipToLocalPlayer() const override { return relationship; }
	bool hasKindOf(const char *kindOfName) const override
	{
		const int bit = ObjectTemplateInfoBuilder::kindOfIndex(kindOfName);
		return bit >= 0 && MaskTest(kindOf, (unsigned)bit);
	}
	bool isHorde() const override { return horde; }
};

LiveFXObject eventPrimary(GameLogic &logic, Object *obj, const FXEvent &e)
{
	Matrix3D m;
	for (int r = 0; r < 3; ++r)
	{
		for (int c = 0; c < 4; ++c)
		{
			m.Row[r][c] = e.transform[r * 4 + c];
		}
	}
	return LiveFXObject(logic, obj, e.primary, e.position, m, toFlags(e.conditions));
}
} // namespace

LiveFX::LiveFX(LiveGame &game, FXPlayback &playback) : m_game(game), m_playback(playback)
{
	m_playback.setWorld(this);
	m_game.logic().fxEvents().setSink(this);
	m_game.drawables().setFXHost(this);
}

LiveFX::~LiveFX()
{
	m_game.drawables().setFXHost(nullptr);
	if (m_game.logic().fxEvents().sink() == this)
	{
		m_game.logic().fxEvents().setSink(nullptr);
	}
	if (m_playback.world() == this)
	{
		m_playback.setWorld(nullptr);
	}
}

std::vector<std::string> LiveFX::stops()
{
	std::vector<std::string> out = FXEventLog::stops();
	out.push_back("[S-682] live FX: bone positions of ParticleSysBone systems and fire FX come from the model's bind pose (FollowBone systems do not follow the "
			 "animation); ParticleSysBone FXTrigger / Persist / PersistID / HouseColor are ignored and OnlyIfOnWater systems are not created; the shooter's "
			 "stealth (RW 0x694C0D, NULL viewer) is read when the effect plays, not at the shot (lane STEALTH-1); muzzle flashes and recoil are not drawn; isWater is false and the shroud is clear for the FX");
	out.push_back("[S-686] TransitionDamageFX: the module class (RW data table 0xC08D10, 117 rows: Damaged / ReallyDamaged / Rubble FXList, OCL and "
				  "ParticleSystem 1..12 with Loc / Bone + RandomBone, the Damage*Types flags, RubbleNeighbor, the sub object show / hide lists; 150 retail uses) "
				  "is raw (S-070) and has no runtime: its damage state transition effects are not played (the draw states' EnteringStateFX and ParticleSysBone are)");
	return out;
}

Object *LiveFX::object(ObjectID id) const
{
	return id != INVALID_ID ? m_game.logic().findObjectByID(id) : nullptr;
}

std::uint32_t LiveFX::logicFrame() const
{
	return m_game.logic().getFrame();
}

float LiveFX::groundHeight(float x, float y)
{
	return m_game.logic().getGroundHeight(x, y);
}

bool LiveFX::playSound(const std::string &eventName, const Coord3D *pos, int playerIndex)
{
	AudioManager *audio = AudioApi::current();
	if (!audio)
	{
		return false; // noted as an FXPlayback event, and AudioApi counts nothing: no manager is a wiring state, not a call
	}
	++m_stats.sounds;
	// RW 0x5DF85E: the event carries the controlling player of the object the nugget played on (-1: none)
	AudioEventRTS event = pos ? AudioEventRTS(eventName, *pos) : AudioEventRTS(eventName);
	event.setPlayerIndex(playerIndex);
	audio->addAudioEvent(event);
	return true;
}

// the bone's transform in the model's bind pose, the drawable scale applied to the whole matrix (ZH scales the root transform before reading the bone).
// `subObjects`: also accept a sub object of that name (the FX paths, ZH doSingleBoneName); false for ParticleSysBone, whose lookup is bones only (lane FX-3:
// RW 0x4C6514 asks the render object's vtable +0xC8 with the bone name, at 0x4C671A .. 0x4C6728, and tests the returned index against 0; INFERENCE from ZH
// rendobj.h's virtual order and the +0xCC call that takes that index: Get_Bone_Index, which an HLod answers from its HTree's pivots alone)
bool LiveFX::modelBone(const RenderObjPrototype *proto, const std::string &bone, float scale, Matrix3D &out, bool subObjects)
{
	if (!proto || !proto->Tree || bone.empty() || bone == kNoneBone)
	{
		return false;
	}
	auto key = std::make_pair(proto, (subObjects ? "s:" : "b:") + bone);
	auto it = m_boneCache.find(key);
	if (it == m_boneCache.end())
	{
		std::pair<bool, Matrix3D> v(false, Matrix3D());
		// ZH doSingleBoneName: the bone (0 = not found: ZH treats the root as not found), else a sub object of that name and the bone it hangs on
		int index = proto->Tree->Get_Bone_Index(bone);
		if (index <= 0 && subObjects)
		{
			const int sub = W3DFindSubObjectByName(*proto, bone);
			index = sub >= 0 ? proto->SubObjects[(size_t)sub].BoneIndex : -1;
		}
		if (index > 0)
		{
			HTreePose pose;
			proto->Tree->Base_Pose(Matrix3D(), pose);
			if ((size_t)index < pose.Transform.size())
			{
				v = std::make_pair(true, pose.Transform[(size_t)index]);
			}
		}
		if (!v.first && m_stats.missingBones.size() < 200)
		{
			m_stats.missingBones.insert(proto->Name + ":" + bone);
		}
		it = m_boneCache.emplace(key, v).first;
	}
	if (!it->second.first)
	{
		return false;
	}
	out = it->second.second;
	if (scale != 1.0f)
	{
		for (int r = 0; r < 3; ++r)
		{
			for (int c = 0; c < 4; ++c)
			{
				out.Row[r][c] *= scale;
			}
		}
	}
	return true;
}

FXParticleSystem::ParticleAttachInfo LiveFX::attachedDrawable(std::uint32_t drawableId, const std::string &boneName)
{
	FXParticleSystem::ParticleAttachInfo info;
	Drawable *d = m_game.drawables().find(drawableId);
	if (!d)
	{
		return info;
	}
	info.found = true;
	info.position = *d->getPosition();
	info.transform = thingMatrix(info.position, d->getBasis());
	if (!boneName.empty())
	{
		for (const DrawEntry &e : d->entries())
		{
			if (e.draw && modelBone(e.draw->frame().model, boneName, d->getInstanceScale(), info.boneTransform))
			{
				info.hasBone = true;
				break;
			}
		}
	}
	return info;
}

FXParticleSystem::ParticleAttachInfo LiveFX::attachedObject(std::uint32_t objectId, const std::string &boneName, int)
{
	FXParticleSystem::ParticleAttachInfo info;
	Object *o = object(objectId);
	if (!o || o->isDestroyed())
	{
		return info;
	}
	info.found = true;
	info.position = *o->getPosition();
	info.transform = thingMatrix(info.position, o->getBasis());
	const Drawable *od = m_game.drawables().findByObject(o->getID());
	if (!boneName.empty() && od)
	{
		for (const DrawEntry &e : od->entries())
		{
			if (e.draw && modelBone(e.draw->frame().model, boneName, od->getInstanceScale(), info.boneTransform))
			{
				info.hasBone = true;
				break;
			}
		}
	}
	return info;
}

bool LiveFX::boneWorldMatrix(const FXObject &obj, const std::string &boneName, Matrix3D &out)
{
	const Drawable *d = m_game.drawables().findByObject(obj.objectId()); // the render side's drawable (SMOOTH-1)
	if (!d)
	{
		return false;
	}
	std::string lower = boneName;
	for (char &c : lower)
	{
		c = (char)std::tolower((unsigned char)c);
	}
	for (const DrawEntry &e : d->entries())
	{
		Matrix3D bone;
		if (e.draw && modelBone(e.draw->frame().model, lower, d->getInstanceScale(), bone))
		{
			Matrix3D::Multiply(thingMatrix(*d->getPosition(), d->getBasis()), bone, &out);
			return true;
		}
	}
	return false;
}

std::vector<FXBoneTransform> LiveFX::boneWorldTransforms(const FXObject &obj, const std::string &boneName, int startIndex, int maxBones)
{
	// RW 0x672AEE: the plain name for index 0, else NAME01.. until the first miss
	std::vector<FXBoneTransform> out;
	for (int i = startIndex; (int)out.size() < maxBones && i <= (startIndex != 0 ? 99 : 0); ++i)
	{
		char buffer[300];
		if (i == 0)
		{
			std::snprintf(buffer, sizeof(buffer), "%s", boneName.c_str());
		}
		else
		{
			std::snprintf(buffer, sizeof(buffer), "%s%02d", boneName.c_str(), i);
		}
		FXBoneTransform t;
		if (!boneWorldMatrix(obj, buffer, t.transform))
		{
			break;
		}
		const Vector3 p = t.transform.Get_Translation();
		t.position = Coord3D{ p.X, p.Y, p.Z };
		out.push_back(t);
	}
	return out;
}

// DONOR ZH W3DModelDraw::handleWeaponFireFX through RW 0x671402 (every model draw in list order, the first that takes it wins)
bool LiveFX::handleWeaponFireFX(const Drawable &shooter, const FXEvent &e, const FXList *fx)
{
	const Drawable *d = &shooter;
	for (const DrawEntry &entry : d->entries())
	{
		if (!entry.draw)
		{
			continue;
		}
		const W3DDrawFrame f = entry.draw->frame();
		const ModelConditionInfo *state = f.modelState;
		if (!state || e.weaponSlot < 0 || (size_t)e.weaponSlot >= state->weaponFireFXBone.size())
		{
			continue;
		}
		const std::string &boneName = state->weaponFireFXBone[(size_t)e.weaponSlot];
		if (boneName.empty())
		{
			continue; // no barrel for the slot in this module: retail's barrel list is empty, the module does not take it
		}
		// the barrel's fire FX bone: NAME01.. (barrel + 1), else the plain name (ZH validateWeaponBarrelInfo's numbered then plain lookup)
		char numbered[300];
		std::snprintf(numbered, sizeof(numbered), "%s%02d", boneName.c_str(), e.barrel + 1);
		Matrix3D bone;
		const float scale = d->getInstanceScale();
		const bool found = modelBone(f.model, numbered, scale, bone) || modelBone(f.model, boneName, scale, bone);
		Matrix3D world = thingMatrix(*d->getPosition(), d->getBasis());
		if (found)
		{
			Matrix3D::Multiply(thingMatrix(*d->getPosition(), d->getBasis()), bone, &world);
		}
		else
		{
			++m_stats.boneMisses;
		}
		const Vector3 t = world.Get_Translation();
		const Coord3D pos{ t.X, t.Y, t.Z };
		FXList::doFXPos(fx, m_playback.services(), m_playback.fxLists(), &pos, &world, e.weaponSpeed, &e.secondaryPosition);
		++m_stats.fireFXAtBone;
		return true;
	}
	return false;
}

struct LiveFX::Captured : public CapturedFXObject
{
};

void LiveFX::onFXEvent(const FXEvent &e)
{
	// on the simulation owner: capture, never play (the playback, the drawables and the audio belong to the render side)
	GameLogic &logic = m_game.logic(); // on the simulation owner: LiveGame::logic does not wait there
	std::vector<const FXList *> lists;
	auto addList = [&](const std::string &n) {
		if (const FXList *fx = m_playback.fxLists().findFXList(n))
		{
			lists.push_back(fx);
		}
	};
	if (e.kind != FXEvent::OBJECT_SOUND)
	{
		if (e.choices)
		{
			for (const std::string &n : *e.choices)
			{
				addList(n);
			}
		}
		else if (e.fxList)
		{
			addList(*e.fxList);
		}
	}
	const Player *local = logic.players().getLocalPlayer();
	auto capture = [&](ObjectID id, bool primary) -> std::shared_ptr<Captured> {
		Object *o = object(id);
		if (!o)
		{
			return nullptr;
		}
		auto c = std::make_shared<Captured>();
		c->id = o->getID();
		c->pos = *o->getPosition();
		c->mtx = thingMatrix(*o->getPosition(), o->getBasis());
		c->flags = toFlags(o->getModelConditionBits());
		c->drawable = o->clientHooks() != nullptr;
		c->radius = CombatQueries::boundingCircleRadius(*o);
		const Player *p = o->getControllingPlayer();
		c->owner = p ? p->getPlayerIndex() : -1;
		c->localControlled = p && p == local;
		c->relationship = local ? (int)local->getRelationship(o->getTeam()) : (int)NEUTRAL;
		c->kindOf = o->getKindOf();
		c->horde = o->isKindOfName("HORDE");
		for (const FXList *fx : lists)
		{
			for (const std::unique_ptr<FXNugget> &n : fx->nuggets())
			{
				const ObjectFilter *f = primary ? n->m_sourceObjectFilter.get() : n->m_objectFilter.get();
				if (f && !c->filters.count(f))
				{
					c->filters[f] = ObjectFilterMatch::allows(logic, *f, *o, local);
				}
			}
		}
		c->uncapturedFilters = &m_stats.uncapturedFilters;
		return c;
	};
	Pending p;
	p.event = e;
	p.primary = capture(e.primary, true);
	p.secondary = capture(e.secondary, false);
	m_pending.push_back(std::move(p));
}

void LiveFX::flushPending()
{
	// the render side at a worker-idle point (the live drawables and the logic may be read; nothing runs beside it)
	std::vector<Pending> events;
	events.swap(m_pending);
	for (const Pending &p : events)
	{
		play(p);
	}
	std::vector<std::pair<DrawableID, std::string>> states;
	states.swap(m_pendingStates);
	for (const auto &st : states)
	{
		playEnteringStateFX(st.first, st.second);
	}
}

void LiveFX::play(const Pending &pending)
{
	const FXEvent &e = pending.event;
	// a list call: the pick is a client draw (RW 0x6D32E4) made here, in the call order of the logic's synchronous events
	const std::string *name = e.fxList;
	if (e.choices)
	{
		const int pick = m_playback.clientRandom().value(0, (int)e.choices->size() - 1);
		name = &(*e.choices)[(size_t)pick];
		if (!FXEventLog::isFXName(*name) || (e.kind == FXEvent::OBJECT_SOUND && AsciiStringUtil::compareNoCase(*name, "NoSound") == 0))
		{
			++m_stats.skipped["picked None"];
			return;
		}
	}
	// AUDIO-3: the request log names the FX call site, the list and the object it played on
	AudioLog::Scope logScope([&] { return std::string("fx ") + e.site + " " + *name + " obj " + std::to_string((unsigned)e.primary); });
	if (e.kind == FXEvent::OBJECT_SOUND)
	{
		// the sound on the object (RW TheAudio + 100 with the object's id): played at the object's position of the call
		if (playSound(*name, &e.position, pending.primary ? pending.primary->owner : -1))
		{
			++m_stats.played[e.site];
		}
		else
		{
			++m_stats.skipped["no audio manager"];
		}
		return;
	}
	const FXList *fx = m_playback.fxLists().findFXList(*name);
	if (!fx)
	{
		m_stats.missingFXLists.insert(*name);
		++m_stats.skipped["unknown FXList"];
		return;
	}
	// SMOOTH-1: the objects as they were at the call (captured), the event's own position / transform / conditions for the primary
	CapturedFXObject primaryView;
	if (pending.primary)
	{
		primaryView = *pending.primary;
	}
	primaryView.id = e.primary;
	primaryView.pos = e.position;
	{
		Matrix3D m;
		for (int r = 0; r < 3; ++r)
		{
			for (int c = 0; c < 4; ++c)
			{
				m.Row[r][c] = e.transform[r * 4 + c];
			}
		}
		primaryView.mtx = m;
	}
	primaryView.flags = toFlags(e.conditions);
	primaryView.uncapturedFilters = &m_stats.uncapturedFilters;
	const CapturedFXObject *secondaryObj = pending.secondary.get();
	const Drawable *primaryDrawable = m_game.drawables().findByObject(e.primary);
	switch (e.kind)
	{
	case FXEvent::POSITION_FX:
	{
		Matrix3D m;
		if (e.hasTransform)
		{
			m = primaryView.transform();
		}
		FXList::doFXPos(fx, m_playback.services(), m_playback.fxLists(), &e.position, e.hasTransform ? &m : nullptr, 0.0f, nullptr);
		break;
	}
	case FXEvent::OBJECT_FX:
	{
		const CapturedFXObject &p = primaryView;
		if (e.bone && pending.primary && primaryDrawable)
		{
			// lane INTEG-1, RW 0x821236 .. 0x8212CF (a LevelUpFx entry with a bone on an object with a drawable): the transform starts as the identity and takes
			// the bone's world transform from the first draw module that has it (RW 0x672B5B, the result is not tested: a miss plays at the identity);
			// doFXPos(fx, its translation, it, 0, null)
			Matrix3D m;
			if (!boneWorldMatrix(p, *e.bone, m))
			{
				++m_stats.boneMisses;
			}
			const Vector3 t = m.Get_Translation();
			const Coord3D pos{ t.X, t.Y, t.Z };
			FXList::doFXPos(fx, m_playback.services(), m_playback.fxLists(), &pos, &m, 0.0f, nullptr);
			break;
		}
		FXList::doFXObj(fx, m_playback.services(), m_playback.fxLists(), &p, secondaryObj);
		break;
	}
	case FXEvent::OBJECT_SOUND:
		break; // handled above
	case FXEvent::WEAPON_FIRE_FX:
	{
		// RW 0x6CC915: no drawable on the shooter, no fire FX block at all
		if (!pending.primary || !pending.primary->drawable || !primaryDrawable)
		{
			++m_stats.skipped["FireFX without a drawable"];
			return;
		}
		// RW 0x6CC915 (lane STEALTH-1): the block runs when the shooter is the local player's (RW 0x68B749), or not stealthed and undetected (RW 0x6CCB91 calls
		// RW 0x694C0D with a NULL viewer), or the weapon has PlayFXWhenStealthed (+0x135)
		if (const Object *shooter = object(e.primary))
		{
			const Player *local = m_game.logic().players().getLocalPlayer();
			if ((!local || shooter->getControllingPlayer() != local) && !e.playWhenStealthed && InvisibilityManager::isStealthedAndUndetected(*shooter, nullptr))
			{
				++m_stats.skipped["FireFX of a shooter stealthed for the local player"];
				return;
			}
		}
		if (!handleWeaponFireFX(*primaryDrawable, e, fx))
		{
			// RW 0x4B1B5A: doFXObj(fx, source, victim)
			FXList::doFXObj(fx, m_playback.services(), m_playback.fxLists(), &primaryView, secondaryObj);
			++m_stats.fireFXOnObject;
		}
		break;
	}
	}
	++m_stats.played[e.site];
}

bool LiveFX::enteringStateFX(DrawableID id, const std::string &name)
{
	// SMOOTH-1: called while the render side applies the client events (the worker may be running): queued, played at the next idle point
	if (!m_game.drawables().find(id))
	{
		return false;
	}
	m_pendingStates.emplace_back(id, name);
	return true;
}

bool LiveFX::playEnteringStateFX(DrawableID id, const std::string &name)
{
	Drawable *d = m_game.drawables().find(id);
	Object *o = d ? object(d->getObjectID()) : nullptr;
	if (!o)
	{
		return false;
	}
	const FXList *fx = m_playback.fxLists().findFXList(name);
	if (!fx)
	{
		m_stats.missingFXLists.insert(name);
		return false;
	}
	const LiveFXObject p = LiveFXObject::of(m_game.logic(), *o);
	FXList::doFXObj(fx, m_playback.services(), m_playback.fxLists(), &p, nullptr);
	++m_stats.played["EnteringStateFX"];
	return true;
}

void LiveFX::destroySystems(Attached &a)
{
	for (FXParticleSystem::ParticleSystemID id : a.systems)
	{
		if (FXParticleSystem::ParticleSystem *s = m_playback.particles().findParticleSystemByID(id))
		{
			s->destroy(); // ZH stopClientParticleSystems: the system stops emitting, its particles live out
			++m_stats.attachedDestroyed;
		}
	}
	a.systems.clear();
}

void LiveFX::createSystems(Drawable &d, Attached &a)
{
	const float scale = d.getInstanceScale();
	for (const DrawEntry &e : d.entries())
	{
		if (!e.draw)
		{
			continue;
		}
		const W3DDrawFrame f = e.draw->frame();
		for (const std::vector<ParticleSysBoneInfo> *list : { f.modelState ? &f.modelState->particleSysBones : nullptr, f.animationState ? &f.animationState->particleSysBones : nullptr })
		{
			if (!list)
			{
				continue;
			}
			for (const ParticleSysBoneInfo &info : *list)
			{
				if (info.onlyIfOnWater)
				{
					continue; // S-682: no water query
				}
				// RW 0x73AECB (the ParticleSysBone parser's template field): the name is looked up in TheFXParticleSystemManager alone (findTemplate
				// RW 0x5F889B); "None" (any case, RW 0xBD3BF8) and an unknown name store NULL, and RW 0x4C6514 skips a NULL template (0x4C6603) without a
				// word. Retail never loads Data\INI\ParticleSystem.ini (lane FX-3, see LiveFX.h), so a name defined only there plays nothing in retail
				// either; the name is listed for the report, not played
				if (AsciiStringUtil::compareNoCase(info.systemName, "None") == 0)
				{
					continue;
				}
				const FXParticleSystem::ParticleSystemTemplate *t = m_playback.particleTemplates().findTemplate(info.systemName);
				if (!t)
				{
					m_stats.unresolvedParticleSystems.insert(info.systemName);
					continue;
				}
				const FXParticleSystem::ParticleSystemID id = m_playback.particles().createParticleSystem(t, true);
				FXParticleSystem::ParticleSystem *sys = m_playback.particles().findParticleSystemByID(id);
				if (!sys)
				{
					continue; // refused (cap / LOD)
				}
				Coord3D pos{ 0.0f, 0.0f, 0.0f };
				float rotation = 0.0f;
				Matrix3D bone;
				// RW 0x4C6514: a bone index of 0 (no such bone; IBUrukPit_A's CONSTDUSTBONE01, a retail data defect) leaves the position at (0, 0, 0)
				// and the rotation at 0 (0x4C6644 .. 0x4C665B), and the system is still created and attached: it plays at the drawable's origin
				if (modelBone(f.model, info.boneName, scale, bone, false))
				{
					const Vector3 v = bone.Get_Translation();
					pos = Coord3D{ v.X, v.Y, v.Z };
					rotation = std::atan2(bone.Row[1][0], bone.Row[0][0]); // Matrix3D::Get_Z_Rotation
				}
				else if (!info.boneName.empty() && info.boneName != kNoneBone)
				{
					++m_stats.boneMisses;
				}
				sys->setPosition(pos);
				sys->rotateLocalTransformZ(rotation);
				sys->attachToDrawable(d.getID());
				a.systems.push_back(id);
				++m_stats.attachedCreated;
			}
		}
	}
}

// RW 0x4B337B: did the animation pass `frame` between the previous and the current integer frame? It reads the track's mode (+0x10) and direction (+0x14):
// the loop modes treat a step against their direction as the wrap, the once / manual modes follow the direction, ping pong wraps at its reflection.
bool LiveFX::framePassed(int prev, int cur, int frame, int mode, int direction)
{
	if (prev == cur)
	{
		return false;
	}
	if (cur == frame)
	{
		return true;
	}
	switch (mode)
	{
	case W3D_ANIM_MODE_LOOP:
		return prev < cur ? (prev < frame && frame < cur) : (frame > prev || frame < cur);
	case W3D_ANIM_MODE_ONCE:
	case W3D_ANIM_MODE_PLAY_TO_FRAME:
	case W3D_ANIM_MODE_ONCE_BACKWARDS:
		return direction == 1 ? (prev < frame && frame < cur) : (cur < frame && frame < prev);
	case W3D_ANIM_MODE_LOOP_PINGPONG:
		if (direction == 1)
		{
			return prev < cur ? (prev < frame && frame < cur) : frame < prev;
		}
		return cur < prev ? (cur < frame && frame < prev) : frame > prev;
	case W3D_ANIM_MODE_LOOP_BACKWARDS:
		return cur < prev ? (cur < frame && frame < prev) : (frame < prev || frame > cur);
	default:
		return false;
	}
}

// RW 0x4BABBA: the event's FX at the drawable's object, or at the bone
void LiveFX::fireFrameEvent(Drawable &d, const FXEventInfo &ev, const RenderObjPrototype *model)
{
	const FXList *fx = m_playback.fxLists().findFXList(ev.fxListName);
	if (!fx)
	{
		m_stats.missingFXLists.insert(ev.fxListName);
		return;
	}
	++m_stats.frameEventsFired;
	++m_stats.played["FXEvent"];
	if (ev.bone.empty())
	{
		if (Object *o = object(d.getObjectID()))
		{
			const LiveFXObject p = LiveFXObject::of(m_game.logic(), *o);
			FXList::doFXObj(fx, m_playback.services(), m_playback.fxLists(), &p, nullptr);
		}
		return;
	}
	std::string lower = ev.bone;
	for (char &c : lower)
	{
		c = (char)std::tolower((unsigned char)c);
	}
	Matrix3D world = thingMatrix(*d.getPosition(), d.getBasis());
	Matrix3D bone;
	if (modelBone(model, lower, d.getInstanceScale(), bone))
	{
		Matrix3D::Multiply(thingMatrix(*d.getPosition(), d.getBasis()), bone, &world);
	}
	else
	{
		++m_stats.boneMisses;
	}
	const Vector3 t = world.Get_Translation();
	const Coord3D pos{ t.X, t.Y, t.Z };
	FXList::doFXPos(fx, m_playback.services(), m_playback.fxLists(), &pos, &world, 0.0f, nullptr);
}

// RW 0x4BCE68, the first loop (see the header comment)
void LiveFX::frameEvents(Drawable &d, size_t entryIndex, const DrawEntry &e)
{
	const AnimationStateInfo *as = e.draw->currentAnimationState();
	const auto key = std::make_pair(d.getID(), entryIndex);
	if (!as || as->fxEvents.empty())
	{
		m_frameEvents.erase(key);
		return;
	}
	const W3DDrawFrame f = e.draw->frame();
	if (f.trackCount < 1)
	{
		return;
	}
	const int cur = (int)f.tracks[0].frame;
	const int prev = (int)f.tracks[0].prevFrame;
	FrameEventState &st = m_frameEvents[key];
	if (st.animState == as && st.lastFrame == cur)
	{
		return; // evaluated for this integer frame already
	}
	st.animState = as;
	st.lastFrame = cur;
	for (const FXEventInfo &ev : as->fxEvents)
	{
		const int frame = ev.frame, step = ev.frameStep, stop = ev.frameStop;
		bool fire = false;
		if (!ev.fireWhenSkipped)
		{
			fire = (frame <= cur && (cur <= stop || cur == frame)) || (frame < 0 && (step == 0 || cur % step == 0));
		}
		else if (stop < 1)
		{
			if (frame < 0)
			{
				if (step == 0 || step == 1)
				{
					fire = true;
				}
				else
				{
					// every skipped frame that is a multiple of the step, in the animation's direction
					if (f.tracks[0].direction == 1)
					{
						for (int i = prev + 1; i <= cur; ++i)
						{
							if (i % step == 0)
							{
								fireFrameEvent(d, ev, f.model);
							}
						}
					}
					else
					{
						for (int i = prev - 1; i >= cur; --i)
						{
							if (i % step == 0)
							{
								fireFrameEvent(d, ev, f.model);
							}
						}
					}
				}
			}
			else
			{
				fire = framePassed(prev, cur, frame, f.tracks[0].mode, f.tracks[0].direction); // RW 0x4B337B
			}
		}
		else
		{
			fire = (frame <= cur && cur <= stop) || (prev < frame && stop < cur);
		}
		if (fire)
		{
			fireFrameEvent(d, ev, f.model);
		}
	}
}

void LiveFX::updateAttachedSystems()
{
	DrawableManager &dm = m_game.drawables();
	// drawables that are gone
	for (auto it = m_attached.begin(); it != m_attached.end();)
	{
		if (!dm.find(it->first))
		{
			destroySystems(it->second);
			it = m_attached.erase(it);
		}
		else
		{
			++it;
		}
	}
	for (auto it = m_frameEvents.begin(); it != m_frameEvents.end();)
	{
		it = dm.find(it->first.first) ? std::next(it) : m_frameEvents.erase(it);
	}
	std::vector<const void *> states;
	for (DrawableID id = 1; id < (DrawableID)dm.slotCount(); ++id)
	{
		Drawable *d = dm.find(id);
		if (!d)
		{
			continue;
		}
		states.clear();
		for (const DrawEntry &e : d->entries())
		{
			if (e.draw)
			{
				states.push_back(e.draw->currentModelState());
				states.push_back(e.draw->currentAnimationState());
			}
		}
		for (size_t k = 0; k < d->entries().size(); ++k)
		{
			if (d->entries()[k].draw)
			{
				frameEvents(*d, k, d->entries()[k]);
			}
		}
		auto it = m_attached.find(id);
		if (it != m_attached.end() && it->second.states == states)
		{
			continue;
		}
		if (it == m_attached.end())
		{
			it = m_attached.emplace(id, Attached()).first;
		}
		destroySystems(it->second);
		it->second.states = states;
		createSystems(*d, it->second);
	}
	unsigned long long live = 0;
	for (const auto &kv : m_attached)
	{
		live += kv.second.systems.size();
	}
	m_stats.attachedLive = live;
}

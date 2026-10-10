// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Drawable (ZH Include/GameClient/Drawable.h, Source/GameClient/Drawable.cpp:349-505, 4162-4186), lane LOGIC-1: the client half of a live
// Object. It owns the draw modules of the object's template (built from the merged draw runtime: W3DScriptedModelDraw / W3DHordeModelDraw
// for model draws, static model names for tree / prop / floor draws), the object's model condition flags, its instance scale, its
// house colour and a RENDER transform that follows the object.
//
// TARGET FACTS (RotWK game.dat, caveat S-001): the Drawable constructor RW 0x679FD7 makes the draw modules in template list order, each
// skipped only when the GameLOD module gate is on and its minimum LOD is above the static LOD (S-114: every module is created here); the
// instance scale starts as the template's Scale; the Object binds the drawable (RW 0x628882 sendObjectCreated -> Object::friend_bindToDrawable,
// which calls onDrawableBoundToObject on every module). DONOR (ZH Object::friend_bindToDrawable, Object.cpp:2975-3010): with GameData
// ForceModelsToFollowTimeOfDay / ForceModelsToFollowWeather a new drawable gets NIGHT / SNOW when the map's time of day / weather is.
//
// RENDER INTERPOLATION (stops S-151, S-810 .. S-813, lane SMOOTH-1). Retail records the object's transform in logic phase 2 (Object::recordTransform,
// RW 0x6260E1), gathers the drawable's interpolation inputs in phase 1 of the next frame (RW 0x674B1F, the "drawableCallback" row) and draws RW 0x6765B9:
// a Catmull-Rom position through the previous, recorded, current and pending positions and a slerped rotation, by the client alpha. This port gathers the
// same inputs into the immutable snapshot of a completed frame (GameClient/LogicSnapshot) and draws RenderInterpolation::retailPose from it
// (syncFromSnapshot). The drawable knows its object by id only and never reads or writes logic state; it consumes no simulation randomness (the draw
// modules use their own client random, W3DClientRandom).
// What the draw modules do not do yet: bones (turrets, barrels, particle bones are not built: S-095 / S-097), the Lua BeginScript bodies run
// on LUA-1's real Lua (W3DLuaDrawScriptHost; what its api cannot answer is S-126), the draw variants' own motion (S-112), effect draws (S-113).

#pragma once

#include "Common/Module.h"
#include "Common/ModelState.h"
#include "Common/Thing/Thing.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/ClientEvents.h"
#include "GameClient/LogicSnapshot.h"
#include "GameClient/RenderInterpolation.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDrawModules.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDrawServices.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DLuaDrawScriptHost.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DScriptedModelDraw.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <memory>
#include <string>
#include <vector>

class Object;

typedef std::uint32_t DrawableID;

// The services every draw module of a game shares (one per DrawableManager).
class ModuleFactory;
class AnimationSoundModuleManager;

// lane FX-2: the client effect player the draw modules call (GameClient/LiveFX). Read at call time, so drawables made before the player was installed use it too.
class DrawableFXHost
{
public:
	virtual ~DrawableFXHost() = default;
	// RW 0x4BF222: a model draw of drawable `id` entered an animation state with EnteringStateFX `fx`; true when it played
	virtual bool enteringStateFX(DrawableID id, const std::string &fx) = 0;
};

struct DrawServices
{
	DrawServices(WW3DAssetManager &a, ModuleFactory &m, const W3DLuaDrawScriptHost::Options &hostOptions)
		: assets(a)
		, modules(m)
		, drawAssets(a)
		, random(RandomAlgorithm::ZH_CarryChain)
		, host(hostOptions)
	{
		random.seed(1); // a fixed client seed (the retail initial client seed is S-093)
	}
	WW3DAssetManager &assets;
	ModuleFactory &modules;  ///< makes the ClientUpdate / ClientBehavior modules (every class registered; unported ones are explicit)
	WW3DDrawAssets drawAssets;
	W3DClientRandom random;
	W3DLuaDrawScriptHost host; ///< the real Lua of the drawable state (lane LUA-1): BeginScript bodies of every draw module
	DrawableFXHost *fx = nullptr; ///< lane FX-2 (null: EnteringStateFX and ParticleSysBone stay S-095)
	AnimationSoundModuleManager *animationSounds = nullptr; ///< lane AUDIO-4: TheAnimationSoundModuleManager of the game's drawables
};

// One draw module of a drawable.
struct DrawEntry
{
	std::string className;
	std::string tag;
	W3DDrawKind kind = W3D_DRAWKIND_NOT_DRAWN;
	const ModuleData *data = nullptr;
	std::string staticModel;          ///< tree / prop / floor kinds: the model the module shows ("" when none)
	std::string notDrawnReason;       ///< why a non-model entry draws nothing
	std::unique_ptr<W3DScriptedModelDraw> draw; ///< MODEL kind
	bool animated = false;            ///< the idle animation moves: advance() each render frame
	bool missingModel = false;        ///< the model is not in the mounted archives (a retail data defect): not drawn
	std::string lastModel;            ///< the model last handed to the device layer (the device layer keeps its own handle)
	bool moduleHidden = false;        ///< a CurDrawableHideModule / ObjectHideModule request of a script hid this module (Drawable::showModule, RW 0x6789B4): not drawn
	float constructionOffsetZ = 0.0f; ///< RENDER-2: the model's move along its own Z while being built (RW 0x4B686D, ADJUST_HEIGHT_BY_CONSTRUCTION_PERCENT)
	bool conditionHidden = false;     ///< RENDER-2: a W3DFloorDraw whose HideIfModelConditions match the drawable's flags (the foundation floor while being built)
	std::vector<ModelConditionFlags> hideIf; ///< RENDER-2: that floor draw's HideIfModelConditions (one set per line)
	// lane COMBAT-4: a W3DTruckDraw's tire spin (RW 0x4CBFFB): the front and rear wheel angles (W3DTruckDraw + 0x310 / + 0x314; the mid tires take the front's / the
	// rear's: + 0x318 / + 0x31C) and the bones they turn (the module data's tire bone names; the front set, then the rear set)
	float tireFront = 0.0f, tireRear = 0.0f;
	std::vector<std::string> frontTireBones, rearTireBones;
	unsigned tireChanges = 0; ///< bumped when an angle changed (the device layer re-poses the instance)
};

class Drawable : public Thing
{
public:
	// Builds the draw modules of `tt` (a final override). Problems are appended to `problems` ("class: message"), never dropped.
	// SMOOTH-1: the drawable knows its object by id only (it lives on the render side; the object belongs to the simulation's thread)
	// `clientModules`: the template's ClientUpdate / ClientBehavior modules as the logic owner resolved them (ClientEvent::clientModules; review r2:
	// the drawable never interns into the logic's name keys)
	Drawable(DrawServices &services, const ThingTemplate *tt, DrawableID id, ObjectID objectId, const std::vector<ClientEvent::ClientModule> &clientModules,
		std::vector<std::string> &problems);

	Drawable *asDrawable() override { return this; }
	DrawableID getID() const { return m_id; }
	ObjectID getObjectID() const { return m_objectId; }
	// SMOOTH-1: the object's KindOf bits as the creation event carried them (Object::isKindOfName for the render side)
	void setKindOf(const KindOfMaskType &kindOf) { m_kindOf = kindOf; }
	bool isKindOfName(const char *name) const;
	// lane FX-3: the object's target record as the latest event or snapshot carried it (what the draw scripts ask: DrawableScriptTarget.h)
	void setScriptTarget(const DrawableScriptTarget &t) { m_scriptTarget = t; }
	const DrawableScriptTarget &scriptTarget() const { return m_scriptTarget; }

	// ZH Drawable::setModelConditionState / clearModelConditionState and the whole-set form this layer needs
	const ModelConditionFlags &getModelConditionFlags() const { return m_flags; }
	// lane COMBAT-4, RW 0x4BF2D8 (W3DModelDraw::replaceModelConditionState): the union of the model draws' DependencySharedModelFlags (+ 0xBC of the module data)
	ModelConditionFlags dependencySharedModelFlags() const;
	// the dependent's half of RW 0x4BF2D8: the container's flags within `shared` are set, the shared ones it lacks cleared (RW 0x67651E clearAndSet on the dependent
	// drawable); the draw modules take the result at once when it changed. True when it changed
	bool applyDependencyFlags(const ModelConditionFlags &shared, const ModelConditionFlags &containerFlags);
	void setModelConditionFlags(const ModelConditionFlags &flags);
	void setModelCondition(int bit, bool on);
	// lane ANIM-1, RW 0x679512 / 0x67449C (see ClientEvents.h): a changed bit is stored and marks the drawable dirty; flushModelConditions hands the
	// stored flags to the draw modules (setModelConditionFlags) when dirty, with the object's weapon cycle for UseWeaponTiming as the flush event carried it
	void storeModelCondition(int bit, bool on);
	void flushModelConditions(bool hasWeaponTiming, int weaponTimingFrames);
	bool modelConditionsDirty() const { return m_flagsDirty; }
	// the weapon cycle the last flush carried (-1: the object had no current weapon), what UseWeaponTiming animations divide by
	int weaponTimingFrames() const { return m_hasWeaponTiming ? m_weaponTimingFrames : -1; }
	// incremented whenever the model condition flags are set: the device layer re-reads the draw frames of a drawable whose count changed
	unsigned getChangeCount() const { return m_changeCount; }

	float getInstanceScale() const { return m_scale; }
	void setInstanceScale(float scale) { m_scale = scale; }
	// 0xAARRGGBB of the owner's colour, 0 when the owner has none
	std::uint32_t getHouseColor() const { return m_houseColor; }
	void setHouseColor(std::uint32_t argb) { m_houseColor = argb; }
	// lane CAH-2 (RW 0x6727B0): the house colour set the draw modules recolour with (kind 0 none, 1 .. 3 colours, HouseColor.h); the revision moves on
	// every set so the device layer re-applies it
	void setCustomColors(int kind, const std::uint32_t colors[3])
	{
		m_customKind = kind;
		for (int i = 0; i < 3; ++i)
		{
			m_customColors[i] = colors[i];
		}
		++m_customRevision;
	}
	int customColorKind() const { return m_customKind; }
	const std::uint32_t *customColors() const { return m_customColors; }
	std::uint32_t customColorRevision() const { return m_customRevision; }
	bool drawsInMirror() const { return m_drawsInMirror; }
	void setDrawsInMirror(bool v) { m_drawsInMirror = v; }

	std::vector<DrawEntry> &entries() { return m_entries; }
	const std::vector<DrawEntry> &entries() const { return m_entries; }
	// The ClientUpdate and ClientBehavior modules of the template, in list order (ZH Drawable.cpp:349-505: ClientUpdate modules follow the draw
	// modules; RotWK's ClientBehavior list is the fourth module list). A class that is not ported is an UnportedDrawableModule (stop S-140).
	const std::vector<std::unique_ptr<DrawableModule>> &clientModules() const { return m_clientModules; }
	// ZH Object::friend_bindToDrawable -> Drawable: calls onDrawableBoundToObject on the client modules
	void friend_boundToObject();
	// lane AUDIO-4: the client modules hear every transform change (Thing::setPosition / setOrientation / setTransform)
	void reactToTransformChange(const Coord3D *oldPos, float oldAngle) override;
	DrawServices &drawServices() { return m_services; }
	bool hasAnimatedEntry() const;
	// steps every model draw's animations by `elapsedMs` of render time
	void advanceAnimation(double elapsedMs);
	void advanceTires(DrawEntry &e, double elapsedMs); ///< lane COMBAT-4: W3DTruckDraw's tire spin (RW 0x4CBFFB)

	// RW 0x6789B4 Drawable::showModule: the draw module whose tag is `name` (exact, case sensitive) takes the request (true: a module exists; the
	// module-first rule: no fall through to a sub object of the same name) and its entry is hidden / shown (DrawEntry::moduleHidden).
	// RW 0x672823 Drawable::showSubObject: every model draw module hides / shows the sub object `name` (permanent: the script's permanent form).
	// Both bump the change count so the device layer re-reads the draws.
	bool showModule(const std::string &name, bool visible, bool permanent);
	// lane BUILD-4: a draw module's state script asked for a module's visibility (RW 0x734EF4 -> 0x6789B4); while the constructor still makes the modules the
	// request waits until all exist
	void scriptModuleVisible(const std::string &name, bool visible);
	void showSubObject(const std::string &name, bool visible, bool permanent);

	// The render transform from the object's record in a completed logic frame's snapshot (SMOOTH-1: RenderInterpolation::retailPose, RW 0x6765B9;
	// `interpolate` false: the record's current transform, the stepped look). Then the construction look (updateConstruction).
	void syncFromSnapshot(const ObjectSnapshot &s, UnsignedInt snapshotFrame, double alpha, bool interpolate);
	// lane PERF-3: syncFromSnapshot in two parts, for DrawableManager::syncTransforms to run the first on the client job pool. prepareSync reads only this
	// drawable and the record and writes only this drawable's own state (the construction look, the move speed, the script target) and `out` (the pose,
	// with the cos / sin of a rotation about Z); commitSync then sets the transform, whose reaction (the footstep manager's lists) needs the drawables'
	// order. prepareSync then commitSync leave the state syncFromSnapshot leaves.
	struct SyncPrep
	{
		const ObjectSnapshot *rec = nullptr;
		RenderInterpolation::Pose pose;
		bool sameBasis = false; ///< a rotation about Z already set on this basis: no new orientation
		float c = 0.0f, s = 0.0f;
	};
	void prepareSync(const ObjectSnapshot &rec, UnsignedInt snapshotFrame, double alpha, bool interpolate, SyncPrep &out);
	void commitSync(const SyncPrep &prep);
	// lane PERF-3: the record DrawableManager::syncTransforms found for this drawable in `snapshot` (null: none), so the render side asks the same snapshot
	// again without a search. syncedRecord answers only for the snapshot (and frame) of the last sync; otherwise `found` is false
	void setSyncedRecord(const LogicSnapshot *snapshot, const ObjectSnapshot *rec)
	{
		m_syncSnapshot = snapshot;
		m_syncFrame = snapshot ? snapshot->frame : 0;
		m_syncRec = rec;
	}
	const ObjectSnapshot *syncedRecord(const LogicSnapshot &snapshot, bool &found) const
	{
		found = m_syncSnapshot == &snapshot && m_syncFrame == snapshot.frame;
		return found ? m_syncRec : nullptr;
	}
	// RENDER-2, once per client frame: RW 0x675996 Drawable::updateDrawable -> RW 0x6730E1: while the flags hold ACTIVELY_BEING_CONSTRUCTED every model draw's
	// RW 0x4B51B5 (the build-up animation frame from the object's construction percent), and every model draw's RW 0x4B686D height adjustment. `alpha` is
	// the client sub-frame fraction; the cached percent is refreshed on the first client frame of a new logic frame (RW 0x63252F). The rate is
	// 1 / the build frames of the object's GettingBuiltBehavior (RW 0x68BD71 recomputes calcTimeToBuild with no producer: INFERENCE, the same number unless
	// a producer changes it). Bumps the change count when a frame or an offset changed.
	void updateConstruction(const ObjectSnapshot &s, UnsignedInt snapshotFrame, double alpha);
	// RENDER-2: where entry `e`'s model is drawn: the render position moved along the drawable's own Z axis by the entry's construction offset (times the instance
	// scale, as the offset is in model units)
	Coord3D entryPosition(const DrawEntry &e) const;

	// ---- lane PROJ-2: the drawable's fade (GameClient/DrawableFade.cpp; RW Drawable +0x128 mode, +0x12C counter, +0x130 target, +0x134 the fade-in after
	// the invisible frames, +0x37C the client frame of the last step, +0xB0 opacity, +0x43D hidden) ----
	struct Fade
	{
		int mode = 0;           ///< 0 none, 1 fading in, 2 fading out, 3 / 4 / 5 delayed (5: hidden, then fade in: RW 0x67309D)
		double counter = 0.0;   ///< client frames (30 per second, retail's client frame rate) since the fade started, at most `target`
		double target = 0.0;    ///< client frames
		double fadeIn = 0.0;    ///< mode 5: the fade-in that follows, client frames
		float opacity = 1.0f;
		bool hidden = false;
		UnsignedInt seenFire = 0; ///< the projectile launch (ObjectSnapshot::projectileFireFrame) the fade was started for
	};
	// RW 0x85EE00 .. 0x85EF31: a projectile launched (or bounced) in the snapshot's frame starts its drawable's fade: InvisibleFrames / FadeInTime when the
	// local player sees the launcher and the end of the flight, a fade over the flight when only one of them is seen, hidden when neither is
	void startProjectileFade(const ObjectSnapshot &rec, const LogicSnapshot &snapshot);
	// RW 0x675AB7 .. 0x675BBF (Drawable::updateDrawable): one step of the fade by `elapsedMs` of render time
	void updateFade(double elapsedMs);
	const Fade &fade() const { return m_fade; }
	// RW 0x670AA2 Drawable::fadeIn(frames): 0 shows at once; else the opacity rises from 0 over `frames` client frames (lane BUILD-4: public, the
	// simulation's FADE_IN request for a wall span's tile)
	void fadeIn(double frames);
	// what the device layer draws: 0 while hidden, else the fade's opacity (1 without a fade)
	float drawOpacity() const { return m_fade.hidden ? 0.0f : m_fade.opacity; }

private:
	void addEntry(DrawServices &services, const ThingTemplate::Nugget &nugget, std::vector<std::string> &problems);

	DrawServices &m_services;
	DrawableID m_id;
	ObjectID m_objectId;
	KindOfMaskType m_kindOf{};
	ModelConditionFlags m_flags;
	float m_scale = 1.0f;
	std::uint32_t m_houseColor = 0;
	int m_customKind = 0;                              // lane CAH-2
	std::uint32_t m_customColors[3] = { 0u, 0u, 0u };
	std::uint32_t m_customRevision = 0;
	unsigned m_changeCount = 0;
	bool m_drawsInMirror = false;
	unsigned m_constructionLogicFrame = ~0u; ///< RENDER-2: the logic frame the cached construction percents were refreshed in
	float m_geometryHeight = -1.0f;          ///< RENDER-2: RW 0xAD1920 of the template's shapes (computed on first use)
	std::vector<DrawEntry> m_entries;
	std::vector<std::unique_ptr<DrawableModule>> m_clientModules;
	Fade m_fade; ///< lane PROJ-2
	bool m_buildingEntries = false;                                     ///< lane BUILD-4: the constructor is making the draw modules
	std::vector<std::pair<std::string, bool>> m_pendingModuleRequests; ///< lane BUILD-4: script module requests made meanwhile
	float m_moveSpeed = 0.0f; ///< lane SMOOTH-3: the presented record's moveSpeed (the Distance animations' speed sync)
	DrawableScriptTarget m_scriptTarget; ///< lane FX-3
	// lane PERF-3: the angle whose rotation basis syncFromSnapshot last set and that basis; a render frame that presents the same angle (bit for bit) on that
	// basis skips setOrientation's trigonometry (its result would be these same values)
	const LogicSnapshot *m_syncSnapshot = nullptr; ///< lane PERF-3: setSyncedRecord
	UnsignedInt m_syncFrame = 0;
	const ObjectSnapshot *m_syncRec = nullptr;
	bool m_zBasisCached = false;
	float m_zCachedAngle = 0.0f;
	float m_zCachedBasis[9] = {};
	bool m_flagsDirty = false;           ///< lane ANIM-1: RW Drawable + 0x443
	bool m_hasWeaponTiming = false;      ///< lane ANIM-1: the last flush's weapon cycle (RW 0x4BEE31 reads the object's when the draw applies the flags)
	int m_weaponTimingFrames = 0;
	void fadeOut(double frames); ///< RW 0x670A50
	void setFadeHidden(bool hidden); ///< RW 0x6718FB (bumps the change count when it changes)
};

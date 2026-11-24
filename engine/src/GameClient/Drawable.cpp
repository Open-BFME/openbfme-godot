// OpenBFME. GPL-3.0.
// See GameClient/Drawable.h for the sources and the stops.

#include "GameClient/Drawable.h"

#include "Common/Audio/AudioRequests.h"

#include "Common/AsciiString.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/MapObjectDrawables.h"
#include "GameClient/RenderInterpolation.h"
#include "GameClient/StealthLook.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DTreeDraw.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDrawVariants.h"
#include "Common/ModelState.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Object/ObjectGeometry.h"

#include <cmath>
#include <cstring>
#include <variant>

Drawable::Drawable(DrawServices &services, const ThingTemplate *tt, DrawableID id, ObjectID objectId, const std::vector<ClientEvent::ClientModule> &clientModules,
	std::vector<std::string> &problems)
	: Thing(tt)
	, m_services(services)
	, m_id(id)
	, m_objectId(objectId)
{
	m_services.host.setChunkName(tt->getName()); // the Lua chunk name of the BeginScript runs: the object template's name
	if (const FieldValue *v = tt->findField("Scale"))
	{
		if (const float *f = std::get_if<float>(v))
		{
			m_scale = *f; // the instance scale starts as the template's Scale (RW 0x679FD7, default 1.0, RW 0x74008C)
		}
	}
	m_buildingEntries = true;
	for (const ThingTemplate::Nugget &n : tt->drawModules().nuggets())
	{
		addEntry(services, n, problems);
	}
	m_buildingEntries = false;
	// lane BUILD-4: a module request a draw module's first state script made before every module existed applies now, in call order
	for (const std::pair<std::string, bool> &r : m_pendingModuleRequests)
	{
		showModule(r.first, r.second, false);
	}
	m_pendingModuleRequests.clear();
	// the ClientUpdate and ClientBehavior modules (after the draw modules, ZH Drawable.cpp); then onObjectCreated on all of them
	for (const ClientEvent::ClientModule &cm : clientModules)
	{
		std::unique_ptr<Module> mod = ModuleFactory::newResolvedModule(this, cm.resolved, cm.name, cm.data);
		DrawableModule *dm = dynamic_cast<DrawableModule *>(mod.get());
		if (!dm)
		{
			problems.push_back(cm.name + ": the client module class did not make a DrawableModule");
			continue;
		}
		mod.release();
		m_clientModules.push_back(std::unique_ptr<DrawableModule>(dm));
	}
	for (std::unique_ptr<DrawableModule> &m : m_clientModules)
	{
		m->onObjectCreated();
	}
}

bool Drawable::isKindOfName(const char *name) const
{
	const int bit = ObjectTemplateInfoBuilder::kindOfIndex(name);
	return bit >= 0 && MaskTest(m_kindOf, (unsigned)bit);
}

bool Drawable::isKindOfIndex(int bit) const
{
	return bit >= 0 && MaskTest(m_kindOf, (unsigned)bit);
}

void Drawable::reactToTransformChange(const Coord3D *oldPos, float oldAngle)
{
	if (m_reactionMuted)
	{
		return;
	}
	for (std::unique_ptr<DrawableModule> &m : m_clientModules)
	{
		m->reactToTransformChange(oldPos, oldAngle); // lane AUDIO-4
	}
}

void Drawable::friend_boundToObject()
{
	for (std::unique_ptr<DrawableModule> &m : m_clientModules)
	{
		m->onDrawableBoundToObject();
	}
}

void Drawable::addEntry(DrawServices &services, const ThingTemplate::Nugget &nugget, std::vector<std::string> &problems)
{
	DrawEntry e;
	e.className = nugget.name;
	e.tag = nugget.tag;
	e.data = nugget.data.get();
	// lane STEALTHLOOK-1 (RW 0x679AE8): +0x3AB, a draw module's ModuleData fails its +0x78 query (the model draws' AffectedByStealth)
	m_cannotHideByStealth = m_cannotHideByStealth || !StealthLook::moduleDataAffectedByStealth(e.data);
	const W3DDrawClassInfo *ci = W3DDrawModules::find(nugget.name);
	if (!ci)
	{
		problems.push_back(nugget.name + ": not a registered draw class");
		e.kind = W3D_DRAWKIND_NOT_DRAWN;
		m_entries.push_back(std::move(e));
		return;
	}
	e.kind = ci->kind;
	if (e.kind == W3D_DRAWKIND_MODEL)
	{
		const W3DModelDrawModuleData *data = dynamic_cast<const W3DModelDrawModuleData *>(nugget.data.get());
		if (!data)
		{
			problems.push_back(nugget.name + ": module data is not typed");
		}
		else
		{
			try
			{
				W3DScriptedModelDraw::Options o;
				o.scale = m_scale;
				o.buildBones = false; // turrets, barrels and particle bones are not drawn (S-095 / S-097)
				// AUDIO-2: Lua CurDrawablePlaySound (RW 0x73529F) plays for this drawable through the installed audio manager
				const DrawableID drawableId = m_id;
				o.playSound = [drawableId](const std::string &name) { AudioApi::playSoundForDrawable(name, (std::uint32_t)drawableId); };
				// lane FX-2: the client effect player, looked up when the draw calls (it may be installed after this drawable was made)
				DrawServices *svc = &services;
				const DrawableID id = m_id;
				o.enteringStateFX = [svc, id](const std::string &fx) { return svc->fx && svc->fx->enteringStateFX(id, fx); };
				o.particleSysBonesPlayed = [svc]() { return svc->fx != nullptr; };
				// lane SMOOTH-3: Distance animations sync to the object's speed (RW 0x4B67D4), taken from the presented snapshot (Drawable lives in a unique_ptr)
				o.objectSpeed = [this]() { return m_moveSpeed; };
				// lane FX-3 (QA-1 U9): the draw scripts' target questions answer from the object's target record (RW 0x73667D / 0x734A75); a drawable
				// without an object answers false / 0 (retail: nil)
				o.targetKindOf = [this](const std::string &kind) { return m_scriptTarget.isTargetKindOf(kind); };
				o.targetBearing = [this]() { return m_scriptTarget.hasObject ? m_scriptTarget.bearing() : 0.0f; };
				// lane ANIM-1: UseWeaponTiming (RW 0x4BEE24 .. 0x4BEEA2) divides by the object's weapon cycle as the flush carried it; no current weapon: -1,
				// the speed factor is left alone (RW 0x4BEE3F)
				o.weaponTimingFrames = [this]() { return m_hasWeaponTiming ? m_weaponTimingFrames : -1; };
				// lane BUILD-4: CurDrawableShowSubObject / HideSubObject with a draw module's tag (RW 0x734EF4) and CurDrawableShowModule / HideModule reach
				// Drawable::showModule (RW 0x6789B4); the template answers which tags exist (the drawable makes every draw module of its template, S-114)
				o.hasModule = [this](const std::string &tag) { return ClientEventRecorder::templateHasDrawModuleTag(*m_template, tag); };
				o.setModuleVisible = [this](const std::string &tag, bool visible) { scriptModuleVisible(tag, visible); };
				if (const W3DHordeModelDrawModuleData *hd = dynamic_cast<const W3DHordeModelDrawModuleData *>(nugget.data.get()))
				{
					e.draw.reset(new W3DHordeModelDraw(*hd, 2, services.drawAssets, services.random, &services.host, o)); // LOD HIGH (S-114)
				}
				else
				{
					e.draw.reset(new W3DScriptedModelDraw(*data, services.drawAssets, services.random, &services.host, o));
				}
				e.animated = e.draw->needsAdvance(); // W3DScriptedModelDraw::needsAdvance: a blend runs whatever track 0's mode (S-090)
				// the draw runtime's errors and stops are collected by DrawableManager (DrawEntry::draw->errors() / stops()), after the script hooks ran
			}
			catch (const std::exception &ex)
			{
				problems.push_back(nugget.name + ": draw runtime failed: " + ex.what());
				e.draw.reset();
			}
		}
	}
	else
	{
		// tree / prop / floor / nothing / effect: the model the module shows for the empty flag set
		MapDrawModule m;
		m.className = nugget.name;
		m.tag = nugget.tag;
		m.kind = e.kind;
		m.data = nugget.data.get();
		std::vector<std::string> errors;
		MapObjectCreation::resolveDrawModel(m, ModelConditionFlags(), m_template->getName(), errors);
		for (const std::string &err : errors)
		{
			problems.push_back(err);
		}
		e.staticModel = m.model;
		e.notDrawnReason = m.notDrawnReason;
		// RENDER-2: a floor draw hidden by its HideIfModelConditions at creation (resolveDrawModel with no flags) is still the floor the structure shows
		// once the flags change: keep its model and its conditions, and let setModelConditionFlags decide (the foundation floor while being built)
		if (const W3DFloorDrawModuleData *fl = dynamic_cast<const W3DFloorDrawModuleData *>(nugget.data.get()))
		{
			e.hideIf = fl->m_hideIfModelConditions;
		}
	}
	m_entries.push_back(std::move(e));
}

bool Drawable::hasAnimatedEntry() const
{
	for (const DrawEntry &e : m_entries)
	{
		if (e.animated && e.draw)
		{
			return true;
		}
	}
	return false;
}

void Drawable::setModelConditionFlags(const ModelConditionFlags &flags)
{
	m_flags = flags;
	++m_changeCount;
	// RENDER-2: a W3DFloorDraw is not drawn while one of its HideIfModelConditions sets is all in the flags (the rule MapObjectCreation::resolveDrawModel applies
	// at creation; e.g. the barracks' floor during AWAITING_CONSTRUCTION / PARTIALLY_CONSTRUCTED)
	for (DrawEntry &e : m_entries)
	{
		bool hide = false;
		for (const ModelConditionFlags &h : e.hideIf)
		{
			hide = hide || (h.any() && flags.testForAll(h));
		}
		e.conditionHidden = hide;
	}
	m_services.host.setChunkName(m_template->getName());
	for (DrawEntry &e : m_entries)
	{
		if (e.draw)
		{
			e.draw->setModelConditionFlags(flags);
			e.animated = e.draw->needsAdvance();
		}
	}
}

ModelConditionFlags Drawable::dependencySharedModelFlags() const
{
	// the union of the draws' sets, a word at a time (lane BIGBATTLE-1: the bit loop ran for every contained drawable every render frame;
	// MODELCONDITION_COUNT is the type's width and its unused high bits are zero, so the words give the same set)
	ModelConditionFlags out;
	for (const DrawEntry &e : m_entries)
	{
		if (e.draw)
		{
			out.set(e.draw->dependencySharedModelFlags());
		}
	}
	return out;
}

bool Drawable::applyDependencyFlags(const ModelConditionFlags &shared, const ModelConditionFlags &containerFlags)
{
	// the shared bits take the container's values, the others keep this drawable's (a word at a time, as dependencySharedModelFlags)
	ModelConditionFlags next = m_flags;
	std::uint32_t *n = next.words();
	const std::uint32_t *s = shared.words(), *c = containerFlags.words();
	for (int w = 0; w < ModelConditionFlags::NUM_WORDS; ++w)
	{
		n[w] = (n[w] & ~s[w]) | (c[w] & s[w]);
	}
	const bool changed = next != m_flags;
	if (changed)
	{
		setModelConditionFlags(next);
	}
	return changed;
}

void Drawable::scriptModuleVisible(const std::string &name, bool visible)
{
	if (m_buildingEntries)
	{
		m_pendingModuleRequests.emplace_back(name, visible);
		return;
	}
	showModule(name, visible, false);
}

bool Drawable::showModule(const std::string &name, bool visible, bool permanent)
{
	(void)permanent; // RW 0x736412 passes permanent = 1; nothing here changes a drawable's modules afterwards, so permanent and temporary hides coincide
	for (DrawEntry &e : m_entries)
	{
		if (e.tag == name)
		{
			e.moduleHidden = !visible;
			++m_changeCount;
			return true;
		}
	}
	return false;
}

void Drawable::showSubObject(const std::string &name, bool visible, bool permanent)
{
	m_services.host.setChunkName(m_template->getName());
	for (DrawEntry &e : m_entries)
	{
		// a draw module whose state shows no model has no sub object to hide: the same filter as MapObjectRuntime (only modules that show a model take
		// the request; RW 0x672823 asks every module, a module without a render object does nothing). Lane RENDER-3: a PERMANENT request reaches every
		// model draw: RW 0x4C3B25 records it by name without looking at the render object, and the next model shows it
		if (!e.draw || (!permanent && e.draw->frame().model == nullptr))
		{
			continue;
		}
		if (permanent)
		{
			visible ? e.draw->showSubObjectPermanently(name) : e.draw->hideSubObjectPermanently(name);
		}
		else
		{
			visible ? e.draw->showSubObject(name) : e.draw->hideSubObject(name);
		}
	}
	++m_changeCount;
}

void Drawable::setModelCondition(int bit, bool on)
{
	ModelConditionFlags flags = m_flags;
	if (on)
	{
		flags.set(bit);
	}
	else
	{
		flags.clearBit(bit);
	}
	setModelConditionFlags(flags);
}

void Drawable::storeModelCondition(int bit, bool on)
{
	const bool was = m_flags.test(bit);
	if (was == on)
	{
		return; // RW 0x679553: the same flags again change nothing
	}
	if (on)
	{
		m_flags.set(bit);
	}
	else
	{
		m_flags.clearBit(bit);
	}
	m_flagsDirty = true; // RW 0x67961F
}

void Drawable::storeModelConditions(const ModelConditionFlags &flags)
{
	if (flags == m_flags)
	{
		return; // RW 0x679553: the same flags again change nothing
	}
	m_flags = flags;
	m_flagsDirty = true; // RW 0x67961F
}

void Drawable::flushModelConditions(bool hasWeaponTiming, int weaponTimingFrames)
{
	m_hasWeaponTiming = hasWeaponTiming;
	m_weaponTimingFrames = weaponTimingFrames;
	if (!m_flagsDirty)
	{
		return; // RW 0x6744A3: a drawable that is not dirty is not flushed
	}
	m_flagsDirty = false;
	setModelConditionFlags(m_flags);
}

bool Drawable::advanceIsLocal() const
{
	for (const DrawEntry &e : m_entries)
	{
		if (e.draw && e.animated && !e.draw->advanceIsLocal())
		{
			return false;
		}
	}
	return true;
}

void Drawable::advanceAnimationLocal(double elapsedMs)
{
	for (DrawEntry &e : m_entries)
	{
		if (e.draw && e.animated)
		{
			e.draw->advance(elapsedMs);
		}
		if (e.isTruck || !e.truckChecked || e.truckData != e.data)
		{
			advanceTires(e, elapsedMs);
		}
	}
}

void Drawable::advanceAnimation(double elapsedMs)
{
	m_services.host.setChunkName(m_template->getName());
	for (DrawEntry &e : m_entries)
	{
		if (e.draw && e.animated)
		{
			e.draw->advance(elapsedMs);
		}
		if (e.isTruck || !e.truckChecked || e.truckData != e.data) // lane PERF-HD: the call only for a truck (or an entry not looked at yet)
		{
			advanceTires(e, elapsedMs);
		}
	}
}

// lane COMBAT-4 (FB-0002), RW 0x4CBFFB (W3DTruckDraw's draw update, run once per client frame: 6 per logic frame, RW 0xDE4324 + 0x38): with a front tire bone
// (+ 0x320) or a mid-left one (+ 0x328), speed = the object's locomotor speed (RW 0x68B34C, per logic frame), negated with PowerslideRotationAddition while the
// locomotor moves backwards (locomotor + 0x44 bit 7); step = TireRotationMultiplier (+ 0x1E8) / 6; front += step * speed; rear += step * speed (powerslide:
// (PowerslideRotationAddition + speed) * step); each tire bone is controlled (render object slots 0xD8 / 0xE4) with Rotate_Y of its angle. The port advances by the
// client frame's share of a logic frame (elapsed / 200 ms) instead of a fixed sixth. INFERENCE / NOT PORTED (stop S-1792): backwards is read from BACKING_UP (the
// locomotor flag is not in the snapshot); the drawable's wheel record (+ 0x13C + 0x3C: the suspension heights and the steering angle of the front tires, a powerslide)
// is not kept, so the tires only spin; the speed gate RW 0x46E918 is not read; the cab / trailer bones and the dust / dirt / powerslide effects are not run.
void Drawable::advanceTires(DrawEntry &e, double elapsedMs)
{
	if (e.truckData != e.data || !e.truckChecked)
	{
		e.truckData = e.data;
		e.truckChecked = true;
		e.isTruck = e.draw && dynamic_cast<const W3DTruckDrawModuleData *>(e.data) != nullptr;
	}
	if (!e.isTruck)
	{
		return;
	}
	const W3DTruckDrawModuleData *truck = static_cast<const W3DTruckDrawModuleData *>(e.data);
	if (e.frontTireBones.empty() && e.rearTireBones.empty())
	{
		for (const std::string *b : { &truck->m_leftFrontTireBone, &truck->m_rightFrontTireBone, &truck->m_leftFrontTireBone2, &truck->m_rightFrontTireBone2,
				 &truck->m_midLeftFrontTireBone, &truck->m_midRightFrontTireBone, &truck->m_midLeftMidTireBone, &truck->m_midRightMidTireBone,
				 &truck->m_midLeftMidTireBone2, &truck->m_midRightMidTireBone2 })
		{
			if (!b->empty())
			{
				e.frontTireBones.push_back(*b);
			}
		}
		for (const std::string *b : { &truck->m_leftRearTireBone, &truck->m_rightRearTireBone, &truck->m_leftRearTireBone2, &truck->m_rightRearTireBone2,
				 &truck->m_midLeftRearTireBone, &truck->m_midRightRearTireBone })
		{
			if (!b->empty())
			{
				e.rearTireBones.push_back(*b);
			}
		}
	}
	if (e.frontTireBones.empty() && e.rearTireBones.empty())
	{
		return;
	}
	static const int kBackingUp = ModelCondition::indexOf("BACKING_UP");
	float speed = m_moveSpeed;
	float powerslide = truck->m_powerslideRotationAddition;
	if (kBackingUp >= 0 && m_flags.test(kBackingUp))
	{
		speed = -speed;
		powerslide = -powerslide;
	}
	(void)powerslide; // the powerslide (+ 0x2EA) comes from the wheel record, which is not kept
	const float share = SimMath::fstpDword(SimMath::divD(elapsedMs, 200.0));
	const float step = SimMath::mulf32(SimMath::mulf32(truck->m_tireRotationMultiplier, speed), share);
	if (step != 0.0f)
	{
		const float twoPi = 6.28318548f;
		auto wrap = [twoPi](float a) {
			while (a >= twoPi)
			{
				a = SimMath::subf32(a, twoPi);
			}
			while (0.0f > a)
			{
				a = SimMath::addf32(a, twoPi);
			}
			return a;
		};
		e.tireFront = wrap(SimMath::addf32(e.tireFront, step));
		e.tireRear = wrap(SimMath::addf32(e.tireRear, step));
		++e.tireChanges;
	}
}

void Drawable::syncFromSnapshot(const ObjectSnapshot &rec, UnsignedInt snapshotFrame, double alpha, bool interpolate)
{
	// the sequential form (lane PERF-3 keeps it as the reference of prepareSync / commitSync, DrawableManager::setParallelSync(false)). Lane SMOOTH-1: the
	// pose of the completed frame's record (RW 0x6765B9: Catmull-Rom translation, slerped rotation, the stale snap); without interpolation the record's
	// current transform (the stepped 5 Hz look of the comparisons)
	RenderInterpolation::Pose p;
	if (interpolate)
	{
		p = RenderInterpolation::smoothPose(rec, snapshotFrame, alpha); // lane SMOOTH-2: retail's pose without its 5 Hz pulse (RenderInterpolation.h)
	}
	else
	{
		p.position = rec.position;
		p.angle = rec.angle;
		std::memcpy(p.basis, rec.basis, sizeof(p.basis));
		p.zRotation = RenderInterpolation::isZRotation(rec.basis);
	}
	if (p.zRotation)
	{
		setOrientation(p.angle);
		setPosition(&p.position);
	}
	else
	{
		setTransform(&p.position, p.basis);
	}
	m_zBasisCached = false; // the next prepareSync recomputes its trigonometry
	updateConstruction(rec, snapshotFrame, alpha); // RENDER-2 (DrawableConstruction.cpp)
	updatePhantomLook(rec);                        // lane FBSIM-1
	m_moveSpeed = rec.moveSpeed;                   // lane SMOOTH-3: the walk / run cycle's speed source (RW 0x4B67D4)
	m_scriptTarget = rec.scriptTarget;             // lane FX-3
	applyPhysicsXform(rec, p);                     // LOCAL-25: presentation physics
}

void Drawable::prepareSync(const ObjectSnapshot &rec, UnsignedInt snapshotFrame, double alpha, bool interpolate, SyncPrep &out)
{
	// lane SMOOTH-1: the pose of the completed frame's record (RW 0x6765B9: Catmull-Rom translation, slerped rotation, the stale snap); without
	// interpolation the record's current transform (the stepped 5 Hz look of the comparisons)
	out.rec = &rec;
	RenderInterpolation::Pose &p = out.pose;
	if (interpolate)
	{
		p = RenderInterpolation::smoothPose(rec, snapshotFrame, alpha); // lane SMOOTH-2: retail's pose without its 5 Hz pulse (RenderInterpolation.h)
	}
	else
	{
		p.position = rec.position;
		p.angle = rec.angle;
		std::memcpy(p.basis, rec.basis, sizeof(p.basis));
		p.zRotation = RenderInterpolation::isZRotation(rec.basis);
	}
	out.sameBasis = false;
	if (p.zRotation)
	{
		// lane PERF-3: the same angle on the basis made from it is the state setOrientation would leave (m_angle = angle, the basis of its cos / sin); the
		// transform reaction it would add is the one setPosition makes next (the footstep manager's toDirty is idempotent, RW 0x83F159)
		const float current = getOrientation();
		out.sameBasis = m_zBasisCached && std::memcmp(&p.angle, &m_zCachedAngle, sizeof(float)) == 0 && std::memcmp(&current, &m_zCachedAngle, sizeof(float)) == 0 &&
			std::memcmp(getBasis(), m_zCachedBasis, sizeof(m_zCachedBasis)) == 0;
		if (!out.sameBasis)
		{
			out.c = SimMath::cosf32(p.angle); // Thing::setOrientation's values
			out.s = SimMath::sinf32(p.angle);
		}
	}
	// the transform does not enter these (the construction look reads the record and the flags): done here, before commitSync sets it
	updateConstruction(rec, snapshotFrame, alpha); // RENDER-2 (DrawableConstruction.cpp)
	updatePhantomLook(rec);                        // lane FBSIM-1
	m_moveSpeed = rec.moveSpeed;                   // lane SMOOTH-3: the walk / run cycle's speed source (RW 0x4B67D4)
	m_scriptTarget = rec.scriptTarget;             // lane FX-3
	// LOCAL-25: no presentation physics here: this runs on the job pool, and the step draws the shared client random (the ordered pass of
	// DrawableManager makes it)
}

void Drawable::updatePhantomLook(const ObjectSnapshot &rec)
{
	// lane FBSIM-1: a dozer's phantom structure is drawn at 0.4 opacity (RW 0x88D3E5: Drawable + 0xB0 = RW 0xBE0DE4) until the placement sets 1.0 (RW 0x88D683, RW 0xBD1908)
	if (rec.phantomStructure != m_phantomLook)
	{
		m_phantomLook = rec.phantomStructure;
		setDrawableOpacity(m_phantomLook ? 0.4f : 1.0f);
	}
}

void Drawable::syncStealthLook(const ObjectSnapshot &rec)
{
	// lane STEALTHLOOK-1: a member of a HORDE imitates (RW 0x673114's parent branch), the setter never runs for it; every other object takes the
	// setter when the snapshot's look changed (RW 0x6760F9; the phase is drawn only then, retail draws it inside the setter on a pulse look)
	m_stealthParentId = rec.stealthParent;
	if (m_stealthParentId != INVALID_ID)
	{
		return;
	}
	if (rec.stealthLook == m_stealth.look)
	{
		return;
	}
	const float phaseDraw = (rec.stealthLook == StealthLook::LOOK_VISIBLE_FRIENDLY || rec.stealthLook == StealthLook::LOOK_VISIBLE_FRIENDLY_DETECTED)
		? m_services.random.real(0.0f, StealthLook::kPi)
		: 0.0f;
	if (StealthLook::setLook(m_stealth, rec.stealthLook, rec.stealthOpacityMin, rec.stealthOpacityMax, rec.stealthCycleSeconds, phaseDraw, m_cannotHideByStealth))
	{
		++m_changeCount;
	}
}

void Drawable::updateStealthLook(const Drawable *parent)
{
	// RW 0x673114, once per client frame (updateDrawable RW 0x675996 via GameClient::update RW 0x64849E)
	if (m_stealthParentId != INVALID_ID)
	{
		if (!parent)
		{
			return; // the parent object has no drawable: the call ends early
		}
		if (StealthLook::imitate(m_stealth, parent->stealth()))
		{
			++m_changeCount;
		}
		return;
	}
	StealthLook::update(m_stealth);
}

void Drawable::commitSync(const SyncPrep &prep)
{
	const RenderInterpolation::Pose &p = prep.pose;
	if (p.zRotation)
	{
		if (!prep.sameBasis)
		{
			setOrientationTrig(p.angle, prep.c, prep.s);
			m_zCachedAngle = p.angle;
			std::memcpy(m_zCachedBasis, getBasis(), sizeof(m_zCachedBasis));
			m_zBasisCached = true;
		}
		setPosition(&p.position);
	}
	else
	{
		setTransform(&p.position, p.basis);
	}
}

Drawable::PresentedLook Drawable::presentedLook(const LogicSnapshot *presented, bool shroudDrawn) const
{
	PresentedLook look;
	if (!presented)
	{
		return look;
	}
	bool synced = false;
	const ObjectSnapshot *rec = syncedRecord(*presented, synced); // lane PERF-3: the record syncTransforms found in this snapshot
	if (!synced)
	{
		rec = presented->find(m_objectId);
	}
	if (shroudDrawn)
	{
		// VIS-1: an object the local player cannot see is not drawn (RW 0xB4E890 SHROUDED); with RW 0x64849E's grace when the record is this frame's sync
		look.hidden = synced ? m_shroudLook.hidden : (rec && rec->shroudedForLocal);
		look.fogLook = synced && m_shroudLook.fullyObscured;
	}
	// lane STEALTHLOOK-1 (RW 0x81AA03 -> RW 0x6760F9, GameClient/StealthLook): the drawable's own hidden-by-stealth flag (+0x43E; a member's is the
	// imitated parent's, RW 0x673114) decides what an enemy sees - a look-5 drawable whose modules are not all affected stays drawn
	if (m_stealth.hidden || (rec && rec->drawableHidden))
	{
		look.hidden = true;
	}
	if (m_clientHidden)
	{
		look.hidden = true; // lane CHAT-1: BeaconClientUpdate::hideBeacon on an enemy's screen
	}
	look.fogLook = look.fogLook && !look.hidden;
	return look;
}

void Drawable::upgradeTexture(const TextureSwap &swap, bool enabled)
{
	for (auto &entry : m_entries)
	{
		if (entry.draw) entry.draw->upgradeTexture(swap, enabled);
	}
	++m_changeCount;
}

// ------------------------------------------------------------------------------------------------
// LOCAL-25: Client presentation physics (pitch, roll, suspension, bobbing)
// Retail RotWK 2.01:
//   - calcPitchRoll: RW 0x670570 (DONOR BFME2 0x00270276, Drawable_calcPitchRoll.cpp)
//   - calcPhysicsXform: RW 0x67BDC0 (DONOR BFME2 0x0027BA95, DrawableCalcPhysicsXform.cpp)
//   - applyPhysicsXform: RW 0x67BE90 (DONOR BFME2 0x0027BB65, DrawableApplyPhysicsXform.cpp)
//   - calcPhysicsXformWheels: RW 0x677026 (DONOR BFME2 0x00276CFB, DrawablePhysicsXformWheels.cpp)
//   - calcPhysicsXformTreads: RW 0x670B11 (DONOR BFME2 0x00270817, DrawablePhysicsXformTreads.cpp)
//   - calcPhysicsXformHugeFourLegs: RW 0x67B7AA (DONOR BFME2 0x0027B47F, DrawablePhysicsXformHugeFourLegs.cpp)
//   - calcPhysicsXformShip: RW 0x6732DC (DONOR BFME2 0x00272FB6, DrawablePhysicsXformShip.cpp)
//   - calcPhysicsXformGiantBird: RW 0x670E09 (DONOR BFME2 0x00270B0F)
// Stop S-7222: the entry points, the dispatcher, the step gate and the wheels field loads are checked in RotWK game.dat; the arithmetic inside
// the five bodies follows the BFME2 donor (INFERENCE that RotWK keeps it)
// ------------------------------------------------------------------------------------------------

bool Drawable::calcPitchRoll(const Coord3D &pos, const Coord3D &dir, const LocomotorTemplate *loco, float &pitch, float &roll)
{
	Coord3D perp;
	perp.x = -dir.y;
	perp.y = dir.x;
	perp.z = 0.0f;

	// RW 0x670570: if LocomotorTemplate flag +0xED (m_useTerrainSmoothing) is set, sample terrain under 3 corners
	if (loco && loco->m_useTerrainSmoothing && m_services.groundHeight)
	{
		float length = 15.0f;
		float width = 15.0f;
		if (m_template)
		{
			const std::vector<ObjectGeometry::Shape> &shapes = ObjectGeometry::cachedShapesOf(*m_template);
			if (!shapes.empty())
			{
				length = shapes[0].majorRadius;
				width = shapes[0].minorRadius;
			}
		}

		Coord3D halfLength;
		halfLength.x = dir.x * 0.5f * length;
		halfLength.y = (length * dir.y) * 0.5f;
		halfLength.z = 0.0f;

		Coord3D vec;
		vec.x = perp.x * 0.5f * width;
		vec.y = perp.y * 0.5f * width;
		vec.z = 0.0f;

		Coord3D corners[3];
		corners[0].x = pos.x - halfLength.x + vec.x;
		corners[0].y = pos.y - halfLength.y + vec.y;
		corners[0].z = pos.z;

		corners[1].x = pos.x - halfLength.x - vec.x;
		corners[1].y = pos.y - halfLength.y - vec.y;
		corners[1].z = pos.z;

		corners[2].x = halfLength.x + pos.x + vec.x;
		corners[2].y = halfLength.y + pos.y + vec.y;
		corners[2].z = pos.z;

		bool tooSteep = false;
		float heights[3];
		for (int i = 0; i < 3; ++i)
		{
			heights[i] = m_services.groundHeight(corners[i].x, corners[i].y, nullptr);
			if (std::abs(heights[i] - pos.z) > (length + width) * 0.5f)
			{
				tooSteep = true;
			}
		}

		if (!tooSteep)
		{
			Coord3D widthEdge;
			widthEdge.x = corners[1].x - corners[0].x;
			widthEdge.y = corners[1].y - corners[0].y;
			widthEdge.z = heights[1] - heights[0];

			Coord3D lengthEdge;
			lengthEdge.x = corners[2].x - corners[0].x;
			lengthEdge.y = corners[2].y - corners[0].y;
			lengthEdge.z = heights[2] - heights[0];

			vec.x = widthEdge.y * lengthEdge.z - widthEdge.z * lengthEdge.y;
			vec.y = widthEdge.z * lengthEdge.x - widthEdge.x * lengthEdge.z;
			vec.z = widthEdge.x * lengthEdge.y - widthEdge.y * lengthEdge.x;

			float len = std::sqrt(vec.x * vec.x + vec.y * vec.y + vec.z * vec.z);
			if (len > 0.0f)
			{
				vec.x /= len;
				vec.y /= len;
				vec.z /= len;
			}

			float s = vec.x * dir.x + vec.y * dir.y + vec.z * dir.z;
			s = std::max(-1.0f, std::min(1.0f, s));
			pitch = std::asin(s);

			s = vec.x * perp.x + vec.y * perp.y + vec.z * perp.z;
			s = std::max(-1.0f, std::min(1.0f, s));
			roll = std::asin(s);
			return true;
		}
	}

	// Fallback (or when !m_useTerrainSmoothing): terrain normal at position
	Coord3D normal{ 0.0f, 0.0f, 1.0f };
	if (m_services.groundHeight)
	{
		m_services.groundHeight(pos.x, pos.y, &normal);
	}
	float s = normal.x * dir.x + normal.y * dir.y;
	s = std::max(-1.0f, std::min(1.0f, s));
	pitch = std::asin(s);

	s = normal.x * perp.x + normal.y * perp.y;
	s = std::max(-1.0f, std::min(1.0f, s));
	roll = std::asin(s);
	return true;
}

bool Drawable::calcPhysicsXform(const ObjectSnapshot &rec, const RenderInterpolation::Pose &pose, PhysicsXformInfo &info)
{
	const LocomotorTemplate *loco = rec.locomotorTemplate;
	if (!loco)
	{
		return false;
	}

	if (!m_locoInfo)
	{
		m_locoInfo = std::make_unique<DrawableLocoInfo>();
	}

	// Dispatcher by LocomotorAppearance (RW 0x67BDC0 / donor 0x0027BA95)
	switch (loco->m_appearance)
	{
	case LOCO_WHEELS_FOUR: // 1
	case LOCO_TREADS:      // 8
		calcPhysicsXformWheels(loco, rec, pose, info);
		return true;

	case LOCO_HOVER: // 2
	case LOCO_WINGS: // 3
		calcPhysicsXformTreads(loco, rec, pose, info);
		return true;

	case LOCO_LEGS_FOUR_HUGE: // 4
		calcPhysicsXformHugeFourLegs(loco, rec, pose, info);
		return true;

	case LOCO_GIANT_BIRD: // 5
		calcPhysicsXformGiantBird(loco, rec, pose, info);
		return true;

	case LOCO_SHIP: // 9
		calcPhysicsXformShip(loco, rec, pose, info);
		return true;

	default:
		return false;
	}
}

void Drawable::calcPhysicsXformWheels(const LocomotorTemplate *loco, const ObjectSnapshot &rec, const RenderInterpolation::Pose &pose, PhysicsXformInfo &info)
{
	const float ACCEL_PITCH_LIMIT = loco->m_accelerationPitchLimit;
	const float BOUNCE_ANGLE_KICK = loco->m_bounceAmount;
	const float PITCH_STIFFNESS = loco->m_pitchStiffness;
	const float ROLL_STIFFNESS = loco->m_rollStiffness;
	const float PITCH_DAMPING = loco->m_pitchDamping;
	const float ROLL_DAMPING = loco->m_rollDamping;
	const float UNIFORM_AXIAL_DAMPING = loco->m_uniformAxialDamping;
	const float MAX_SUSPENSION_EXTENSION = loco->m_maximumWheelExtension;
	const bool DO_WHEELS = loco->m_hasSuspension;

	float length = 15.0f;
	float width = 15.0f;
	if (m_template)
	{
		const std::vector<ObjectGeometry::Shape> &shapes = ObjectGeometry::cachedShapesOf(*m_template);
		if (!shapes.empty())
		{
			length = shapes[0].majorRadius;
			width = shapes[0].minorRadius;
		}
	}

	const Coord3D &pos = pose.position;
	Coord3D dir{ pose.basis[0], pose.basis[3], 0.0f };
	float dirLen = std::sqrt(dir.x * dir.x + dir.y * dir.y);
	if (dirLen > 0.0f)
	{
		dir.x /= dirLen;
		dir.y /= dirLen;
	}
	else
	{
		dir = Coord3D{ 1.0f, 0.0f, 0.0f };
	}

	float groundPitch = 0.0f;
	float groundRoll = 0.0f;
	calcPitchRoll(pos, dir, loco, groundPitch, groundRoll);

	float hheight = m_services.groundHeight ? m_services.groundHeight(pos.x, pos.y, nullptr) : 0.0f;
	bool airborne = rec.aboveTerrain;

	if (airborne)
	{
		if (DO_WHEELS)
		{
			m_locoInfo->m_framesAirborne = 0;
			m_locoInfo->m_framesAirborneCounter++;
			if (pos.z - hheight > -MAX_SUSPENSION_EXTENSION)
			{
				m_locoInfo->m_rearLeftHeightOffset += (MAX_SUSPENSION_EXTENSION - m_locoInfo->m_rearLeftHeightOffset) * 0.5f;
				m_locoInfo->m_rearRightHeightOffset += (MAX_SUSPENSION_EXTENSION - m_locoInfo->m_rearRightHeightOffset) * 0.5f;
			}
			else
			{
				m_locoInfo->m_rearLeftHeightOffset += (0.0f - m_locoInfo->m_rearLeftHeightOffset) * 0.5f;
				m_locoInfo->m_rearRightHeightOffset += (0.0f - m_locoInfo->m_rearRightHeightOffset) * 0.5f;
			}
		}

		float pitchHeight = length * std::sin(m_locoInfo->m_accelerationPitch + m_locoInfo->m_pitch - groundPitch);
		float rollHeight = width * std::sin(m_locoInfo->m_accelerationRoll + m_locoInfo->m_roll - groundRoll);
		info.m_totalZ = (std::abs(pitchHeight) + std::abs(rollHeight)) * 0.25f;
		return;
	}

	float curSpeed = rec.moveSpeed;
	float maxSpeed = loco->m_maxSpeed > 0.0f ? loco->m_maxSpeed : 1.0f;
	if (curSpeed > maxSpeed * 0.1f && BOUNCE_ANGLE_KICK > 0.0f)
	{
		float factor = curSpeed / maxSpeed;
		if (std::abs(m_locoInfo->m_pitchRate) < factor * BOUNCE_ANGLE_KICK * 0.25f && std::abs(m_locoInfo->m_rollRate) < factor * BOUNCE_ANGLE_KICK * 0.125f)
		{
			int r = m_services.random.value(0, 3);
			if (r == 0)
			{
				m_locoInfo->m_pitchRate -= BOUNCE_ANGLE_KICK * factor;
				m_locoInfo->m_rollRate -= BOUNCE_ANGLE_KICK * factor * 0.5f;
			}
			else if (r == 1)
			{
				m_locoInfo->m_pitchRate += BOUNCE_ANGLE_KICK * factor;
				m_locoInfo->m_rollRate -= BOUNCE_ANGLE_KICK * factor * 0.5f;
			}
			else if (r == 2)
			{
				m_locoInfo->m_pitchRate -= BOUNCE_ANGLE_KICK * factor;
				m_locoInfo->m_rollRate += BOUNCE_ANGLE_KICK * factor * 0.5f;
			}
			else
			{
				m_locoInfo->m_pitchRate += BOUNCE_ANGLE_KICK * factor;
				m_locoInfo->m_rollRate += BOUNCE_ANGLE_KICK * factor * 0.5f;
			}
		}
	}

	m_locoInfo->m_pitchRate += (-PITCH_STIFFNESS * (m_locoInfo->m_pitch - groundPitch)) + (-PITCH_DAMPING * m_locoInfo->m_pitchRate);
	if (m_locoInfo->m_pitchRate > 0.0f)
	{
		m_locoInfo->m_pitchRate *= 0.5f;
	}
	m_locoInfo->m_rollRate += (-ROLL_STIFFNESS * (m_locoInfo->m_roll - groundRoll)) + (-ROLL_DAMPING * m_locoInfo->m_rollRate);

	m_locoInfo->m_pitch += m_locoInfo->m_pitchRate * UNIFORM_AXIAL_DAMPING;
	m_locoInfo->m_roll += m_locoInfo->m_rollRate * UNIFORM_AXIAL_DAMPING;

	m_locoInfo->m_accelerationPitchRate += (-PITCH_STIFFNESS * m_locoInfo->m_accelerationPitch) + (-PITCH_DAMPING * m_locoInfo->m_accelerationPitchRate);
	m_locoInfo->m_accelerationPitch += m_locoInfo->m_accelerationPitchRate;

	m_locoInfo->m_accelerationRollRate += (-ROLL_STIFFNESS * m_locoInfo->m_accelerationRoll) + (-ROLL_DAMPING * m_locoInfo->m_accelerationRollRate);
	m_locoInfo->m_accelerationRoll += m_locoInfo->m_accelerationRollRate;

	info.m_totalPitch = m_locoInfo->m_accelerationPitch + m_locoInfo->m_pitch;
	info.m_totalRoll = m_locoInfo->m_accelerationRoll + m_locoInfo->m_roll;

	if (m_locoInfo->m_accelerationPitch > ACCEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationPitch = ACCEL_PITCH_LIMIT;
	else if (m_locoInfo->m_accelerationPitch < -ACCEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationPitch = -ACCEL_PITCH_LIMIT;

	if (m_locoInfo->m_accelerationRoll > ACCEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationRoll = ACCEL_PITCH_LIMIT;
	else if (m_locoInfo->m_accelerationRoll < -ACCEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationRoll = -ACCEL_PITCH_LIMIT;

	info.m_totalYaw = 0.0f;
	info.m_totalZ = 0.0f;

	float pitchHeight = length * std::sin(info.m_totalPitch - groundPitch);
	float rollHeight = width * std::sin(info.m_totalRoll - groundRoll);

	if (DO_WHEELS)
	{
		m_locoInfo->m_framesAirborne = m_locoInfo->m_framesAirborneCounter;
		m_locoInfo->m_framesAirborneCounter = 0;
		float newFrontLeft = 0.0f, newFrontRight = 0.0f, newRearLeft = 0.0f, newRearRight = 0.0f;

		const float SPRING_FACTOR = 0.9f;
		if (pitchHeight < 0.0f)
		{
			newFrontLeft = SPRING_FACTOR * (pitchHeight * (1.0f / 3.0f + 1.0f / 2.0f));
			newFrontRight = SPRING_FACTOR * (pitchHeight * (1.0f / 3.0f + 1.0f / 2.0f));
			newRearLeft = pitchHeight * (-1.0f / 2.0f + 1.0f / 4.0f);
			newRearRight = pitchHeight * (-1.0f / 2.0f + 1.0f / 4.0f);
		}
		else
		{
			newFrontLeft = (pitchHeight * (-1.0f / 4.0f + 1.0f / 2.0f));
			newFrontRight = (pitchHeight * (-1.0f / 4.0f + 1.0f / 2.0f));
			newRearLeft = SPRING_FACTOR * (pitchHeight * (-1.0f / 2.0f - 1.0f / 3.0f));
			newRearRight = SPRING_FACTOR * (pitchHeight * (-1.0f / 2.0f - 1.0f / 3.0f));
		}
		if (rollHeight > 0.0f)
		{
			newFrontRight += -SPRING_FACTOR * (rollHeight * (1.0f / 3.0f + 1.0f / 2.0f));
			newRearRight += -SPRING_FACTOR * (rollHeight * (1.0f / 3.0f + 1.0f / 2.0f));
			newRearLeft += rollHeight * 0.5f - rollHeight * 0.25f;
			newFrontLeft += rollHeight * 0.5f - rollHeight * 0.25f;
		}
		else
		{
			newFrontRight += -rollHeight * (1.0f / 2.0f - 1.0f / 4.0f);
			newRearRight += -rollHeight * (1.0f / 2.0f - 1.0f / 4.0f);
			newRearLeft += SPRING_FACTOR * (rollHeight * (1.0f / 3.0f + 1.0f / 2.0f));
			newFrontLeft += SPRING_FACTOR * (rollHeight * (1.0f / 3.0f + 1.0f / 2.0f));
		}

		if (newFrontLeft < m_locoInfo->m_frontLeftHeightOffset)
			m_locoInfo->m_frontLeftHeightOffset += (newFrontLeft - m_locoInfo->m_frontLeftHeightOffset) * 0.5f;
		else
			m_locoInfo->m_frontLeftHeightOffset = newFrontLeft;

		if (newFrontRight < m_locoInfo->m_frontRightHeightOffset)
			m_locoInfo->m_frontRightHeightOffset += (newFrontRight - m_locoInfo->m_frontRightHeightOffset) * 0.5f;
		else
			m_locoInfo->m_frontRightHeightOffset = newFrontRight;

		if (newRearLeft < m_locoInfo->m_rearLeftHeightOffset)
			m_locoInfo->m_rearLeftHeightOffset += (newRearLeft - m_locoInfo->m_rearLeftHeightOffset) * 0.5f;
		else
			m_locoInfo->m_rearLeftHeightOffset = newRearLeft;

		if (newRearRight < m_locoInfo->m_rearRightHeightOffset)
			m_locoInfo->m_rearRightHeightOffset += (newRearRight - m_locoInfo->m_rearRightHeightOffset) * 0.5f;
		else
			m_locoInfo->m_rearRightHeightOffset = newRearRight;

		if (m_locoInfo->m_frontLeftHeightOffset < MAX_SUSPENSION_EXTENSION)
			m_locoInfo->m_frontLeftHeightOffset = MAX_SUSPENSION_EXTENSION;
		if (m_locoInfo->m_frontRightHeightOffset < MAX_SUSPENSION_EXTENSION)
			m_locoInfo->m_frontRightHeightOffset = MAX_SUSPENSION_EXTENSION;
		if (m_locoInfo->m_rearLeftHeightOffset < MAX_SUSPENSION_EXTENSION)
			m_locoInfo->m_rearLeftHeightOffset = MAX_SUSPENSION_EXTENSION;
		if (m_locoInfo->m_rearRightHeightOffset < MAX_SUSPENSION_EXTENSION)
			m_locoInfo->m_rearRightHeightOffset = MAX_SUSPENSION_EXTENSION;
	}

	float divisor = 4.0f;
	float pitchDiff = std::abs(info.m_totalPitch - groundPitch);
	constexpr float kPi = 3.14159265358979323846f;
	if (pitchDiff > kPi / 8.0f)
	{
		divisor = ((4.0f * kPi / 8.0f) + (1.0f * (pitchDiff - kPi / 8.0f))) / pitchDiff;
	}
	info.m_totalZ += std::abs(pitchHeight) / divisor;
	info.m_totalZ += std::abs(rollHeight) / divisor;
}

void Drawable::calcPhysicsXformTreads(const LocomotorTemplate *loco, const ObjectSnapshot &rec, const RenderInterpolation::Pose &pose, PhysicsXformInfo &info)
{
	const float ACCEL_PITCH_LIMIT = loco->m_accelerationPitchLimit;
	const float PITCH_STIFFNESS = loco->m_pitchStiffness;
	const float ROLL_STIFFNESS = loco->m_rollStiffness;
	const float PITCH_DAMPING = loco->m_pitchDamping;
	const float ROLL_DAMPING = loco->m_rollDamping;
	const float UNIFORM_AXIAL_DAMPING = loco->m_uniformAxialDamping;

	m_locoInfo->m_pitchRate += ((-PITCH_STIFFNESS * m_locoInfo->m_pitch) + (-PITCH_DAMPING * m_locoInfo->m_pitchRate));
	m_locoInfo->m_rollRate += ((-ROLL_STIFFNESS * m_locoInfo->m_roll) + (-ROLL_DAMPING * m_locoInfo->m_rollRate));

	if (!rec.aboveTerrain)
	{
		m_locoInfo->m_pitch += m_locoInfo->m_pitchRate * UNIFORM_AXIAL_DAMPING;
		m_locoInfo->m_roll += m_locoInfo->m_rollRate * UNIFORM_AXIAL_DAMPING;

		m_locoInfo->m_accelerationPitchRate += ((-PITCH_STIFFNESS * m_locoInfo->m_accelerationPitch) + (-PITCH_DAMPING * m_locoInfo->m_accelerationPitchRate));
		m_locoInfo->m_accelerationPitch += m_locoInfo->m_accelerationPitchRate;

		m_locoInfo->m_accelerationRollRate += ((-ROLL_STIFFNESS * m_locoInfo->m_accelerationRoll) + (-ROLL_DAMPING * m_locoInfo->m_accelerationRollRate));
		m_locoInfo->m_accelerationRoll += m_locoInfo->m_accelerationRollRate;
	}
	else
	{
		m_locoInfo->m_accelerationPitch *= 0.5f;
		if (std::abs(m_locoInfo->m_accelerationPitch) < 0.0001f)
			m_locoInfo->m_accelerationPitch = 0.0f;
		m_locoInfo->m_accelerationRoll *= 0.5f;
		if (std::abs(m_locoInfo->m_accelerationRoll) < 0.0001f)
			m_locoInfo->m_accelerationRoll = 0.0f;
	}

	info.m_totalPitch = m_locoInfo->m_accelerationPitch + m_locoInfo->m_pitch;
	info.m_totalRoll = m_locoInfo->m_accelerationRoll + m_locoInfo->m_roll;

	if (m_locoInfo->m_accelerationPitch > ACCEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationPitch = ACCEL_PITCH_LIMIT;
	else if (m_locoInfo->m_accelerationPitch < -ACCEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationPitch = -ACCEL_PITCH_LIMIT;

	if (m_locoInfo->m_accelerationRoll > ACCEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationRoll = ACCEL_PITCH_LIMIT;
	else if (m_locoInfo->m_accelerationRoll < -ACCEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationRoll = -ACCEL_PITCH_LIMIT;

	info.m_totalYaw = 0.0f;
	info.m_totalZ = 0.0f;
}

void Drawable::calcPhysicsXformHugeFourLegs(const LocomotorTemplate *loco, const ObjectSnapshot &rec, const RenderInterpolation::Pose &pose, PhysicsXformInfo &info)
{
	const float ACCEL_PITCH_LIMIT = loco->m_accelerationPitchLimit;
	const float PITCH_STIFFNESS = loco->m_pitchStiffness;
	const float ROLL_STIFFNESS = loco->m_rollStiffness;
	const float PITCH_DAMPING = loco->m_pitchDamping;
	const float ROLL_DAMPING = loco->m_rollDamping;
	const float UNIFORM_AXIAL_DAMPING = loco->m_uniformAxialDamping;

	float length = 15.0f;
	float width = 15.0f;
	if (m_template)
	{
		const std::vector<ObjectGeometry::Shape> &shapes = ObjectGeometry::cachedShapesOf(*m_template);
		if (!shapes.empty())
		{
			length = shapes[0].majorRadius;
			width = shapes[0].minorRadius;
		}
	}

	const Coord3D &pos = pose.position;
	float direction = pose.angle;
	float s = std::sin(direction);
	float c = std::cos(direction);

	Coord3D points[4];
	points[0] = Coord3D{ -length * c - width * s + pos.x, -width * c + -length * s + pos.y, pos.z };
	points[1] = Coord3D{  length * c - width * s + pos.x, -width * c +  length * s + pos.y, pos.z };
	points[2] = Coord3D{ -length * c + width * s + pos.x,  width * c + -length * s + pos.y, pos.z };
	points[3] = Coord3D{  length * c + width * s + pos.x,  width * c +  length * s + pos.y, pos.z };

	float heights[4];
	for (int i = 0; i < 4; ++i)
	{
		heights[i] = m_services.groundHeight ? m_services.groundHeight(points[i].x, points[i].y, nullptr) : pos.z;
	}

	float front = (heights[0] + heights[1]) * 0.5f;
	float back = (heights[2] + heights[3]) * 0.5f;
	float frontX = (points[0].x + points[1].x) * 0.5f;
	float frontY = (points[0].y + points[1].y) * 0.5f;
	float backX = (points[2].x + points[3].x) * 0.5f;
	float backY = (points[2].y + points[3].y) * 0.5f;
	float dx = frontX - backX;
	float dy = frontY - backY;
	float dist = std::sqrt(dx * dx + dy * dy);
	if (dist < 1.0f) dist = 1.0f;
	float pitch = std::atan2(back - front, dist);

	m_locoInfo->m_pitchRate += (-PITCH_STIFFNESS * (m_locoInfo->m_pitch - pitch)) + (-PITCH_DAMPING * m_locoInfo->m_pitchRate);
	if (m_locoInfo->m_pitchRate > 0.0f)
		m_locoInfo->m_pitchRate *= 0.5f;

	m_locoInfo->m_pitch += UNIFORM_AXIAL_DAMPING * m_locoInfo->m_pitchRate;
	m_locoInfo->m_roll += UNIFORM_AXIAL_DAMPING * m_locoInfo->m_rollRate;

	m_locoInfo->m_accelerationPitchRate += (-PITCH_STIFFNESS * m_locoInfo->m_accelerationPitch) + (-PITCH_DAMPING * m_locoInfo->m_accelerationPitchRate);
	m_locoInfo->m_accelerationPitch += m_locoInfo->m_accelerationPitchRate;

	m_locoInfo->m_accelerationRollRate += (-ROLL_STIFFNESS * m_locoInfo->m_accelerationRoll) + (-ROLL_DAMPING * m_locoInfo->m_accelerationRollRate);
	m_locoInfo->m_accelerationRoll += m_locoInfo->m_accelerationRollRate;

	info.m_totalPitch = m_locoInfo->m_accelerationPitch + m_locoInfo->m_pitch;
	info.m_totalRoll = m_locoInfo->m_accelerationRoll + m_locoInfo->m_roll;

	if (m_locoInfo->m_accelerationPitch > ACCEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationPitch = ACCEL_PITCH_LIMIT;
	else if (m_locoInfo->m_accelerationPitch < -ACCEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationPitch = -ACCEL_PITCH_LIMIT;

	if (m_locoInfo->m_accelerationRoll > ACCEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationRoll = ACCEL_PITCH_LIMIT;
	else if (m_locoInfo->m_accelerationRoll < -ACCEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationRoll = -ACCEL_PITCH_LIMIT;

	float oldZ = m_locoInfo->m_overlapZ;
	float wanted = (front + back) * 0.5f - pos.z;
	m_locoInfo->m_overlapZ += m_locoInfo->m_overlapZVel;

	if ((wanted >= oldZ && wanted <= m_locoInfo->m_overlapZ) || (wanted <= oldZ && wanted >= m_locoInfo->m_overlapZ))
	{
		m_locoInfo->m_overlapZ = wanted;
		m_locoInfo->m_overlapZVel = 0.0f;
	}
	else if ((wanted > m_locoInfo->m_overlapZ && m_locoInfo->m_overlapZVel < 0.0f) || (wanted < m_locoInfo->m_overlapZ && m_locoInfo->m_overlapZVel > 0.0f))
	{
		m_locoInfo->m_overlapZ -= m_locoInfo->m_overlapZVel;
		m_locoInfo->m_overlapZVel = 0.0f;
	}
	else if (wanted > m_locoInfo->m_overlapZ)
	{
		m_locoInfo->m_overlapZVel += 1.5f;
	}
	else
	{
		m_locoInfo->m_overlapZVel -= 0.5f;
	}

	info.m_totalZ = m_locoInfo->m_overlapZ;
	info.m_totalYaw = 0.0f;
}

void Drawable::calcPhysicsXformGiantBird(const LocomotorTemplate *loco, const ObjectSnapshot &rec, const RenderInterpolation::Pose &pose, PhysicsXformInfo &info)
{
	// RW 0x670E09 / donor 0x00270B0F: roll ease towards banked angle, clamped to +-0.8
	m_locoInfo->m_giantBirdRoll *= 0.8f;
	info.m_totalRoll = m_locoInfo->m_giantBirdRoll;
	info.m_totalPitch = 0.0f;
	info.m_totalYaw = 0.0f;
	info.m_totalZ = 0.0f;
}

void Drawable::calcPhysicsXformShip(const LocomotorTemplate *loco, const ObjectSnapshot &rec, const RenderInterpolation::Pose &pose, PhysicsXformInfo &info)
{
	const float ACCEL_PITCH_LIMIT = loco->m_accelerationPitchLimit;
	const float PITCH_STIFFNESS = loco->m_pitchStiffness;
	const float ROLL_STIFFNESS = loco->m_rollStiffness;
	const float PITCH_DAMPING = loco->m_pitchDamping;
	const float ROLL_DAMPING = loco->m_rollDamping;
	const float FORWARD_VEL_COEF = loco->m_forwardVelocityPitchFactor;
	const float FORWARD_ACCEL_COEF = loco->m_forwardAccelerationPitchFactor;
	const float LATERAL_ACCEL_COEF = loco->m_lateralAccelerationRollFactor;
	const float UNIFORM_AXIAL_DAMPING = loco->m_uniformAxialDamping;
	const float PITCH_KICK = loco->m_bounceAmount;

	float length = 20.0f;
	float width = 10.0f;
	float height = 15.0f;
	if (m_template)
	{
		const std::vector<ObjectGeometry::Shape> &shapes = ObjectGeometry::cachedShapesOf(*m_template);
		if (!shapes.empty())
		{
			length = shapes[0].majorRadius;
			width = shapes[0].minorRadius;
			height = shapes[0].height;
		}
	}

	Coord3D dir{ pose.basis[0], pose.basis[3], 0.0f };
	float dirLen = std::sqrt(dir.x * dir.x + dir.y * dir.y);
	if (dirLen > 0.0f)
	{
		dir.x /= dirLen;
		dir.y /= dirLen;
	}
	else
	{
		dir = Coord3D{ 1.0f, 0.0f, 0.0f };
	}

	Coord3D vel;
	vel.x = pose.position.x - rec.previousPos.x;
	vel.y = pose.position.y - rec.previousPos.y;
	vel.z = 0.0f;

	m_locoInfo->springStep(m_locoInfo->m_pitchRate, m_locoInfo->m_pitch, PITCH_STIFFNESS, PITCH_DAMPING);
	m_locoInfo->springStep(m_locoInfo->m_rollRate, m_locoInfo->m_roll, ROLL_STIFFNESS, ROLL_DAMPING);
	m_locoInfo->m_pitch += UNIFORM_AXIAL_DAMPING * m_locoInfo->m_pitchRate;
	m_locoInfo->m_roll += UNIFORM_AXIAL_DAMPING * m_locoInfo->m_rollRate;

	m_locoInfo->springStep(m_locoInfo->m_accelerationPitchRate, m_locoInfo->m_accelerationPitch, PITCH_STIFFNESS, PITCH_DAMPING);
	m_locoInfo->m_accelerationPitch += m_locoInfo->m_accelerationPitchRate;
	m_locoInfo->springStep(m_locoInfo->m_accelerationRollRate, m_locoInfo->m_accelerationRoll, ROLL_STIFFNESS, ROLL_DAMPING);
	m_locoInfo->m_accelerationRoll += m_locoInfo->m_accelerationRollRate;

	info.m_totalPitch = m_locoInfo->m_pitch + m_locoInfo->m_accelerationPitch;
	info.m_totalRoll = m_locoInfo->m_roll + m_locoInfo->m_accelerationRoll;

	if (!rec.aboveTerrain)
	{
		float forwardVel = vel.x * dir.x + vel.y * dir.y;
		m_locoInfo->m_pitchRate += -(FORWARD_VEL_COEF * forwardVel);
		m_locoInfo->m_accelerationPitchRate += -(FORWARD_ACCEL_COEF * forwardVel * 0.1f);
	}

	if (m_locoInfo->m_accelerationPitch > ACCEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationPitch = ACCEL_PITCH_LIMIT;
	else if (m_locoInfo->m_accelerationPitch < -ACCEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationPitch = -ACCEL_PITCH_LIMIT;

	if (m_locoInfo->m_accelerationRoll > ACCEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationRoll = ACCEL_PITCH_LIMIT;
	else if (m_locoInfo->m_accelerationRoll < -ACCEL_PITCH_LIMIT)
		m_locoInfo->m_accelerationRoll = -ACCEL_PITCH_LIMIT;

	// Bobbing modulators (donor 0x00272FB6: pitch and roll sinusoidal bobbing)
	m_locoInfo->m_yawModulator += 0.05f;
	info.m_totalRoll += std::sin(m_locoInfo->m_yawModulator) * 0.02f;
	m_locoInfo->m_pitchModulator += 0.03f;
	info.m_totalPitch += std::cos(m_locoInfo->m_pitchModulator) * 0.02f;

	float pitchHeight = std::sin(info.m_totalPitch) * length;
	float rollHeight = std::sin(info.m_totalRoll) * width;
	const float Z_SCALE = 0.7f;
	info.m_totalZ = (std::abs(pitchHeight) + std::abs(rollHeight)) * Z_SCALE - 0.5f * height;
	if (info.m_totalZ < 0.0f)
		info.m_totalZ = 0.0f;
	info.m_totalYaw = 0.0f;
}

void Drawable::applyPhysicsXform(const ObjectSnapshot &rec, const RenderInterpolation::Pose &pose)
{
	if (!rec.locomotorTemplate)
	{
		m_hasPhysicsXform = false;
		m_physicsStepped = false;
		return;
	}
	// RW 0x67BDF6 .. 0x67BE17: the step is made once per client frame (`call [TheGameClient + 0x7C]`, compared with the stamp at the locomotor info
	// + 0xAC, RW 0x67BE04 `cmp` / `je`; stamped at RW 0x67BE17). Retail draws one picture per client frame, so it has nothing between two steps;
	// INFERENCE: the port, drawing more often, keeps the last step's angles and height on the pictures between two client frames
	const bool step = !m_services.clientFrame || !m_physicsStepped || *m_services.clientFrame != m_physicsClientFrame;
	if (step)
	{
		PhysicsXformInfo info;
		m_hasPhysicsXform = calcPhysicsXform(rec, pose, info);
		if (m_hasPhysicsXform)
		{
			m_lastPhysicsInfo = info;
		}
		m_physicsStepped = true;
		m_physicsClientFrame = m_services.clientFrame ? *m_services.clientFrame : 0;
	}
	if (m_hasPhysicsXform)
	{
		const PhysicsXformInfo &info = m_lastPhysicsInfo;

		Matrix3D mtx;
		for (int r = 0; r < 3; ++r)
		{
			for (int c = 0; c < 3; ++c)
			{
				mtx.Row[r][c] = pose.basis[r * 3 + c];
			}
		}
		mtx.Row[0][3] = pose.position.x;
		mtx.Row[1][3] = pose.position.y;
		mtx.Row[2][3] = pose.position.z;

		// Retail RW 0x67BE90 (donor BFME2 0x0027BB65):
		// Translate Z, Rotate Y (pitch), Rotate X (-roll), Rotate Z (yaw)
		mtx.Translate(Vector3(0.0f, 0.0f, info.m_totalZ));
		mtx.Rotate_Y(info.m_totalPitch);
		mtx.Rotate_X(-info.m_totalRoll);
		mtx.Rotate_Z(info.m_totalYaw);

		for (int r = 0; r < 3; ++r)
		{
			for (int c = 0; c < 3; ++c)
			{
				m_presentationBasis[r * 3 + c] = mtx.Row[r][c];
			}
		}
		m_presentationPosition.x = mtx.Row[0][3];
		m_presentationPosition.y = mtx.Row[1][3];
		m_presentationPosition.z = mtx.Row[2][3];
	}
}

void Drawable::getPresentationPose(float outBasis[9], Coord3D &outPos) const
{
	if (m_hasPhysicsXform)
	{
		std::memcpy(outBasis, m_presentationBasis, sizeof(m_presentationBasis));
		outPos = m_presentationPosition;
	}
	else
	{
		std::memcpy(outBasis, getBasis(), sizeof(float) * 9);
		outPos = *getPosition();
	}
}

Coord3D Drawable::getPresentationPosition() const
{
	if (m_hasPhysicsXform)
	{
		return m_presentationPosition;
	}
	return *getPosition();
}

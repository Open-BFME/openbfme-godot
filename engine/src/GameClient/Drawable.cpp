// OpenBFME. GPL-3.0.
// See GameClient/Drawable.h for the sources and the stops.

#include "GameClient/Drawable.h"

#include "Common/Audio/AudioRequests.h"

#include "Common/AsciiString.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/MapObjectDrawables.h"
#include "GameClient/RenderInterpolation.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DTreeDraw.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDrawVariants.h"
#include "Common/ModelState.h"
#include "GameLogic/SimMath.h"

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

void Drawable::reactToTransformChange(const Coord3D *oldPos, float oldAngle)
{
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
				const W3DDrawFrame f = e.draw->frame();
				e.animated = f.trackCount > 0 && f.tracks[0].anim && f.tracks[0].anim->Get_Num_Frames() > 1 && f.tracks[0].mode != W3D_ANIM_MODE_MANUAL;
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
			const W3DDrawFrame f = e.draw->frame();
			e.animated = f.trackCount > 0 && f.tracks[0].anim && f.tracks[0].anim->Get_Num_Frames() > 1 && f.tracks[0].mode != W3D_ANIM_MODE_MANUAL;
		}
	}
}

ModelConditionFlags Drawable::dependencySharedModelFlags() const
{
	ModelConditionFlags out;
	for (const DrawEntry &e : m_entries)
	{
		if (e.draw)
		{
			const ModelConditionFlags &f = e.draw->dependencySharedModelFlags();
			for (int b = 0; b < MODELCONDITION_COUNT; ++b)
			{
				if (f.test(b))
				{
					out.set(b);
				}
			}
		}
	}
	return out;
}

bool Drawable::applyDependencyFlags(const ModelConditionFlags &shared, const ModelConditionFlags &containerFlags)
{
	ModelConditionFlags next = m_flags;
	for (int b = 0; b < MODELCONDITION_COUNT; ++b)
	{
		if (shared.test(b))
		{
			next.set(b, containerFlags.test(b));
		}
	}
	bool changed = false;
	for (int b = 0; b < MODELCONDITION_COUNT && !changed; ++b)
	{
		changed = next.test(b) != m_flags.test(b);
	}
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

void Drawable::advanceAnimation(double elapsedMs)
{
	m_services.host.setChunkName(m_template->getName());
	for (DrawEntry &e : m_entries)
	{
		if (e.draw && e.animated)
		{
			e.draw->advance(elapsedMs);
		}
		advanceTires(e, elapsedMs);
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
	const W3DTruckDrawModuleData *truck = e.draw ? dynamic_cast<const W3DTruckDrawModuleData *>(e.data) : nullptr;
	if (!truck)
	{
		return;
	}
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
	m_moveSpeed = rec.moveSpeed;                   // lane SMOOTH-3: the walk / run cycle's speed source (RW 0x4B67D4)
	m_scriptTarget = rec.scriptTarget;             // lane FX-3
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
	m_moveSpeed = rec.moveSpeed;                   // lane SMOOTH-3: the walk / run cycle's speed source (RW 0x4B67D4)
	m_scriptTarget = rec.scriptTarget;             // lane FX-3
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

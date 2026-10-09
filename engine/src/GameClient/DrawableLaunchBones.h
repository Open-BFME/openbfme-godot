// OpenBFME. GPL-3.0.
//
// DrawableLaunchBones (lane RENDER-2): the answer of the launcher's drawable to the logic's launch-bone query. The logic's
// Weapon::calcProjectileLaunchPosition (RW 0x6CAB85, ported in BezierProjectileBehavior::calcLaunchTransform) asks the launcher's drawable where the
// weapon's launch bone is (RW 0x6756A1). This class computes the same answer WITHOUT a drawable, from the template's draw module data and logic state
// only (review r1): so a peer without drawables (headless, a LiveGame that never renders, an object whose drawable was dropped) gets the same bits.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   RW 0x6756A1 Drawable::getProjectileLaunchOffset: walks the drawable's draw modules in list order (+0x14C, the template's draw module order) and asks
//     each module's ObjectDrawInterface (vslot 0xA8) slot 0x18 with the drawable's model condition flags (+0x258); the first that answers true wins.
//   RW 0x4C34A2 W3DModelDraw::getProjectileLaunchOffset (every model draw class: the interface tables RW 0xBE3B78 / 0xBE1AD0 / 0xBE1E58 hold it): the
//     model state RW 0x4B4379(flags) and the animation state RW 0x4B4443(flags); no model state: false. validateStuff RW 0x4C2F4E(robj = null, the
//     drawable scale RW 0x478180, the module's ExtraPublicBone list (+0x30), the animation state). With AttachToBoneInAnotherModule (+0x40), Drawable::
//     getPristineBonePositions (RW 0x672A73 -> every module's RW 0x4C3731) looks that bone up (one bone, the plain name); found exactly once, its
//     translation (x, y) is turned by the drawable's angle (Thing +0x44; CRT cos / sin RW 0xA3CF84 / 0xA3CF90, then SSE: x c - y s, y c + x s) and added
//     to the launch translation. The barrels are the ANIMATION state's (+0xAC + slot * 0xC, 0x3C byte records, the 3x4 launch matrix at +0xC); none:
//     false; a barrel index out of range is 0.
//   RW 0x4BD9A7 validateCachedBones: the pristine pose is a NEW render object of the model state's model posed with the animation state's FIRST animation
//     (RW 0x4BD789 with the model state's Skeleton / prefix, then numbered with the LOD number RW 0x4B24C4) at min(FrameForPristineBonePositions (+0x60),
//     frames - 1); with no animation, or one that does not resolve, the bind pose. The bones: GameData StandardPublicBone, then the model state's public bones.
//   RW 0x4BDED7 validateWeaponBarrelInfo: see W3DModelDrawBones.cpp.
//
// THE INPUTS, all logic state or template data (hashed by Object::crc / GameLogicSettings::crc): the template's draw modules; the flags (launchFlags: the
// object's model condition bits, plus the bits the drawable starts with - the map placement's flags (Object::getPlacementConditionBits), else NIGHT / SNOW
// from GameLogicSettings as DrawableManager::objectCreated sets them); the instance scale (Object::getInstanceScale); the object's angle (the drawable's
// follows it at once in retail). The pose is evaluated by GameLogic/Object/PristinePose (simulation code, numeric facade, canonical environment) from the
// values the W3D loaders store as read; the excluded W3D code on this path does lookups and bookkeeping only (asset and bone name lookups, the state
// selection RW 0x4B4379 / 0x4B4443, W3DModelBones::buildFromPose).
//
// THE CACHE: per (template name, draw module index, model state index, animation state index, scale bits): stable identifiers, never pointers. reset() drops
// it and starts a new generation; LiveGame calls it at every load, the asset data being fixed for a loaded game. Results do not depend on the cache (a cold
// and a warm query give the same bits, pinned by test).
//
// INFERENCE / not ported (stop S-460, docs/STOPS.md, reported at runtime by stops()):
//   * retail caches the pristine bones IN the shared animation state, filled by whoever asks first (the draw module may ask first with its LIVE render
//     object: an animation state without an animation then takes the live object's current animation); this port always takes the launch path's branch
//     (a new render object: the bind pose);
//   * the model variation index (drawable +0x364) picks modelNames[index % count]; this port takes the first name (as the draw runtime does);
//   * the scale is applied to the posed bones afterwards, element by element (exact for scale 1); the raw-animation arm sums in the classic order (S-028);
//   * the adaptive delta filter table and the attach turn use SimMath::sinDet / cosDet, not the MSVCR71 functions (S-167);
//   * the drawable's flags are taken as the object's bits plus its starting bits (a bit the object set before its drawable existed and later cleared is
//     not tracked separately).

#pragma once

#include "Common/ModelState.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDrawServices.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDrawBones.h"
#include "GameLogic/Combat/WeaponDelivery.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

class ThingTemplate;

class DrawableLaunchBones : public ProjectileLaunchOffsets
{
public:
	// The stable identity of one draw module: its template's name and its index in the template's draw module list.
	struct ModuleKey
	{
		std::string templateName;
		int module = 0;
	};

	// standardPublicBones: GameData StandardPublicBone (supplied by the caller, never hard-coded). animationLodNumber: the LOD number the numbered retry
	// of the animation resolver appends (RW 0x4B24C4).
	DrawableLaunchBones(W3DDrawAssets &assets, std::vector<std::string> standardPublicBones, int animationLodNumber = 0);

	// ProjectileLaunchOffsets: RW 0x6756A1 for `launcher`, from its template and logic state (no drawable needed).
	bool launchOffset(const Object &launcher, int wslot, int barrel, float launch[12]) override;
	// RW 0x68C5AF's drawable part (RW 0x672A73 with one slot) from the template data and logic state, like launchOffset
	bool singleLogicalBone(const Object &obj, const std::string &bone, float out[12]) override;
	// lane GARRISON-1: RW 0x68C650 (start index 1) with the flags `bits` in place of the object's own model condition bits (the garrison's three damage states)
	int multiLogicalBones(const Object &obj, const std::string &prefix, const std::array<std::uint32_t, 19> &bits, int maxBones, float (*out)[12]) override;

	// The flags the launcher's drawable carries (see the header comment).
	static ModelConditionFlags launchFlags(const Object &launcher);
	// launchFlags with `bits` instead of the object's model condition bits
	static ModelConditionFlags launchFlags(const Object &launcher, const std::array<std::uint32_t, 19> &bits);

	// RW 0x6756A1 over the template's draw modules.
	bool getProjectileLaunchOffset(const ThingTemplate &tt, const ModelConditionFlags &flags, float scale, float angle, int wslot, int barrel, float launch[12]);
	// RW 0x4C34A2 for one model draw module. `tt` (may be null) supplies the other modules for AttachToBoneInAnotherModule.
	bool getProjectileLaunchOffset(const W3DModelDrawModuleData &data, const ModuleKey &key, const ModelConditionFlags &flags, float scale, float angle,
		const ThingTemplate *tt, int wslot, int barrel, float launch[12]);
	// RW 0x672A73 over the template's draw modules / RW 0x4C3731 for one module: up to `maxBones` transforms of the bone `name` (numbered NAME01.. when
	// startIndex > 0). Returns the count.
	int getPristineBonePositions(const ThingTemplate &tt, const ModelConditionFlags &flags, float scale, const std::string &name, int startIndex,
		Matrix3D *transforms, int maxBones);
	int getPristineBonePositions(const W3DModelDrawModuleData &data, const ModuleKey &key, const ModelConditionFlags &flags, float scale,
		const std::string &name, int startIndex, Matrix3D *transforms, int maxBones);
	// RW 0x4C3545..0x4C3637: the attach bone's translation turned by the drawable's angle: x c - y s, y c + x s, z
	static void turnAttachOffset(const Matrix3D &bone, float angle, float out[3]);

	// RW 0x4C2F4E / 0x4BD9A7 / 0x4BDED7 with robj = null, cached. nullptr when the model state has no model, the model is missing or the pose has no value
	// (each reported in problems()).
	const W3DModelBones *bones(const W3DModelDrawModuleData &data, const ModuleKey &key, const ModelConditionInfo &modelState,
		const AnimationStateInfo *animState, float scale);

	// the pose animation the bones of (modelState, animState) were taken from ("" = the bind pose) and its frame; for tests and reports
	struct PoseInfo
	{
		std::string animation;
		int frame = 0;
	};
	PoseInfo poseOf(const W3DModelDrawModuleData &data, const ModuleKey &key, const ModelConditionInfo &modelState, const AnimationStateInfo *animState,
		float scale);

	// Drops every cached pose and starts a new generation (a new load: the asset data and the templates may change).
	void reset();
	unsigned generation() const { return m_generation; }

	// problems met while building (a missing model, an animation that did not resolve, an undefined frame, missing bones), each once, in order
	const std::vector<std::string> &problems() const { return m_problems; }
	// "[S-460] ..." once the provider has answered a query (the numeric and fidelity deviations of this path, with the problem count)
	std::vector<std::string> stops() const;
	size_t cachedStates() const { return m_cache.size(); }
	unsigned long long queries() const { return m_queries; }
	unsigned long long answered() const { return m_answered; }

private:
	struct Entry
	{
		std::unique_ptr<W3DModelBones> bones;
		PoseInfo pose;
		bool ok = false;
	};
	typedef std::tuple<std::string, int, int, int, std::uint32_t> Key;
	Entry &entry(const W3DModelDrawModuleData &data, const ModuleKey &key, const ModelConditionInfo &modelState, const AnimationStateInfo *animState,
		float scale);
	void problem(const std::string &p);

	W3DDrawAssets &m_assets;
	std::vector<std::string> m_standardPublicBones;
	int m_animationLodNumber = 0;
	std::map<Key, Entry> m_cache; // looked up only, never iterated
	std::vector<std::string> m_problems;
	unsigned long long m_queries = 0, m_answered = 0;
	unsigned m_generation = 0;
};

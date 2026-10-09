// OpenBFME. GPL-3.0.
//
// Public bones, pristine bone positions, weapon barrels and turret bones of one model condition state on one model.
// Port of ZH W3DModelDraw.cpp ModelConditionInfo::validateStuff / validateCachedBones / validateWeaponBarrelInfo /
// validateTurretInfo / findPristineBone and the file-static findSingleBone, findSingleSubObj, doSingleBoneName (W3DModelDraw.cpp:
// 439-966), working on the engine's RenderObjPrototype + HTreeClass instead of a live RenderObjClass.
//
// DONOR: ZH, with the RotWK barrel rules of validateWeaponBarrelInfo (RW 0x4BDED7, lane RENDER-2; see build()). The INI fields that
// feed them (WeaponFireFXBone, WeaponLaunchBone, WeaponRecoilBone, WeaponMuzzleFlash, Turret*, ExtraPublicBone, ParticleSysBone) are
// the RotWK ones. TARGET (RW 0x4BD9A7, RENDER-2): RotWK keeps the pristine bones per ANIMATION state, posed with that state's first
// animation at min(FrameForPristineBonePositions, frames - 1); the caller passes that pose (GameClient/DrawableLaunchBones does) or
// none (the bind pose: stop S-097 for the draw module's own bones).

#pragma once

#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDraw.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <map>
#include <string>
#include <vector>

struct W3DPristineBone
{
	Matrix3D mtx;      ///< the bone's transform in the pristine pose, with the drawable scale applied at the root
	int boneIndex = 0; ///< pivot index (the bone of the sub object when found as a sub object)
};

struct W3DWeaponBarrel
{
	int recoilBone = 0;
	int fxBone = 0;
	int muzzleFlashBone = 0;
	Matrix3D projectileOffset; ///< the launch bone's pristine transform
	std::string muzzleFlashName;
};

struct W3DTurretBones
{
	int angleBone = 0;
	int pitchBone = 0;
};

class W3DModelBones
{
public:
	// scale: the drawable's scale. poseAnim/poseFrame: the pristine pose (nullptr: the bind pose). extraPublicBones: the module's
	// ExtraPublicBone list; standardPublicBones: the global catch-all bones (GameData.ini StandardPublicBone, supplied by the
	// caller, never hard-coded here).
	void build(const ModelConditionInfo &state, const RenderObjPrototype *proto, const std::vector<std::string> &extraPublicBones,
		const std::vector<std::string> &standardPublicBones, float scale, const HAnimClass *poseAnim, float poseFrame);

	// RENDER-2 (review r1): the same bookkeeping from pivot transforms already posed and scaled by the caller (GameLogic/Object/PristinePose, the
	// simulation's deterministic evaluator); no floating arithmetic here. `pose` has one matrix per pivot of proto->Tree.
	void buildFromPose(const ModelConditionInfo &state, const RenderObjPrototype *proto, const std::vector<std::string> &extraPublicBones,
		const std::vector<std::string> &standardPublicBones, const std::vector<Matrix3D> &pose);

	// ZH ModelConditionInfo::findPristineBone: nullptr (and *boneIndex 0) when the bone is not cached.
	const W3DPristineBone *findPristineBone(const std::string &lowerName, int *boneIndex = nullptr) const;
	bool findPristineBonePos(const std::string &lowerName, Vector3 &pos) const;

	std::map<std::string, W3DPristineBone> pristine;
	std::vector<W3DWeaponBarrel> barrels[W3D_WEAPONSLOT_COUNT];
	bool hasRecoilBonesOrMuzzleFlashes[W3D_WEAPONSLOT_COUNT] = { false, false, false, false, false };
	std::vector<W3DTurretBones> turrets; ///< parallel to ModelConditionInfo::turrets
	std::vector<std::string> missingBones; ///< public / barrel / turret bones the model lacks (ZH DEBUG_CRASHes; reported here)
	bool valid = false;                    ///< a model was given
};

// Sub object lookup the draw module uses for hide / show and for bones that are really sub objects: a sub object matches when its
// name equals `name` or the part of its name after the first '.', without case. INFERENCE (S-098): ZH's
// RenderObjClass::Get_Sub_Object_By_Name is not in the reference clone; BFME scripts use the short mesh name ("arrow").
int W3DFindSubObjectByName(const RenderObjPrototype &proto, const std::string &name);

// ZH doHideShowBoneSubObjs (W3DModelDraw.cpp:2275-2300): the sub object and every sub object whose bone descends from its bone; TARGET RW 0x4B3155 /
// 0x4B31C9 -> 0x4B2964 (lane RENDER-3): only for a bone index > 0 and below the pivot count (a root-bone sub object is alone).
// Returns the indices into proto.SubObjects.
std::vector<int> W3DSubObjectsUnderSubObject(const RenderObjPrototype &proto, int subObjectIndex);

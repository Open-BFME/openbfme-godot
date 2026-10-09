// OpenBFME. GPL-3.0. See W3DModelDrawBones.h.

#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDrawBones.h"

#include "Common/AsciiString.h"

#include <algorithm>
#include <cstdio>

namespace
{
bool isNone(const std::string &s) { return AsciiStringUtil::compareNoCase(s, "NONE") == 0; }

struct BoneFinder
{
	const RenderObjPrototype &proto;
	HTreePose pose;

	// ZH findSingleBone
	bool bone(const std::string &name, W3DPristineBone &out) const
	{
		if (name.empty() || isNone(name) || !proto.Tree)
		{
			return false;
		}
		const int index = proto.Tree->Get_Bone_Index(name); // 0 when not found, and ZH treats 0 as not found (the root is unreachable)
		if (index == 0)
		{
			return false;
		}
		out.boneIndex = index;
		out.mtx = pose.Transform[(size_t)index];
		return true;
	}

	// ZH findSingleSubObj
	bool subObj(const std::string &name, W3DPristineBone &out) const
	{
		if (name.empty() || isNone(name))
		{
			return false;
		}
		const int i = W3DFindSubObjectByName(proto, name);
		if (i < 0)
		{
			return false;
		}
		const int b = proto.SubObjects[(size_t)i].BoneIndex;
		if (b < 0 || b >= (int)pose.Transform.size())
		{
			return false;
		}
		out.boneIndex = b;
		out.mtx = pose.Transform[(size_t)b];
		return true;
	}
};

std::string numbered(const std::string &base, int i)
{
	char buffer[16];
	std::snprintf(buffer, sizeof(buffer), "%02d", i);
	return base + buffer;
}

// ZH doSingleBoneName (W3DModelDraw.cpp:496-566)
bool doSingleBoneName(const BoneFinder &finder, const std::string &boneName, std::map<std::string, W3DPristineBone> &map)
{
	bool foundAsBone = false;
	bool foundAsSubObj = false;
	const std::string name = AsciiStringUtil::lowered(boneName);
	W3DPristineBone info;
	if (finder.bone(name, info))
	{
		map[name] = info;
		foundAsBone = true;
	}
	for (int i = 1; i <= 99; ++i)
	{
		const std::string tmp = numbered(name, i);
		if (finder.bone(tmp, info))
		{
			map[tmp] = info;
			foundAsBone = true;
		}
		else
		{
			break;
		}
	}
	if (!foundAsBone)
	{
		if (finder.subObj(name, info))
		{
			map[name] = info;
			foundAsSubObj = true;
		}
		for (int i = 1; i <= 99; ++i)
		{
			const std::string tmp = numbered(name, i);
			if (finder.subObj(tmp, info))
			{
				map[tmp] = info;
				foundAsSubObj = true;
			}
			else
			{
				break;
			}
		}
	}
	return foundAsBone || foundAsSubObj;
}

std::string slotName(const std::vector<std::string> &v, size_t slot)
{
	return slot < v.size() ? v[slot] : std::string();
}
} // namespace

int W3DFindSubObjectByName(const RenderObjPrototype &proto, const std::string &name)
{
	for (size_t i = 0; i < proto.SubObjects.size(); ++i)
	{
		const std::string &n = proto.SubObjects[i].Name;
		if (AsciiStringUtil::compareNoCase(n, name) == 0)
		{
			return (int)i;
		}
		const size_t dot = n.find('.');
		if (dot != std::string::npos && AsciiStringUtil::compareNoCase(n.substr(dot + 1), name) == 0)
		{
			return (int)i;
		}
	}
	return -1;
}

std::vector<int> W3DSubObjectsUnderSubObject(const RenderObjPrototype &proto, int subObjectIndex)
{
	std::vector<int> out;
	if (subObjectIndex < 0 || subObjectIndex >= (int)proto.SubObjects.size() || !proto.Tree)
	{
		return out;
	}
	out.push_back(subObjectIndex);
	const int boneIdx = proto.SubObjects[(size_t)subObjectIndex].BoneIndex;
	// lane RENDER-3, TARGET RW 0x4B3155 / 0x4B31C9 (the apply of one hide / show record): the bone walk (RW 0x4B2964) runs only when the sub object's
	// bone index is > 0 and below the pivot count; a sub object on the root bone (GBBarracks_SKN's V1 / V2) hides itself alone
	if (boneIdx <= 0 || boneIdx >= proto.Tree->Num_Pivots())
	{
		return out;
	}
	for (int i = 0; i < (int)proto.SubObjects.size(); ++i)
	{
		if (i == subObjectIndex)
		{
			continue;
		}
		// the ZH loop: walk up from the sub object's bone to the root; a hit on boneIdx makes it a child
		int parent = proto.SubObjects[(size_t)i].BoneIndex;
		bool isChild = false;
		int guard = 0;
		while (parent != 0 && guard++ <= proto.Tree->Num_Pivots())
		{
			parent = proto.Tree->Get_Pivot(parent).ParentIdx;
			if (parent == boneIdx)
			{
				isChild = true;
				break;
			}
			if (parent < 0)
			{
				break;
			}
		}
		if (isChild)
		{
			out.push_back(i);
		}
	}
	return out;
}

void W3DModelBones::build(const ModelConditionInfo &state, const RenderObjPrototype *proto, const std::vector<std::string> &extraPublicBones,
	const std::vector<std::string> &standardPublicBones, float scale, const HAnimClass *poseAnim, float poseFrame)
{
	std::vector<Matrix3D> transforms;
	if (proto && proto->Tree)
	{
		HTreePose pose;
		// ZH sets Matrix3D::Scale(scale) as the render object's transform and reads Get_Bone_Transform, which is that scale times the
		// bone's transform in the identity-rooted pose. (The BFME2 pose holds each pivot as a quaternion and a translation, so a scale
		// cannot ride in the root matrix passed to the evaluators; it is applied to the result instead.)
		const Matrix3D identity;
		if (poseAnim)
		{
			proto->Tree->Anim_Pose(identity, poseAnim, poseFrame, pose);
		}
		else
		{
			proto->Tree->Base_Pose(identity, pose);
		}
		if (scale != 1.0f)
		{
			Matrix3D scaleMatrix;
			scaleMatrix.Row[0][0] = scaleMatrix.Row[1][1] = scaleMatrix.Row[2][2] = scale;
			for (Matrix3D &m : pose.Transform)
			{
				Matrix3D::Multiply(scaleMatrix, m, &m);
			}
		}
		transforms = pose.Transform;
	}
	buildFromPose(state, proto, extraPublicBones, standardPublicBones, transforms);
}

void W3DModelBones::buildFromPose(const ModelConditionInfo &state, const RenderObjPrototype *proto, const std::vector<std::string> &extraPublicBones,
	const std::vector<std::string> &standardPublicBones, const std::vector<Matrix3D> &poseTransforms)
{
	pristine.clear();
	missingBones.clear();
	turrets.clear();
	for (int s = 0; s < W3D_WEAPONSLOT_COUNT; ++s)
	{
		barrels[s].clear();
		hasRecoilBonesOrMuzzleFlashes[s] = false;
	}
	valid = false;
	if (!proto || !proto->Tree || poseTransforms.size() != (size_t)proto->Tree->Num_Pivots())
	{
		return;
	}
	valid = true;
	BoneFinder finder{ *proto, HTreePose() };
	finder.pose.Transform = poseTransforms;

	// ZH validateCachedBones: the global bones first (catch-alls, silently absent from most models), then the state's own
	for (const std::string &b : standardPublicBones)
	{
		doSingleBoneName(finder, b, pristine);
	}
	std::vector<std::string> publicBones = state.publicBones;
	for (const std::string &b : extraPublicBones) // ZH validateStuff adds the module's ExtraPublicBone list to each state
	{
		const std::string lower = AsciiStringUtil::lowered(b);
		if (!lower.empty() && !isNone(lower) && std::find(publicBones.begin(), publicBones.end(), lower) == publicBones.end())
		{
			publicBones.push_back(lower);
		}
	}
	for (const std::string &b : publicBones)
	{
		if (!doSingleBoneName(finder, b, pristine))
		{
			missingBones.push_back(b);
		}
	}

	// ZH validateWeaponBarrelInfo (W3DModelDraw.cpp:716-856). TARGET RW 0x4BDED7 (the RotWK twin, lane RENDER-2) differs in two places: a slot is
	// looked at only when it names a WeaponFireFXBone or a WeaponLaunchBone (RW 0x4BDFA9..0x4BDFC7: the recoil and muzzle flash names alone add no
	// barrel), and the unnumbered fallback looks up only the launch bone and the fx bone and keeps a barrel when one of them was found (RW 0x4BE2EB..0x4BE395:
	// its recoil and muzzle flash bones stay 0).
	for (int wslot = 0; wslot < W3D_WEAPONSLOT_COUNT; ++wslot)
	{
		const std::string fxBoneName = slotName(state.weaponFireFXBone, (size_t)wslot);
		const std::string recoilBoneName = slotName(state.weaponRecoilBone, (size_t)wslot);
		const std::string mfName = slotName(state.weaponMuzzleFlash, (size_t)wslot);
		const std::string plbName = slotName(state.weaponLaunchBone, (size_t)wslot);
		if (fxBoneName.empty() && plbName.empty())
		{
			continue;
		}
		int prevFxBone = 0;
		for (int i = 1; i <= 99; ++i)
		{
			W3DWeaponBarrel info;
			if (!recoilBoneName.empty())
			{
				findPristineBone(numbered(recoilBoneName, i), &info.recoilBone);
			}
			if (!mfName.empty())
			{
				findPristineBone(numbered(mfName, i), &info.muzzleFlashBone);
				if (info.muzzleFlashBone)
				{
					info.muzzleFlashName = numbered(mfName, i);
				}
			}
			if (!fxBoneName.empty())
			{
				findPristineBone(numbered(fxBoneName, i), &info.fxBone);
				if (info.fxBone == 0 && info.muzzleFlashBone != 0)
				{
					info.fxBone = prevFxBone; // several muzzle flashes, one fx bone: reuse it
				}
			}
			int plbBoneIndex = 0;
			if (!plbName.empty())
			{
				if (const W3DPristineBone *m = findPristineBone(numbered(plbName, i), &plbBoneIndex))
				{
					info.projectileOffset = m->mtx;
				}
			}
			if (info.fxBone == 0 && info.recoilBone == 0 && info.muzzleFlashBone == 0 && plbBoneIndex == 0)
			{
				break;
			}
			barrels[wslot].push_back(info);
			if (info.recoilBone != 0 || info.muzzleFlashBone != 0)
			{
				hasRecoilBonesOrMuzzleFlashes[wslot] = true;
			}
			prevFxBone = info.fxBone;
		}
		if (barrels[wslot].empty())
		{
			// the unadorned names
			W3DWeaponBarrel info;
			const W3DPristineBone *plb = plbName.empty() ? nullptr : findPristineBone(plbName, nullptr);
			if (plb)
			{
				info.projectileOffset = plb->mtx;
			}
			if (!fxBoneName.empty())
			{
				findPristineBone(fxBoneName, &info.fxBone);
			}
			if (info.fxBone != 0 || plb != nullptr)
			{
				barrels[wslot].push_back(info);
			}
			else
			{
				missingBones.push_back(!fxBoneName.empty() ? fxBoneName : (!plbName.empty() ? plbName : (!mfName.empty() ? mfName : recoilBoneName)));
			}
		}
	}

	// ZH validateTurretInfo
	for (const ModelTurretInfo &t : state.turrets)
	{
		W3DTurretBones tb;
		if (!t.angleBone.empty() && !findPristineBone(t.angleBone, &tb.angleBone))
		{
			missingBones.push_back(t.angleBone);
			tb.angleBone = 0;
		}
		if (!t.pitchBone.empty() && !findPristineBone(t.pitchBone, &tb.pitchBone))
		{
			missingBones.push_back(t.pitchBone);
			tb.pitchBone = 0;
		}
		turrets.push_back(tb);
	}
}

const W3DPristineBone *W3DModelBones::findPristineBone(const std::string &lowerName, int *boneIndex) const
{
	if (boneIndex)
	{
		*boneIndex = 0;
	}
	if (lowerName.empty())
	{
		return nullptr;
	}
	auto it = pristine.find(lowerName);
	if (it == pristine.end())
	{
		return nullptr;
	}
	if (boneIndex)
	{
		*boneIndex = it->second.boneIndex;
	}
	return &it->second;
}

bool W3DModelBones::findPristineBonePos(const std::string &lowerName, Vector3 &pos) const
{
	if (const W3DPristineBone *b = findPristineBone(lowerName))
	{
		pos = b->mtx.Get_Translation();
		return true;
	}
	pos = Vector3();
	return false;
}

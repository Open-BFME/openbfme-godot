// OpenBFME. GPL-3.0.
//
// DrawableAttach: a draw module placed on another module's bone (AttachToBoneInAnotherModule, W3DModelDraw module data +0x40; RW 0x4B6A6F..0x4B6B26)
// and the transform of a draw entry's model. CLIENT ONLY: it reads the drawable's draw frames and transform and writes nothing but the matrix handed
// in; it consumes no random and never reaches the simulation (tools/sim/sim_policy.json excludes it).
//
// RW 0x4B686D assembles the model's matrix; when the module names a bone of another module (RW 0x4B6A77 / 0x4B6AA2 ask the drawable, RW 0x672B5B)
// and that bone is found, the matrix is the bone's transform and the draw's vtable slot +0x104 (RW 0x4B6B26) may turn it (W3DSailModelDraw, RW 0x4D00CD).
// A bone that is not found leaves the ordinary object transform and calls no slot (Editions: RotWK 2.01 and BFME2 1.06 share the call; DONOR BFME2 0x00272835).

#include "GameClient/Drawable.h"
#include "Common/AsciiString.h"
#include "Libraries/WWVegas/WWMath/matrix3d.h"

// RW 0x672B5B (DONOR BFME2 0x00272835): the bone's pose in the model of the first entry that has it (RW 0x4B2F19 poses the state the draw shows: Blend_Pose
// of the two tracks, else Anim_Pose of track 0, else Base_Pose), carried to the world by the object's transform (basis times the instance scale, position).
bool Drawable::boneTransform(const std::string &boneName, Matrix3D &outTransform) const
{
	if (boneName.empty() || AsciiStringUtil::compareNoCase(boneName, "None") == 0)
	{
		return false; // RW 0x73AECB: "None" (and an empty name) is no bone
	}
	std::string lower = boneName;
	AsciiStringUtil::toLower(lower); // bone names are case insensitive (HTreeClass::Get_Bone_Index)

	for (const DrawEntry &e : m_entries)
	{
		if (!e.draw)
		{
			continue;
		}
		const W3DDrawFrame f = e.draw->frame();
		if (!f.model || !f.model->Tree)
		{
			continue;
		}
		int boneIdx = f.model->Tree->Get_Bone_Index(lower);
		if (boneIdx <= 0)
		{
			// a sub object's bone counts too (RW 0x4C6510 asks the model's sub objects when the hierarchy has no such bone)
			const int sub = W3DFindSubObjectByName(*f.model, lower);
			boneIdx = sub >= 0 ? f.model->SubObjects[(size_t)sub].BoneIndex : -1;
		}
		if (boneIdx <= 0)
		{
			continue;
		}
		HTreePose pose;
		if (f.blending && f.motion1 != nullptr)
		{
			f.model->Tree->Blend_Pose(Matrix3D(), f.motion0, f.frame0, f.motion1, f.frame1, f.blendPercentage, pose);
		}
		else if (f.motion0 != nullptr)
		{
			f.model->Tree->Anim_Pose(Matrix3D(), f.motion0, f.frame0, pose);
		}
		else
		{
			f.model->Tree->Base_Pose(Matrix3D(), pose);
		}
		if ((size_t)boneIdx >= pose.Transform.size())
		{
			continue;
		}
		Matrix3D object;
		const float *basis = getBasis();
		const float scale = getInstanceScale();
		const Coord3D *pos = getPosition();
		for (int r = 0; r < 3; ++r)
		{
			for (int c = 0; c < 3; ++c)
			{
				object.Row[r][c] = basis[r * 3 + c] * scale;
			}
		}
		object.Row[0][3] = pos->x;
		object.Row[1][3] = pos->y;
		object.Row[2][3] = pos->z;
		Matrix3D::Multiply(object, pose.Transform[(size_t)boneIdx], &outTransform);
		return true;
	}
	return false;
}

// RW 0x4B6A6F..0x4B6B26: see Drawable.h. The unattached transform is the object's own with the entry's construction offset (entryPosition, RW 0x4B686D).
bool Drawable::entryTransform(const DrawEntry &e, Matrix3D &outMatrix) const
{
	if (!e.attachToBone.empty() && boneTransform(e.attachToBone, outMatrix))
	{
		if (e.draw)
		{
			e.draw->adjustAttachedTransform(outMatrix); // RW 0x4B6B26: vtable slot +0x104, after the bone was found
		}
		return true;
	}
	const float *basis = getBasis();
	const float scale = getInstanceScale();
	const Coord3D pos = entryPosition(e);
	for (int r = 0; r < 3; ++r)
	{
		for (int c = 0; c < 3; ++c)
		{
			outMatrix.Row[r][c] = basis[r * 3 + c] * scale;
		}
	}
	outMatrix.Row[0][3] = pos.x;
	outMatrix.Row[1][3] = pos.y;
	outMatrix.Row[2][3] = pos.z;
	return false;
}

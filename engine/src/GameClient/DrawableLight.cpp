// OpenBFME. GPL-3.0.
//
// DrawableLight (lane LOCAL-28, #331): the drawable's side of W3DLightDraw - the light module's flicker / pulse step and the bone position an
// AttachToBoneInAnotherModule light follows. CLIENT ONLY: it reads the drawable's draw frames and transform and writes only the client light pool
// (GameClient/ClientEffects.h) through W3DLightDraw; it consumes no random and never reaches the simulation (tools/sim/sim_policy.json excludes it).
// The code moved here unchanged from Drawable.cpp (that file is a simulation-input file under tools/sim/sim_audit.py).

#include "GameClient/Drawable.h"
#include "Common/AsciiString.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DLightDraw.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDrawBones.h"
#include "GameLogic/SimMath.h"

// lane LOCAL-28: the light module's flicker / pulse, stepped by render time in seconds (RW 0x4CE930)
void Drawable::stepLightDraw(DrawEntry &e, double elapsedMs)
{
	e.lightDraw->stepFlicker((float)(elapsedMs * 0.001));
}

// lane LOCAL-28, RW 0x672B5B (Drawable::getBonePosition, DONOR BFME2 0x272835): the world position of a named bone of this drawable's current pose.
// Retail asks every draw module for the bone (a module without a render object answers no) and, the first one that has it, builds the pose of
// the animation state it shows (RW 0x4B2F19: Blend_Pose of the two tracks, else Anim_Pose of track 0, else Base_Pose), takes the bone's translation,
// scales it by the drawable's instance scale and rotates it by the drawable's orientation about Z. The scale is applied to the model-space
// translation and the rotation to the result, the order INFERENCE (retail's matrix composition of the two was not decoded).
// Retail iterates m_modules, calls slotA4() on each module, and if non-null calls slot14(boneName, &matrix) (RW 0x004B2F19),
// which queries the current animated pose of m_renderObj via Get_Bone_Index and Get_Bone_Transform.
bool Drawable::boneWorldPosition(const std::string &boneName, Coord3D &outPos) const
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
		const Matrix3D &m = pose.Transform[(size_t)boneIdx];
		const float scale = getInstanceScale();
		const float px = m.Row[0][3] * scale;
		const float py = m.Row[1][3] * scale;
		const float pz = m.Row[2][3] * scale;
		const float c = SimMath::cosDet(getOrientation());
		const float s = SimMath::sinDet(getOrientation());
		const float wx = SimMath::sseSub(SimMath::sseMul(px, c), SimMath::sseMul(py, s));
		const float wy = SimMath::sseAdd(SimMath::sseMul(py, c), SimMath::sseMul(px, s));
		outPos = Coord3D{ getPosition()->x + wx, getPosition()->y + wy, getPosition()->z + pz };
		return true;
	}
	return false;
}

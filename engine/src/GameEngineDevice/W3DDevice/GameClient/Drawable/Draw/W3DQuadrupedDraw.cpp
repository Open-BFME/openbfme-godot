// OpenBFME. GPL-3.0. See W3DQuadrupedDraw.h for RotWK 2.01 binary facts.
// TARGET RW 0x004C97E6 getModuleNameKey, RW 0x004C982B GetSnapshotName, RW 0x004C984A xfer,
// RW 0x004C9892 scalar deleting dtor, RW 0x004CA6F0 loadPostProcess.

#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DQuadrupedDraw.h"

#include "Common/AsciiString.h"
#include "Common/Xfer.h"
#include "GameLogic/SnapshotXfer.h"
#include "GameClient/DrawableLaunchBones.h"

W3DQuadrupedDraw::W3DQuadrupedDraw(const W3DQuadrupedDrawModuleData &data, W3DDrawAssets &assets, W3DDrawRandom &random,
	W3DDrawScriptHost *scripts, const Options &options)
	: W3DScriptedModelDraw(data, assets, random, scripts, options)
	, m_quadData(data)
	, m_drawAssets(assets)
	, m_quadOptions(options)
{
}

W3DQuadrupedDraw::~W3DQuadrupedDraw() = default;

// RW 0x4B2F19: W3DModelDraw::getCurrentWorldspaceClientBonePositions
bool W3DQuadrupedDraw::getCurrentWorldspaceClientBonePositions(const std::string &boneName, const Matrix3D &renderTransform,
	Matrix3D &outTransform) const
{
	if (boneName.empty())
	{
		return false;
	}
	const W3DDrawFrame f = frame();
	if (!f.model || !f.model->Tree)
	{
		return false;
	}
	// HLodClass::Get_Bone_Index checks HTree pivots alone and treats index 0 (RootTransform / missing) as not found.
	const int boneIdx = f.model->Tree->Get_Bone_Index(boneName);
	if (boneIdx <= 0)
	{
		return false;
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
		return false;
	}
	Matrix3D::Multiply(renderTransform, pose.Transform[(size_t)boneIdx], &outTransform);
	return true;
}

// RW 0x4C9A39: W3DQuadrupedDraw::getFootLocations
bool W3DQuadrupedDraw::getFootLocations(std::vector<Coord3D> &feet, bool worldSpace, const Matrix3D &renderTransform,
	const Matrix3D *objectTransform, const ModelConditionFlags *conditionFlags) const
{
	const std::string *boneNames[4] = {
		&m_quadData.m_leftFrontFootBone,
		&m_quadData.m_rightFrontFootBone,
		&m_quadData.m_leftRearFootBone,
		&m_quadData.m_rightRearFootBone,
	};
	// RW 0x4C9A51..0x4C9A92: require at least one configured front bone name and one configured rear bone name.
	if (boneNames[0]->empty() && boneNames[1]->empty())
	{
		return false;
	}
	if (boneNames[2]->empty() && boneNames[3]->empty())
	{
		return false;
	}
	// RW 0x4C9A9A..0x4C9AC4: worldSpace queries require a drawable/object transform before resizing `feet`.
	if (worldSpace && !objectTransform)
	{
		return false;
	}

	feet.resize(4);
	bool found[4] = { false, false, false, false };
	const ModelConditionFlags &flags = conditionFlags ? *conditionFlags : pendingFlags();
	const float scale = m_quadOptions.instanceScale ? m_quadOptions.instanceScale() : m_quadOptions.scale;

	for (int i = 0; i < 4; ++i)
	{
		if (boneNames[i]->empty())
		{
			found[i] = false;
			continue;
		}
		Matrix3D mtx;
		if (worldSpace)
		{
			if (!m_pristineBones)
			{
				m_pristineBones.reset(new DrawableLaunchBones(m_drawAssets, m_quadOptions.standardPublicBones, m_quadOptions.animationLodNumber));
			}
			const int count = m_pristineBones->getPristineBonePositions(m_quadData, DrawableLaunchBones::ModuleKey{ std::string(), 0 }, flags,
				scale, *boneNames[i], 0, &mtx, 1);
			found[i] = (count > 0);
			if (found[i])
			{
				Matrix3D worldMtx;
				Matrix3D::Multiply(*objectTransform, mtx, &worldMtx);
				mtx = worldMtx;
			}
		}
		else
		{
			found[i] = getCurrentWorldspaceClientBonePositions(*boneNames[i], renderTransform, mtx);
		}
		if (found[i])
		{
			feet[(size_t)i] = Coord3D{ mtx.Row[0][3], mtx.Row[1][3], mtx.Row[2][3] };
		}
	}

	// RW 0x4C9DB4..0x4C9E0E: sibling completion for front pair [0, 1] and rear pair [2, 3].
	if (!found[0])
	{
		if (!found[1])
		{
			return false;
		}
		feet[0] = feet[1];
	}
	else if (!found[1])
	{
		feet[1] = feet[0];
	}

	if (!found[2])
	{
		if (!found[3])
		{
			return false;
		}
		feet[2] = feet[3];
	}
	else if (!found[3])
	{
		feet[3] = feet[2];
	}

	return true;
}

void W3DQuadrupedDraw::xfer(Xfer &x)
{
	// RW 0x004C984A (DONOR BFME2 0x000CA137): version(1, 1) + W3DScriptedModelDraw::xfer
	SnapshotXfer::version(x, 1, 1);
}

// OpenBFME. GPL-3.0.
//
// W3DQuadrupedDraw runtime override: the four foot locations of a huge four-legged beast (RotWK 2.01).
// The consumer of getFootLocations (Drawable::calcPhysicsXformHugeFourLegs, RW 0x67B7AA) is not part of this class.
//
// TARGET (RotWK game.dat, caveat S-001):
//   - Factory RW 0x4641AA (size 0x2E8) -> constructor RW 0x4C97BC (calls W3DScriptedModelDraw ctor RW 0x4C04A3).
//   - Vtables installed by RW 0x4C97BC:
//       * DrawModule primary vtable RW 0xBE1B98 at +0x00
//       * ObjectDrawInterface vtable RW 0xBE1AD0 at +0x0C
//       * secondary vtable RW 0xBE3B70 at +0x10
//   - W3DQuadrupedDrawModuleData: createData RW 0x464A0F, ctor RW 0x4649B9, field table RW 0xBE1A80
//     (LeftFrontFootBone +0x188, RightFrontFootBone +0x18C, LeftRearFootBone +0x190, RightRearFootBone +0x194).
//   - Scanning every slot across 0xBE1B98 / 0xBE1AD0 / 0xBE3B70 confirms the only W3DQuadrupedDraw overrides beyond
//     W3DScriptedModelDraw are destructor (0x4C9892 / 0x4C9831), RTTI (0x4C982B / 0x4C97E6), xfer (0x4C984A forwarding
//     to base RW 0x4C52FE with no extra fields), and ObjectDrawInterface slot 47 (+0xBC) -> RW 0x4C9A39
//     W3DQuadrupedDraw::getFootLocations (base ObjectDrawInterface 0xBE3B78 slot 47 is RW 0x8F8014 returning false).
//   - When worldSpace == false (called from the physics transform of the huge four-legged beasts, RW 0x67B7AA at RW 0x67B8AE),
//     RW 0x4C9A39 queries getCurrentWorldspaceClientBonePositions (RW 0x4B2F19: requires live render object,
//     Get_Bone_Index != 0, Get_Bone_Transform) for [LeftFront, RightFront, LeftRear, RightRear].
//     If one side of a front/rear pair is missing, it copies its sibling; if both front or both rear are missing,
//     getFootLocations returns false so HugeFourLegs falls back to geometry bounds.
//   - When worldSpace == true, RW 0x4C9A39 queries getPristineBonePositionsForConditionState (RW 0x4C3731) and
//     pre-multiplies by the object's world transform (*objectTransform).

#pragma once

#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDrawVariants.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DScriptedModelDraw.h"

#include <memory>
#include <string>
#include <vector>

class DrawableLaunchBones;
class Xfer;

// RW 0x004C9831 (~W3DQuadrupedDraw)
class W3DQuadrupedDraw : public W3DScriptedModelDraw
{
public:
	W3DQuadrupedDraw(const W3DQuadrupedDrawModuleData &data, W3DDrawAssets &assets, W3DDrawRandom &random, W3DDrawScriptHost *scripts = nullptr,
		const Options &options = Options());
	~W3DQuadrupedDraw() override;

	// RW 0x4C9A39 (ObjectDrawInterface slot 47 / +0xBC): fills `feet` with 4 world-space positions
	// [LeftFront, RightFront, LeftRear, RightRear]. Returns false if required foot bones are missing or cannot be resolved.
	bool getFootLocations(std::vector<Coord3D> &feet, bool worldSpace, const Matrix3D &renderTransform, const Matrix3D *objectTransform = nullptr,
		const ModelConditionFlags *conditionFlags = nullptr) const override;

	// RW 0x4B2F19: animated client world transform of `boneName` under `renderTransform`.
	// Returns false if no model/HTree is loaded or Get_Bone_Index returns 0 (missing or RootTransform).
	bool getCurrentWorldspaceClientBonePositions(const std::string &boneName, const Matrix3D &renderTransform, Matrix3D &outTransform) const;

	const W3DQuadrupedDrawModuleData &quadrupedData() const { return m_quadData; }
	// RW 0x004C984A (DONOR BFME2 0x000CA137): version(1, 1) + W3DScriptedModelDraw::xfer
	void xfer(Xfer &x);

private:
	const W3DQuadrupedDrawModuleData &m_quadData;
	W3DDrawAssets &m_drawAssets;
	Options m_quadOptions;
	mutable std::unique_ptr<DrawableLaunchBones> m_pristineBones;
};

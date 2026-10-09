// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Port of ZH Libraries/Source/WWVegas/WW3D2/htree.h/.cpp: loading (Load_W3D, read_pivots),
// the bind pose (Base_Update) and the pose evaluators Anim_Update / Blend_Update.
//
// Target facts (BFME2 retail, Ghidra of the BFME2 1.06 game.dat): HTree::Anim_Update is FUN_00562bb0 and
// Blend_Update FUN_005645b0 (reached from Animatable3DObjClass::Blend_Update RVA 0x1A4890). Both keep ZH's per-pivot
// steps (base multiply, Translate(trans * ScaleFactor), postMul(rotation) only when the animation HAS a rotation channel,
// visibility, then BFME's per-pivot fade) and Blend_Update lerps with (b - a) * pct + a and blends rotations with the
// BFME2 nlerp (call to 0x00B17550 = RVA 0x00717550), never Fast_Slerp.
// Donor facts (Open-BFME-1 game/Libraries/Source/WWVegas/WW3D2/htree.cpp:547-873): the same structure with Fast_Slerp,
// the fade arithmetic, and HTree_Post_Multiply<false> for Blend_Update's rotation post-multiply.
// An HRawAnimClass takes BFME2's raw-only overload (FUN_00563a80, reached from FUN_005a4820 when the animation's class id is 0):
// rounded integer frame, no interpolation, stored quaternion not renormalised; compressed animations take the generic path.
// Not ported: captured bones (turrets). BFME2 stores them as a sorted array of 36 byte records, not ZH's per-pivot
// CapTransform; that belongs to the bone/turret work (spec checklist step 17).
//
// A pose is evaluated into an HTreePose so one shared HTreeClass can serve any number of animated instances; the
// in-place Base_Update / Anim_Update / Blend_Update (ZH signatures) are wrappers that evaluate into the pivots.

#pragma once

#include "Libraries/WWVegas/WWMath/matrix3d.h"
#include "Libraries/WWVegas/WWMath/quat.h"

#include <cstdint>
#include <string>
#include <vector>

class ChunkLoadClass;

class HAnimClass;
class HRawAnimClass;

struct PivotClass
{
	std::string Name;
	int Index = 0;
	int ParentIdx = -1;        // ZH stores a Parent pointer
	Matrix3D BaseTransform;    // relative to parent, from the file
	Quaternion BaseQuat;       // the same transform as BFME2 keeps it (pivot +0x14): the file's quaternion, as written, and
	Vector3 BaseTrans;         // its translation (+0x24); pose evaluation composes these, not the matrix
	Matrix3D Transform;        // world, after Base_Update / Anim_Update
	bool IsVisible = true;     // result of the visibility channel
	float PivotFade = 1.0f;    // BFME per-pivot fade scalar (BFME1 pivot.h offset 0xAC)
};

// BFME2 stores a pivot's world transform as a quaternion (+0x30, x y z w) and a translation (+0x40); the 3x4 matrix is built from
// them only when something asks for it (HLodClass::Update_Sub_Object_Transforms, 0x59D640).
struct HTreePoseQT
{
	float Q[4];
	float T[3];
};

// Which of the retail arms evaluated the pose. BFME2 has separate compiled arms of Anim_Update / Blend_Update (0x562BB0 motion-channel
// arm lines 84-222, classic arm lines 276-397, raw 0x563A80, blend 0x5645B0) that differ only in the order floats are summed. The
// motion-channel and classic generic arms are ported exactly; raw and blend poses use the classic arm's order (stop S-028).
enum HTreePoseArm
{
	HTREE_POSE_ARM_BASE = 0,           // Base_Update 0x5628A0
	HTREE_POSE_ARM_CLASSIC = 1,        // exact order of the classic generic arm; also used for raw and blended poses
	HTREE_POSE_ARM_MOTION_CHANNEL = 2, // exact order of the motion-channel generic arm
};

// The result of evaluating a hierarchy: per pivot world transform, visibility and fade.
struct HTreePose
{
	std::vector<Matrix3D> Transform;
	std::vector<HTreePoseQT> World;
	std::vector<std::uint8_t> IsVisible;
	std::vector<float> PivotFade;
	HTreePoseArm Arm = HTREE_POSE_ARM_BASE;
	bool ExactOperationOrder = true; // false: evaluated with an arm whose float summation order differs from retail's (S-028)

	int Num_Pivots() const { return (int)Transform.size(); }
	void Resize(int n)
	{
		Transform.resize((size_t)n);
		World.resize((size_t)n);
		IsVisible.resize((size_t)n);
		PivotFade.resize((size_t)n);
	}
};

class HTreeClass
{
public:
	enum { OK = 0, LOAD_ERROR = 1 };

	// Called with the W3D_CHUNK_HIERARCHY chunk open (like ZH).
	int Load_W3D(ChunkLoadClass &cload, std::string *error);

	// In-place evaluators with ZH's signatures; they write the pivots' Transform / IsVisible / PivotFade.
	void Base_Update(const Matrix3D &root);
	void Anim_Update(const Matrix3D &root, const HAnimClass *motion, float frame);
	void Blend_Update(const Matrix3D &root, const HAnimClass *motion0, float frame0, const HAnimClass *motion1, float frame1, float percentage);

	// Evaluators that leave the tree untouched and write into `out`, so one HTreeClass serves many instances.
	// Frames the animations cannot define throw UndefinedFrameError (motchan.h); nothing is guessed.
	void Base_Pose(const Matrix3D &root, HTreePose &out) const;
	void Anim_Pose(const Matrix3D &root, const HAnimClass *motion, float frame, HTreePose &out) const;
	// The same with the arm chosen by the caller (tests). Anim_Pose picks MOTION_CHANNEL for HCompressedAnimClass::Uses_Motion_Channels(),
	// CLASSIC otherwise (raw animations take their own overload).
	void Anim_Pose(const Matrix3D &root, const HAnimClass *motion, float frame, HTreePose &out, HTreePoseArm arm) const;
	// percentage 0.0 = motion0, 1.0 = motion1.
	void Blend_Pose(const Matrix3D &root, const HAnimClass *motion0, float frame0, const HAnimClass *motion1, float frame1,
		float percentage, HTreePose &out) const;

	// ZH animobj.cpp:107-120 / BFME2 HTreeClass::Init_Default (RVA 0x001666D0): the one pivot hierarchy a model with no
	// hierarchy gets. Its pivot is named "RootTransform" and has the identity base transform.
	static HTreeClass Make_Default();

	float Get_Scale_Factor() const { return ScaleFactor; }
	void Set_Scale_Factor(float f) { ScaleFactor = f; }

	const std::string &Get_Name() const { return Name; }
	int Num_Pivots() const { return (int)Pivot.size(); }
	const Matrix3D &Get_Transform(int pivot) const { return Pivot[pivot].Transform; }
	const PivotClass &Get_Pivot(int pivot) const { return Pivot[pivot]; }
	int Get_Bone_Index(const std::string &name) const;

private:
	bool read_pivots(ChunkLoadClass &cload, bool pre30, std::string *error);
	void Anim_Pose_Raw(const Matrix3D &root, const HRawAnimClass &motion, float frame, HTreePose &out) const;

	std::string Name;
	float ScaleFactor = 1.0f;            // ZH HTreeClass::ScaleFactor (Scale()); 1.0 for every loaded tree
	std::uint32_t NumPivotsDeclared = 0; // header count, including the pre-3.0 added root
	std::vector<PivotClass> Pivot;
};

// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Port of ZH Libraries/Source/WWVegas/WW3D2/htree.cpp.

#include "Libraries/WWVegas/WW3D2/htree.h"
#include "Libraries/WWVegas/WW3D2/hanim.h"
#include "Libraries/WWVegas/WW3D2/hcanim.h"
#include "Libraries/WWVegas/WW3D2/hrawanim.h"
#include "Libraries/WWVegas/WW3D2/w3d_file.h"
#include "Libraries/WWVegas/WWLib/chunkio.h"
#include "Common/AsciiString.h"

#include <cmath>
#include <cstring>

namespace
{
std::string fixedName(const char *src, size_t len)
{
	size_t n = 0;
	while (n < len && src[n] != 0)
	{
		++n;
	}
	return std::string(src, n);
}
}

int HTreeClass::Load_W3D(ChunkLoadClass &cload, std::string *error)
{
	Name.clear();
	Pivot.clear();

	// Read the first chunk, it should be the hierarchy header
	if (!cload.Open_Chunk() || cload.Cur_Chunk_ID() != W3D_CHUNK_HIERARCHY_HEADER)
	{
		if (error)
		{
			*error = "hierarchy does not start with W3D_CHUNK_HIERARCHY_HEADER";
		}
		return LOAD_ERROR;
	}

	W3dHierarchyStruct header;
	if (cload.Read(&header, sizeof(W3dHierarchyStruct)) != sizeof(W3dHierarchyStruct))
	{
		if (error)
		{
			*error = "short hierarchy header";
		}
		return LOAD_ERROR;
	}
	cload.Close_Chunk();

	// Version < 3.0 files get a root node added for everything to attach to.
	bool pre30 = false;
	if (header.Version < W3D_MAKE_VERSION(3, 0))
	{
		header.NumPivots++;
		pre30 = true;
	}

	Name = fixedName(header.Name, W3D_NAME_LEN);
	// ZH trusts NumPivots. Here a hierarchy with no pivots has no root to attach anything to
	// (Base_Update and the animation code index pivot 0), so it is a load error, not an empty tree.
	if (header.NumPivots == 0)
	{
		if (error)
		{
			*error = "hierarchy " + Name + " declares zero pivots";
		}
		return LOAD_ERROR;
	}
	NumPivotsDeclared = header.NumPivots;
	// Pivot storage is sized in read_pivots, after the PIVOTS chunk length has vouched for the count.

	bool gotPivots = false;
	while (cload.Open_Chunk())
	{
		switch (cload.Cur_Chunk_ID())
		{
		case W3D_CHUNK_PIVOTS:
			if (!read_pivots(cload, pre30, error))
			{
				Pivot.clear();
				return LOAD_ERROR;
			}
			gotPivots = true;
			break;
		case W3D_CHUNK_PIVOT_FIXUPS:
			// The one deliberate ignore: w3d_file.h marks the fixups "only needed by the exporter" and no loader
			// reads them. 4,244 retail hierarchies carry one.
			break;
		default:
			// ZH silently skips every other chunk. Unknown data is an error here (PLAN rule 10).
			Pivot.clear();
			if (error)
			{
				*error = "hierarchy " + Name + ": unexpected chunk " + std::to_string(cload.Cur_Chunk_ID());
			}
			return LOAD_ERROR;
		}
		cload.Close_Chunk();
	}
	if (cload.Had_Error())
	{
		if (error)
		{
			*error = "malformed chunk inside hierarchy " + Name;
		}
		return LOAD_ERROR;
	}
	if (!gotPivots)
	{
		if (error)
		{
			*error = "hierarchy " + Name + " has no W3D_CHUNK_PIVOTS";
		}
		return LOAD_ERROR;
	}
	return OK;
}

bool HTreeClass::read_pivots(ChunkLoadClass &cload, bool pre30, std::string *error)
{
	// pre-3.0 files list one pivot fewer than the tree holds: ZH adds the root itself.
	std::uint32_t filePivots = pre30 ? NumPivotsDeclared - 1 : NumPivotsDeclared;
	if ((std::uint64_t)filePivots * sizeof(W3dPivotStruct) > cload.Cur_Chunk_Length())
	{
		if (error)
		{
			*error = "hierarchy " + Name + " declares " + std::to_string(filePivots) + " pivots but its PIVOTS chunk holds " +
				std::to_string(cload.Cur_Chunk_Length() / sizeof(W3dPivotStruct));
		}
		return false;
	}
	Pivot.assign(NumPivotsDeclared, PivotClass());

	int first_piv = 0;
	if (pre30)
	{
		Pivot[0].Index = 0;
		Pivot[0].ParentIdx = -1;
		Pivot[0].BaseTransform.Make_Identity();
		Pivot[0].Transform.Make_Identity();
		Pivot[0].Name = "RootTransform";
		first_piv++;
	}

	for (int pidx = first_piv; pidx < (int)Pivot.size(); pidx++)
	{
		W3dPivotStruct piv;
		if (cload.Read(&piv, sizeof(W3dPivotStruct)) != sizeof(W3dPivotStruct))
		{
			if (error)
			{
				*error = "short pivot array in hierarchy " + Name;
			}
			return false;
		}

		Pivot[pidx].Name = fixedName(piv.Name, W3D_NAME_LEN);
		Pivot[pidx].Index = pidx;

		Pivot[pidx].BaseQuat = Quaternion(piv.Rotation.Q[0], piv.Rotation.Q[1], piv.Rotation.Q[2], piv.Rotation.Q[3]);
		Pivot[pidx].BaseTrans = Vector3(piv.Translation.X, piv.Translation.Y, piv.Translation.Z);
		Pivot[pidx].BaseTransform.Make_Identity();
		Pivot[pidx].BaseTransform.Translate(Vector3(piv.Translation.X, piv.Translation.Y, piv.Translation.Z));
		Pivot[pidx].BaseTransform.postMul(Build_Matrix3D(piv.Rotation.Q[0], piv.Rotation.Q[1], piv.Rotation.Q[2], piv.Rotation.Q[3]));

		// ZH: pre-3.0 parent indices shift by one (the old root, 0xFFFFFFFF, wraps to the added root 0).
		// The parent is validated as an unsigned value: the earlier signed cast let 0xFFFFFFFE through.
		std::uint32_t parent = piv.ParentIdx;
		if (pre30)
		{
			parent += 1;
			if (parent == 0xFFFFFFFFu)
			{
				if (error)
				{
					*error = "hierarchy " + Name + " pivot " + std::to_string(pidx) + " has an invalid parent index";
				}
				return false;
			}
		}

		if (parent == 0xFFFFFFFFu)
		{
			Pivot[pidx].ParentIdx = -1;
			if (pidx != 0)
			{
				if (error)
				{
					*error = "hierarchy " + Name + " has a second root pivot";
				}
				return false;
			}
		}
		else
		{
			if (parent >= (std::uint32_t)pidx)
			{
				// Base_Update walks pivots in order; a parent must come first.
				if (error)
				{
					*error = "hierarchy " + Name + " pivot " + std::to_string(pidx) + " has parent index " +
						std::to_string(parent) + " (must be an earlier pivot or the root marker)";
				}
				return false;
			}
			Pivot[pidx].ParentIdx = (int)parent;
		}
	}

	Pivot[0].Transform.Make_Identity();
	return true;
}

namespace
{

// ---------------------------------------------------------------------------------------------------------------------------
// BFME2 composes pivots as quaternion + translation (pivot +0x30 / +0x40), every product written out component by component in
// the binary and never normalised. The groupings below are transcribed from the Ghidra pseudocode of the 1.06 game.dat:
//   Base_Update 0x5628A0                        -> base variant U
//   Anim_Update 0x562BB0 motion-channel arm      -> base variant A, apply variant A (lines 84-222)
//   Anim_Update 0x562BB0 classic arm             -> base variant B, apply variant B (lines 276-397)
// With P the parent's world quaternion (Px Py Pz Pw) and translation PT, b / bt the pivot's base quaternion and translation:
//   a = P (x) (bt, 0)            (a quaternion product, a.w = -(P.xyz . bt))
//   T = PT + (a (x) conj(P)).xyz
//   W = P (x) b
// Animation: with the new W, a = W (x) (t * ScaleFactor, 0); T += (a (x) conj(W)).xyz; then, when the animation has a rotation for
// the pivot, W = W (x) q. For a non-unit quaternion this scales a translation by |q|^2: the binary does exactly that.
// ---------------------------------------------------------------------------------------------------------------------------

enum BaseVariant { BASE_U, BASE_A, BASE_B };

void composeBase(BaseVariant v, const HTreePoseQT &P, const Quaternion &bq, const Vector3 &bt, HTreePoseQT &W)
{
	const float Px = P.Q[0], Py = P.Q[1], Pz = P.Q[2], Pw = P.Q[3];
	const float bx = bq.X, by = bq.Y, bz = bq.Z, bw = bq.W;
	const float tx = bt.X, ty = bt.Y, tz = bt.Z;
	float ax, ay, az, aw;
	float Tx, Ty, Tz, Wx, Wy, Wz, Ww;
	if (v == BASE_A)
	{
		ax = (Py * tz - Pz * ty) + tx * Pw;
		ay = Pw * ty - (Px * tz - tx * Pz);
		az = (Px * ty - Py * tx) + tz * Pw;
		aw = 0.0f - (tz * Pz + Px * tx + Py * ty);
		Tx = P.T[0] + ((Py * az - Pz * ay) + (Pw * ax - Px * aw));
		Ty = P.T[1] + ((Pw * ay - Py * aw) - (Px * az - Pz * ax));
		Tz = P.T[2] + ((Px * ay - Py * ax) + (Pw * az - Pz * aw));
		Wx = (bz * Py - Pz * by) + bx * Pw + bw * Px;
		Wy = (Pw * by + bw * Py) - (bz * Px - bx * Pz);
		Wz = (Px * by - Py * bx) + bw * Pz + bz * Pw;
		Ww = bw * Pw - (Px * bx + bz * Pz + Py * by);
	}
	else if (v == BASE_B)
	{
		ax = (tz * Py - Pz * ty) + Pw * tx;
		ay = Pw * ty - (tz * Px - Pz * tx);
		az = (Px * ty - tx * Py) + tz * Pw;
		aw = 0.0f - (ty * Py + tz * Pz + Px * tx);
		Tx = (az * Py - Pz * ay) + (Pw * ax - Px * aw) + P.T[0];
		Ty = P.T[1] + ((Pw * ay - aw * Py) - (Px * az - Pz * ax));
		Tz = P.T[2] + ((Px * ay - ax * Py) + (Pw * az - Pz * aw));
		Wx = (bz * Py - Pz * by) + bx * Pw + bw * Px;
		Wy = (bw * Py + Pw * by) - (Px * bz - bx * Pz);
		Wz = (by * Px - bx * Py) + Pz * bw + Pw * bz;
		Ww = Pw * bw - (by * Py + bx * Px + Pz * bz);
	}
	else // BASE_U
	{
		ax = (tz * Py - Pz * ty) + Pw * tx;
		ay = Pw * ty - (tz * Px - Pz * tx);
		az = (Px * ty - tx * Py) + tz * Pw;
		aw = 0.0f - (ty * Py + tz * Pz + Px * tx);
		Tx = P.T[0] + ((az * Py - Pz * ay) + (Pw * ax - Px * aw));
		Ty = P.T[1] + ((Pw * ay - aw * Py) - (Px * az - Pz * ax));
		Tz = P.T[2] + ((Px * ay - ax * Py) + (Pw * az - Pz * aw));
		Wx = (bz * Py - Pz * by) + Pw * bx + bw * Px;
		Wy = (bw * Py + Pw * by) - (Px * bz - bx * Pz);
		Wz = (by * Px - bx * Py) + Pz * bw + Pw * bz;
		Ww = Pw * bw - (Px * bx + by * Py + Pz * bz);
	}
	W.Q[0] = Wx; W.Q[1] = Wy; W.Q[2] = Wz; W.Q[3] = Ww;
	W.T[0] = Tx; W.T[1] = Ty; W.T[2] = Tz;
}

// The animated translation, in the pivot's own (already composed) frame. hasRotation says which compiled sub-arm of the retail
// function follows. Every grouping below is read from the compiled SSE code (0x562BB0), not from the decompile: the decompiler writes
// "T + a + b" where the code computes T + (a + b) (the two product pairs are added before the translation).
void applyTranslation(bool motionArm, bool hasRotation, HTreePoseQT &W, float tx, float ty, float tz)
{
	const float Wx = W.Q[0], Wy = W.Q[1], Wz = W.Q[2], Ww = W.Q[3];
	float ax, ay, az, aw;
	if (motionArm)
	{
		ax = (tz * Wy - Wz * ty) + Ww * tx;
		ay = Ww * ty - (tz * Wx - Wz * tx);
		az = (ty * Wx - tx * Wy) + Ww * tz;
		aw = 0.0f - (ty * Wy + tx * Wx + Wz * tz);
		W.T[0] = W.T[0] + ((az * Wy - Wz * ay) + (Ww * ax - aw * Wx));
		if (hasRotation)
		{
			W.T[1] = W.T[1] + ((Ww * ay - aw * Wy) - (az * Wx - Wz * ax));
			W.T[2] = W.T[2] + ((ay * Wx - ax * Wy) + (Ww * az - Wz * aw));
		}
		else
		{
			W.T[1] = ((Ww * ay - aw * Wy) - (az * Wx - Wz * ax)) + W.T[1];
			W.T[2] = (ay * Wx - ax * Wy) + (Ww * az - Wz * aw) + W.T[2];
		}
		return;
	}
	ax = (tz * Wy - ty * Wz) + tx * Ww;
	az = (ty * Wx - Wy * tx) + tz * Ww;
	if (hasRotation)
	{
		ay = ty * Ww - (tz * Wx - tx * Wz);
		aw = 0.0f - (tx * Wx + ty * Wy + tz * Wz);
		W.T[0] = (Wy * az - ay * Wz) + (ax * Ww - aw * Wx) + W.T[0];
	}
	else
	{
		ay = ty * Ww - (tz * Wx - Wz * tx);
		aw = 0.0f - (tx * Wx + tz * Wz + ty * Wy);
		W.T[0] = W.T[0] + ((az * Wy - ay * Wz) + (ax * Ww - aw * Wx));
	}
	W.T[1] = ((ay * Ww - aw * Wy) - (az * Wx - ax * Wz)) + W.T[1];
	W.T[2] = (ay * Wx - Wy * ax) + (az * Ww - aw * Wz) + W.T[2];
}

// W = W (x) q (Hamilton), in the term order of the compiled arm.
void applyRotation(bool motionArm, HTreePoseQT &W, const Quaternion &q)
{
	const float Wx = W.Q[0], Wy = W.Q[1], Wz = W.Q[2], Ww = W.Q[3];
	const float qx = q.X, qy = q.Y, qz = q.Z, qw = q.W;
	if (motionArm)
	{
		W.Q[0] = (qz * Wy - qy * Wz) + qx * Ww + qw * Wx;
		W.Q[1] = (qw * Wy + qy * Ww) - (qz * Wx - qx * Wz);
		W.Q[2] = (qy * Wx - qx * Wy) + qw * Wz + qz * Ww;
		W.Q[3] = qw * Ww - (qx * Wx + qz * Wz + qy * Wy);
	}
	else
	{
		W.Q[0] = (qz * Wy - qy * Wz) + qw * Wx + qx * Ww;
		W.Q[1] = (qw * Wy + qy * Ww) - (qz * Wx - qx * Wz);
		W.Q[2] = (qy * Wx - qx * Wy) + qw * Wz + qz * Ww;
		W.Q[3] = qw * Ww - (qz * Wz + qy * Wy + qx * Wx);
	}
}

// FUN_00b17b40 (RVA 0x717B40): the root matrix as a quaternion. The standard trace-based conversion (ZH quat.cpp Build_Quaternion).
Quaternion quaternionOf(const Matrix3D &m)
{
	const float trace = m.Row[1][1] + m.Row[0][0] + m.Row[2][2];
	float q[4];
	if (trace > 0.0f)
	{
		float s = std::sqrt(trace + 1.0f);
		q[3] = 0.5f * s;
		s = 0.5f / s;
		q[0] = (m.Row[2][1] - m.Row[1][2]) * s;
		q[1] = (m.Row[0][2] - m.Row[2][0]) * s;
		q[2] = (m.Row[1][0] - m.Row[0][1]) * s;
	}
	else
	{
		static const int nxt[3] = { 1, 2, 0 };
		int i = 0;
		if (m.Row[1][1] > m.Row[0][0]) i = 1;
		if (m.Row[2][2] > m.Row[i][i]) i = 2;
		const int j = nxt[i];
		const int k = nxt[j];
		float s = std::sqrt((m.Row[i][i] - (m.Row[j][j] + m.Row[k][k])) + 1.0f);
		q[i] = s * 0.5f;
		if (s != 0.0f) s = 0.5f / s;
		q[3] = (m.Row[k][j] - m.Row[j][k]) * s;
		q[j] = (m.Row[i][j] + m.Row[j][i]) * s;
		q[k] = (m.Row[i][k] + m.Row[k][i]) * s;
	}
	return Quaternion(q[0], q[1], q[2], q[3]);
}

// HLodClass::Update_Sub_Object_Transforms 0x59D640: the 3x4 matrix of a pivot from its quaternion and translation, unnormalised.
Matrix3D matrixOf(const HTreePoseQT &w)
{
	const float x = w.Q[0], y = w.Q[1], z = w.Q[2], qw = w.Q[3];
	const float two = 2.0f;
	const float xx2 = x * x * two;
	const float xz2 = x * z * two;
	const float zz2 = z * z * two;
	const float yx2 = y * x * two;
	const float wz2 = qw * z * two;
	const float yw2 = y * qw * two;
	const float xw2 = x * qw * two;
	const float yz2 = y * z * two;
	const float one_yy = 1.0f - y * y * two;
	Matrix3D m;
	m.Row[0][0] = one_yy - zz2;
	m.Row[0][1] = yx2 - wz2;
	m.Row[0][2] = yw2 + xz2;
	m.Row[0][3] = w.T[0];
	m.Row[1][0] = wz2 + yx2;
	m.Row[1][1] = (1.0f - zz2) - xx2;
	m.Row[1][2] = yz2 - xw2;
	m.Row[1][3] = w.T[1];
	m.Row[2][0] = xz2 - yw2;
	m.Row[2][1] = yz2 + xw2;
	m.Row[2][2] = one_yy - xx2;
	m.Row[2][3] = w.T[2];
	return m;
}

void setRoot(const Matrix3D &root, HTreePose &out)
{
	Quaternion q = quaternionOf(root);
	out.World[0].Q[0] = q.X; out.World[0].Q[1] = q.Y; out.World[0].Q[2] = q.Z; out.World[0].Q[3] = q.W;
	out.World[0].T[0] = root.Row[0][3];
	out.World[0].T[1] = root.Row[1][3];
	out.World[0].T[2] = root.Row[2][3];
	out.IsVisible[0] = 1;
	out.PivotFade[0] = 1.0f;
}

} // namespace

void HTreeClass::Base_Pose(const Matrix3D &root, HTreePose &out) const
{
	out.Resize((int)Pivot.size());
	out.Arm = HTREE_POSE_ARM_BASE;
	out.ExactOperationOrder = true;
	if (Pivot.empty())
	{
		return;
	}
	setRoot(root, out);
	for (int i = 1; i < (int)Pivot.size(); i++)
	{
		const PivotClass &pivot = Pivot[i];
		composeBase(BASE_U, out.World[(size_t)pivot.ParentIdx], pivot.BaseQuat, pivot.BaseTrans, out.World[(size_t)i]);
		out.IsVisible[i] = 1;
		out.PivotFade[i] = 1.0f;
	}
	for (int i = 0; i < (int)Pivot.size(); i++) out.Transform[(size_t)i] = matrixOf(out.World[(size_t)i]);
}

void HTreeClass::Anim_Pose(const Matrix3D &root, const HAnimClass *motion, float frame, HTreePose &out) const
{
	// BFME2 FUN_005a4820 sends an animation of class id 0 (HRawAnimClass) to FUN_00563a80, the raw-only overload (ZH "Customized
	// version of the above which excludes interpolation, for use by Generals"); a compressed motion-channel animation takes the
	// first arm of FUN_00562bb0 and every other animation its second arm.
	if (const HRawAnimClass *raw = dynamic_cast<const HRawAnimClass *>(motion))
	{
		Anim_Pose_Raw(root, *raw, frame, out);
		return;
	}
	const HCompressedAnimClass *comp = dynamic_cast<const HCompressedAnimClass *>(motion);
	Anim_Pose(root, motion, frame, out, comp && comp->Uses_Motion_Channels() ? HTREE_POSE_ARM_MOTION_CHANNEL : HTREE_POSE_ARM_CLASSIC);
}

// BFME2 FUN_00562bb0 (both generic arms) / donor htree.cpp:605-651; spec 4.1 (WW/htree.cpp:562-608).
void HTreeClass::Anim_Pose(const Matrix3D &root, const HAnimClass *motion, float frame, HTreePose &out, HTreePoseArm arm) const
{
	out.Resize((int)Pivot.size());
	out.Arm = arm;
	out.ExactOperationOrder = true;
	if (Pivot.empty())
	{
		return;
	}
	const bool motionArm = arm == HTREE_POSE_ARM_MOTION_CHANNEL;
	setRoot(root, out);
	const int num_anim_pivots = motion->Get_Num_Pivots();

	for (int piv_idx = 1; piv_idx < (int)Pivot.size(); piv_idx++)
	{
		const PivotClass &pivot = Pivot[piv_idx];
		HTreePoseQT &W = out.World[(size_t)piv_idx];
		composeBase(motionArm ? BASE_A : BASE_B, out.World[(size_t)pivot.ParentIdx], pivot.BaseQuat, pivot.BaseTrans, W);
		out.IsVisible[piv_idx] = 1;
		out.PivotFade[piv_idx] = 1.0f;

		// Don't update this pivot if the animation has no data for it
		if (piv_idx < num_anim_pivots)
		{
			Vector3 trans;
			motion->Get_Translation(trans, piv_idx, frame);
			if (motionArm || ScaleFactor != 1.0f) // the motion-channel arm multiplies unconditionally; same value for a factor of 1
			{
				trans = Vector3(trans.X * ScaleFactor, trans.Y * ScaleFactor, trans.Z * ScaleFactor);
			}
			// BFME: the rotation is applied only when the animation has one for this pivot
			Quaternion q;
			const bool hasRotation = motion->Get_Orientation(q, piv_idx, frame);
			applyTranslation(motionArm, hasRotation, W, trans.X, trans.Y, trans.Z);
			if (hasRotation)
			{
				applyRotation(motionArm, W, q);
			}
			out.IsVisible[piv_idx] = motion->Get_Visibility(piv_idx, frame) ? 1 : 0;
			out.PivotFade[piv_idx] = motion->Get_Fade(piv_idx, frame);
		}
	}
	for (int i = 0; i < (int)Pivot.size(); i++) out.Transform[(size_t)i] = matrixOf(out.World[(size_t)i]);
}

// BFME2 FUN_00563a80 (target) and Open-BFME-1 htree.cpp:656-742 (donor): no interpolation. The frame is rounded to the nearest
// integer (the binary's ROUND, the FPU's default rounding) and a frame at or past the end becomes 0; each channel is read at that
// frame (outside its range: 0, identity quaternion, fade 1.0, visibility = its default bit); the rotation is the stored quaternion,
// not renormalised. Composed with the classic arm's operation order (the raw arm's own summation order is not transcribed: S-028).
void HTreeClass::Anim_Pose_Raw(const Matrix3D &root, const HRawAnimClass &motion, float frame, HTreePose &out) const
{
	out.Resize((int)Pivot.size());
	out.Arm = HTREE_POSE_ARM_CLASSIC;
	out.ExactOperationOrder = false;
	if (Pivot.empty())
	{
		return;
	}
	setRoot(root, out);

	int iframe = (int)std::lrint(frame);
	if (iframe >= motion.Get_Num_Frames())
	{
		iframe = 0;
	}
	const int num_anim_pivots = motion.Get_Num_Pivots();

	for (int piv_idx = 1; piv_idx < (int)Pivot.size(); piv_idx++)
	{
		const PivotClass &pivot = Pivot[piv_idx];
		HTreePoseQT &W = out.World[(size_t)piv_idx];
		composeBase(BASE_B, out.World[(size_t)pivot.ParentIdx], pivot.BaseQuat, pivot.BaseTrans, W);
		out.IsVisible[piv_idx] = 1;
		out.PivotFade[piv_idx] = 1.0f;

		if (piv_idx < num_anim_pivots)
		{
			const NodeMotionStruct &nm = motion.Get_Node_Motion(piv_idx);
			float trans[3] = { 0.0f, 0.0f, 0.0f };
			if (nm.X) nm.X->Get_Vector(iframe, &trans[0]);
			if (nm.Y) nm.Y->Get_Vector(iframe, &trans[1]);
			if (nm.Z) nm.Z->Get_Vector(iframe, &trans[2]);
			if (ScaleFactor != 1.0f)
			{
				trans[0] *= ScaleFactor;
				trans[1] *= ScaleFactor;
				trans[2] *= ScaleFactor;
			}
			const bool hasRotation = nm.Q != nullptr;
			applyTranslation(false, hasRotation, W, trans[0], trans[1], trans[2]);
			if (hasRotation)
			{
				Quaternion q;
				nm.Q->Get_Vector_As_Quat(iframe, q);
				applyRotation(false, W, q);
			}
			float fade = 1.0f;
			if (nm.Fade) nm.Fade->Get_Vector(iframe, &fade);
			out.PivotFade[piv_idx] = fade;
			out.IsVisible[piv_idx] = (nm.Vis == nullptr || nm.Vis->Get_Bit(iframe) == 1) ? 1 : 0;
		}
	}
	for (int i = 0; i < (int)Pivot.size(); i++) out.Transform[(size_t)i] = matrixOf(out.World[(size_t)i]);
}

// BFME2 FUN_005645b0 (else-arm) / donor htree.cpp:801-873. Differences from the donor read off the binary: the translation lerp is
// (t1 - t0) * pct + t0 and the rotations blend with BFME2_Nlerp (RVA 0x00717550), not Fast_Slerp. BFME2's first arm (animation class
// id 1 with a non-null motion channel array) reads the same values through the 0x284 channel rows; HCompressedAnimClass already
// hides that behind the HAnimClass getters. The composition uses the classic arm's order (S-028).
void HTreeClass::Blend_Pose(const Matrix3D &root, const HAnimClass *motion0, float frame0, const HAnimClass *motion1, float frame1,
	float percentage, HTreePose &out) const
{
	out.Resize((int)Pivot.size());
	out.Arm = HTREE_POSE_ARM_CLASSIC;
	out.ExactOperationOrder = false;
	if (Pivot.empty())
	{
		return;
	}
	setRoot(root, out);

	const int n0 = motion0->Get_Num_Pivots();
	const int n1 = motion1->Get_Num_Pivots();
	const int num_anim_pivots = n0 < n1 ? n0 : n1;

	for (int piv_idx = 1; piv_idx < (int)Pivot.size(); piv_idx++)
	{
		const PivotClass &pivot = Pivot[piv_idx];
		HTreePoseQT &W = out.World[(size_t)piv_idx];
		composeBase(BASE_B, out.World[(size_t)pivot.ParentIdx], pivot.BaseQuat, pivot.BaseTrans, W);
		out.IsVisible[piv_idx] = 1;
		out.PivotFade[piv_idx] = 1.0f;

		if (piv_idx < num_anim_pivots)
		{
			Vector3 t0, t1;
			motion0->Get_Translation(t0, piv_idx, frame0);
			motion1->Get_Translation(t1, piv_idx, frame1);
			Vector3 lerped((t1.X - t0.X) * percentage + t0.X, (t1.Y - t0.Y) * percentage + t0.Y, (t1.Z - t0.Z) * percentage + t0.Z);
			if (ScaleFactor != 1.0f)
			{
				lerped = Vector3(lerped.X * ScaleFactor, lerped.Y * ScaleFactor, lerped.Z * ScaleFactor);
			}

			// An animation without a rotation for the pivot contributes the identity; with neither, no rotation at all.
			Quaternion q0, q1;
			bool got0 = motion0->Get_Orientation(q0, piv_idx, frame0);
			bool got1 = motion1->Get_Orientation(q1, piv_idx, frame1);
			const bool hasRotation = got0 || got1;
			applyTranslation(false, hasRotation, W, lerped.X, lerped.Y, lerped.Z);
			if (hasRotation)
			{
				if (!got0) q0.Make_Identity();
				if (!got1) q1.Make_Identity();
				Quaternion q;
				BFME2_Nlerp(q, q0, q1, percentage);
				applyRotation(false, W, q);
			}

			out.IsVisible[piv_idx] = (motion0->Get_Visibility(piv_idx, frame0) || motion1->Get_Visibility(piv_idx, frame1)) ? 1 : 0;

			float fade1 = motion1->Get_Fade(piv_idx, frame1);
			float fade0 = motion0->Get_Fade(piv_idx, frame0);
			out.PivotFade[piv_idx] = (fade1 - fade0) * percentage + fade0;
		}
	}
	for (int i = 0; i < (int)Pivot.size(); i++) out.Transform[(size_t)i] = matrixOf(out.World[(size_t)i]);
}

namespace
{
void storePose(std::vector<PivotClass> &pivots, const HTreePose &pose)
{
	for (size_t i = 0; i < pivots.size(); ++i)
	{
		pivots[i].Transform = pose.Transform[i];
		pivots[i].IsVisible = pose.IsVisible[i] != 0;
		pivots[i].PivotFade = pose.PivotFade[i];
	}
}
} // namespace

void HTreeClass::Base_Update(const Matrix3D &root)
{
	HTreePose pose;
	Base_Pose(root, pose);
	storePose(Pivot, pose);
}

void HTreeClass::Anim_Update(const Matrix3D &root, const HAnimClass *motion, float frame)
{
	HTreePose pose;
	Anim_Pose(root, motion, frame, pose);
	storePose(Pivot, pose);
}

void HTreeClass::Blend_Update(const Matrix3D &root, const HAnimClass *motion0, float frame0, const HAnimClass *motion1, float frame1, float percentage)
{
	HTreePose pose;
	Blend_Pose(root, motion0, frame0, motion1, frame1, percentage, pose);
	storePose(Pivot, pose);
}

HTreeClass HTreeClass::Make_Default()
{
	HTreeClass tree;
	tree.Name = "";
	tree.NumPivotsDeclared = 1;
	tree.Pivot.assign(1, PivotClass());
	tree.Pivot[0].Name = "RootTransform";
	tree.Pivot[0].Index = 0;
	tree.Pivot[0].ParentIdx = -1;
	return tree;
}

int HTreeClass::Get_Bone_Index(const std::string &name) const
{
	for (int i = 0; i < (int)Pivot.size(); ++i)
	{
		if (AsciiStringUtil::compareNoCase(Pivot[i].Name, name) == 0)
		{
			return i;
		}
	}
	return 0; // ZH returns 0 (the root) when the bone is not found
}

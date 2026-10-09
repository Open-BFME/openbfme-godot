// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Port of ZH Libraries/Source/WWVegas/WW3D2/hrawanim.cpp (Load_W3D, add_channel, add_bit_channel,
// Get_Translation, Get_Orientation, Get_Visibility) and BFME1 hrawanim.cpp _bfme_hanim_fade.

#include "Libraries/WWVegas/WW3D2/hrawanim.h"
#include "Libraries/WWVegas/WW3D2/w3d_file.h"
#include "Libraries/WWVegas/WWLib/chunkio.h"

#include <algorithm>
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

int setError(std::string *error, const std::string &text)
{
	if (error)
	{
		*error = text;
	}
	return HRawAnimClass::LOAD_ERROR;
}

// WWMath::Float_To_Long(frame - 0.499999f): round to nearest, which is floor(frame) for every
// frame the engine passes.
int frame0Of(float frame)
{
	return (int)std::lrint(frame - 0.499999f);
}

} // namespace

// ZH hrawanim.cpp HRawAnimClass::Load_W3D. `numNodes` replaces the Get_HTree(HierarchyName)->Num_Pivots() lookup.
int HRawAnimClass::Load_W3D(ChunkLoadClass &cload, int numNodes, std::string *error)
{
	Name.clear();
	NodeMotion.clear();
	NumNodes = 0;
	DroppedChannels = 0;
	MaxPivotReferenced = -1;

	if (!cload.Open_Chunk())
	{
		return setError(error, "empty W3D_CHUNK_ANIMATION");
	}
	if (cload.Cur_Chunk_ID() != W3D_CHUNK_ANIMATION_HEADER)
	{
		return setError(error, "animation does not start with W3D_CHUNK_ANIMATION_HEADER");
	}
	W3dAnimHeaderStruct aheader;
	if (cload.Read(&aheader, sizeof(W3dAnimHeaderStruct)) != sizeof(W3dAnimHeaderStruct))
	{
		return setError(error, "short W3D_CHUNK_ANIMATION_HEADER");
	}
	cload.Close_Chunk();

	// In version 3.0 onward all htrees use bone 0 as the root node; older files shift by one.
	bool pre30 = aheader.Version < W3D_MAKE_VERSION(3, 0);

	Version = aheader.Version;
	ShortName = fixedName(aheader.Name, W3D_NAME_LEN);
	HierarchyName = fixedName(aheader.HierarchyName, W3D_NAME_LEN);
	Name = HierarchyName + "." + ShortName;
	NumFrames = aheader.NumFrames;
	FrameRate = (float)aheader.FrameRate;
	if (NumFrames == 0 || aheader.FrameRate == 0)
	{
		return setError(error, "animation " + Name + " has " + std::to_string(NumFrames) + " frames at frame rate " +
			std::to_string(aheader.FrameRate));
	}

	std::vector<std::unique_ptr<MotionChannelClass>> channels;
	std::vector<std::unique_ptr<BitChannelClass>> bitChannels;
	while (cload.Open_Chunk())
	{
		switch (cload.Cur_Chunk_ID())
		{
		case W3D_CHUNK_ANIMATION_CHANNEL:
		{
			std::unique_ptr<MotionChannelClass> chan(new MotionChannelClass);
			std::string chanError;
			if (!chan->Load_W3D(cload, &chanError))
			{
				return setError(error, "animation " + Name + ": " + chanError);
			}
			if (pre30)
			{
				chan->Set_Pivot(chan->Get_Pivot() + 1);
			}
			channels.push_back(std::move(chan));
			break;
		}
		case W3D_CHUNK_BIT_CHANNEL:
		{
			std::unique_ptr<BitChannelClass> chan(new BitChannelClass);
			std::string chanError;
			if (!chan->Load_W3D(cload, &chanError))
			{
				return setError(error, "animation " + Name + ": " + chanError);
			}
			if (pre30)
			{
				chan->Set_Pivot(chan->Get_Pivot() + 1);
			}
			bitChannels.push_back(std::move(chan));
			break;
		}
		default:
			return setError(error, "animation " + Name + ": unexpected chunk " + std::to_string(cload.Cur_Chunk_ID()));
		}
		cload.Close_Chunk();
	}
	if (cload.Had_Error())
	{
		return setError(error, "animation " + Name + ": malformed chunk");
	}

	for (const auto &c : channels)
	{
		MaxPivotReferenced = std::max(MaxPivotReferenced, c->Get_Pivot());
	}
	for (const auto &c : bitChannels)
	{
		MaxPivotReferenced = std::max(MaxPivotReferenced, c->Get_Pivot());
	}
	NumNodes = numNodes >= 0 ? numNodes : MaxPivotReferenced + 1;
	NodeMotion.resize((size_t)NumNodes);

	// A second channel of the same kind for one pivot would silently replace the first in ZH.
	auto put = [&](auto &slot, auto &chan, const char *what, int idx) -> bool {
		if (slot)
		{
			setError(error, "animation " + Name + ": pivot " + std::to_string(idx) + " has two " + what + " channels");
			return false;
		}
		slot = std::move(chan);
		return true;
	};
	for (auto &chan : channels)
	{
		int idx = chan->Get_Pivot();
		// (gth) a channel naming a pivot outside the hierarchy is thrown away ("please re-export")
		if (idx >= NumNodes)
		{
			DroppedChannels++;
			continue;
		}
		NodeMotionStruct &node = NodeMotion[idx];
		bool ok = true;
		switch (chan->Get_Type())
		{
		case ANIM_CHANNEL_X: ok = put(node.X, chan, "X", idx); break;
		case ANIM_CHANNEL_Y: ok = put(node.Y, chan, "Y", idx); break;
		case ANIM_CHANNEL_Z: ok = put(node.Z, chan, "Z", idx); break;
		case ANIM_CHANNEL_XR: ok = put(node.XR, chan, "XR", idx); break;
		case ANIM_CHANNEL_YR: ok = put(node.YR, chan, "YR", idx); break;
		case ANIM_CHANNEL_ZR: ok = put(node.ZR, chan, "ZR", idx); break;
		case ANIM_CHANNEL_Q: ok = put(node.Q, chan, "Q", idx); break;
		case ANIM_CHANNEL_FADE: ok = put(node.Fade, chan, "fade", idx); break;
		}
		if (!ok)
		{
			NodeMotion.clear();
			return LOAD_ERROR;
		}
	}
	for (auto &chan : bitChannels)
	{
		int idx = chan->Get_Pivot();
		if (idx >= NumNodes)
		{
			DroppedChannels++;
			continue;
		}
		if (!put(NodeMotion[idx].Vis, chan, "visibility", idx))
		{
			NodeMotion.clear();
			return LOAD_ERROR;
		}
	}
	return OK;
}

// ZH hrawanim.cpp HRawAnimClass::Get_Translation
void HRawAnimClass::Get_Translation(Vector3 &trans, int pividx, float frame) const
{
	const NodeMotionStruct *motion = &NodeMotion[pividx];

	if ((motion->X == nullptr) && (motion->Y == nullptr) && (motion->Z == nullptr))
	{
		trans = Vector3(0.0f, 0.0f, 0.0f);
		return;
	}

	int frame0 = frame0Of(frame);
	int frame1 = frame0 + 1;
	float ratio = frame - (float)frame0;
	if (frame1 >= (int)NumFrames)
	{
		frame1 = 0;
	}

	float trans0[3] = { 0.0f, 0.0f, 0.0f };
	if (motion->X) motion->X->Get_Vector(frame0, &trans0[0]);
	if (motion->Y) motion->Y->Get_Vector(frame0, &trans0[1]);
	if (motion->Z) motion->Z->Get_Vector(frame0, &trans0[2]);

	if (ratio == 0.0f)
	{
		trans = Vector3(trans0[0], trans0[1], trans0[2]);
		return;
	}

	float trans1[3] = { 0.0f, 0.0f, 0.0f };
	if (motion->X) motion->X->Get_Vector(frame1, &trans1[0]);
	if (motion->Y) motion->Y->Get_Vector(frame1, &trans1[1]);
	if (motion->Z) motion->Z->Get_Vector(frame1, &trans1[2]);

	trans = Vector3(trans0[0] + (trans1[0] - trans0[0]) * ratio,
		trans0[1] + (trans1[1] - trans0[1]) * ratio,
		trans0[2] + (trans1[2] - trans0[2]) * ratio);
}

// BFME2 hrawanim.cpp HRawAnimClass::Get_Orientation (retail 0x0018DD60): ZH logic, blend by BFME2_Nlerp.
bool HRawAnimClass::Get_Orientation(Quaternion &q, int pividx, float frame) const
{
	int frame0 = frame0Of(frame);
	int frame1 = frame0 + 1;
	float ratio = frame - (float)frame0;
	if (frame1 >= (int)NumFrames)
	{
		frame1 = 0;
	}

	Quaternion q0, q1;
	const MotionChannelClass *mc = NodeMotion[pividx].Q.get();
	if (mc != nullptr)
	{
		mc->Get_Vector_As_Quat(frame0, q0);
		mc->Get_Vector_As_Quat(frame1, q1);
	}
	else
	{
		q0.Make_Identity();
		q1.Make_Identity();
	}

	if (ratio == 0.0f)
	{
		q = q0;
	}
	else if (ratio == 1.0f)
	{
		q = q1;
	}
	else
	{
		BFME2_Nlerp(q, q0, q1, ratio);
	}
	return true;
}

bool HRawAnimClass::Get_Visibility(int pividx, float frame) const
{
	if (NodeMotion[pividx].Vis != nullptr)
	{
		return NodeMotion[pividx].Vis->Get_Bit((int)frame) == 1;
	}
	// default to always visible
	return true;
}

// BFME1 hrawanim.cpp HRawAnimClass::_bfme_hanim_fade
float HRawAnimClass::Get_Fade(int pividx, float frame) const
{
	const NodeMotionStruct *node = &NodeMotion[pividx];
	if (node->Fade == nullptr)
	{
		return 1.0f;
	}

	int frame0 = frame0Of(frame);
	int frame1 = frame0 + 1;
	float ratio = frame - (float)frame0;
	if (frame1 >= (int)NumFrames)
	{
		frame1 = 0;
	}

	float value0 = 1.0f;
	node->Fade->Get_Vector(frame0, &value0);
	if (ratio == 0.0f)
	{
		return value0;
	}
	float value1 = 1.0f;
	node->Fade->Get_Vector(frame1, &value1);
	return value0 + (value1 - value0) * ratio;
}

bool HRawAnimClass::Is_Node_Motion_Present(int pividx) const
{
	const NodeMotionStruct &m = NodeMotion[pividx];
	return m.X || m.Y || m.Z || m.XR || m.YR || m.ZR || m.Q || m.Fade || m.Vis;
}

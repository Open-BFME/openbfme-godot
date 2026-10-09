// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Port of ZH Libraries/Source/WWVegas/WW3D2/hcanim.cpp (Load_W3D, add_channel, add_bit_channel,
// Get_Translation, Get_Orientation, Get_Visibility), BFME1 hcanim.cpp (fade slot) and BFME2
// HCompressedAnimGetters.cpp (the second, motion channel node array).

#include "Libraries/WWVegas/WW3D2/hcanim.h"
#include "Libraries/WWVegas/WW3D2/w3d_file.h"
#include "Libraries/WWVegas/WWLib/chunkio.h"

#include <algorithm>

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
	return HCompressedAnimClass::LOAD_ERROR;
}

// Slot of a channel type (hcanim.cpp add_channel: X, Y, Z, Q and, in BFME, FADE). Types XR/YR/ZR
// have no compressed slot.
int slotOf(int type)
{
	switch (type)
	{
	case ANIM_CHANNEL_X: return CHANNEL_SLOT_X;
	case ANIM_CHANNEL_Y: return CHANNEL_SLOT_Y;
	case ANIM_CHANNEL_Z: return CHANNEL_SLOT_Z;
	case ANIM_CHANNEL_Q: return CHANNEL_SLOT_Q;
	case ANIM_CHANNEL_FADE: return CHANNEL_SLOT_FADE;
	}
	return -1;
}

} // namespace

// ZH hcanim.cpp HCompressedAnimClass::Load_W3D.
int HCompressedAnimClass::Load_W3D(ChunkLoadClass &cload, int numNodes, std::string *error)
{
	Name.clear();
	NodeMotion.clear();
	VectorMotion.clear();
	NumNodes = 0;
	DroppedChannels = 0;
	MaxPivotReferenced = -1;
	MotionChannelForm = false;

	if (!cload.Open_Chunk())
	{
		return setError(error, "empty W3D_CHUNK_COMPRESSED_ANIMATION");
	}
	if (cload.Cur_Chunk_ID() != W3D_CHUNK_COMPRESSED_ANIMATION_HEADER)
	{
		return setError(error, "compressed animation does not start with W3D_CHUNK_COMPRESSED_ANIMATION_HEADER");
	}
	W3dCompressedAnimHeaderStruct aheader;
	if (cload.Read(&aheader, sizeof(W3dCompressedAnimHeaderStruct)) != sizeof(W3dCompressedAnimHeaderStruct))
	{
		return setError(error, "short W3D_CHUNK_COMPRESSED_ANIMATION_HEADER");
	}
	cload.Close_Chunk();

	Version = aheader.Version;
	ShortName = fixedName(aheader.Name, W3D_NAME_LEN);
	HierarchyName = fixedName(aheader.HierarchyName, W3D_NAME_LEN);
	Name = HierarchyName + "." + ShortName;
	NumFrames = aheader.NumFrames;
	FrameRate = (float)aheader.FrameRate;
	Flavor = aheader.Flavor;

	if (Flavor != ANIM_FLAVOR_TIMECODED && Flavor != ANIM_FLAVOR_ADAPTIVE_DELTA)
	{
		return setError(error, "compressed animation " + Name + " has unknown flavor " + std::to_string(Flavor));
	}
	if (NumFrames == 0 || aheader.FrameRate == 0)
	{
		return setError(error, "compressed animation " + Name + " has " + std::to_string(NumFrames) + " frames at frame rate " +
			std::to_string(aheader.FrameRate));
	}
	// BFME2 Load_W3D (RVA 0x0018FEE8-0x0018FF01) compares the header version with 1 and with 0x10000 and refuses
	// anything else; 1 selects the classic node array, 0x10000 the motion channel one.
	if (Version != 1 && Version != 0x10000)
	{
		return setError(error, "compressed animation " + Name + " has version " + std::to_string(Version) + "; retail accepts only 1 and 0x10000");
	}
	MotionChannelForm = Version == 0x10000;
	if (MotionChannelForm && Flavor != ANIM_FLAVOR_TIMECODED)
	{
		return setError(error, "compressed animation " + Name + ": motion channel form (version " + std::to_string(Version) +
			") with flavor " + std::to_string(Flavor));
	}

	std::vector<std::unique_ptr<TimeCodedMotionChannelClass>> tcChannels;
	std::vector<std::unique_ptr<AdaptiveDeltaMotionChannelClass>> adChannels;
	std::vector<std::unique_ptr<BFME2MotionChannel>> motionChannels;
	std::vector<std::unique_ptr<TimeCodedBitChannelClass>> bitChannels;

	while (cload.Open_Chunk())
	{
		std::string chanError;
		switch (cload.Cur_Chunk_ID())
		{
		case W3D_CHUNK_COMPRESSED_ANIMATION_CHANNEL:
			if (MotionChannelForm)
			{
				return setError(error, "compressed animation " + Name + ": classic channel chunk in a motion channel form animation");
			}
			if (Flavor == ANIM_FLAVOR_TIMECODED)
			{
				std::unique_ptr<TimeCodedMotionChannelClass> chan(new TimeCodedMotionChannelClass);
				if (!chan->Load_W3D(cload, &chanError))
				{
					return setError(error, "compressed animation " + Name + ": " + chanError);
				}
				tcChannels.push_back(std::move(chan));
			}
			else
			{
				std::unique_ptr<AdaptiveDeltaMotionChannelClass> chan(new AdaptiveDeltaMotionChannelClass);
				if (!chan->Load_W3D(cload, &chanError))
				{
					return setError(error, "compressed animation " + Name + ": " + chanError);
				}
				adChannels.push_back(std::move(chan));
			}
			break;

		case W3D_CHUNK_COMPRESSED_ANIMATION_MOTION_CHANNEL:
		{
			if (!MotionChannelForm)
			{
				return setError(error, "compressed animation " + Name + ": motion channel chunk in a classic form animation");
			}
			std::unique_ptr<BFME2MotionChannel> chan(new BFME2MotionChannel);
			if (!chan->Load_W3D(cload, &chanError))
			{
				return setError(error, "compressed animation " + Name + ": " + chanError);
			}
			motionChannels.push_back(std::move(chan));
			break;
		}

		case W3D_CHUNK_COMPRESSED_BIT_CHANNEL:
		{
			std::unique_ptr<TimeCodedBitChannelClass> chan(new TimeCodedBitChannelClass);
			if (!chan->Load_W3D(cload, &chanError))
			{
				return setError(error, "compressed animation " + Name + ": " + chanError);
			}
			bitChannels.push_back(std::move(chan));
			break;
		}

		default:
			return setError(error, "compressed animation " + Name + ": unexpected chunk " + std::to_string(cload.Cur_Chunk_ID()));
		}
		cload.Close_Chunk();
	}
	if (cload.Had_Error())
	{
		return setError(error, "compressed animation " + Name + ": malformed chunk");
	}

	for (const auto &c : tcChannels) MaxPivotReferenced = std::max(MaxPivotReferenced, c->Get_Pivot());
	for (const auto &c : adChannels) MaxPivotReferenced = std::max(MaxPivotReferenced, c->Get_Pivot());
	for (const auto &c : motionChannels) MaxPivotReferenced = std::max(MaxPivotReferenced, c->Get_Pivot());
	for (const auto &c : bitChannels) MaxPivotReferenced = std::max(MaxPivotReferenced, c->Get_Pivot());
	NumNodes = numNodes >= 0 ? numNodes : MaxPivotReferenced + 1;

	if (MotionChannelForm)
	{
		VectorMotion.resize((size_t)NumNodes);
	}
	else
	{
		NodeMotion.resize((size_t)NumNodes);
		for (NodeCompressedMotionStruct &n : NodeMotion)
		{
			n.Flavor = Flavor;
		}
	}

	auto duplicate = [&](int idx, const char *what) {
		NodeMotion.clear();
		VectorMotion.clear();
		return setError(error, "compressed animation " + Name + ": pivot " + std::to_string(idx) + " has two " + what + " channels");
	};

	// add_channel / add_bit_channel. A channel naming a pivot outside the hierarchy is dropped (ZH, with a
	// "please re-export" warning); it is counted here. XR/YR/ZR channels have no slot and are refused.
	for (auto &chan : tcChannels)
	{
		int idx = chan->Get_Pivot();
		if (idx >= NumNodes) { DroppedChannels++; continue; }
		int slot = slotOf(chan->Get_Type());
		if (slot < 0) return setError(error, "compressed animation " + Name + ": channel type " + std::to_string(chan->Get_Type()) + " has no slot");
		if (NodeMotion[idx].tc[slot]) return duplicate(idx, "same-kind");
		NodeMotion[idx].tc[slot] = std::move(chan);
	}
	for (auto &chan : adChannels)
	{
		int idx = chan->Get_Pivot();
		if (idx >= NumNodes) { DroppedChannels++; continue; }
		int slot = slotOf(chan->Get_Type());
		if (slot < 0) return setError(error, "compressed animation " + Name + ": channel type " + std::to_string(chan->Get_Type()) + " has no slot");
		if (NodeMotion[idx].ad[slot]) return duplicate(idx, "same-kind");
		NodeMotion[idx].ad[slot] = std::move(chan);
	}
	for (auto &chan : motionChannels)
	{
		int idx = chan->Get_Pivot();
		if (idx >= NumNodes) { DroppedChannels++; continue; }
		int slot = slotOf(chan->Get_Type());
		if (slot < 0) return setError(error, "compressed animation " + Name + ": channel type " + std::to_string(chan->Get_Type()) + " has no slot");
		if (VectorMotion[idx].Channels[slot]) return duplicate(idx, "same-kind");
		VectorMotion[idx].Channels[slot] = std::move(chan);
	}
	for (auto &chan : bitChannels)
	{
		int idx = chan->Get_Pivot();
		if (idx >= NumNodes) { DroppedChannels++; continue; }
		auto &slot = MotionChannelForm ? VectorMotion[idx].Visibility : NodeMotion[idx].Vis;
		if (slot) return duplicate(idx, "visibility");
		slot = std::move(chan);
	}
	return OK;
}

// ZH hcanim.cpp HCompressedAnimClass::Get_Translation
void HCompressedAnimClass::Get_Translation(Vector3 &trans, int pividx, float frame) const
{
	float v[3] = { 0.0f, 0.0f, 0.0f };
	if (MotionChannelForm)
	{
		const BFME2CompressedMotionChannels &m = VectorMotion[pividx];
		for (int i = 0; i < 3; i++)
		{
			if (m.Channels[i]) m.Channels[i]->Get_Vector(frame, &v[i]);
		}
	}
	else if (NodeMotion[pividx].Flavor == ANIM_FLAVOR_TIMECODED)
	{
		const NodeCompressedMotionStruct &m = NodeMotion[pividx];
		for (int i = 0; i < 3; i++)
		{
			if (m.tc[i] && !m.tc[i]->Get_Vector(frame, &v[i]))
			{
				throw UndefinedFrameError("animation " + Name + ": frame " + std::to_string(frame) + " precedes the first key of a classic timecoded channel");
			}
		}
	}
	else
	{
		const NodeCompressedMotionStruct &m = NodeMotion[pividx];
		for (int i = 0; i < 3; i++)
		{
			if (m.ad[i]) m.ad[i]->Get_Vector(frame, &v[i]);
		}
	}
	trans = Vector3(v[0], v[1], v[2]);
}

// BFME2 HCompressedAnimClass::Get_Orientation (RVA 0x00190C60): returns false, leaving the quaternion alone, when the
// pivot has no rotation channel (0x00190C92, 0x00190EFC); Get_Transform then uses the identity. Here the quaternion is
// set to the identity first.
bool HCompressedAnimClass::Get_Orientation(Quaternion &q, int pividx, float frame) const
{
	q.Make_Identity();
	if (MotionChannelForm)
	{
		const auto &c = VectorMotion[pividx].Channels[CHANNEL_SLOT_Q];
		if (!c) return false;
		q = c->Get_QuatVector(frame);
	}
	else if (NodeMotion[pividx].Flavor == ANIM_FLAVOR_TIMECODED)
	{
		const auto &c = NodeMotion[pividx].tc[CHANNEL_SLOT_Q];
		if (!c) return false;
		if (!c->Get_QuatVector(frame, q))
		{
			throw UndefinedFrameError("animation " + Name + ": frame " + std::to_string(frame) + " precedes the first key of a classic timecoded channel");
		}
	}
	else
	{
		const auto &c = NodeMotion[pividx].ad[CHANNEL_SLOT_Q];
		if (!c) return false;
		q = c->Get_QuatVector(frame);
	}
	return true;
}

bool HCompressedAnimClass::Frame_Is_Defined(int pividx, float frame) const
{
	if (MotionChannelForm || NodeMotion[pividx].Flavor != ANIM_FLAVOR_TIMECODED)
	{
		return true;
	}
	for (const auto &c : NodeMotion[pividx].tc)
	{
		if (c && !c->Is_Defined_At(frame))
		{
			return false;
		}
	}
	return true;
}

bool HCompressedAnimClass::Get_Visibility(int pividx, float frame) const
{
	const TimeCodedBitChannelClass *vis = MotionChannelForm ? VectorMotion[pividx].Visibility.get() : NodeMotion[pividx].Vis.get();
	if (vis != nullptr)
	{
		return vis->Get_Bit((int)frame) == 1;
	}
	// default to always visible
	return true;
}

// BFME1 Rva0095B260HanimFade.cpp Get_Fade: 1.0 unless the pivot's fade slot holds a channel.
float HCompressedAnimClass::Get_Fade(int pividx, float frame) const
{
	float value = 1.0f;
	if (MotionChannelForm)
	{
		const auto &c = VectorMotion[pividx].Channels[CHANNEL_SLOT_FADE];
		if (c) c->Get_Vector(frame, &value);
	}
	else if (NodeMotion[pividx].Flavor == ANIM_FLAVOR_TIMECODED)
	{
		const auto &c = NodeMotion[pividx].tc[CHANNEL_SLOT_FADE];
		if (c && !c->Get_Vector(frame, &value))
		{
			throw UndefinedFrameError("animation " + Name + ": frame " + std::to_string(frame) + " precedes the first key of a classic timecoded fade channel");
		}
	}
	else
	{
		const auto &c = NodeMotion[pividx].ad[CHANNEL_SLOT_FADE];
		if (c) c->Get_Vector(frame, &value);
	}
	return value;
}

// ZH Is_Node_Motion_Present checks X, Y, Z, Q and Vis; BFME2's motion form also checks the fifth slot
// (HCompressedAnimGetters.cpp), the classic form does not.
bool HCompressedAnimClass::Is_Node_Motion_Present(int pividx) const
{
	if (MotionChannelForm)
	{
		const BFME2CompressedMotionChannels &m = VectorMotion[pividx];
		for (int i = 0; i < CHANNEL_SLOT_COUNT; ++i)
		{
			if (m.Channels[i]) return true;
		}
		return m.Visibility != nullptr;
	}
	const NodeCompressedMotionStruct &m = NodeMotion[pividx];
	for (int i = 0; i < CHANNEL_SLOT_Q + 1; ++i)
	{
		if (m.tc[i] || m.ad[i]) return true;
	}
	return m.Vis != nullptr;
}

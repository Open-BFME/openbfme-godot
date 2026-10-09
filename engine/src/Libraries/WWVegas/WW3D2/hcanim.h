// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Port of ZH Libraries/Source/WWVegas/WW3D2/hcanim.h / hcanim.cpp (HCompressedAnimClass,
// W3D_CHUNK_COMPRESSED_ANIMATION) with the BFME changes:
//  * BFME1 hcanim.cpp: a fifth channel slot per node, ANIM_CHANNEL_FADE (type 15), read by
//    Rva0095B260 Get_Fade (default 1.0).
//  * BFME2: a second per-node array of 24 byte rows (five polymorphic channels + visibility) filled
//    from W3D_CHUNK_COMPRESSED_ANIMATION_MOTION_CHANNEL (0x284); when it exists Get_Visibility and
//    the Has_* queries read it instead of the classic array (HCompressedAnimGetters.cpp).
//
// Which array an animation uses is decided by the loader at retail 0x005901B0, which is not
// decompiled. The corpus fixes the rule: header Version 0x1 is always classic (0x282 channels,
// flavor 0 or 1) and Version 0x10000 is always the motion channel form (0x284 channels, flavor 0);
// all 5,693 retail compressed animations follow it. Here a mismatch is a load error.

#pragma once

#include "Libraries/WWVegas/WW3D2/hanim.h"
#include "Libraries/WWVegas/WW3D2/motchan.h"
#include "Libraries/WWVegas/WW3D2/w3d_file.h"

#include <memory>
#include <vector>

class ChunkLoadClass;

// Channel slots, as the ZH NodeCompressedMotionStruct fields X Y Z Q plus the BFME fade.
enum
{
	CHANNEL_SLOT_X = 0,
	CHANNEL_SLOT_Y = 1,
	CHANNEL_SLOT_Z = 2,
	CHANNEL_SLOT_Q = 3,
	CHANNEL_SLOT_FADE = 4,
	CHANNEL_SLOT_COUNT = 5,
};

struct NodeCompressedMotionStruct
{
	int Flavor = ANIM_FLAVOR_TIMECODED;
	std::unique_ptr<TimeCodedMotionChannelClass> tc[CHANNEL_SLOT_COUNT];
	std::unique_ptr<AdaptiveDeltaMotionChannelClass> ad[CHANNEL_SLOT_COUNT];
	std::unique_ptr<TimeCodedBitChannelClass> Vis;
};

struct BFME2CompressedMotionChannels
{
	std::unique_ptr<BFME2MotionChannel> Channels[CHANNEL_SLOT_COUNT];
	std::unique_ptr<TimeCodedBitChannelClass> Visibility;
};

class HCompressedAnimClass : public HAnimClass
{
public:
	enum { OK, LOAD_ERROR };

	// Called with the W3D_CHUNK_COMPRESSED_ANIMATION chunk open. numNodes: see HRawAnimClass::Load_W3D.
	int Load_W3D(ChunkLoadClass &cload, int numNodes, std::string *error);

	const std::string &Get_Name() const override { return Name; }
	const std::string &Get_HName() const override { return HierarchyName; }
	int Get_Num_Frames() const override { return (int)NumFrames; }
	float Get_Frame_Rate() const override { return FrameRate; }
	int Get_Num_Pivots() const override { return NumNodes; }

	void Get_Translation(Vector3 &trans, int pividx, float frame) const override;
	bool Get_Orientation(Quaternion &q, int pividx, float frame) const override;
	bool Get_Visibility(int pividx, float frame) const override;
	float Get_Fade(int pividx, float frame) const override;
	bool Is_Node_Motion_Present(int pividx) const override;
	bool Frame_Is_Defined(int pividx, float frame) const override;

	const std::string &Get_Short_Name() const { return ShortName; }
	std::uint32_t Get_Version() const { return Version; }
	int Get_Flavor() const { return Flavor; }
	bool Uses_Motion_Channels() const { return !VectorMotion.empty() || MotionChannelForm; }
	int Get_Dropped_Channels() const { return DroppedChannels; }
	int Get_Max_Pivot_Referenced() const { return MaxPivotReferenced; }
	const NodeCompressedMotionStruct &Get_Node_Motion(int pividx) const { return NodeMotion[pividx]; }
	const BFME2CompressedMotionChannels &Get_Vector_Motion(int pividx) const { return VectorMotion[pividx]; }

private:
	std::string Name;
	std::string ShortName;
	std::string HierarchyName;
	std::uint32_t Version = 0;
	std::uint32_t NumFrames = 0;
	float FrameRate = 0.0f;
	int Flavor = 0;
	int NumNodes = 0;
	int DroppedChannels = 0;
	int MaxPivotReferenced = -1;
	bool MotionChannelForm = false;
	std::vector<NodeCompressedMotionStruct> NodeMotion;   // classic form
	std::vector<BFME2CompressedMotionChannels> VectorMotion; // motion channel form
};

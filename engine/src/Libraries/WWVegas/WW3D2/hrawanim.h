// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Port of ZH Libraries/Source/WWVegas/WW3D2/hrawanim.h / hrawanim.cpp (HRawAnimClass,
// W3D_CHUNK_ANIMATION), with the BFME fade channel (NodeMotionStruct::Fade, BFME2 hrawanim.cpp
// add_channel case 15, BFME1 _bfme_hanim_fade) and the BFME2 normalised-lerp orientation blend
// (BFME2 hrawanim.cpp Get_Orientation, retail 0x0018DD60).
//
// ZH reaches into the asset manager for the hierarchy while loading, to size the per-pivot array.
// Here the caller passes the pivot count (or -1 to size by the highest pivot a channel names);
// channels naming a pivot beyond it are dropped as ZH does, but counted so a test can see them.

#pragma once

#include "Libraries/WWVegas/WW3D2/hanim.h"
#include "Libraries/WWVegas/WW3D2/motchan.h"

#include <memory>
#include <vector>

class ChunkLoadClass;

struct NodeMotionStruct
{
	std::unique_ptr<MotionChannelClass> X, Y, Z, XR, YR, ZR, Q;
	std::unique_ptr<MotionChannelClass> Fade;
	std::unique_ptr<BitChannelClass> Vis;
};

class HRawAnimClass : public HAnimClass
{
public:
	enum { OK, LOAD_ERROR };

	// Called with the W3D_CHUNK_ANIMATION chunk open.
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
	bool Frame_Is_Defined(int, float) const override { return true; } // raw channels are the identity outside their range

	const std::string &Get_Short_Name() const { return ShortName; } // header Name, without the hierarchy prefix
	std::uint32_t Get_Version() const { return Version; }
	int Get_Dropped_Channels() const { return DroppedChannels; }
	const NodeMotionStruct &Get_Node_Motion(int pividx) const { return NodeMotion[pividx]; }
	int Get_Max_Pivot_Referenced() const { return MaxPivotReferenced; }

private:
	std::string Name;
	std::string ShortName;
	std::string HierarchyName;
	std::uint32_t Version = 0;
	std::uint32_t NumFrames = 0;
	float FrameRate = 0.0f;
	int NumNodes = 0;
	int DroppedChannels = 0;
	int MaxPivotReferenced = -1;
	std::vector<NodeMotionStruct> NodeMotion;
};

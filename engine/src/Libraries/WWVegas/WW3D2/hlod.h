// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Port of the loading half of ZH Libraries/Source/WWVegas/WW3D2/hlod.cpp (HLodDefClass).
// Lod[0] is the lowest level of detail, Lod[LodCount-1] the highest (ZH HLodClass).

#pragma once

#include <string>
#include <vector>

class ChunkLoadClass;

class HLodDefClass
{
public:
	struct SubObjectArrayClass
	{
		float MaxScreenSize = 0.0f;
		std::vector<std::string> ModelName; // "CONTAINER.MESH"
		std::vector<int> BoneIndex;

		bool Load_W3D(ChunkLoadClass &cload);
	};

	// Called with the W3D_CHUNK_HLOD chunk open.
	bool Load_W3D(ChunkLoadClass &cload, std::string *error);

	std::string Name;
	std::string HierarchyName;
	std::vector<SubObjectArrayClass> Lod;
	// LOD arrays beyond the header's LodCount. 38 retail HLODs carry a second array although LodCount is 1;
	// ZH reads exactly LodCount arrays and ignores the rest. Whether retail uses them is unknown (spec 2.1),
	// so they are kept here, visible, instead of being dropped.
	std::vector<SubObjectArrayClass> ExtraLod;
	SubObjectArrayClass Aggregates;
	SubObjectArrayClass Proxies;
};

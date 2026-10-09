// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// W3D_CHUNK_SHDMESH ("shader mesh", ZH w3d_file.h:2170-2237): the structure of the three retail files that
// have one (gbmtwalld, gbmtwalldramp, gbmtwalle). Neither the ZH source in the repository nor the BFME
// decompiles contain a loader for this chunk (there is no shdmesh.cpp in the ZH WW3D2 tree; the scaleable
// shader library it targets is the WWVegas/wwshade directory), so this reader only captures the layout
// that w3d_file.h documents and validates every array against the header counts. Whether retail draws
// these meshes is unknown (spec 2.1, [U]).
//
// OpenBFME addition: no ZH counterpart.

#pragma once

#include "Libraries/WWVegas/WW3D2/w3d_file.h"

#include <string>
#include <vector>

class ChunkLoadClass;

struct ShdSubMeshDef
{
	W3dShdSubMeshHeaderStruct Header = {};
	std::uint32_t ShaderClassId = 0;
	std::vector<std::uint8_t> ShaderDef; // body of W3D_CHUNK_SHDSUBMESH_SHADER_DEF, a WWShade chunk stream
	std::vector<W3dVectorStruct> Vertices;
	std::vector<W3dVectorStruct> Normals;
	std::vector<std::uint16_t> Triangles; // 3 vertex indices per triangle
	std::vector<std::uint32_t> ShadeIndices;
	std::vector<W3dTexCoordStruct> UV0;
	std::vector<W3dTexCoordStruct> UV1;
	std::vector<W3dVectorStruct> TangentBasisS;
	std::vector<W3dVectorStruct> TangentBasisT;
	std::vector<W3dVectorStruct> TangentBasisSxT;
	std::vector<std::uint32_t> VertexColors;
	std::vector<std::uint8_t> VertexInfluences;
};

class ShdMeshDefClass
{
public:
	// Called with the W3D_CHUNK_SHDMESH chunk open.
	bool Load_W3D(ChunkLoadClass &cload, std::string *error);

	std::string Name;
	std::string UserText;
	W3dShdMeshHeaderStruct Header = {};
	std::vector<ShdSubMeshDef> SubMeshes;
};

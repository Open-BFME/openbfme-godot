// OpenBFME unit tests: HTreeClass loading and bind pose on synthetic hierarchy chunks. GPL-3.0.
// Expected values are derived by hand from the W3D format (w3d_file.h) and ZH htree.cpp rules.

#include "doctest.h"
#include "W3dTestUtil.h"

#include "Libraries/WWVegas/WW3D2/htree.h"
#include "Libraries/WWVegas/WW3D2/w3d_file.h"
#include "Libraries/WWVegas/WWLib/chunkio.h"

#include <cmath>
#include <string>

using namespace w3dtest;

namespace
{

W3dPivotStruct makePivot(const char *name, std::uint32_t parent, float x, float y, float z)
{
	W3dPivotStruct p = {};
	setName(p.Name, W3D_NAME_LEN, name);
	p.ParentIdx = parent;
	p.Translation = { x, y, z };
	p.Rotation.Q[3] = 1.0f;
	return p;
}

// A W3D_CHUNK_HIERARCHY wrapper: header declaring `declared` pivots, then a PIVOTS chunk holding `pivots`.
std::vector<std::uint8_t> hierarchyBytes(std::uint32_t version, std::uint32_t declared, const std::vector<W3dPivotStruct> &pivots)
{
	ChunkWriter hier;
	W3dHierarchyStruct hh = {};
	hh.Version = version;
	setName(hh.Name, sizeof(hh.Name), "T_SKL");
	hh.NumPivots = declared;
	hier.chunk(W3D_CHUNK_HIERARCHY_HEADER, ChunkWriter::of(hh));
	hier.chunk(W3D_CHUNK_PIVOTS, ChunkWriter::ofArray(pivots));
	ChunkWriter file;
	file.wrapper(W3D_CHUNK_HIERARCHY, hier);
	return file.bytes;
}

int loadTree(const std::vector<std::uint8_t> &bytes, HTreeClass &tree, std::string &error)
{
	ChunkLoadClass cload(bytes.data(), bytes.size());
	REQUIRE(cload.Open_Chunk());
	REQUIRE(cload.Cur_Chunk_ID() == W3D_CHUNK_HIERARCHY);
	return tree.Load_W3D(cload, &error);
}

} // namespace

TEST_CASE("HTree: base pose is T[i] = T[parent] * Base[i] for a root plus a chain")
{
	std::vector<W3dPivotStruct> pivots = {
		makePivot("ROOTTRANSFORM", 0xFFFFFFFFu, 0, 0, 0),
		makePivot("A", 0, 1, 0, 0),
		makePivot("B", 1, 0, 2, 0),
	};
	HTreeClass tree;
	std::string error;
	REQUIRE_MESSAGE(loadTree(hierarchyBytes(W3D_MAKE_VERSION(4, 1), 3, pivots), tree, error) == HTreeClass::OK, error);
	CHECK(tree.Num_Pivots() == 3);
	CHECK(tree.Get_Name() == "T_SKL");
	tree.Base_Update(Matrix3D());
	// Identity rotations: world position of B is (1, 2, 0) by adding translations down the chain.
	Vector3 b = tree.Get_Transform(2).Get_Translation();
	CHECK(b.X == doctest::Approx(1.0f));
	CHECK(b.Y == doctest::Approx(2.0f));
	CHECK(b.Z == doctest::Approx(0.0f));
	CHECK(tree.Get_Bone_Index("b") == 2); // case-insensitive
}

TEST_CASE("HTree: ParentIdx 0xFFFFFFFE is rejected, not cast to -2 and used as an index")
{
	// Sol scaffold finding 3: (int)0xFFFFFFFE == -2 passed the forward-parent check.
	std::vector<W3dPivotStruct> pivots = {
		makePivot("ROOTTRANSFORM", 0xFFFFFFFFu, 0, 0, 0),
		makePivot("A", 0xFFFFFFFEu, 1, 0, 0),
	};
	HTreeClass tree;
	std::string error;
	CHECK(loadTree(hierarchyBytes(W3D_MAKE_VERSION(4, 1), 2, pivots), tree, error) == HTreeClass::LOAD_ERROR);
	CHECK(error.find("parent") != std::string::npos);
	CHECK(tree.Num_Pivots() == 0);
}

TEST_CASE("HTree: a parent index equal to the pivot's own index or later is rejected")
{
	std::vector<W3dPivotStruct> pivots = {
		makePivot("ROOTTRANSFORM", 0xFFFFFFFFu, 0, 0, 0),
		makePivot("A", 1, 1, 0, 0), // parent == self
	};
	HTreeClass tree;
	std::string error;
	CHECK(loadTree(hierarchyBytes(W3D_MAKE_VERSION(4, 1), 2, pivots), tree, error) == HTreeClass::LOAD_ERROR);
	CHECK(error.find("parent") != std::string::npos);
}

TEST_CASE("HTree: a post-3.0 hierarchy declaring zero pivots is an error, not a write to Pivot[0]")
{
	// Sol scaffold finding 3: zero pivots plus an empty PIVOTS chunk reached Pivot[0].
	HTreeClass tree;
	std::string error;
	CHECK(loadTree(hierarchyBytes(W3D_MAKE_VERSION(4, 1), 0, {}), tree, error) == HTreeClass::LOAD_ERROR);
	CHECK(error.find("zero pivots") != std::string::npos);
	CHECK(tree.Num_Pivots() == 0);
}

TEST_CASE("HTree: a declared pivot count larger than the PIVOTS chunk is an error")
{
	std::vector<W3dPivotStruct> pivots = { makePivot("ROOTTRANSFORM", 0xFFFFFFFFu, 0, 0, 0) };
	HTreeClass tree;
	std::string error;
	CHECK(loadTree(hierarchyBytes(W3D_MAKE_VERSION(4, 1), 4000000000u, pivots), tree, error) == HTreeClass::LOAD_ERROR);
	CHECK(error.find("pivot") != std::string::npos);
}

TEST_CASE("HTree: an unknown child chunk is an error; the exporter's pivot fixups are the one explicit ignore")
{
	std::vector<W3dPivotStruct> pivots = { makePivot("ROOTTRANSFORM", 0xFFFFFFFFu, 0, 0, 0) };
	auto withExtra = [&](std::uint32_t id) {
		ChunkWriter hier;
		W3dHierarchyStruct hh = {};
		hh.Version = W3D_MAKE_VERSION(4, 1);
		setName(hh.Name, sizeof(hh.Name), "T_SKL");
		hh.NumPivots = 1;
		hier.chunk(W3D_CHUNK_HIERARCHY_HEADER, ChunkWriter::of(hh));
		hier.chunk(W3D_CHUNK_PIVOTS, ChunkWriter::ofArray(pivots));
		hier.chunk(id, { 1, 2, 3, 4 });
		ChunkWriter file;
		file.wrapper(W3D_CHUNK_HIERARCHY, hier);
		return file.bytes;
	};
	HTreeClass tree;
	std::string error;
	CHECK(loadTree(withExtra(W3D_CHUNK_PIVOT_FIXUPS), tree, error) == HTreeClass::OK);
	CHECK(loadTree(withExtra(0x7777), tree, error) == HTreeClass::LOAD_ERROR);
	CHECK(error.find("unexpected chunk 30583") != std::string::npos);
	CHECK(tree.Num_Pivots() == 0);
}

TEST_CASE("HTree: a second root pivot is rejected")
{
	std::vector<W3dPivotStruct> pivots = {
		makePivot("ROOTTRANSFORM", 0xFFFFFFFFu, 0, 0, 0),
		makePivot("A", 0xFFFFFFFFu, 1, 0, 0),
	};
	HTreeClass tree;
	std::string error;
	CHECK(loadTree(hierarchyBytes(W3D_MAKE_VERSION(4, 1), 2, pivots), tree, error) == HTreeClass::LOAD_ERROR);
	CHECK(error.find("root") != std::string::npos);
}

TEST_CASE("HTree: pre-3.0 files get an added RootTransform and parents shift by one (ZH htree.cpp)")
{
	// Version 2.0: two file pivots; ZH adds a root so the tree has three. The old root (parent -1)
	// becomes a child of the added root; the second pivot's parent 0 becomes 1.
	std::vector<W3dPivotStruct> pivots = {
		makePivot("A", 0xFFFFFFFFu, 5, 0, 0),
		makePivot("B", 0, 0, 7, 0),
	};
	HTreeClass tree;
	std::string error;
	REQUIRE_MESSAGE(loadTree(hierarchyBytes(W3D_MAKE_VERSION(2, 0), 2, pivots), tree, error) == HTreeClass::OK, error);
	CHECK(tree.Num_Pivots() == 3);
	CHECK(tree.Get_Pivot(0).Name == "RootTransform");
	CHECK(tree.Get_Pivot(1).ParentIdx == 0);
	CHECK(tree.Get_Pivot(2).ParentIdx == 1);
	tree.Base_Update(Matrix3D());
	Vector3 b = tree.Get_Transform(2).Get_Translation();
	CHECK(b.X == doctest::Approx(5.0f));
	CHECK(b.Y == doctest::Approx(7.0f));
}

TEST_CASE("HTree: pre-3.0 ParentIdx 0xFFFFFFFE is rejected rather than wrapping to the root marker")
{
	std::vector<W3dPivotStruct> pivots = {
		makePivot("A", 0xFFFFFFFEu, 5, 0, 0),
	};
	HTreeClass tree;
	std::string error;
	CHECK(loadTree(hierarchyBytes(W3D_MAKE_VERSION(2, 0), 1, pivots), tree, error) == HTreeClass::LOAD_ERROR);
	CHECK(error.find("parent") != std::string::npos);
}

// OpenBFME unit tests: six retail models built through the render pipeline, and the Gondor soldier's idle clip evaluated
// against an independent decode of its raw channel bytes. GPL-3.0.
//
// Runs only when ROTWK_INSTALL and BFME2_INSTALL are set (prints SKIP otherwise); uses the shared retail mount.
// Expected values come from the W3D files themselves, read here by a second, minimal chunk reader that shares no code with
// the engine's, and from the format as the ZH / BFME2 sources define it. Nothing is read back from the code under test.

#include "doctest.h"
#include "RetailTestMount.h"

#include "Common/AsciiString.h"
#include "GameEngineDevice/W3DDevice/GameClient/HouseColor.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"
#include "Libraries/WWVegas/WW3D2/hanim.h"
#include "Libraries/WWVegas/WW3D2/htree.h"
#include "Libraries/WWVegas/WW3D2/mapper.h"
#include "Libraries/WWVegas/WW3D2/meshrender.h"
#include "Libraries/WWVegas/WW3D2/textureloader.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{

#define NEEDS_RETAIL(testName)                          \
	retailtest::Mount *mount = retailtest::pureMount(); \
	if (!mount)                                         \
	{                                                   \
		retailtest::printSkip(testName);                \
		return;                                         \
	}                                                   \
	REQUIRE_MESSAGE(mount->fs, mount->error)

// ---- a second, independent minimal chunk reader ------------------------------------------------------------------------
struct RawChunk
{
	std::uint32_t Id = 0;
	std::uint32_t Size = 0;
	size_t Body = 0; // offset of the body in the file
	bool HasChildren = false;
};

std::uint32_t rd32(const std::vector<std::uint8_t> &d, size_t at)
{
	return (std::uint32_t)d[at] | ((std::uint32_t)d[at + 1] << 8) | ((std::uint32_t)d[at + 2] << 16) | ((std::uint32_t)d[at + 3] << 24);
}
std::uint16_t rd16(const std::vector<std::uint8_t> &d, size_t at)
{
	return (std::uint16_t)(d[at] | (d[at + 1] << 8));
}
float rdf(const std::vector<std::uint8_t> &d, size_t at)
{
	std::uint32_t v = rd32(d, at);
	float f;
	std::memcpy(&f, &v, 4);
	return f;
}

std::vector<RawChunk> children(const std::vector<std::uint8_t> &d, size_t begin, size_t end)
{
	std::vector<RawChunk> out;
	size_t p = begin;
	while (p + 8 <= end)
	{
		RawChunk c;
		c.Id = rd32(d, p);
		std::uint32_t sz = rd32(d, p + 4);
		c.HasChildren = (sz & 0x80000000u) != 0;
		c.Size = sz & 0x7FFFFFFFu;
		c.Body = p + 8;
		REQUIRE(c.Body + c.Size <= end);
		out.push_back(c);
		p = c.Body + c.Size;
	}
	REQUIRE(p == end);
	return out;
}

std::string cstr(const std::vector<std::uint8_t> &d, size_t at, size_t max)
{
	std::string s;
	for (size_t i = 0; i < max && d[at + i]; ++i) s += (char)d[at + i];
	return s;
}

std::string lowerName(const std::string &s) { return AsciiStringUtil::lowered(s); }

bool readFile(retailtest::Mount *mount, const std::string &path, std::vector<std::uint8_t> &out)
{
	std::string error;
	bool ok = mount->fs->readFile(path, out, &error);
	INFO(path << ": " << error);
	return ok;
}

// ---- header counts straight from the bytes ---------------------------------------------------------------------------
struct RawMeshCounts
{
	bool Found = false;
	std::uint32_t HeaderVertices = 0, HeaderTris = 0;
	std::uint32_t VertexChunkVertices = 0, TriangleChunkTriangles = 0; // from chunk sizes
};

RawMeshCounts rawMeshCounts(const std::vector<std::uint8_t> &file, const std::string &fullName)
{
	RawMeshCounts r;
	for (const RawChunk &top : children(file, 0, file.size()))
	{
		if (top.Id != 0x0000 || !top.HasChildren) continue;
		std::string container, name;
		RawMeshCounts cand;
		for (const RawChunk &c : children(file, top.Body, top.Body + top.Size))
		{
			if (c.Id == 0x1F) // MESH_HEADER3: Version(4) Attributes(4) MeshName[16] ContainerName[16] NumTris(4) NumVertices(4)
			{
				name = cstr(file, c.Body + 8, 16);
				container = cstr(file, c.Body + 24, 16);
				cand.HeaderTris = rd32(file, c.Body + 40);
				cand.HeaderVertices = rd32(file, c.Body + 44);
			}
			else if (c.Id == 0x02) cand.VertexChunkVertices = c.Size / 12;
			else if (c.Id == 0x20) cand.TriangleChunkTriangles = c.Size / 32;
		}
		std::string full = container.empty() ? name : container + "." + name;
		if (lowerName(full) == lowerName(fullName))
		{
			cand.Found = true;
			return cand;
		}
	}
	return r;
}

// ---- independent evaluation of the soldier's skeleton at a frame ------------------------------------------------------
struct Mat4
{
	double m[4][4];
	Mat4()
	{
		for (int r = 0; r < 4; ++r)
			for (int c = 0; c < 4; ++c) m[r][c] = r == c ? 1.0 : 0.0;
	}
};
Mat4 mul(const Mat4 &a, const Mat4 &b)
{
	Mat4 o;
	for (int r = 0; r < 4; ++r)
		for (int c = 0; c < 4; ++c)
		{
			double s = 0;
			for (int k = 0; k < 4; ++k) s += a.m[r][k] * b.m[k][c];
			o.m[r][c] = s;
		}
	return o;
}
Mat4 translate(double x, double y, double z)
{
	Mat4 t;
	t.m[0][3] = x;
	t.m[1][3] = y;
	t.m[2][3] = z;
	return t;
}
Mat4 rotation(double x, double y, double z, double w)
{
	double n = std::sqrt(x * x + y * y + z * z + w * w);
	x /= n; y /= n; z /= n; w /= n;
	Mat4 r;
	r.m[0][0] = 1 - 2 * (y * y + z * z); r.m[0][1] = 2 * (x * y - z * w);     r.m[0][2] = 2 * (x * z + y * w);
	r.m[1][0] = 2 * (x * y + z * w);     r.m[1][1] = 1 - 2 * (x * x + z * z); r.m[1][2] = 2 * (y * z - x * w);
	r.m[2][0] = 2 * (x * z - y * w);     r.m[2][1] = 2 * (y * z + x * w);     r.m[2][2] = 1 - 2 * (x * x + y * y);
	return r;
}

// BFME2 motion channel (chunk 0x284), encoding 1 = 4-bit adaptive delta: header u8 zero, u8 encoding, u8 components, u8 type,
// u16 count, u16 pivot; then f32 scale, f32 initial[components], then ceil((count - 1) / 16) rows of `components` blocks of
// (u8 table index, 8 bytes of 16 nibbles, low nibble first). Sample f (f >= 1) adds nibble (f - 1) % 16 of row (f - 1) / 16:
// delta = signed nibble * table[index] * scale (ZH motchan.cpp decompress; table: ZH filtertable). Held past the last sample.
struct RawChannel
{
	int Encoding = 0, Components = 0, Type = 0, Pivot = 0, Count = 0;
	std::vector<std::vector<double>> Samples; // [frame][component]
};

double filterTable(int i)
{
	static const double first[16] = { 1e-8, 1e-7, 1e-6, 1e-5, 1e-4, 1e-3, 1e-2, 1e-1, 1.0, 10.0, 100.0, 1000.0, 1e4, 1e5, 1e6, 1e7 };
	if (i < 16) return first[i];
	const double pi = 3.14159265358979323846;
	return 1.0 - std::sin(pi / 2.0 * (double)(i - 16) / 240.0);
}

RawChannel decodeChannel(const std::vector<std::uint8_t> &d, const RawChunk &c)
{
	RawChannel ch;
	REQUIRE(d[c.Body] == 0);
	ch.Encoding = d[c.Body + 1];
	ch.Components = d[c.Body + 2];
	ch.Type = d[c.Body + 3];
	ch.Count = rd16(d, c.Body + 4);
	ch.Pivot = rd16(d, c.Body + 6);
	REQUIRE(ch.Encoding == 1); // the idle clip of the Gondor soldier uses nothing else (checked by the test below)
	size_t at = c.Body + 8;
	const double scale = rdf(d, at);
	at += 4;
	std::vector<double> value(ch.Components);
	for (int k = 0; k < ch.Components; ++k)
	{
		value[k] = rdf(d, at);
		at += 4;
	}
	ch.Samples.assign((size_t)ch.Count, value);
	const int rows = (ch.Count - 1 + 15) / 16;
	for (int f = 1; f < ch.Count; ++f)
	{
		const int row = (f - 1) / 16;
		const int nib = (f - 1) % 16;
		for (int k = 0; k < ch.Components; ++k)
		{
			size_t block = at + ((size_t)row * ch.Components + k) * 9;
			int idx = d[block];
			int byte = d[block + 1 + nib / 2];
			int v = (nib & 1) ? (byte >> 4) : (byte & 0xF);
			if (v & 8) v -= 16;
			value[k] += (double)v * filterTable(idx) * scale;
		}
		ch.Samples[(size_t)f] = value;
	}
	(void)rows;
	return ch;
}

struct RawPivot
{
	int Parent;
	double T[3];
	double Q[4];
};

} // namespace

TEST_CASE("retail models: six models from different factions and kinds build, with header counts, resolved sub objects and textures")
{
	NEEDS_RETAIL("retail models");
	ArchiveW3DFileSource source(*mount->fs);
	WW3DAssetManager am(source);

	struct Model
	{
		const char *name;
		const char *kind;
		bool hlod;
	};
	// Gondor soldier, Gondor hero Aragorn, Gondor cavalry, Gondor barracks (HLOD of many sub objects), a tree and a prop
	// (both bare meshes). Names from the retail object INIs (gondorfighter.ini, aragorn.ini, gondorcavalry.ini, barracks.ini,
	// naturetrees.ini, civilianprop.ini).
	const Model models[] = { { "GUMAArms_SKN", "soldier", true }, { "GUAragorn_SKN", "hero", true }, { "GUCavalry_SKN", "cavalry", true },
		{ "GBBarracks_SKN", "building", true }, { "PTree08", "tree", false }, { "PCrate", "prop", false } };

	size_t totalMeshes = 0, totalTextures = 0, totalSurfaces = 0, divergent = 0, fxSurfaces = 0;
	std::set<std::string> missingTextures;
	std::set<std::string> divergences;

	for (const Model &m : models)
	{
		INFO("model " << m.name << " (" << m.kind << ")");
		std::string error;
		const RenderObjPrototype *proto = am.Create_Render_Obj(m.name, &error);
		REQUIRE_MESSAGE(proto, error);
		CHECK((proto->Type == RenderObjPrototype::PROTO_HLOD) == m.hlod);
		REQUIRE_MESSAGE(proto->Tree, "no hierarchy");
		CHECK_FALSE(proto->HierarchyMissing);
		REQUIRE_FALSE(proto->SubObjects.empty());
		if (m.hlod)
		{
			CHECK(proto->Tree->Num_Pivots() > 1);
		}
		else
		{
			CHECK(proto->Tree->Num_Pivots() == 1); // bare mesh: ZH animobj.cpp:107-120 default tree
		}

		size_t meshes = 0;
		for (const RenderSubObject &sub : proto->SubObjects)
		{
			INFO("sub object " << sub.Name << " bone " << sub.BoneIndex);
			CHECK_MESSAGE(sub.Type != RenderSubObject::SUB_UNRESOLVED, sub.Reason);
			CHECK(sub.BoneIndex >= 0);
			CHECK(sub.BoneIndex < proto->Tree->Num_Pivots());
			if (sub.Type != RenderSubObject::SUB_MESH)
			{
				continue;
			}
			++meshes;
			MeshRenderData r;
			REQUIRE_MESSAGE(Build_Mesh_Render_Data(*sub.Mesh, r, &error), error);

			// header counts of the file's own bytes: a second reader
			std::string name = sub.Mesh->Get_Name();
			std::string lower = lowerName(name);
			size_t dot = lower.find('.');
			std::string stem = dot == std::string::npos ? lower : lower.substr(0, dot);
			std::vector<std::uint8_t> bytes;
			REQUIRE(readFile(mount, W3D_Asset_Path(stem), bytes));
			RawMeshCounts raw = rawMeshCounts(bytes, name);
			REQUIRE_MESSAGE(raw.Found, name);
			CHECK(raw.VertexChunkVertices == raw.HeaderVertices);
			CHECK(raw.TriangleChunkTriangles == raw.HeaderTris);
			CHECK(r.NumVertices == raw.HeaderVertices);
			CHECK(r.NumTriangles == raw.HeaderTris);
			CHECK(r.Position.size() == (size_t)raw.HeaderVertices * 3);
			CHECK(r.Normal.size() == (size_t)raw.HeaderVertices * 3);
			if (r.Skin)
			{
				CHECK(r.Bone0.size() == (size_t)raw.HeaderVertices);
			}

			// every pass covers every triangle exactly once
			std::map<int, std::set<std::uint32_t>> perPass;
			std::map<int, size_t> perPassCount;
			for (const MeshDrawSurface &s : r.Surfaces)
			{
				++totalSurfaces;
				perPassCount[s.Pass] += s.Triangles.size();
				perPass[s.Pass].insert(s.Triangles.begin(), s.Triangles.end());
				CHECK(s.Indices.size() == s.Triangles.size() * 3);
				for (std::uint32_t idx : s.Indices) CHECK(idx < raw.HeaderVertices);
				if (s.Kind == MATERIAL_CLASSIC && s.State.Divergent)
				{
					++divergent;
					divergences.insert(s.State.Divergence);
				}
				if (s.Kind == MATERIAL_FX) ++fxSurfaces;

				// every texture resolves to a file (no missing-texture checker)
				auto resolve = [&](const std::string &texName) {
					++totalTextures;
					TextureResolution res = Resolve_W3D_Texture(texName, [&](const std::string &p) { return mount->fs->doesFileExist(p); });
					if (!res.Found) missingTextures.insert(m.name + std::string(": ") + texName);
				};
				for (int st = 0; st < 2; ++st)
				{
					if (s.Stage[st].HasTexture) resolve(s.Stage[st].TextureName);
				}
				if (s.Kind == MATERIAL_FX)
				{
					const MeshShaderMaterialDef &fx = sub.Mesh->ShaderMaterials[(size_t)s.ShaderMaterialId];
					for (const MeshShaderMaterialProperty &p : fx.Properties)
					{
						if (p.Type == W3DSHADERMATERIAL_PROPERTY_TEXTURE && !p.StringValue.empty()) resolve(p.StringValue);
					}
				}
			}
			for (const auto &kv : perPass)
			{
				CHECK_MESSAGE(kv.second.size() == raw.HeaderTris, name << " pass " << kv.first << " covers " << kv.second.size() << " of " << raw.HeaderTris << " triangles");
				CHECK(perPassCount[kv.first] == raw.HeaderTris);
			}
		}
		CHECK(meshes > 0);
		totalMeshes += meshes;
		std::printf("[w3d render] %-14s %-9s %2zu pivots, %2zu sub objects, %2zu meshes (LOD arrays %zu, extra %zu)\n", m.name, m.kind,
			(size_t)proto->Tree->Num_Pivots(), proto->SubObjects.size(), meshes, proto->LodCount, proto->ExtraLodArrays);
	}
	std::printf("[w3d render] %zu meshes, %zu draw surfaces (%zu FX), %zu texture references, %zu divergent classic surfaces\n", totalMeshes,
		totalSurfaces, fxSurfaces, totalTextures, divergent);
	for (const std::string &d : divergences) std::printf("[w3d render]   divergence: %s\n", d.c_str());

	for (const std::string &t : missingTextures) CHECK_MESSAGE(false, "texture does not resolve: " << t);
	CHECK(missingTextures.empty());
	CHECK(am.Faults().empty());
}

TEST_CASE("retail models: housecolor.ini holds 511 blocks over 503 base textures and 313 house textures (spec 1.4 and 3.6)")
{
	NEEDS_RETAIL("housecolor.ini");
	std::vector<std::uint8_t> bytes;
	REQUIRE(readFile(mount, "data\\ini\\housecolor.ini", bytes));
	HouseColorTable table;
	std::string error;
	REQUIRE_MESSAGE(table.Parse(std::string(bytes.begin(), bytes.end()), &error), error);
	std::printf("[w3d render] housecolor.ini: %zu blocks, %zu base textures, %zu house textures, %zu replaced keys\n", table.Block_Count(),
		table.Base_Count(), table.House_Texture_Count(), table.Replaced().size());
	CHECK(table.Block_Count() == 511);
	CHECK(table.Base_Count() == 503);
	CHECK(table.House_Texture_Count() == 313);
	// the first block of the retail file
	REQUIRE(table.Find("IUWargSntryB.tga"));
	CHECK(*table.Find("IUWargSntryB.tga") == "HC_IUWarg.tga");
}

TEST_CASE("retail soldier: the idle clip's bone transforms at frames 0, N/2 and N-1 equal an independent decode of its raw channels")
{
	NEEDS_RETAIL("retail soldier idle");
	ArchiveW3DFileSource source(*mount->fs);
	WW3DAssetManager am(source);
	std::string error;

	// The soldier's model, skeleton and idle clip (gondorfighter.ini: Model = GUMAArms_SKN, Skeleton = GUMAArms_SKL, the
	// IdleAnimationState picks GUManMocap_IDLB). The hierarchy comes from the HLOD, the clip by BFME's prefix.name form.
	const RenderObjPrototype *proto = am.Create_Render_Obj("GUMAArms_SKN", &error);
	REQUIRE_MESSAGE(proto, error);
	CHECK(proto->HierarchyName == "GUMAARMS_SKL");
	std::string clipName;
	REQUIRE(am.Resolve_Animation(proto->HierarchyName, "GUManMocap_IDLB", false, 0, &clipName));
	CHECK(lowerName(clipName) == "gumaarms_skl.gumanmocap_idlb");
	const HAnimClass *anim = am.Get_HAnim(clipName, &error);
	REQUIRE_MESSAGE(anim, error);
	const int frames = anim->Get_Num_Frames();
	REQUIRE(frames == 154); // header NumFrames of art\w3d\gu\gumanmocap_idlb.w3d (read by the survey script: 154 at 30 fps)
	CHECK(anim->Get_Frame_Rate() == 30.0f);

	// ---- independent side: the skeleton and the clip from raw bytes ----
	std::vector<std::uint8_t> sklBytes, clipBytes;
	REQUIRE(readFile(mount, "art\\w3d\\gu\\gumaarms_skl.w3d", sklBytes));
	REQUIRE(readFile(mount, "art\\w3d\\gu\\gumanmocap_idlb.w3d", clipBytes));

	std::vector<RawPivot> pivots;
	{
		std::vector<RawChunk> top = children(sklBytes, 0, sklBytes.size());
		REQUIRE(top.size() == 1);
		REQUIRE(top[0].Id == 0x0100);
		for (const RawChunk &c : children(sklBytes, top[0].Body, top[0].Body + top[0].Size))
		{
			if (c.Id != 0x0102) continue; // PIVOTS: Name[16] Parent(u32) Translation(3f) Euler(3f) Rotation(4f) = 60 bytes
			REQUIRE(c.Size % 60 == 0);
			for (size_t at = c.Body; at < c.Body + c.Size; at += 60)
			{
				RawPivot p;
				std::uint32_t parent = rd32(sklBytes, at + 16);
				p.Parent = parent == 0xFFFFFFFFu ? -1 : (int)parent;
				for (int k = 0; k < 3; ++k) p.T[k] = rdf(sklBytes, at + 20 + 4 * k);
				for (int k = 0; k < 4; ++k) p.Q[k] = rdf(sklBytes, at + 44 + 4 * k);
				pivots.push_back(p);
			}
		}
	}
	REQUIRE(pivots.size() == (size_t)proto->Tree->Num_Pivots());

	std::map<int, std::map<int, RawChannel>> channels; // pivot -> channel type -> decoded
	{
		std::vector<RawChunk> top = children(clipBytes, 0, clipBytes.size());
		REQUIRE(top.size() == 1);
		REQUIRE(top[0].Id == 0x0280);
		int motionChannels = 0;
		for (const RawChunk &c : children(clipBytes, top[0].Body, top[0].Body + top[0].Size))
		{
			if (c.Id != 0x0284) continue;
			RawChannel ch = decodeChannel(clipBytes, c);
			REQUIRE(ch.Pivot < (int)pivots.size());
			REQUIRE(channels[ch.Pivot].count(ch.Type) == 0);
			channels[ch.Pivot][ch.Type] = ch;
			++motionChannels;
		}
		std::printf("[w3d render] soldier idle clip %s: %d frames, %d motion channels on %zu pivots, skeleton of %zu pivots\n",
			clipName.c_str(), frames, motionChannels, channels.size(), pivots.size());
		CHECK(motionChannels == 52); // counted by the survey script above: 52 channels, all encoding 1
	}

	auto independent = [&](int frame, std::vector<Mat4> &world) {
		world.assign(pivots.size(), Mat4());
		for (size_t i = 0; i < pivots.size(); ++i)
		{
			Mat4 local = mul(translate(pivots[i].T[0], pivots[i].T[1], pivots[i].T[2]), rotation(pivots[i].Q[0], pivots[i].Q[1], pivots[i].Q[2], pivots[i].Q[3]));
			auto it = channels.find((int)i);
			if (i > 0 && it != channels.end())
			{
				auto sample = [&](int type, int comp, double def) {
					auto c = it->second.find(type);
					if (c == it->second.end()) return def;
					const RawChannel &ch = c->second;
					int f = frame < ch.Count ? frame : ch.Count - 1;
					return ch.Samples[(size_t)f][(size_t)comp];
				};
				local = mul(local, translate(sample(0, 0, 0), sample(1, 0, 0), sample(2, 0, 0)));
				auto q = it->second.find(6);
				if (q != it->second.end())
				{
					const RawChannel &ch = q->second;
					int f = frame < ch.Count ? frame : ch.Count - 1;
					const std::vector<double> &s = ch.Samples[(size_t)f];
					local = mul(local, rotation(s[0], s[1], s[2], s[3]));
				}
			}
			world[i] = pivots[i].Parent < 0 ? local : mul(world[(size_t)pivots[i].Parent], local);
		}
	};

	const int sampleFrames[3] = { 0, frames / 2, frames - 1 };
	double worstPos = 0, worstRot = 0, movement = 0;
	for (int frame : sampleFrames)
	{
		HTreePose pose;
		proto->Tree->Anim_Pose(Matrix3D(), anim, (float)frame, pose);
		std::vector<Mat4> expect;
		independent(frame, expect);
		REQUIRE(pose.Num_Pivots() == (int)pivots.size());
		for (size_t i = 0; i < pivots.size(); ++i)
		{
			INFO("frame " << frame << " pivot " << i);
			Vector3 t = pose.Transform[i].Get_Translation();
			const double ex = expect[i].m[0][3], ey = expect[i].m[1][3], ez = expect[i].m[2][3];
			worstPos = std::max(worstPos, std::max(std::fabs(t.X - ex), std::max(std::fabs(t.Y - ey), std::fabs(t.Z - ez))));
			for (int r = 0; r < 3; ++r)
				for (int c = 0; c < 3; ++c)
					worstRot = std::max(worstRot, std::fabs((double)pose.Transform[i].Row[r][c] - expect[i].m[r][c]));
		}
	}
	// the clip must actually move: bone positions at frame N/2 differ from frame 0 somewhere by more than the tolerance
	{
		HTreePose a, b;
		proto->Tree->Anim_Pose(Matrix3D(), anim, 0.0f, a);
		proto->Tree->Anim_Pose(Matrix3D(), anim, (float)(frames / 2), b);
		for (size_t i = 0; i < pivots.size(); ++i)
		{
			Vector3 p = a.Transform[i].Get_Translation(), q = b.Transform[i].Get_Translation();
			movement = std::max(movement, (double)std::max(std::fabs(p.X - q.X), std::max(std::fabs(p.Y - q.Y), std::fabs(p.Z - q.Z))));
		}
	}
	std::printf("[w3d render] idle clip vs independent decode: worst position error %.3g, worst rotation error %.3g, largest bone movement frame 0 -> %d: %.3g\n",
		worstPos, worstRot, frames / 2, movement);
	CHECK(worstPos < 1e-3);
	CHECK(worstRot < 1e-4);
	CHECK(movement > 0.05);
}

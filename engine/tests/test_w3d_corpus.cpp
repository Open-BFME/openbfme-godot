// OpenBFME unit tests: the W3D corpus of the pure RotWK 2.01 + BFME2 1.06 mount. GPL-3.0.
//
// Runs only when ROTWK_INSTALL and BFME2_INSTALL are set (prints SKIP otherwise). Expected values are
// the independent survey of the same files by the spec author (tests/data/w3d-survey.json, from
// survey.py), never this engine's own output.

#include "doctest.h"
#include "RetailTestMount.h"

#include "Common/MiniJson.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "Libraries/WWVegas/WW3D2/hcanim.h"
#include "Libraries/WWVegas/WW3D2/hlod.h"
#include "Libraries/WWVegas/WW3D2/hrawanim.h"
#include "Libraries/WWVegas/WW3D2/htree.h"
#include "Libraries/WWVegas/WW3D2/meshmdl.h"
#include "Libraries/WWVegas/WW3D2/part_ldr.h"
#include "Libraries/WWVegas/WW3D2/shdmesh.h"
#include "Libraries/WWVegas/WW3D2/w3d_file.h"
#include "Libraries/WWVegas/WW3D2/w3dchunks.h"
#include "Libraries/WWVegas/WWLib/chunkio.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <sstream>

namespace
{

std::string hex(std::uint32_t v)
{
	char buf[16];
	std::snprintf(buf, sizeof(buf), "0x%x", v);
	return buf;
}

std::string pair2(int a, int b)
{
	return "(" + std::to_string(a) + ", " + std::to_string(b) + ")";
}
std::string triple(int a, int b, int c)
{
	return "(" + std::to_string(a) + ", " + std::to_string(b) + ", " + std::to_string(c) + ")";
}
std::string quad(int a, int b, int c, int d)
{
	return "(" + std::to_string(a) + ", " + std::to_string(b) + ", " + std::to_string(c) + ", " + std::to_string(d) + ")";
}

std::string lowerStr(std::string s)
{
	for (char &c : s)
	{
		if (c >= 'A' && c <= 'Z')
		{
			c = (char)(c - 'A' + 'a');
		}
	}
	return s;
}

typedef std::map<std::string, std::uint64_t> Histogram;

// Channel shapes of one animation chunk read straight from the chunk bytes (the survey's method), keyed
// like survey.json's stats: raw "(type, vlen)", compressed "(flavor, type, vlen)", motion
// "(flavor, deltaType, type, vlen)", bit channels by flags.
struct ChannelPeek
{
	Histogram Raw, RawBit, Compressed, CompressedBit, Motion;
	int Count = 0;

	void Add(const ChannelPeek &o)
	{
		for (const auto &kv : o.Raw) Raw[kv.first] += kv.second;
		for (const auto &kv : o.RawBit) RawBit[kv.first] += kv.second;
		for (const auto &kv : o.Compressed) Compressed[kv.first] += kv.second;
		for (const auto &kv : o.CompressedBit) CompressedBit[kv.first] += kv.second;
		for (const auto &kv : o.Motion) Motion[kv.first] += kv.second;
		Count += o.Count;
	}
};

std::uint16_t u16at(const std::uint8_t *p, size_t at) { return (std::uint16_t)(p[at] | (p[at + 1] << 8)); }

// `cload` is positioned inside an animation chunk that has had its header read.
void peekChannels(ChunkLoadClass &cload, int flavor, ChannelPeek &peek)
{
	std::uint8_t buf[16];
	while (cload.Open_Chunk())
	{
		std::uint32_t id = cload.Cur_Chunk_ID();
		std::uint32_t len = cload.Cur_Chunk_Length();
		std::uint32_t want = len < 16 ? len : 16;
		if (want && cload.Read(buf, want) != want)
		{
			return;
		}
		if (id == W3D_CHUNK_ANIMATION_CHANNEL && want >= 8)
		{
			peek.Raw[pair2(u16at(buf, 6), u16at(buf, 4))]++;
			peek.Count++;
		}
		else if (id == W3D_CHUNK_BIT_CHANNEL && want >= 8)
		{
			peek.RawBit[std::to_string(u16at(buf, 4))]++;
			peek.Count++;
		}
		else if (id == W3D_CHUNK_COMPRESSED_ANIMATION_CHANNEL && want >= 8)
		{
			peek.Compressed[triple(flavor, buf[7], buf[6])]++;
			peek.Count++;
		}
		else if (id == W3D_CHUNK_COMPRESSED_BIT_CHANNEL && want >= 8)
		{
			peek.CompressedBit[triple(flavor, buf[6], buf[7])]++;
			peek.Count++;
		}
		else if (id == W3D_CHUNK_COMPRESSED_ANIMATION_MOTION_CHANNEL && want >= 8)
		{
			peek.Motion[quad(flavor, buf[1], buf[3], buf[2])]++;
			peek.Count++;
		}
		cload.Close_Chunk();
	}
}

// What a decoded animation holds, in the same keys.
void decodedChannels(const HRawAnimClass &a, ChannelPeek &out)
{
	for (int p = 0; p < a.Get_Num_Pivots(); ++p)
	{
		const NodeMotionStruct &m = a.Get_Node_Motion(p);
		const MotionChannelClass *chans[] = { m.X.get(), m.Y.get(), m.Z.get(), m.XR.get(), m.YR.get(), m.ZR.get(), m.Q.get(), m.Fade.get() };
		for (const MotionChannelClass *c : chans)
		{
			if (c)
			{
				out.Raw[pair2(c->Get_Type(), c->Get_Vector_Length())]++;
				out.Count++;
			}
		}
		if (m.Vis)
		{
			out.RawBit[std::to_string(m.Vis->Get_Type())]++;
			out.Count++;
		}
	}
}

void decodedChannels(const HCompressedAnimClass &a, ChannelPeek &out)
{
	for (int p = 0; p < a.Get_Num_Pivots(); ++p)
	{
		if (a.Uses_Motion_Channels())
		{
			const BFME2CompressedMotionChannels &m = a.Get_Vector_Motion(p);
			for (const auto &c : m.Channels)
			{
				if (c)
				{
					out.Motion[quad(a.Get_Flavor(), c->Get_Encoding(), c->Get_Type(), c->Get_Components())]++;
					out.Count++;
				}
			}
			if (m.Visibility)
			{
				out.CompressedBit[triple(a.Get_Flavor(), m.Visibility->Get_Type(), 1)]++; // default value is checked below
				out.Count++;
			}
		}
		else
		{
			const NodeCompressedMotionStruct &m = a.Get_Node_Motion(p);
			for (const auto &c : m.tc)
			{
				if (c)
				{
					out.Compressed[triple(a.Get_Flavor(), c->Get_Type(), c->Get_Vector_Length())]++;
					out.Count++;
				}
			}
			for (const auto &c : m.ad)
			{
				if (c)
				{
					out.Compressed[triple(a.Get_Flavor(), c->Get_Type(), c->Get_Vector_Length())]++;
					out.Count++;
				}
			}
			if (m.Vis)
			{
				out.CompressedBit[triple(a.Get_Flavor(), m.Vis->Get_Type(), 1)]++;
				out.Count++;
			}
		}
	}
}

struct CategoryNorm
{
	std::uint64_t Checked = 0, Bad = 0;
	double Worst = 0.0, Sum = 0.0;
};

struct NormStats
{
	std::string Category; // set by the caller before checking keys
	std::map<std::string, CategoryNorm> ByCategory;
	std::uint64_t QuatKeysChecked = 0;
	std::uint64_t QuatBad = 0;      // |norm - 1| > 1e-3
	double WorstQuatError = 0.0;
	std::uint64_t NonFinite = 0;    // NaN / inf anywhere in a decoded key
	std::uint64_t KeysChecked = 0;
	std::uint64_t ClassicTimecodedChannels = 0;
	std::uint64_t ClassicFirstKeyAfterZero = 0; // classic timecoded channels whose first key is after frame 0
	std::vector<std::string> Samples;

	void Quat(const float *q, const std::string &who)
	{
		QuatKeysChecked++;
		double n = std::sqrt((double)q[0] * q[0] + (double)q[1] * q[1] + (double)q[2] * q[2] + (double)q[3] * q[3]);
		double err = std::fabs(n - 1.0);
		CategoryNorm &cat = ByCategory[Category];
		cat.Checked++;
		cat.Sum += err;
		if (err > cat.Worst)
		{
			cat.Worst = err;
		}
		if (!(err <= 1e-3))
		{
			cat.Bad++;
		}
		if (!(err <= 1e-3))
		{
			QuatBad++;
			if (Samples.size() < 8)
			{
				Samples.push_back(who + " norm " + std::to_string(n));
			}
		}
		if (err > WorstQuatError || err != err)
		{
			WorstQuatError = err;
		}
	}
	void Value(const float *v, int n)
	{
		KeysChecked++;
		for (int i = 0; i < n; ++i)
		{
			if (!std::isfinite(v[i]))
			{
				NonFinite++;
			}
		}
	}
};

// Walks every key of every channel of a decoded animation.
void checkValues(const HRawAnimClass &a, NormStats &ns)
{
	ns.Category = "raw (0x202)";
	for (int p = 0; p < a.Get_Num_Pivots(); ++p)
	{
		const NodeMotionStruct &m = a.Get_Node_Motion(p);
		const MotionChannelClass *chans[] = { m.X.get(), m.Y.get(), m.Z.get(), m.XR.get(), m.YR.get(), m.ZR.get(), m.Q.get(), m.Fade.get() };
		for (const MotionChannelClass *c : chans)
		{
			if (!c)
			{
				continue;
			}
			for (int f = c->Get_First_Frame(); f <= c->Get_Last_Frame(); ++f)
			{
				float v[4];
				c->Get_Vector(f, v);
				ns.Value(v, c->Get_Vector_Length());
				if (c->Get_Type() == ANIM_CHANNEL_Q)
				{
					ns.Quat(v, a.Get_Name() + " raw pivot " + std::to_string(p) + " frame " + std::to_string(f));
				}
			}
		}
	}
}

void checkValues(const HCompressedAnimClass &a, NormStats &ns, std::uint64_t &keysBeyondFrames, std::uint64_t &stepFlagChannels, std::uint64_t &firstKeyAfterZero)
{
	auto checkTimeCoded = [&](const auto &c, bool isQuat, const std::string &who, const char *category) {
		ns.Category = category;
		int n = c.Get_Num_Keys();
		std::uint32_t maxTime = 0;
		bool anyStep = false;
		for (int k = 0; k < n; ++k)
		{
			float v[4];
			c.Get_Key(k, v);
			ns.Value(v, c.Get_Vector_Length());
			if (isQuat)
			{
				ns.Quat(v, who + " key " + std::to_string(k));
			}
		}
		(void)maxTime;
		(void)anyStep;
	};
	for (int p = 0; p < a.Get_Num_Pivots(); ++p)
	{
		std::string who = a.Get_Name() + " pivot " + std::to_string(p);
		if (a.Uses_Motion_Channels())
		{
			for (const auto &c : a.Get_Vector_Motion(p).Channels)
			{
				if (!c)
				{
					continue;
				}
				int n = c->Get_Num_Keys();
				bool step = false;
				for (int k = 0; k < n; ++k)
				{
					float v[4];
					c->Get_Key(k, v);
					ns.Value(v, c->Get_Components());
					if (c->Get_Type() == ANIM_CHANNEL_Q)
					{
						ns.Category = c->Get_Encoding() == 0 ? "motion encoding 0 (timecoded)" : c->Get_Encoding() == 1 ? "motion encoding 1 (4-bit delta)" : "motion encoding 2 (8-bit delta)";
						ns.Quat(v, who + " motion key " + std::to_string(k));
					}
					if (c->Get_Encoding() == BFME2MotionChannel::ENCODING_TIMECODED)
					{
						std::uint32_t t = c->Get_Key_Time_Code(k);
						step = step || (t & 0x8000);
						if ((t & ~0x8000u) >= (std::uint32_t)a.Get_Num_Frames())
						{
							keysBeyondFrames++;
						}
					}
				}
				if (step)
				{
					stepFlagChannels++;
				}
				if (c->Get_Encoding() == BFME2MotionChannel::ENCODING_TIMECODED && (c->Get_Key_Time_Code(0) & ~0x8000u) != 0)
				{
					firstKeyAfterZero++;
				}
			}
		}
		else
		{
			const NodeCompressedMotionStruct &m = a.Get_Node_Motion(p);
			for (int s = 0; s < CHANNEL_SLOT_COUNT; ++s)
			{
				if (m.tc[s])
				{
					ns.ClassicTimecodedChannels++;
					if ((m.tc[s]->Get_Key_Time_Code(0) & ~W3D_TIMECODED_BINARY_MOVEMENT_FLAG) != 0)
					{
						ns.ClassicFirstKeyAfterZero++;
					}
					checkTimeCoded(*m.tc[s], m.tc[s]->Get_Type() == ANIM_CHANNEL_Q, who + " tc", "classic timecoded (0x282 flavor 0)");
					for (int k = 0; k < m.tc[s]->Get_Num_Keys(); ++k)
					{
						if ((m.tc[s]->Get_Key_Time_Code(k) & ~W3D_TIMECODED_BINARY_MOVEMENT_FLAG) >= (std::uint32_t)a.Get_Num_Frames())
						{
							keysBeyondFrames++;
						}
					}
				}
				if (m.ad[s])
				{
					checkTimeCoded(*m.ad[s], m.ad[s]->Get_Type() == ANIM_CHANNEL_Q, who + " ad", "classic adaptive delta (0x282 flavor 1, 4-bit)");
				}
			}
		}
	}
}

struct AnimFact
{
	std::string Path;
	std::string Name;
	std::string Hierarchy;
	bool Compressed = false;
	std::uint32_t Version = 0;
	int Flavor = 0;
	bool MotionForm = false;
	int MaxPivot = -1;
	int Frames = 0;
	int FrameRate = 0;
	bool PeekHasMotionChannels = false;  // the chunk bytes hold 0x284 channels
	bool PeekHasClassicChannels = false; // the chunk bytes hold 0x282 channels
};

struct Failure
{
	std::string Path;
	std::string What;
};

struct Corpus
{
	size_t Files = 0;
	W3DChunkWalkStats Walk;
	std::map<std::uint32_t, std::uint64_t> FilesWithId;
	std::set<std::string> CorruptFiles;               // chunk walk reported an error
	std::set<std::string> UnknownIdFiles;             // files holding a chunk id outside the table
	std::map<std::string, std::set<std::uint32_t>> UnknownIdsByFile;
	struct TreeDef { std::string Leaf, Path, Name; int Pivots; };
	std::vector<TreeDef> TreeDefs;
	std::map<std::string, int> TreePivots;            // lower hierarchy name -> pivots, first file in catalog (leaf name) order wins
	std::vector<Failure> TreeFailures, AnimFailures, ReadFailures, MeshFailures, SurfaceFailures, HLodFailures, BoxFailures;
	// mesh / HLod / box inventory, compared with the survey
	size_t MeshesLoaded = 0, MeshesWithInfluences = 0, MeshesDualBone = 0, MeshesWithTangents = 0, MeshesWithAABTree = 0;
	size_t ShaderMaterials = 0, ShaderMaterialProperties = 0, MeshPasses = 0, MeshesWithShaderMaterialIds = 0;
	size_t MeshesOutsideBox = 0, SkinsOutsideBox = 0, MeshesWithUnhandled = 0, SurfaceGroupsTotal = 0;
	std::map<std::uint32_t, std::uint64_t> UnhandledMeshChunks;
	std::map<std::string, std::uint64_t> FxTypes;
	size_t HLodsLoaded = 0, HLodsWithExtraLod = 0, HLodAggregateArrays = 0, BoxesLoaded = 0;
	size_t EmittersLoaded = 0, ShdMeshesLoaded = 0, ShdSubMeshes = 0;
	std::vector<std::string> EmitterNames;
	std::vector<Failure> EmitterFailures; // emitters and ShdMeshes
	size_t TreesLoaded = 0, RawAnims = 0, CompressedAnims = 0;
	std::vector<AnimFact> Anims;                      // decoded animations
	ChannelPeek PeekAll;                              // every animation chunk, decoded or not
	ChannelPeek DecodedAll;                           // only what the decoders hold
	size_t MismatchedPerAnim = 0;                     // decoded histogram != peek histogram of the same animation
	std::uint64_t DroppedChannels = 0;
	std::uint64_t ExtraBytes = 0;                     // channels whose chunk carried bytes past the data
	NormStats Norms;
	std::uint64_t KeysBeyondFrames = 0, StepFlagMotionChannels = 0, FirstKeyAfterZeroMotion = 0;
	std::uint64_t ClassicVersion = 0, MotionVersion = 0;
	double Seconds = 0.0;
};


// Reads the chunk at [chunk, chunk + len) (header included) as an animation and records what happened.
void processAnimation(Corpus &c, const std::uint8_t *chunk, size_t len, bool compressed, const std::string &path)
{
	// Peek at the channel shapes straight from the bytes.
	ChannelPeek peek;
	int flavor = 0;
	{
		ChunkLoadClass cl(chunk, len);
		if (cl.Open_Chunk() && cl.Open_Chunk())
		{
			W3dCompressedAnimHeaderStruct h = {};
			if (cl.Cur_Chunk_Length() >= sizeof(h) && cl.Read(&h, sizeof(h)) == sizeof(h))
			{
				flavor = compressed ? h.Flavor : 0;
				cl.Close_Chunk();
				peekChannels(cl, flavor, peek);
			}
		}
	}
	c.PeekAll.Add(peek);

	ChunkLoadClass cload(chunk, len);
	if (!cload.Open_Chunk())
	{
		c.AnimFailures.push_back({ path, "cannot open animation chunk" });
		return;
	}
	std::string error;
	ChannelPeek decoded;
	AnimFact fact;
	fact.Path = path;
	fact.Compressed = compressed;
	if (compressed)
	{
		HCompressedAnimClass anim;
		if (anim.Load_W3D(cload, -1, &error) != HCompressedAnimClass::OK)
		{
			c.AnimFailures.push_back({ path, error });
			return;
		}
		c.CompressedAnims++;
		fact.Name = anim.Get_Short_Name();
		fact.Hierarchy = anim.Get_HName();
		fact.Version = anim.Get_Version();
		fact.Flavor = anim.Get_Flavor();
		fact.MotionForm = anim.Uses_Motion_Channels();
		fact.MaxPivot = anim.Get_Max_Pivot_Referenced();
		fact.Frames = anim.Get_Num_Frames();
		fact.FrameRate = (int)anim.Get_Frame_Rate();
		c.DroppedChannels += (std::uint64_t)anim.Get_Dropped_Channels(); // numNodes = -1: always 0
		decodedChannels(anim, decoded);
		checkValues(anim, c.Norms, c.KeysBeyondFrames, c.StepFlagMotionChannels, c.FirstKeyAfterZeroMotion);
		(anim.Uses_Motion_Channels() ? c.MotionVersion : c.ClassicVersion)++;
	}
	else
	{
		HRawAnimClass anim;
		if (anim.Load_W3D(cload, -1, &error) != HRawAnimClass::OK)
		{
			c.AnimFailures.push_back({ path, error });
			return;
		}
		c.RawAnims++;
		fact.Name = anim.Get_Short_Name();
		fact.Hierarchy = anim.Get_HName();
		fact.Version = anim.Get_Version();
		fact.MaxPivot = anim.Get_Max_Pivot_Referenced();
		fact.Frames = anim.Get_Num_Frames();
		fact.FrameRate = (int)anim.Get_Frame_Rate();
		c.DroppedChannels += (std::uint64_t)anim.Get_Dropped_Channels();
		decodedChannels(anim, decoded);
		checkValues(anim, c.Norms);
	}
	c.DecodedAll.Add(decoded);
	// The decoder must hold exactly the channels the bytes declare (type, vector length, flavor).
	if (decoded.Raw != peek.Raw || decoded.RawBit != peek.RawBit || decoded.Compressed != peek.Compressed ||
		decoded.Motion != peek.Motion || decoded.Count != peek.Count)
	{
		c.MismatchedPerAnim++;
	}
	fact.PeekHasMotionChannels = !peek.Motion.empty();
	fact.PeekHasClassicChannels = !peek.Compressed.empty();
	c.Anims.push_back(fact);
}

const Corpus &corpus()
{
	static Corpus c;
	static bool done = false;
	if (done)
	{
		return c;
	}
	done = true;
	retailtest::Mount *mount = retailtest::pureMount();
	REQUIRE(mount != nullptr);
	REQUIRE_MESSAGE(mount->fs != nullptr, mount->error);
	auto t0 = std::chrono::steady_clock::now();

	FilenameList list;
	mount->fs->getFileListInDirectory("", "", "*.w3d", list, true);
	std::vector<std::uint8_t> bytes;
	for (const std::string &path : list)
	{
		std::string readError;
		if (!mount->fs->readFile(path, bytes, &readError))
		{
			c.ReadFailures.push_back({ path, readError });
			continue;
		}
		c.Files++;
		std::string lpath = lowerStr(path);

		// 1. generic chunk walk
		W3DChunkWalkStats fileStats;
		std::string walkError;
		if (!Walk_W3D_Chunks(bytes.data(), bytes.size(), fileStats, &walkError))
		{
			c.CorruptFiles.insert(lpath);
		}
		if (!fileStats.Unknown.empty())
		{
			c.UnknownIdFiles.insert(lpath);
			c.UnknownIdsByFile[lpath].insert(fileStats.Unknown.begin(), fileStats.Unknown.end());
		}
		for (const auto &kv : fileStats.ById)
		{
			c.FilesWithId[kv.first]++;
		}
		c.Walk.Merge(fileStats);

		// 2. typed loads of the top-level prototypes this lane owns, one fresh reader per chunk
		size_t off = 0;
		while (off + 8 <= bytes.size())
		{
			std::uint32_t id, raw;
			std::memcpy(&id, &bytes[off], 4);
			std::memcpy(&raw, &bytes[off + 4], 4);
			size_t end = off + 8 + (size_t)(raw & 0x7FFFFFFF);
			if (end > bytes.size())
			{
				break;
			}
			if (id == W3D_CHUNK_HIERARCHY)
			{
				ChunkLoadClass cl(&bytes[off], end - off);
				cl.Open_Chunk();
				HTreeClass tree;
				std::string error;
				if (tree.Load_W3D(cl, &error) == HTreeClass::OK)
				{
					c.TreesLoaded++;
					size_t slash = lpath.find_last_of('\\');
					c.TreeDefs.push_back({ lpath.substr(slash + 1), lpath, lowerStr(tree.Get_Name()), tree.Num_Pivots() });
				}
				else
				{
					c.TreeFailures.push_back({ lpath, error });
				}
			}
			else if (id == W3D_CHUNK_ANIMATION || id == W3D_CHUNK_COMPRESSED_ANIMATION)
			{
				processAnimation(c, &bytes[off], end - off, id == W3D_CHUNK_COMPRESSED_ANIMATION, lpath);
			}
			else if (id == W3D_CHUNK_MESH)
			{
				ChunkLoadClass cl(&bytes[off], end - off);
				cl.Open_Chunk();
				MeshModelClass mesh;
				std::string error;
				if (!mesh.Load_W3D(cl, &error))
				{
					c.MeshFailures.push_back({ lpath, error });
				}
				else
				{
					c.MeshesLoaded++;
					c.MeshesWithInfluences += !mesh.Influences.empty();
					c.MeshesDualBone += !mesh.Vertices2.empty();
					c.MeshesWithTangents += !mesh.Tangents.empty();
					c.MeshesWithAABTree += mesh.HasAABTree;
					c.ShaderMaterials += mesh.ShaderMaterials.size();
					for (const MeshShaderMaterialDef &d : mesh.ShaderMaterials)
					{
						c.ShaderMaterialProperties += d.Properties.size();
						c.FxTypes[d.TypeName]++;
					}
					c.MeshPasses += mesh.Passes.size();
					for (const MeshMaterialPassData &p : mesh.Passes)
					{
						c.MeshesWithShaderMaterialIds += !p.ShaderMaterialIds.empty();
					}
					if (!mesh.UnhandledChunks.empty())
					{
						c.MeshesWithUnhandled++;
						for (std::uint32_t u : mesh.UnhandledChunks)
						{
							c.UnhandledMeshChunks[u]++;
						}
					}
					// every vertex inside the header's bounding box (spec 5.4 item 4), within float slack
					bool outside = false;
					const float slack = 1e-3f;
					for (const W3dVectorStruct &v : mesh.Vertices)
					{
						if (v.X < mesh.Header.Min.X - slack || v.X > mesh.Header.Max.X + slack || v.Y < mesh.Header.Min.Y - slack ||
							v.Y > mesh.Header.Max.Y + slack || v.Z < mesh.Header.Min.Z - slack || v.Z > mesh.Header.Max.Z + slack)
						{
							outside = true;
							break;
						}
					}
					(mesh.Is_Skin() ? c.SkinsOutsideBox : c.MeshesOutsideBox) += outside;
					std::map<MeshSurfaceKey, std::vector<std::uint32_t>> groups;
					std::string groupError;
					if (mesh.Group_Triangles(groups, &groupError))
					{
						c.SurfaceGroupsTotal += groups.size();
					}
					else
					{
						c.SurfaceFailures.push_back({ lpath, groupError });
					}
				}
			}
			else if (id == W3D_CHUNK_HLOD)
			{
				ChunkLoadClass cl(&bytes[off], end - off);
				cl.Open_Chunk();
				HLodDefClass hlod;
				std::string error;
				if (hlod.Load_W3D(cl, &error))
				{
					c.HLodsLoaded++;
					c.HLodsWithExtraLod += !hlod.ExtraLod.empty();
					c.HLodAggregateArrays += !hlod.Aggregates.ModelName.empty();
				}
				else
				{
					c.HLodFailures.push_back({ lpath, error });
				}
			}
			else if (id == W3D_CHUNK_EMITTER)
			{
				ChunkLoadClass cl(&bytes[off], end - off);
				cl.Open_Chunk();
				ParticleEmitterDefClass emitter;
				std::string error;
				if (emitter.Load_W3D(cl, &error))
				{
					c.EmittersLoaded++;
					c.EmitterNames.push_back(emitter.Name);
				}
				else
				{
					c.EmitterFailures.push_back({ lpath, error });
				}
			}
			else if (id == W3D_CHUNK_SHDMESH)
			{
				ChunkLoadClass cl(&bytes[off], end - off);
				cl.Open_Chunk();
				ShdMeshDefClass shd;
				std::string error;
				if (shd.Load_W3D(cl, &error))
				{
					c.ShdMeshesLoaded++;
					c.ShdSubMeshes += shd.SubMeshes.size();
				}
				else
				{
					c.EmitterFailures.push_back({ lpath, error });
				}
			}
			else if (id == W3D_CHUNK_BOX)
			{
				if (end - off - 8 == sizeof(W3dBoxStruct))
				{
					c.BoxesLoaded++;
				}
				else
				{
					c.BoxFailures.push_back({ lpath, "W3D_CHUNK_BOX is " + std::to_string(end - off - 8) + " bytes" });
				}
			}
			off = end;
		}
	}
	c.Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	// registry order: catalog order is by lower-cased leaf file name; the first definition of a name wins
	std::stable_sort(c.TreeDefs.begin(), c.TreeDefs.end(), [](const Corpus::TreeDef &a, const Corpus::TreeDef &b) { return a.Leaf < b.Leaf; });
	for (const Corpus::TreeDef &d : c.TreeDefs)
	{
		c.TreePivots.emplace(d.Name, d.Pivots);
	}
	std::printf("[w3d corpus] %zu files, %.1f s\n", c.Files, c.Seconds);
	return c;
}

// ---- the survey ------------------------------------------------------------------------------------

const JsonValue &survey()
{
	static JsonValue root;
	static bool loaded = false;
	if (!loaded)
	{
		loaded = true;
		std::vector<unsigned char> bytes;
		std::string error;
		REQUIRE_MESSAGE(retailtest::readLocalFile(retailtest::dataDir() + "/w3d-survey.json", bytes, &error), error);
		REQUIRE_MESSAGE(JsonValue::parse(std::string(bytes.begin(), bytes.end()), root, &error), error);
	}
	return root;
}

std::uint64_t num(const JsonValue *v)
{
	REQUIRE(v != nullptr);
	REQUIRE(v->isNumber());
	return (std::uint64_t)v->number;
}

// survey histogram section -> Histogram
Histogram surveyHist(const char *section)
{
	Histogram h;
	const JsonValue *s = survey().get(section);
	REQUIRE(s != nullptr);
	for (const auto &kv : s->object)
	{
		h[kv.first] = (std::uint64_t)kv.second.number;
	}
	return h;
}

std::string showHist(const Histogram &h)
{
	std::ostringstream o;
	for (const auto &kv : h)
	{
		o << kv.first << "=" << kv.second << " ";
	}
	return o.str();
}

#define REQUIRE_RETAIL(name)                      \
	if (!retailtest::pureMount())                 \
	{                                             \
		retailtest::printSkip(name);              \
		return;                                   \
	}

} // namespace

TEST_CASE("corpus: 14,475 W3D files in the pure 2.01 + 1.06 mount, every chunk recognised except in the 8 known-corrupt files")
{
	REQUIRE_RETAIL("W3D corpus walk");
	const Corpus &c = corpus();
	CHECK(c.ReadFailures.empty());
	CHECK(c.Files == num(survey().get("files_w3d")));

	// the corrupt-file list is exactly the spec's (section 2.3)
	std::set<std::string> expectedCorrupt;
	for (const JsonValue &f : survey().get("corrupt_files")->array)
	{
		expectedCorrupt.insert(f.string);
	}
	CHECK(expectedCorrupt.size() == 8);
	CHECK(c.CorruptFiles == expectedCorrupt);
	// an id outside the engine's table may only appear in those files (the survey's garbage ids come from them)
	for (const std::string &f : c.UnknownIdFiles)
	{
		std::string ids;
		for (std::uint32_t id : c.UnknownIdsByFile.at(f))
		{
			ids += hex(id) + " ";
		}
		CHECK_MESSAGE(expectedCorrupt.count(f) == 1, "unrecognised chunk ids " << ids << "in " << f);
	}
	std::printf("[w3d corpus] corrupt files: %zu, files with an unrecognised id: %zu\n", c.CorruptFiles.size(), c.UnknownIdFiles.size());
}

TEST_CASE("corpus: per-chunk-id count, byte total and file count equal the survey")
{
	REQUIRE_RETAIL("W3D chunk counts");
	const Corpus &c = corpus();
	const JsonValue *chunks = survey().get("chunks");
	REQUIRE(chunks != nullptr);
	std::set<std::uint32_t> seen;
	for (const auto &kv : chunks->object)
	{
		std::uint32_t id = (std::uint32_t)std::stoul(kv.first, nullptr, 16);
		seen.insert(id);
		auto it = c.Walk.ById.find(id);
		REQUIRE_MESSAGE(it != c.Walk.ById.end(), "chunk " << kv.first << " absent from the walk");
		CHECK_MESSAGE(it->second.Count == num(kv.second.get("count")), "count of chunk " << kv.first);
		CHECK_MESSAGE(it->second.Bytes == num(kv.second.get("bytes")), "bytes of chunk " << kv.first);
		CHECK_MESSAGE(c.FilesWithId.at(id) == num(kv.second.get("files")), "files with chunk " << kv.first);
	}
	for (const auto &kv : c.Walk.ById)
	{
		CHECK_MESSAGE(seen.count(kv.first) == 1, "chunk " << hex(kv.first) << " is not in the survey");
	}
	CHECK(c.Walk.ById.size() == chunks->object.size());
	std::printf("[w3d corpus] %zu distinct chunk ids, all counts equal the survey\n", c.Walk.ById.size());
}

TEST_CASE("corpus: parent -> child chunk pair counts equal the survey")
{
	REQUIRE_RETAIL("W3D chunk nesting");
	const Corpus &c = corpus();
	std::map<std::string, std::uint64_t> mine;
	for (const auto &kv : c.Walk.ByParent)
	{
		std::string parent = kv.first.first == W3DChunkWalkStats::ROOT ? "root" : hex(kv.first.first);
		mine[parent + ">" + hex(kv.first.second)] = kv.second;
	}
	const JsonValue *parents = survey().get("parents");
	REQUIRE(parents != nullptr);
	for (const auto &kv : parents->object)
	{
		auto it = mine.find(kv.first);
		REQUIRE_MESSAGE(it != mine.end(), "pair " << kv.first << " absent");
		CHECK_MESSAGE(it->second == (std::uint64_t)kv.second.number, "pair " << kv.first);
	}
	CHECK(mine.size() == parents->object.size());
}

TEST_CASE("corpus: every hierarchy chunk loads as an HTree, except the one inside a corrupt file")
{
	REQUIRE_RETAIL("HTree corpus load");
	const Corpus &c = corpus();
	std::set<std::string> expectedCorrupt;
	for (const JsonValue &f : survey().get("corrupt_files")->array)
	{
		expectedCorrupt.insert(f.string);
	}
	for (const Failure &f : c.TreeFailures)
	{
		CHECK_MESSAGE(expectedCorrupt.count(f.Path) == 1, f.Path << ": " << f.What);
		std::printf("[w3d corpus] hierarchy failed in %s: %s\n", f.Path.c_str(), f.What.c_str());
	}
	CHECK(c.TreesLoaded + c.TreeFailures.size() == num(survey().get("chunks")->get("0x100")->get("count")));
	// the survey counts 4,611 distinct hierarchy names; the failed one is a name only that corrupt file defines
	CHECK(c.TreePivots.size() + c.TreeFailures.size() == num(survey().get("hierarchies")));
	std::printf("[w3d corpus] %zu hierarchies loaded, %zu failed, %zu distinct names\n", c.TreesLoaded, c.TreeFailures.size(), c.TreePivots.size());
}

TEST_CASE("corpus: every animation decodes; the failures are exactly the animations inside the corrupt files")
{
	REQUIRE_RETAIL("animation corpus decode");
	const Corpus &c = corpus();
	std::set<std::string> expectedCorrupt;
	for (const JsonValue &f : survey().get("corrupt_files")->array)
	{
		expectedCorrupt.insert(f.string);
	}
	for (const Failure &f : c.AnimFailures)
	{
		CHECK_MESSAGE(expectedCorrupt.count(f.Path) == 1, f.Path << ": " << f.What);
		std::printf("[w3d corpus] animation failed in %s: %s\n", f.Path.c_str(), f.What.c_str());
	}
	const JsonValue *chunks = survey().get("chunks");
	CHECK(c.RawAnims + c.CompressedAnims + c.AnimFailures.size() == num(chunks->get("0x200")->get("count")) + num(chunks->get("0x280")->get("count")));
	CHECK(c.MismatchedPerAnim == 0);
	std::printf("[w3d corpus] %zu raw + %zu compressed animations decoded, %zu failed (in corrupt files), %llu channels held\n",
		c.RawAnims, c.CompressedAnims, c.AnimFailures.size(), (unsigned long long)c.DecodedAll.Count);
}

TEST_CASE("corpus: channel shapes read from the bytes equal the survey's histograms")
{
	REQUIRE_RETAIL("animation channel histograms");
	const Corpus &c = corpus();
	// survey keys are python tuple strings: "(type, vlen)", "(flavor, type, vlen)", "(flavor, deltaType, type, vlen)"
	CHECK_MESSAGE(c.PeekAll.Raw == surveyHist("anim_channel"), showHist(c.PeekAll.Raw));
	CHECK_MESSAGE(c.PeekAll.Compressed == surveyHist("canim_channel"), showHist(c.PeekAll.Compressed));
	CHECK_MESSAGE(c.PeekAll.Motion == surveyHist("canim_motion"), showHist(c.PeekAll.Motion));
	// the survey's bit channel keys: raw "flags", compressed "(flavor, flags, default)"
	CHECK_MESSAGE(c.PeekAll.RawBit == surveyHist("anim_bitchannel"), showHist(c.PeekAll.RawBit));
	CHECK_MESSAGE(c.PeekAll.CompressedBit == surveyHist("canim_bitchannel"), showHist(c.PeekAll.CompressedBit));
}

TEST_CASE("corpus: animations bind to the hierarchies the corpus holds")
{
	REQUIRE_RETAIL("animation hierarchy binding");
	const Corpus &c = corpus();
	std::uint64_t missingHierarchy = 0, pivotOverflow = 0;
	for (const AnimFact &a : c.Anims)
	{
		auto it = c.TreePivots.find(lowerStr(a.Hierarchy));
		if (it == c.TreePivots.end())
		{
			missingHierarchy++;
			std::printf("[w3d corpus] animation %s.%s (%s) names a hierarchy no file defines\n", a.Hierarchy.c_str(), a.Name.c_str(), a.Path.c_str());
			continue;
		}
		if (a.MaxPivot >= it->second)
		{
			pivotOverflow++;
			std::printf("[w3d corpus] animation %s.%s drives pivot %d but %s has %d pivots\n", a.Hierarchy.c_str(), a.Name.c_str(), a.MaxPivot, a.Hierarchy.c_str(), it->second);
		}
	}
	// 17 animations name a hierarchy no file defines (spec section 1.3, survey resolution anim.hier MISSING), plus
	// the 2 whose hierarchy sits in the corrupt cuwyrm_cld_skl.w3d and does not load.
	CHECK(missingHierarchy == 19);
	// Spec section 5.4 item 5 says every channel pivot is below the hierarchy's pivot count. It is not: 106
	// animations drive pivots past it (an independent Python pass over the same files agrees on 106). ZH drops
	// such channels with a "please re-export" warning, which is what Load_W3D does when it is given the count.
	CHECK(pivotOverflow == 106);
	std::printf("[w3d corpus] %llu animations without a hierarchy, %llu with channels past the hierarchy's pivots\n",
		(unsigned long long)missingHierarchy, (unsigned long long)pivotOverflow);
}

TEST_CASE("corpus: decoded values are finite and every quaternion key is a unit quaternion")
{
	REQUIRE_RETAIL("animation value checks");
	const Corpus &c = corpus();
	std::printf("[w3d corpus] %llu keys checked, %llu quaternion keys, worst |norm - 1| = %g, %llu bad\n",
		(unsigned long long)c.Norms.KeysChecked, (unsigned long long)c.Norms.QuatKeysChecked, c.Norms.WorstQuatError, (unsigned long long)c.Norms.QuatBad);
	for (const std::string &s : c.Norms.Samples)
	{
		std::printf("[w3d corpus]   %s\n", s.c_str());
	}
	for (const auto &kv : c.Norms.ByCategory)
	{
		std::printf("[w3d corpus]   %-46s %9llu keys, %8llu off by > 1e-3, worst %.5f, mean %.2e\n", kv.first.c_str(),
			(unsigned long long)kv.second.Checked, (unsigned long long)kv.second.Bad, kv.second.Worst,
			kv.second.Checked ? kv.second.Sum / (double)kv.second.Checked : 0.0);
	}
	CHECK(c.Norms.NonFinite == 0);
	// The classic timecoded evaluator hangs in retail (and throws here) before a channel's first key. No retail classic
	// channel starts after frame 0, so that acceptance stop is unreachable for retail data.
	std::printf("[w3d corpus] classic timecoded channels: %llu, starting after frame 0: %llu\n",
		(unsigned long long)c.Norms.ClassicTimecodedChannels, (unsigned long long)c.Norms.ClassicFirstKeyAfterZero);
	CHECK(c.Norms.ClassicFirstKeyAfterZero == 0);
	// Lossless channels store the unit quaternions as floats: every key is a unit quaternion. The delta encodings
	// quantise each component per frame, so the spec's "every quaternion within 1e-3 of unit length" (5.4 item 5)
	// does not hold for them: 11 % of the 4-bit and 22 % of the 8-bit motion keys miss it. What does hold is that the
	// error stays at quantisation level. For the 8-bit encoding this is also the check on the decode rule (flipped
	// top bit, 1/16 scale; spec marks it uncertain): an independent sweep of 12 alternative rules, over 4,000 retail
	// channels, gave mean unit-length errors between 0.0008 (this rule) and 800.
	for (const auto &kv : c.Norms.ByCategory)
	{
		const bool lossless = kv.first.find("delta") == std::string::npos;
		double mean = kv.second.Checked ? kv.second.Sum / (double)kv.second.Checked : 0.0;
		if (lossless)
		{
			CHECK_MESSAGE(kv.second.Bad == 0, kv.first);
			CHECK_MESSAGE(kv.second.Worst < 1e-4, kv.first);
		}
		else
		{
			CHECK_MESSAGE(mean < 1e-3, kv.first << " mean " << mean);
			CHECK_MESSAGE(kv.second.Worst < 0.15, kv.first << " worst " << kv.second.Worst);
		}
	}
	std::printf("[w3d corpus] motion channels with a step flag: %llu, motion channels starting after frame 0: %llu, keys at or past the frame count: %llu\n",
		(unsigned long long)c.StepFlagMotionChannels, (unsigned long long)c.FirstKeyAfterZeroMotion, (unsigned long long)c.KeysBeyondFrames);
}

TEST_CASE("corpus: compressed header versions are exactly 1 and 0x10000, and each goes with the channel kind in its bytes")
{
	REQUIRE_RETAIL("compressed animation forms");
	const Corpus &c = corpus();
	// BFME2 Load_W3D (RVA 0x0018FEE8-0x0018FF01) accepts only versions 1 and 0x10000. The expectation here is the
	// channel kinds actually present in the bytes (0x282 vs 0x284 chunks, read by the test's own peek) and the survey's
	// version counts; it does not repeat the loader's rule.
	std::uint64_t v1 = 0, v10000 = 0;
	for (const AnimFact &a : c.Anims)
	{
		if (!a.Compressed)
		{
			continue;
		}
		CHECK_MESSAGE((a.Version == 1 || a.Version == 0x10000), a.Path << " version " << a.Version);
		if (a.PeekHasMotionChannels)
		{
			CHECK_MESSAGE(a.Version == 0x10000, a.Path << " has 0x284 channels");
			CHECK_FALSE(a.PeekHasClassicChannels);
		}
		if (a.PeekHasClassicChannels)
		{
			CHECK_MESSAGE(a.Version == 1, a.Path << " has 0x282 channels");
		}
		(a.Version == 1 ? v1 : v10000)++;
	}
	const JsonValue *ver = survey().get("canim_version");
	// the survey counts every compressed animation (3,884 + 1,809 = 5,693); the 5 that fail to decode sit in the corrupt files
	CHECK(v1 + v10000 + c.AnimFailures.size() == num(ver->get("0x10000")) + num(ver->get("0x1")));
	CHECK(v10000 <= num(ver->get("0x10000")));
	CHECK(v1 <= num(ver->get("0x1")));
	CHECK(num(ver->get("0x10000")) - v10000 + num(ver->get("0x1")) - v1 == c.AnimFailures.size());
	std::printf("[w3d corpus] compressed animations: %llu version 0x10000 (motion channels), %llu version 1 (classic), %zu failed\n",
		(unsigned long long)v10000, (unsigned long long)v1, c.AnimFailures.size());
}

TEST_CASE("corpus: every mesh loads, with every chunk handled, and the BFME streams match the survey's inventory")
{
	REQUIRE_RETAIL("mesh corpus load");
	const Corpus &c = corpus();
	std::set<std::string> expectedCorrupt;
	for (const JsonValue &f : survey().get("corrupt_files")->array)
	{
		expectedCorrupt.insert(f.string);
	}
	for (const Failure &f : c.MeshFailures)
	{
		CHECK_MESSAGE(expectedCorrupt.count(f.Path) == 1, f.Path << ": " << f.What);
		std::printf("[w3d corpus] mesh failed in %s: %s\n", f.Path.c_str(), f.What.c_str());
	}
	const JsonValue *chunks = survey().get("chunks");
	auto count = [&](const char *id) { return num(chunks->get(id)->get("count")); };
	CHECK(c.MeshesLoaded + c.MeshFailures.size() == count("0x0"));
	CHECK(c.MeshesWithInfluences == count("0xe"));         // 4,323 skins
	CHECK(c.MeshesDualBone == count("0xc00"));             // 1,697 dual-bone skins
	CHECK(c.MeshesWithTangents == count("0x60"));          // 8,437
	CHECK(c.MeshesWithAABTree == count("0x90"));           // 16,043
	CHECK(c.ShaderMaterials == count("0x51"));             // 8,609 (0x50 wraps 8,603 lists)
	CHECK(c.ShaderMaterialProperties == count("0x53"));    // 67,602
	CHECK(c.MeshPasses == count("0x38"));                  // 28,623
	CHECK(c.MeshesWithShaderMaterialIds == count("0x3f")); // 8,603 passes carry a shader material id array
	// the whole mesh scope is interpreted: no chunk is read past unhandled
	CHECK(c.MeshesWithUnhandled == 0);
	for (const auto &kv : c.UnhandledMeshChunks)
	{
		FAIL_CHECK("mesh chunk " << hex(kv.first) << " is unhandled in " << kv.second << " meshes");
	}
	for (const Failure &f : c.SurfaceFailures)
	{
		FAIL_CHECK(f.Path << ": " << f.What);
	}
	std::printf("[w3d corpus] %zu meshes loaded (%zu skins, %zu dual-bone, %zu with tangents, %zu AABTrees), %zu FX materials with %zu properties, %zu passes\n",
		c.MeshesLoaded, c.MeshesWithInfluences, c.MeshesDualBone, c.MeshesWithTangents, c.MeshesWithAABTree, c.ShaderMaterials, c.ShaderMaterialProperties, c.MeshPasses);
	for (const auto &kv : c.FxTypes)
	{
		std::printf("[w3d corpus]   FX shader %s: %llu materials\n", kv.first.c_str(), (unsigned long long)kv.second);
	}
	std::printf("[w3d corpus] vertices outside the header bounding box (+-1e-3): %zu rigid meshes, %zu skins (skin vertices are in bone space, the box is not); %zu surface groups over all pass-0 triangles\n",
		c.MeshesOutsideBox, c.SkinsOutsideBox, c.SurfaceGroupsTotal);
	// Spec 5.4 item 4 ("vertices lie inside the header bbox") holds for rigid meshes and is wrong for skins.
	CHECK(c.MeshesOutsideBox == 0);
}

TEST_CASE("corpus: every HLod and box loads; the extra LOD arrays and aggregates are the counts the survey saw")
{
	REQUIRE_RETAIL("hlod corpus load");
	const Corpus &c = corpus();
	std::set<std::string> expectedCorrupt;
	for (const JsonValue &f : survey().get("corrupt_files")->array)
	{
		expectedCorrupt.insert(f.string);
	}
	for (const Failure &f : c.HLodFailures)
	{
		CHECK_MESSAGE(expectedCorrupt.count(f.Path) == 1, f.Path << ": " << f.What);
	}
	for (const Failure &f : c.BoxFailures)
	{
		FAIL_CHECK(f.Path << ": " << f.What);
	}
	const JsonValue *chunks = survey().get("chunks");
	auto count = [&](const char *id) { return num(chunks->get(id)->get("count")); };
	CHECK(c.HLodsLoaded + c.HLodFailures.size() == count("0x700"));
	CHECK(c.BoxesLoaded == count("0x740"));
	// spec 2.1: 5,972 LOD arrays for 5,934 HLODs whose header says LodCount 1: 38 carry a second array
	CHECK(c.HLodsWithExtraLod == count("0x702") - count("0x700"));
	CHECK(c.HLodsWithExtraLod == 38);
	CHECK(c.HLodAggregateArrays == count("0x705")); // 24
	std::printf("[w3d corpus] %zu HLODs loaded (%zu with an extra LOD array, %zu with aggregates), %zu boxes\n",
		c.HLodsLoaded, c.HLodsWithExtraLod, c.HLodAggregateArrays, c.BoxesLoaded);
}

TEST_CASE("corpus: every emitter and ShdMesh loads")
{
	REQUIRE_RETAIL("emitter and ShdMesh corpus load");
	const Corpus &c = corpus();
	for (const Failure &f : c.EmitterFailures)
	{
		FAIL_CHECK(f.Path << ": " << f.What);
	}
	const JsonValue *chunks = survey().get("chunks");
	CHECK(c.EmittersLoaded == num(chunks->get("0x500")->get("count")));   // 4 emitters
	CHECK(c.ShdMeshesLoaded == num(chunks->get("0xb00")->get("count")));  // 3 ShdMeshes
	CHECK(c.ShdSubMeshes == num(chunks->get("0xb20")->get("count")));     // one sub-mesh each
	std::string names;
	for (const std::string &n : c.EmitterNames)
	{
		names += n + " ";
	}
	std::printf("[w3d corpus] %zu emitters (%s), %zu ShdMeshes with %zu sub-meshes\n", c.EmittersLoaded, names.c_str(), c.ShdMeshesLoaded, c.ShdSubMeshes);
}

// ---- no retail files needed: struct layouts against the survey's byte totals ------------------------------

TEST_CASE("w3d_file.h structs: every fixed-size chunk's retail byte total is count * sizeof(struct)")
{
	struct Fixed { const char *id; size_t size; const char *what; };
	const Fixed fixed[] = {
		{ "0x1f", sizeof(W3dMeshHeader3Struct), "W3dMeshHeader3Struct" },
		{ "0x28", sizeof(W3dMaterialInfoStruct), "W3dMaterialInfoStruct" },
		{ "0x2d", sizeof(W3dVertexMaterialStruct), "W3dVertexMaterialStruct" },
		{ "0x33", sizeof(W3dTextureInfoStruct), "W3dTextureInfoStruct" },
		{ "0x52", sizeof(W3dShaderMaterialHeaderStruct), "W3dShaderMaterialHeaderStruct" },
		{ "0x91", sizeof(W3dMeshAABTreeHeader), "W3dMeshAABTreeHeader" },
		{ "0x101", sizeof(W3dHierarchyStruct), "W3dHierarchyStruct" },
		{ "0x201", sizeof(W3dAnimHeaderStruct), "W3dAnimHeaderStruct" },
		{ "0x281", sizeof(W3dCompressedAnimHeaderStruct), "W3dCompressedAnimHeaderStruct" },
		{ "0x501", sizeof(W3dEmitterHeaderStruct), "W3dEmitterHeaderStruct" },
		{ "0x502", sizeof(W3dEmitterUserInfoStruct), "W3dEmitterUserInfoStruct (every retail emitter has an empty string)" },
		{ "0x503", sizeof(W3dEmitterInfoStruct), "W3dEmitterInfoStruct" },
		{ "0x504", sizeof(W3dEmitterInfoStructV2), "W3dEmitterInfoStructV2" },
		{ "0x509", sizeof(W3dEmitterLinePropertiesStruct), "W3dEmitterLinePropertiesStruct" },
		{ "0x50d", sizeof(W3dEmitterExtraInfoStruct), "W3dEmitterExtraInfoStruct" },
		{ "0x701", sizeof(W3dHLodHeaderStruct), "W3dHLodHeaderStruct" },
		{ "0x703", sizeof(W3dHLodArrayHeaderStruct), "W3dHLodArrayHeaderStruct" },
		{ "0x704", sizeof(W3dHLodSubObjectStruct), "W3dHLodSubObjectStruct" },
		{ "0x740", sizeof(W3dBoxStruct), "W3dBoxStruct" },
		{ "0xb02", sizeof(W3dShdMeshHeaderStruct), "W3dShdMeshHeaderStruct" },
		{ "0xb21", sizeof(W3dShdSubMeshHeaderStruct), "W3dShdSubMeshHeaderStruct" },
	};
	const JsonValue *chunks = survey().get("chunks");
	REQUIRE(chunks != nullptr);
	for (const Fixed &f : fixed)
	{
		const JsonValue *entry = chunks->get(f.id);
		REQUIRE_MESSAGE(entry != nullptr, "chunk " << f.id << " absent from the survey");
		CHECK_MESSAGE(num(entry->get("bytes")) == num(entry->get("count")) * f.size, f.what << ": " << num(entry->get("bytes")) << " bytes in " << num(entry->get("count")) << " chunks, sizeof " << f.size);
	}
	// array chunks: element sizes divide the retail totals
	struct Array { const char *id; size_t element; const char *what; };
	const Array arrays[] = {
		{ "0xe", sizeof(W3dVertInfStruct), "W3dVertInfStruct (BFME layout)" },
		{ "0x2", sizeof(W3dVectorStruct), "vertices" },
		{ "0x3", sizeof(W3dVectorStruct), "normals" },
		{ "0xc00", sizeof(W3dVectorStruct), "VERTICES_2" },
		{ "0xc01", sizeof(W3dVectorStruct), "VERTEX_NORMALS_2" },
		{ "0x60", sizeof(W3dVectorStruct), "tangents" },
		{ "0x61", sizeof(W3dVectorStruct), "bitangents" },
		{ "0x20", sizeof(W3dTriStruct), "W3dTriStruct" },
		{ "0x29", sizeof(W3dShaderStruct), "W3dShaderStruct" },
		{ "0x3b", sizeof(W3dRGBAStruct), "DCG" },
		{ "0x93", sizeof(W3dMeshAABTreeNode), "W3dMeshAABTreeNode" },
		{ "0x103", sizeof(W3dPivotFixupStruct), "W3dPivotFixupStruct" },
		{ "0x102", sizeof(W3dPivotStruct), "W3dPivotStruct" },
	};
	for (const Array &a : arrays)
	{
		const JsonValue *entry = chunks->get(a.id);
		REQUIRE_MESSAGE(entry != nullptr, "chunk " << a.id << " absent from the survey");
		CHECK_MESSAGE(num(entry->get("bytes")) % a.element == 0, a.what << ": total " << num(entry->get("bytes")) << " is not a multiple of " << a.element);
	}
	// the 0xE total is also exactly 8 bytes per vertex of the 4,323 skins' vertices: 18,520,072 / 8 = 2,315,009 = 2,313,356 + 1,653
	CHECK(num(chunks->get("0xe")->get("bytes")) / sizeof(W3dVertInfStruct) == 2313356 + 1653); // weights sum to 100 (2,313,356) or 0/0 (1,653)
}

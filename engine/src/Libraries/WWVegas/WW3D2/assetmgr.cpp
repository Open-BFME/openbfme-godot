// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.

#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <atomic>
#include "Libraries/WWVegas/WWLib/chunkio.h"
#include "Common/ArchiveFileSystem.h"
#include "Common/AsciiString.h"

namespace
{
bool sameName(const std::string &a, const std::string &b)
{
	return AsciiStringUtil::compareNoCase(a, b) == 0;
}

std::string boxName(const W3dBoxStruct &box)
{
	size_t n = 0;
	while (n < sizeof(box.Name) && box.Name[n] != 0)
	{
		++n;
	}
	return std::string(box.Name, n);
}

std::string twoLetterFolder(const std::string &name)
{
	return AsciiStringUtil::lowered(name.substr(0, 2));
}
}

const MeshModelClass *W3DFileContents::Find_Mesh(const std::string &name) const
{
	for (const MeshModelClass &m : Meshes)
	{
		if (sameName(m.Get_Name(), name))
		{
			return &m;
		}
	}
	return nullptr;
}

const HLodDefClass *W3DFileContents::Find_HLod(const std::string &name) const
{
	for (const HLodDefClass &h : HLods)
	{
		if (sameName(h.Name, name))
		{
			return &h;
		}
	}
	return nullptr;
}

const HTreeClass *W3DFileContents::Find_HTree(const std::string &name) const
{
	for (const HTreeClass &h : HTrees)
	{
		if (sameName(h.Get_Name(), name))
		{
			return &h;
		}
	}
	return nullptr;
}

const W3dBoxStruct *W3DFileContents::Find_Box(const std::string &name) const
{
	for (const W3dBoxStruct &b : Boxes)
	{
		if (sameName(boxName(b), name))
		{
			return &b;
		}
	}
	return nullptr;
}

const HAnimClass *W3DFileContents::Find_Animation(const std::string &fullName) const
{
	for (const HRawAnimClass &a : RawAnims)
	{
		if (sameName(a.Get_Name(), fullName))
		{
			return &a;
		}
	}
	for (const HCompressedAnimClass &a : CompressedAnims)
	{
		if (sameName(a.Get_Name(), fullName))
		{
			return &a;
		}
	}
	return nullptr;
}

bool Load_W3D_File(const std::uint8_t *data, size_t size, W3DFileContents &out, std::string *error)
{
	out = W3DFileContents();
	ChunkLoadClass cload(data, size);
	while (cload.Open_Chunk())
	{
		switch (cload.Cur_Chunk_ID())
		{
		case W3D_CHUNK_MESH:
		{
			MeshModelClass mesh;
			if (!mesh.Load_W3D(cload, error))
			{
				return false;
			}
			out.Meshes.push_back(std::move(mesh));
			break;
		}
		case W3D_CHUNK_HIERARCHY:
		{
			HTreeClass tree;
			if (tree.Load_W3D(cload, error) != HTreeClass::OK)
			{
				return false;
			}
			out.HTrees.push_back(std::move(tree));
			break;
		}
		case W3D_CHUNK_HLOD:
		{
			HLodDefClass hlod;
			if (!hlod.Load_W3D(cload, error))
			{
				return false;
			}
			out.HLods.push_back(std::move(hlod));
			break;
		}
		case W3D_CHUNK_BOX:
		{
			W3dBoxStruct box;
			if (cload.Read(&box, sizeof(box)) != sizeof(box))
			{
				if (error)
				{
					*error = "short W3D_CHUNK_BOX";
				}
				return false;
			}
			out.Boxes.push_back(box);
			break;
		}
		case W3D_CHUNK_ANIMATION:
		{
			HRawAnimClass anim;
			if (anim.Load_W3D(cload, -1, error) != HRawAnimClass::OK)
			{
				return false;
			}
			out.RawAnims.push_back(std::move(anim));
			break;
		}
		case W3D_CHUNK_COMPRESSED_ANIMATION:
		{
			HCompressedAnimClass anim;
			if (anim.Load_W3D(cload, -1, error) != HCompressedAnimClass::OK)
			{
				return false;
			}
			out.CompressedAnims.push_back(std::move(anim));
			break;
		}
		case W3D_CHUNK_EMITTER:
		{
			ParticleEmitterDefClass emitter;
			if (!emitter.Load_W3D(cload, error))
			{
				return false;
			}
			out.Emitters.push_back(std::move(emitter));
			break;
		}
		case W3D_CHUNK_SHDMESH:
		{
			ShdMeshDefClass shd;
			if (!shd.Load_W3D(cload, error))
			{
				return false;
			}
			out.ShdMeshes.push_back(std::move(shd));
			break;
		}
		default:
			out.OtherChunks.push_back(cload.Cur_Chunk_ID());
			break;
		}
		cload.Close_Chunk();
	}
	if (cload.Had_Error())
	{
		if (error)
		{
			*error = "malformed top-level chunk";
		}
		return false;
	}
	return true;
}

std::string W3D_Asset_Path(const std::string &assetName)
{
	return "art\\w3d\\" + twoLetterFolder(assetName) + "\\" + AsciiStringUtil::lowered(assetName) + ".w3d";
}

std::string W3D_Compiled_Texture_Path(const std::string &textureName)
{
	std::string stem = AsciiStringUtil::lowered(textureName);
	size_t dot = stem.find_last_of('.');
	if (dot != std::string::npos)
	{
		stem = stem.substr(0, dot);
	}
	return "art\\compiledtextures\\" + twoLetterFolder(stem) + "\\" + stem + ".dds";
}

// ---------------------------------------------------------------------------------------------------------------------

bool ArchiveW3DFileSource::Exists(const std::string &path) const
{
	return Fs.doesFileExist(path);
}

bool ArchiveW3DFileSource::Read(const std::string &path, std::vector<std::uint8_t> &out, std::string *error)
{
	return Fs.readFile(path, out, error);
}

namespace
{
std::atomic<W3DPrototypeEvictFn> g_evictObserver{ nullptr };
}

void W3DSetPrototypeEvictObserver(W3DPrototypeEvictFn fn)
{
	g_evictObserver.store(fn);
}

WW3DAssetManager::~WW3DAssetManager()
{
	if (const W3DPrototypeEvictFn fn = g_evictObserver.load())
	{
		for (const auto &kv : Prototypes)
		{
			if (kv.second)
			{
				fn(kv.second.get());
			}
		}
	}
}

const W3DFileContents *WW3DAssetManager::Load_File(const std::string &path, std::string *error)
{
	const std::string key = AsciiStringUtil::lowered(path);
	auto it = Files.find(key);
	if (it == Files.end())
	{
		Entry entry;
		std::vector<std::uint8_t> bytes;
		std::string readError;
		if (!Source.Read(path, bytes, &readError))
		{
			entry.Error = readError;
			FaultList.push_back(readError);
		}
		else
		{
			std::unique_ptr<W3DFileContents> contents(new W3DFileContents());
			std::string parseError;
			if (!Load_W3D_File(bytes.data(), bytes.size(), *contents, &parseError))
			{
				entry.Error = path + ": " + parseError;
				FaultList.push_back(entry.Error);
			}
			else
			{
				entry.Contents = std::move(contents);
			}
		}
		it = Files.emplace(key, std::move(entry)).first;
	}
	if (!it->second.Contents && error)
	{
		*error = it->second.Error;
	}
	return it->second.Contents.get();
}

// The file named after a stem; nullptr (without an error) when no such file exists. A malformed file is nullptr as well,
// and is remembered in Faults().
const W3DFileContents *WW3DAssetManager::Try_Stem_File(const std::string &stem)
{
	if (stem.empty())
	{
		return nullptr;
	}
	std::string path = W3D_Asset_Path(stem);
	if (!Source.Exists(path))
	{
		return nullptr;
	}
	std::string error;
	return Load_File(path, &error);
}

const HTreeClass *WW3DAssetManager::Get_HTree(const std::string &name, std::string *error)
{
	// registry key "h*name"; the hierarchy prototype loader opens "name.w3d" (HierarchyPrototypeLoad.cpp:118)
	const W3DFileContents *file = Try_Stem_File(AsciiStringUtil::lowered(name));
	const HTreeClass *tree = file ? file->Find_HTree(name) : nullptr;
	if (!tree && error)
	{
		*error = "hierarchy " + name + " is not registered (looked in " + W3D_Asset_Path(AsciiStringUtil::lowered(name)) + ")";
	}
	return tree;
}

const HAnimClass *WW3DAssetManager::Get_HAnim(const std::string &fullName, std::string *error)
{
	// registry key "a*hier.anim"; the loader opens the text after the first '.' (Load_Animation_W3D.cpp:148-157)
	const size_t dot = fullName.find('.');
	auto known = AnimByName.find(fullName);
	if (known == AnimByName.end())
	{
		std::string lower = AsciiStringUtil::lowered(fullName);
		const W3DFileContents *file = dot == std::string::npos ? nullptr : Try_Stem_File(lower.substr(dot + 1));
		known = AnimByName.emplace(fullName, file ? file->Find_Animation(fullName) : nullptr).first;
	}
	const HAnimClass *anim = known->second;
	if (!anim && error)
	{
		*error = "animation " + fullName + " is not registered" + (dot == std::string::npos ? " (animation names are HIERARCHY.ANIM)" : "");
	}
	return anim;
}

bool WW3DAssetManager::Resolve_Animation(const std::string &prefix, const std::string &name, bool numbered, int number, std::string *resolvedName)
{
	std::string candidate;
	if (!prefix.empty())
	{
		candidate = prefix;
		if (numbered) candidate += std::to_string(number);
		candidate += '.';
		candidate += name;
		if (numbered) candidate += std::to_string(number);
	}
	else
	{
		candidate = name;
	}
	std::string error;
	if (Get_HAnim(candidate, &error))
	{
		if (resolvedName) *resolvedName = candidate;
		return true;
	}
	if (numbered && !prefix.empty())
	{
		candidate = prefix + "." + name;
		if (Get_HAnim(candidate, &error))
		{
			if (resolvedName) *resolvedName = candidate;
			return true;
		}
	}
	return false;
}

void WW3DAssetManager::Resolve_Sub_Object(const std::string &name, int bone, bool aggregate, const HTreeClass &tree, RenderSubObject &out)
{
	out = RenderSubObject();
	out.Name = name;
	out.BoneIndex = bone;
	out.Aggregate = aggregate;
	if (bone < 0 || bone >= tree.Num_Pivots())
	{
		out.Reason = "bone " + std::to_string(bone) + " is outside the hierarchy of " + std::to_string(tree.Num_Pivots()) + " pivots";
		return;
	}
	const std::string lower = AsciiStringUtil::lowered(name);
	const size_t dot = lower.find('.');

	// "container.name": meshes and boxes live in the file named after the container (Rva00970EC0Proto_Load,
	// Rva00972480Proto_LoadBox); emitters, bare meshes and HLODs in the file named after the whole name.
	if (dot != std::string::npos)
	{
		if (const W3DFileContents *f = Try_Stem_File(lower.substr(0, dot)))
		{
			if (const MeshModelClass *m = f->Find_Mesh(name))
			{
				out.Type = RenderSubObject::SUB_MESH;
				out.Mesh = m;
				return;
			}
			if (const W3dBoxStruct *b = f->Find_Box(name))
			{
				out.Type = RenderSubObject::SUB_BOX;
				out.Box = b;
				return;
			}
		}
	}
	if (const W3DFileContents *f = Try_Stem_File(lower))
	{
		if (f->Find_HLod(name))
		{
			out.Reason = "names an HLOD (nested HLODs are not supported)";
			return;
		}
		if (const MeshModelClass *m = f->Find_Mesh(name))
		{
			out.Type = RenderSubObject::SUB_MESH;
			out.Mesh = m;
			return;
		}
		if (const W3dBoxStruct *b = f->Find_Box(name))
		{
			out.Type = RenderSubObject::SUB_BOX;
			out.Box = b;
			return;
		}
		for (const ParticleEmitterDefClass &e : f->Emitters)
		{
			if (AsciiStringUtil::compareNoCase(e.Name, name) == 0)
			{
				out.Type = RenderSubObject::SUB_EMITTER;
				out.Emitter = &e;
				return;
			}
		}
	}
	out.Reason = "no prototype of that name is registered (ZH skips such a sub object silently)";
}

const RenderObjPrototype *WW3DAssetManager::Create_Render_Obj(const std::string &name, std::string *error)
{
	const std::string key = AsciiStringUtil::lowered(name);
	auto cached = Prototypes.find(key);
	if (cached != Prototypes.end())
	{
		if (!cached->second && error) *error = "render object " + name + " is not registered";
		return cached->second.get();
	}

	std::unique_ptr<RenderObjPrototype> proto(new RenderObjPrototype());
	proto->Name = name;
	const size_t dot = key.find('.');

	// HLOD: the whole name names the file and the prototype.
	const W3DFileContents *file = Try_Stem_File(key);
	const HLodDefClass *hlod = file ? file->Find_HLod(name) : nullptr;
	if (hlod)
	{
		proto->Type = RenderObjPrototype::PROTO_HLOD;
		proto->HLod = hlod;
		proto->HierarchyName = hlod->HierarchyName;
		proto->LodCount = hlod->Lod.size();
		proto->ExtraLodArrays = hlod->ExtraLod.size();
		std::string treeError;
		const HTreeClass *tree = hlod->HierarchyName.empty() ? nullptr : Get_HTree(hlod->HierarchyName, &treeError);
		if (!tree)
		{
			proto->HierarchyMissing = true;
			proto->DefaultTree = HTreeClass::Make_Default();
			tree = &proto->DefaultTree;
		}
		proto->Tree = tree;

		auto add = [&](const HLodDefClass::SubObjectArrayClass &array, bool aggregate) {
			for (size_t i = 0; i < array.ModelName.size(); ++i)
			{
				RenderSubObject sub;
				Resolve_Sub_Object(array.ModelName[i], array.BoneIndex[i], aggregate, *tree, sub);
				proto->SubObjects.push_back(std::move(sub));
			}
		};
		if (!hlod->Lod.empty())
		{
			add(hlod->Lod.back(), false); // ZH: the top LOD is LodCount - 1
		}
		add(hlod->Aggregates, true);
	}
	else
	{
		// A bare mesh: its own file, up to the first '.' (or the whole name).
		const W3DFileContents *meshFile = dot != std::string::npos ? Try_Stem_File(key.substr(0, dot)) : file;
		const MeshModelClass *mesh = meshFile ? meshFile->Find_Mesh(name) : nullptr;
		if (!mesh)
		{
			Prototypes[key].reset();
			if (error) *error = "render object " + name + " is not registered (no HLOD or mesh of that name in " + W3D_Asset_Path(key) + ")";
			return nullptr;
		}
		proto->Type = RenderObjPrototype::PROTO_MESH;
		proto->DefaultTree = HTreeClass::Make_Default();
		proto->Tree = &proto->DefaultTree;
		RenderSubObject sub;
		sub.Name = name;
		sub.BoneIndex = 0;
		sub.Type = RenderSubObject::SUB_MESH;
		sub.Mesh = mesh;
		proto->SubObjects.push_back(std::move(sub));
	}
	RenderObjPrototype *raw = proto.get();
	Prototypes[key] = std::move(proto);
	return raw;
}

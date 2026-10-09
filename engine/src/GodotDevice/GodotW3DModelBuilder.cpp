// OpenBFME. GPL-3.0.

#include "GodotDevice/GodotW3DModelBuilder.h"
#include "GodotDevice/GodotRetailFileSystem.h"

#include "Common/AsciiString.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"
#include "Libraries/WWVegas/WW3D2/textureloader.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <vector>

namespace godot
{

namespace
{

String toGodot(const std::string &s)
{
	return String::utf8(s.c_str(), (int64_t)s.size());
}

std::string toNative(const String &s)
{
	CharString utf8 = s.utf8();
	return std::string(utf8.get_data(), (size_t)utf8.length());
}

Transform3D toGodot(const Matrix3D &m)
{
	Basis b(m.Row[0][0], m.Row[0][1], m.Row[0][2],
		m.Row[1][0], m.Row[1][1], m.Row[1][2],
		m.Row[2][0], m.Row[2][1], m.Row[2][2]);
	return Transform3D(b, Vector3(m.Row[0][3], m.Row[1][3], m.Row[2][3]));
}

godot::Vector3 toGodot(const ::Vector3 &v)
{
	return godot::Vector3(v.X, v.Y, v.Z);
}

// ZH shader.cpp sets D3DRS_ALPHAREF to 0x60 for alpha-tested shaders.
const float W3D_ALPHA_TEST_REFERENCE = 96.0f / 255.0f;

} // namespace

void W3DModelBuilder::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("build_model", "fs", "model_name"), &W3DModelBuilder::build_model);
	ClassDB::bind_method(D_METHOD("get_report"), &W3DModelBuilder::get_report);
}

Ref<Texture2D> W3DModelBuilder::loadTexture(RetailFileSystem &fs, const std::string &name, Dictionary &meshInfo)
{
	std::string key = AsciiStringUtil::lowered(name);
	// Only successful loads are cached. A failed load is retried and re-reported by every build: caching the
	// null made the second build silently paint the surface magenta with no error in its fresh report.
	auto cached = m_textureCache.find(key);
	if (cached != m_textureCache.end())
	{
		meshInfo["texture_path"] = cached->second->get_meta("retail_path");
		return cached->second;
	}

	// BFME retail keeps unit textures compiled to DDS under a two-letter folder; the W3D
	// still names the source .tga. Loose art\textures\<name> is checked second.
	// TODO(faithfulness): confirm the lookup order against the BFME2 texture loader.
	std::vector<std::string> candidates = { W3D_Compiled_Texture_Path(name), "art\\textures\\" + key };
	Ref<Texture2D> texture;
	for (const std::string &path : candidates)
	{
		if (!fs.existsNative(path))
		{
			continue;
		}
		std::vector<uint8_t> bytes;
		std::string error;
		if (!fs.readBytes(path, bytes, &error))
		{
			((Array)m_report["errors"]).push_back(toGodot(error));
			break;
		}
		PackedByteArray buffer;
		buffer.resize((int64_t)bytes.size());
		memcpy(buffer.ptrw(), bytes.data(), bytes.size());
		Ref<Image> image;
		image.instantiate();
		std::string ext = path.substr(path.size() - 4);
		Error err = ext == ".dds" ? image->load_dds_from_buffer(buffer) : image->load_tga_from_buffer(buffer);
		if (err != OK || image->is_empty())
		{
			((Array)m_report["errors"]).push_back(toGodot("could not decode texture " + path));
			break;
		}
		texture = ImageTexture::create_from_image(image);
		texture->set_meta("retail_path", toGodot(path));
		meshInfo["texture_path"] = toGodot(path);
		break;
	}
	if (texture.is_null())
	{
		((Array)m_report["errors"]).push_back(toGodot("texture not found: " + name + " (tried " + candidates[0] + ", " + candidates[1] + ")"));
		return texture;
	}
	m_textureCache[key] = texture;
	return texture;
}

Node3D *W3DModelBuilder::build_model(const Ref<RetailFileSystem> &fsRef, const String &model_name)
{
	m_report = Dictionary();
	Array errors;
	Array meshes;
	Array skipped;
	m_report["errors"] = errors;
	m_report["meshes"] = meshes;
	m_report["skipped"] = skipped;
	m_report["model"] = model_name;

	auto fail = [&](const std::string &message) -> Node3D * {
		errors.push_back(toGodot(message));
		UtilityFunctions::push_error("W3DModelBuilder: ", toGodot(message));
		return nullptr;
	};

	if (fsRef.is_null() || !fsRef->is_mounted())
	{
		return fail("retail file system is not mounted");
	}
	RetailFileSystem &fs = *fsRef.ptr();
	std::string model = toNative(model_name);

	// Model file
	std::string modelPath = W3D_Asset_Path(model);
	m_report["file"] = toGodot(modelPath);
	std::vector<uint8_t> modelBytes;
	std::string error;
	if (!fs.readBytes(modelPath, modelBytes, &error))
	{
		return fail(error);
	}
	W3DFileContents contents;
	if (!Load_W3D_File(modelBytes.data(), modelBytes.size(), contents, &error))
	{
		return fail(modelPath + ": " + error);
	}
	const HLodDefClass *hlod = contents.Find_HLod(model);
	if (hlod == nullptr)
	{
		return fail(modelPath + " has no HLod named " + model + " (bare-mesh models are not supported yet)");
	}
	if (hlod->Lod.empty())
	{
		return fail("HLod " + model + " has no LOD arrays");
	}

	// Hierarchy: same file, or its own file named after the hierarchy (the _SKL convention).
	HTreeClass tree;
	bool haveTree = false;
	if (!hlod->HierarchyName.empty())
	{
		if (const HTreeClass *local = contents.Find_HTree(hlod->HierarchyName))
		{
			tree = *local;
			haveTree = true;
		}
		else
		{
			std::string treePath = W3D_Asset_Path(hlod->HierarchyName);
			std::vector<uint8_t> treeBytes;
			if (!fs.readBytes(treePath, treeBytes, &error))
			{
				return fail("hierarchy " + hlod->HierarchyName + ": " + error);
			}
			W3DFileContents treeContents;
			if (!Load_W3D_File(treeBytes.data(), treeBytes.size(), treeContents, &error))
			{
				return fail(treePath + ": " + error);
			}
			const HTreeClass *found = treeContents.Find_HTree(hlod->HierarchyName);
			if (found == nullptr)
			{
				return fail(treePath + " does not contain hierarchy " + hlod->HierarchyName);
			}
			tree = *found;
			haveTree = true;
		}
		m_report["hierarchy"] = toGodot(tree.Get_Name());
		m_report["pivot_count"] = tree.Num_Pivots();
	}
	if (!haveTree)
	{
		return fail("HLod " + model + " names no hierarchy");
	}
	tree.Base_Update(Matrix3D());

	Node3D *root = memnew(Node3D);
	root->set_name(model_name);
	Node3D *space = memnew(Node3D);
	space->set_name("W3DSpace");
	// W3D X -> Godot X, W3D Y -> Godot -Z, W3D Z -> Godot Y
	space->set_basis(Basis(godot::Vector3(1, 0, 0), godot::Vector3(0, 0, -1), godot::Vector3(0, 1, 0)));
	root->add_child(space);

	const int lodIndex = (int)hlod->Lod.size() - 1; // highest detail (ZH: top = LodCount-1)
	m_report["lod_index"] = lodIndex;

	std::vector<std::pair<std::string, int>> subObjects;
	for (size_t i = 0; i < hlod->Lod[lodIndex].ModelName.size(); ++i)
	{
		subObjects.push_back({ hlod->Lod[lodIndex].ModelName[i], hlod->Lod[lodIndex].BoneIndex[i] });
	}
	for (size_t i = 0; i < hlod->Aggregates.ModelName.size(); ++i)
	{
		subObjects.push_back({ hlod->Aggregates.ModelName[i], hlod->Aggregates.BoneIndex[i] });
	}

	for (const auto &sub : subObjects)
	{
		const std::string &subName = sub.first;
		int bone = sub.second;
		if (bone < 0 || bone >= tree.Num_Pivots())
		{
			errors.push_back(toGodot("sub-object " + subName + " is on bone " + std::to_string(bone) + " outside the hierarchy"));
			continue;
		}
		const MeshModelClass *mesh = contents.Find_Mesh(subName);
		if (mesh == nullptr)
		{
			if (contents.Find_Box(subName))
			{
				skipped.push_back(toGodot(subName + " (collision box, not drawn)"));
			}
			else
			{
				errors.push_back(toGodot("sub-object " + subName + " is not a mesh or box in " + modelPath));
			}
			continue;
		}

		Dictionary info;
		info["name"] = toGodot(mesh->Get_Name());
		info["header_vertices"] = (int64_t)mesh->Header.NumVertices;
		info["header_triangles"] = (int64_t)mesh->Header.NumTris;
		info["skin"] = mesh->Is_Skin();
		info["hidden"] = mesh->Is_Hidden();
		info["bone"] = bone;
		PackedStringArray unhandled;
		for (uint32 id : mesh->UnhandledChunks)
		{
			unhandled.push_back(String::num_int64(id, 16));
		}
		info["unhandled_chunks"] = unhandled;

		const size_t vcount = mesh->Vertices.size();
		if (mesh->Is_Skin() && mesh->Influences.size() != vcount)
		{
			errors.push_back(toGodot("skin " + mesh->Get_Name() + " has " + std::to_string(mesh->Influences.size()) +
				" influences for " + std::to_string(vcount) + " vertices"));
			continue;
		}

		// Positions / normals: skins deformed into model space by their influence bone.
		PackedVector3Array positions;
		PackedVector3Array normals;
		positions.resize((int64_t)vcount);
		normals.resize((int64_t)vcount);
		bool badBone = false;
		for (size_t v = 0; v < vcount; ++v)
		{
			::Vector3 p(mesh->Vertices[v].X, mesh->Vertices[v].Y, mesh->Vertices[v].Z);
			::Vector3 n(0, 0, 1);
			if (v < mesh->Normals.size())
			{
				n = ::Vector3(mesh->Normals[v].X, mesh->Normals[v].Y, mesh->Normals[v].Z);
			}
			if (mesh->Is_Skin())
			{
				int vb = mesh->Influences[v].BoneIdx;
				if (vb >= tree.Num_Pivots())
				{
					badBone = true;
					break;
				}
				const Matrix3D &tm = tree.Get_Transform(vb);
				p = tm.Transform_Point(p);
				n = tm.Rotate_Vector(n);
			}
			positions.set((int64_t)v, toGodot(p));
			normals.set((int64_t)v, toGodot(n).normalized());
		}
		if (badBone)
		{
			errors.push_back(toGodot("skin " + mesh->Get_Name() + " references a bone outside the hierarchy"));
			continue;
		}
		if (mesh->Normals.size() != vcount)
		{
			errors.push_back(toGodot("mesh " + mesh->Get_Name() + " has no per-vertex normals"));
		}

		// First pass, first texture stage (multi-pass shading is a later port).
		const MeshMaterialPassData *pass = mesh->Passes.empty() ? nullptr : &mesh->Passes[0];
		const MeshTextureStageData *stage = (pass && !pass->Stages.empty()) ? &pass->Stages[0] : nullptr;
		PackedVector2Array uvs;
		bool haveUV = stage && stage->TexCoords.size() == vcount;
		if (haveUV)
		{
			uvs.resize((int64_t)vcount);
			for (size_t v = 0; v < vcount; ++v)
			{
				// ZH meshmdlio: uv = (U, 1 - V)
				uvs.set((int64_t)v, Vector2(stage->TexCoords[v].U, 1.0f - stage->TexCoords[v].V));
			}
		}
		info["pass_count"] = (int64_t)mesh->Passes.size();

		// Group triangles by (texture id, shader id): ZH meshmdlio.cpp:1182 assigns shaders per polygon, so a
		// per-triangle shader array means several surfaces. A pass whose ids cannot be resolved is an error for
		// this mesh; it is never drawn with the first shader (Sol scaffold finding 7).
		std::map<MeshSurfaceKey, std::vector<uint32>> trisBySurface;
		{
			std::string groupError;
			if (!mesh->Group_Triangles(trisBySurface, &groupError))
			{
				errors.push_back(toGodot(groupError));
				continue;
			}
		}

		Ref<ArrayMesh> arrayMesh;
		arrayMesh.instantiate();
		int64_t surfaceVertices = 0;
		int64_t surfaceTriangles = 0;
		PackedStringArray textureNames;
		for (const auto &group : trisBySurface)
		{
			PackedInt32Array indices;
			indices.resize((int64_t)group.second.size() * 3);
			const MeshSurfaceKey &surfaceKey = group.first;
			int64_t k = 0;
			for (uint32 t : group.second)
			{
				const W3dTriStruct &tri = mesh->Triangles[t];
				// W3D front faces are counter-clockwise (right-handed); Godot's are clockwise.
				indices.set(k++, (int32_t)tri.Vindex[0]);
				indices.set(k++, (int32_t)tri.Vindex[2]);
				indices.set(k++, (int32_t)tri.Vindex[1]);
			}
			Array arrays;
			arrays.resize(Mesh::ARRAY_MAX);
			arrays[Mesh::ARRAY_VERTEX] = positions;
			arrays[Mesh::ARRAY_NORMAL] = normals;
			if (haveUV)
			{
				arrays[Mesh::ARRAY_TEX_UV] = uvs;
			}
			arrays[Mesh::ARRAY_INDEX] = indices;
			arrayMesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
			int32_t surface = arrayMesh->get_surface_count() - 1;
			surfaceVertices = arrayMesh->surface_get_array_len(surface);
			surfaceTriangles += arrayMesh->surface_get_array_index_len(surface) / 3;

			Ref<StandardMaterial3D> material;
			material.instantiate();
			if (mesh->Is_Two_Sided())
			{
				material->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
			}
			// This surface's own shader (Group_Triangles bounds-checked the id against the shader table).
			const W3dShaderStruct *shader = surfaceKey.Shader >= 0 ? &mesh->Shaders[(size_t)surfaceKey.Shader] : nullptr;
			if (shader)
			{
				if (shader->AlphaTest == W3DSHADER_ALPHATEST_ENABLE)
				{
					material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA_SCISSOR);
					material->set_alpha_scissor_threshold(W3D_ALPHA_TEST_REFERENCE);
				}
				else if (shader->SrcBlend == W3DSHADER_SRCBLENDFUNC_SRC_ALPHA && shader->DestBlend == W3DSHADER_DESTBLENDFUNC_ONE_MINUS_SRC_ALPHA)
				{
					material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
				}
				else if (shader->SrcBlend == W3DSHADER_SRCBLENDFUNC_ONE && shader->DestBlend == W3DSHADER_DESTBLENDFUNC_ONE)
				{
					material->set_blend_mode(BaseMaterial3D::BLEND_MODE_ADD);
				}
			}
			if (surfaceKey.Texture >= 0) // Group_Triangles bounds-checked the id against the texture table
			{
				const std::string &texName = mesh->Textures[(size_t)surfaceKey.Texture].Name;
				textureNames.push_back(toGodot(texName));
				Ref<Texture2D> texture = loadTexture(fs, texName, info);
				if (texture.is_valid())
				{
					material->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, texture);
				}
				else
				{
					// retail's missing texture (RW 0x53193E, textureloader.h): a 1 x 1 opaque magenta texture, sampled like the real one; the error is in the report
					Ref<Image> img = Image::create_empty(1, 1, false, Image::FORMAT_RGBA8);
					img->fill(Color::hex(((W3D_MISSING_TEXTURE_COLOR & 0x00FFFFFFu) << 8) | (W3D_MISSING_TEXTURE_COLOR >> 24)));
					material->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, ImageTexture::create_from_image(img));
				}
			}
			arrayMesh->surface_set_material(surface, material);
		}
		info["surface_count"] = (int64_t)arrayMesh->get_surface_count();
		info["surface_vertices"] = surfaceVertices;
		info["surface_triangles"] = surfaceTriangles;
		info["textures"] = textureNames;

		MeshInstance3D *instance = memnew(MeshInstance3D);
		instance->set_name(toGodot(mesh->Get_Name())); // Godot turns '.' into '_' in node names
		instance->set_meta("w3d_mesh", toGodot(mesh->Get_Name()));
		instance->set_mesh(arrayMesh);
		// Rigid meshes ride their bone; skins are already deformed into model space.
		if (!mesh->Is_Skin())
		{
			instance->set_transform(toGodot(tree.Get_Transform(bone)));
		}
		instance->set_visible(!mesh->Is_Hidden());
		space->add_child(instance);
		meshes.push_back(info);
	}

	for (int64_t i = 0; i < errors.size(); ++i)
	{
		UtilityFunctions::push_error("W3DModelBuilder: ", errors[i]);
	}
	return root;
}

} // namespace godot

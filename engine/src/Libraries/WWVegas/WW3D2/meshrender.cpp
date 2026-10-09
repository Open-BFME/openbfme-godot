// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// See meshrender.h for what is ported and what is not.

#include "Libraries/WWVegas/WW3D2/meshrender.h"
#include "Libraries/WWVegas/WW3D2/mapper.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <stdexcept>
#include <tuple>

namespace
{

bool fail(std::string *error, const MeshModelClass &mesh, const std::string &what)
{
	if (error)
	{
		*error = "mesh " + mesh.Get_Name() + ": " + what;
	}
	return false;
}

// 1 value for every item, or exactly one per item; empty is "none". Anything else is malformed.
bool expandIds(const std::vector<std::uint32_t> &ids, size_t items, const char *what, const MeshModelClass &mesh, std::string *error,
	std::vector<std::int64_t> &out, size_t tableSize, bool allOnesIsNone)
{
	out.assign(items, -1);
	if (ids.empty())
	{
		return true;
	}
	if (ids.size() != 1 && ids.size() != items)
	{
		return fail(error, mesh, std::string(what) + " has " + std::to_string(ids.size()) + " ids for " + std::to_string(items) +
			" items (one id, or one per item)");
	}
	for (size_t i = 0; i < items; ++i)
	{
		std::uint32_t id = ids.size() == 1 ? ids[0] : ids[i];
		if (allOnesIsNone && id == 0xFFFFFFFFu)
		{
			continue; // see MeshModelClass::Group_Triangles: two retail meshes carry "no texture" this way
		}
		if (id >= tableSize)
		{
			return fail(error, mesh, std::string(what) + " id " + std::to_string(id) + " is outside its table of " + std::to_string(tableSize));
		}
		out[i] = id;
	}
	return true;
}

Vector3 rotate(const Matrix3D &m, const Vector3 &v) { return m.Rotate_Vector(v); }

} // namespace

std::uint32_t MeshRenderData::TotalSurfaceTriangles() const
{
	std::uint32_t n = 0;
	for (const MeshDrawSurface &s : Surfaces)
	{
		n += (std::uint32_t)s.Triangles.size();
	}
	return n;
}

bool Build_Mesh_Render_Data(const MeshModelClass &mesh, MeshRenderData &out, std::string *error)
{
	out = MeshRenderData();
	const size_t nv = mesh.Vertices.size();
	const size_t nt = mesh.Triangles.size();

	out.Name = mesh.Get_Name();
	out.Attributes = mesh.Header.Attributes;
	out.Skin = mesh.Is_Skin();
	out.TwoSided = mesh.Is_Two_Sided();
	out.Hidden = mesh.Is_Hidden();
	out.CastShadow = (mesh.Header.Attributes & W3D_MESH_FLAG_CAST_SHADOW) != 0;
	out.SortLevel = mesh.Header.SortLevel;
	out.NumVertices = nv;
	out.NumTriangles = nt;
	const std::uint32_t geometry = mesh.Header.Attributes & W3D_MESH_FLAG_GEOMETRY_TYPE_MASK;
	if (geometry == W3D_MESH_FLAG_GEOMETRY_TYPE_CAMERA_ALIGNED)
	{
		out.Camera = MESH_CAMERA_ALIGNED;
	}
	else if (geometry == W3D_MESH_FLAG_GEOMETRY_TYPE_CAMERA_ORIENTED)
	{
		out.Camera = MESH_CAMERA_ORIENTED;
	}
	out.BoundsMin[0] = mesh.Header.Min.X;
	out.BoundsMin[1] = mesh.Header.Min.Y;
	out.BoundsMin[2] = mesh.Header.Min.Z;
	out.BoundsMax[0] = mesh.Header.Max.X;
	out.BoundsMax[1] = mesh.Header.Max.Y;
	out.BoundsMax[2] = mesh.Header.Max.Z;

	// ---- vertex streams ----
	out.Position.resize(nv * 3);
	out.Normal.assign(nv * 3, 0.0f);
	for (size_t v = 0; v < nv; ++v)
	{
		out.Position[v * 3 + 0] = mesh.Vertices[v].X;
		out.Position[v * 3 + 1] = mesh.Vertices[v].Y;
		out.Position[v * 3 + 2] = mesh.Vertices[v].Z;
	}
	if (mesh.Normals.size() == nv)
	{
		for (size_t v = 0; v < nv; ++v)
		{
			out.Normal[v * 3 + 0] = mesh.Normals[v].X;
			out.Normal[v * 3 + 1] = mesh.Normals[v].Y;
			out.Normal[v * 3 + 2] = mesh.Normals[v].Z;
		}
	}
	else if (!mesh.Normals.empty())
	{
		return fail(error, mesh, "normal stream length differs from the vertex count");
	}
	else
	{
		return fail(error, mesh, "no vertex normals");
	}

	if (out.Skin)
	{
		if (mesh.Influences.size() != nv)
		{
			return fail(error, mesh, "skin has " + std::to_string(mesh.Influences.size()) + " influences for " + std::to_string(nv) + " vertices");
		}
		out.Bone0.resize(nv);
		out.Bone1.resize(nv);
		out.Weight0.resize(nv);
		out.Weight1.resize(nv);
		for (size_t v = 0; v < nv; ++v)
		{
			const W3dVertInfStruct &inf = mesh.Influences[v];
			out.Bone0[v] = inf.BoneIdx;
			out.Bone1[v] = inf.Bone1Idx;
			unsigned w0 = inf.Weight0, w1 = inf.Weight1;
			if (w0 + w1 == 0)
			{
				out.ZeroWeightVertices++;
				w0 = 100; // OpenSAGE's reading of 0/0, not a retail fact (header)
			}
			else if (w0 + w1 != 100)
			{
				return fail(error, mesh, "influence of vertex " + std::to_string(v) + " has weights " + std::to_string(w0) + " + " + std::to_string(w1) +
					" (the retail corpus always sums to 100)");
			}
			out.Weight0[v] = (float)w0 / 100.0f;
			out.Weight1[v] = (float)w1 / 100.0f;
		}
		bool splitWeights = false;
		for (size_t v = 0; v < nv; ++v)
		{
			splitWeights = splitWeights || out.Weight1[v] > 0.0f;
		}
		if (splitWeights && mesh.Vertices2.empty())
		{
			return fail(error, mesh, "a vertex is weighted to a second bone but the mesh has no second-bone streams");
		}
		if (!mesh.Vertices2.empty())
		{
			if (mesh.Vertices2.size() != nv || mesh.Normals2.size() != nv)
			{
				return fail(error, mesh, "second-bone streams do not match the vertex count");
			}
			out.DualBone = true;
			out.Position1.resize(nv * 3);
			out.Normal1.resize(nv * 3);
			for (size_t v = 0; v < nv; ++v)
			{
				out.Position1[v * 3 + 0] = mesh.Vertices2[v].X;
				out.Position1[v * 3 + 1] = mesh.Vertices2[v].Y;
				out.Position1[v * 3 + 2] = mesh.Vertices2[v].Z;
				out.Normal1[v * 3 + 0] = mesh.Normals2[v].X;
				out.Normal1[v * 3 + 1] = mesh.Normals2[v].Y;
				out.Normal1[v * 3 + 2] = mesh.Normals2[v].Z;
			}
		}
	}
	else if (!mesh.Influences.empty() || !mesh.Vertices2.empty())
	{
		return fail(error, mesh, "a mesh that is not a skin carries influence or second-bone streams");
	}
	if (!mesh.Tangents.empty())
	{
		if (mesh.Tangents.size() != nv || mesh.Bitangents.size() != nv)
		{
			return fail(error, mesh, "tangent streams do not match the vertex count");
		}
		out.Tangent.resize(nv * 3);
		out.Bitangent.resize(nv * 3);
		for (size_t v = 0; v < nv; ++v)
		{
			out.Tangent[v * 3 + 0] = mesh.Tangents[v].X;
			out.Tangent[v * 3 + 1] = mesh.Tangents[v].Y;
			out.Tangent[v * 3 + 2] = mesh.Tangents[v].Z;
			out.Bitangent[v * 3 + 0] = mesh.Bitangents[v].X;
			out.Bitangent[v * 3 + 1] = mesh.Bitangents[v].Y;
			out.Bitangent[v * 3 + 2] = mesh.Bitangents[v].Z;
		}
	}

	// ---- passes ----
	if (mesh.MaterialInfo.PassCount != 0 && mesh.MaterialInfo.PassCount != mesh.Passes.size())
	{
		return fail(error, mesh, "MATERIAL_INFO declares " + std::to_string(mesh.MaterialInfo.PassCount) + " passes, the file holds " +
			std::to_string(mesh.Passes.size()));
	}
	out.PassDCG.resize(mesh.Passes.size());

	auto uvStream = [&](int pass, int stage, const std::vector<W3dTexCoordStruct> &tc, int &index) -> bool {
		if (tc.size() != nv)
		{
			return fail(error, mesh, "pass " + std::to_string(pass) + " stage " + std::to_string(stage) + " has " + std::to_string(tc.size()) +
				" texture coordinates for " + std::to_string(nv) + " vertices");
		}
		MeshUVStream s;
		s.Pass = pass;
		s.Stage = stage;
		s.UV.resize(nv * 2);
		for (size_t v = 0; v < nv; ++v)
		{
			s.UV[v * 2 + 0] = tc[v].U;
			s.UV[v * 2 + 1] = 1.0f - tc[v].V; // ZH meshmdlio.cpp:1513
		}
		index = (int)out.PassUV.size();
		out.PassUV.push_back(std::move(s));
		return true;
	};

	// UV arrays in load order (ZH MeshMatDescClass::Install_UV_Array): array 0 is the first texture coordinate chunk of the mesh.
	std::vector<std::array<int, 2>> uvOfPass(mesh.Passes.size(), std::array<int, 2>{ { -1, -1 } });
	for (size_t p = 0; p < mesh.Passes.size(); ++p)
	{
		const MeshMaterialPassData &pass = mesh.Passes[p];
		if (!pass.ShaderMaterialIds.empty())
		{
			if (!pass.TexCoords.empty() && !uvStream((int)p, 0, pass.TexCoords, uvOfPass[p][0]))
			{
				return false;
			}
			continue;
		}
		for (size_t st = 0; st < pass.Stages.size() && st < 2; ++st)
		{
			if (!pass.Stages[st].TexCoords.empty() && !uvStream((int)p, (int)st, pass.Stages[st].TexCoords, uvOfPass[p][st]))
			{
				return false;
			}
		}
	}

	for (size_t p = 0; p < mesh.Passes.size(); ++p)
	{
		const MeshMaterialPassData &pass = mesh.Passes[p];
		const int passIndex = (int)p;

		if (!pass.DCG.empty())
		{
			if (pass.DCG.size() != nv)
			{
				return fail(error, mesh, "pass " + std::to_string(p) + " DCG has " + std::to_string(pass.DCG.size()) + " colours for " + std::to_string(nv) + " vertices");
			}
			std::vector<std::uint8_t> &dcg = out.PassDCG[p];
			dcg.resize(nv * 4);
			for (size_t v = 0; v < nv; ++v)
			{
				dcg[v * 4 + 0] = pass.DCG[v].R;
				dcg[v * 4 + 1] = pass.DCG[v].G;
				dcg[v * 4 + 2] = pass.DCG[v].B;
				dcg[v * 4 + 3] = pass.DCG[v].A;
			}
		}

		const bool fx = !pass.ShaderMaterialIds.empty();
		std::vector<std::int64_t> shaders, fxIds, tex[2];
		std::vector<std::int64_t> vmatPerVertex;
		if (fx)
		{
			if (!expandIds(pass.ShaderMaterialIds, nt, "SHADER_MATERIAL_ID", mesh, error, fxIds, mesh.ShaderMaterials.size(), false))
			{
				return false;
			}
		}
		else
		{
			if (!expandIds(pass.ShaderIds, nt, "SHADER_IDS", mesh, error, shaders, mesh.Shaders.size(), false))
			{
				return false;
			}
			for (int s = 0; s < 2; ++s)
			{
				if (s < (int)pass.Stages.size())
				{
					if (!expandIds(pass.Stages[s].TextureIds, nt, "TEXTURE_IDS", mesh, error, tex[s], mesh.Textures.size(), true))
					{
						return false;
					}
				}
				else
				{
					tex[s].assign(nt, -1);
				}
			}
			if (pass.Stages.size() > 2)
			{
				return fail(error, mesh, "pass " + std::to_string(p) + " has " + std::to_string(pass.Stages.size()) + " texture stages (ZH MAX_TEX_STAGES is 2)");
			}
			if (!expandIds(pass.VertexMaterialIds, nv, "VERTEX_MATERIAL_IDS", mesh, error, vmatPerVertex, mesh.VertexMaterials.size(), false))
			{
				return false;
			}
		}

		int uvIndex[2] = { uvOfPass[p][0], uvOfPass[p][1] };

		// triangles -> surfaces
		typedef std::tuple<std::int64_t, std::int64_t, std::int64_t, std::int64_t, std::int64_t> Key; // shader, vmat, tex0, tex1, fx
		std::map<Key, std::vector<std::uint32_t>> groups;
		for (size_t t = 0; t < nt; ++t)
		{
			const W3dTriStruct &tri = mesh.Triangles[t];
			Key k;
			if (fx)
			{
				k = Key(-1, -1, -1, -1, fxIds[t]);
			}
			else
			{
				std::int64_t vm = vmatPerVertex.empty() ? -1 : vmatPerVertex[tri.Vindex[0]]; // dx8renderer.cpp:959
				k = Key(shaders[t], vm, tex[0][t], tex[1][t], -1);
			}
			groups[k].push_back((std::uint32_t)t);
		}

		for (const auto &g : groups)
		{
			MeshDrawSurface surf;
			surf.Pass = passIndex;
			surf.Kind = fx ? MATERIAL_FX : MATERIAL_CLASSIC;
			surf.Triangles = g.second;
			surf.Indices.reserve(g.second.size() * 3);
			for (std::uint32_t t : g.second)
			{
				for (int k = 0; k < 3; ++k)
				{
					surf.Indices.push_back(mesh.Triangles[t].Vindex[k]);
				}
			}
			surf.SortLevel = out.SortLevel;
			surf.HasDCG = !out.PassDCG[p].empty();
			surf.StageUVStream[0] = uvIndex[0];
			surf.StageUVStream[1] = uvIndex[1];

			const std::int64_t shaderId = std::get<0>(g.first);
			const std::int64_t vmatId = std::get<1>(g.first);
			if (fx)
			{
				surf.ShaderMaterialId = (int)std::get<4>(g.first);
			}
			else
			{
				if (shaderId >= 0)
				{
					surf.Shader = mesh.Shaders[(size_t)shaderId];
				}
				else
				{
					// ZH ShaderClass::Reset(): opaque, modulate, no texturing. A textured stage without a shader is malformed.
					if (std::get<2>(g.first) >= 0 || std::get<3>(g.first) >= 0)
					{
						return fail(error, mesh, "pass " + std::to_string(p) + " has a texture but no shader id");
					}
					W3dShaderStruct d = {};
					d.DepthCompare = 3;
					d.DepthMask = 1;
					d.ColorMask = 1;
					d.PriGradient = W3D_GRADIENT_MODULATE;
					d.SrcBlend = W3DSHADER_SRCBLENDFUNC_ONE;
					d.DestBlend = W3DSHADER_DESTBLENDFUNC_ZERO;
					d.Texturing = 0;
					surf.Shader = d;
				}
				surf.State = Translate_W3D_Shader(surf.Shader);
				surf.Sorted = (p == 0 && W3D_Shader_Is_Sorted(surf.Shader) && out.SortLevel == 0) || out.SortLevel > 0;
				if (vmatId >= 0)
				{
					surf.HasVertexMaterial = true;
					surf.VertexMaterialId = (int)vmatId;
					surf.VertexMaterial = mesh.VertexMaterials[(size_t)vmatId].Info;
				}
				const std::int64_t stageTexture[2] = { std::get<2>(g.first), std::get<3>(g.first) };
				for (int s = 0; s < 2; ++s)
				{
					const std::int64_t texId = stageTexture[s];
					W3DTexGen texGen = W3D_TEXGEN_UV;
					if (surf.HasVertexMaterial)
					{
						const MeshVertexMaterialDef &vm = mesh.VertexMaterials[(size_t)vmatId];
						const std::uint32_t attr = vm.Info.Attributes;
						surf.Stage[s].MapperType = s == 0 ? (int)((attr >> 16) & 0xFF) : (int)((attr >> 8) & 0xFF);
						surf.Stage[s].MapperArgs = s == 0 ? vm.MapperArgs0 : vm.MapperArgs1;
						// Build one now: bad arguments are an error at load time, as ZH's would be at first draw.
						std::string mapperError;
						bool unknownType = false;
						std::unique_ptr<TextureMapperClass> probe = Create_Texture_Mapper(surf.Stage[s].MapperType, (unsigned)s,
							surf.Stage[s].MapperArgs, 0, MapperRandom(), &mapperError, &unknownType);
						if (!mapperError.empty())
						{
							return fail(error, mesh, "vertex material " + vm.Name + " stage " + std::to_string(s) + ": " + mapperError);
						}
						if (probe)
						{
							texGen = probe->Get_TexGen();
						}
					}
					if (texId >= 0)
					{
						surf.Stage[s].HasTexture = true;
						surf.Stage[s].TextureName = mesh.Textures[(size_t)texId].Name;
						surf.Stage[s].HasTextureInfo = mesh.Textures[(size_t)texId].HasInfo;
						surf.Stage[s].TextureInfo = mesh.Textures[(size_t)texId].Info;
						// A texgen mapper (environment, edge, screen: ZH mapper.cpp Apply, D3DTSS_TEXCOORDINDEX) makes its own
						// coordinates. Any other stage without its own array reads UV array 0 (ZH meshmatdesc.cpp Configure_Material:
						// UVSource -1 becomes 0); a mesh with no array at all has no coordinates to read, which is reported.
						if (uvIndex[s] < 0 && texGen == W3D_TEXGEN_UV)
						{
							if (out.PassUV.empty())
							{
								out.Warnings.push_back("pass " + std::to_string(p) + " stage " + std::to_string(s) + " has texture " + surf.Stage[s].TextureName +
									" but the mesh has no texture coordinates at all (D3D reads zeros)");
							}
							else
							{
								surf.StageUVStream[s] = 0;
							}
						}
					}
				}
			}
			out.Surfaces.push_back(std::move(surf));
		}
	}
	return true;
}

namespace
{
size_t checkedBone(int bone, const HTreePose &pose)
{
	if (bone < 0 || bone >= pose.Num_Pivots())
	{
		throw std::out_of_range("bone " + std::to_string(bone) + " is outside a pose of " + std::to_string(pose.Num_Pivots()) + " pivots");
	}
	return (size_t)bone;
}
} // namespace

Vector3 Deform_Position(const MeshRenderData &mesh, size_t v, const HTreePose &pose, int rigidBone)
{
	const Vector3 p0(mesh.Position[v * 3], mesh.Position[v * 3 + 1], mesh.Position[v * 3 + 2]);
	if (!mesh.Skin)
	{
		return pose.Transform[checkedBone(rigidBone, pose)].Transform_Point(p0);
	}
	const Vector3 a = pose.Transform[checkedBone(mesh.Bone0[v], pose)].Transform_Point(p0);
	const float w0 = mesh.Weight0[v];
	const float w1 = mesh.Weight1[v];
	if (w1 == 0.0f)
	{
		return Vector3(a.X * w0, a.Y * w0, a.Z * w0);
	}
	const Vector3 p1 = mesh.DualBone ? Vector3(mesh.Position1[v * 3], mesh.Position1[v * 3 + 1], mesh.Position1[v * 3 + 2]) : p0;
	const Vector3 b = pose.Transform[checkedBone(mesh.Bone1[v], pose)].Transform_Point(p1);
	return Vector3(a.X * w0 + b.X * w1, a.Y * w0 + b.Y * w1, a.Z * w0 + b.Z * w1);
}

Vector3 Deform_Normal(const MeshRenderData &mesh, size_t v, const HTreePose &pose, int rigidBone)
{
	const Vector3 n0(mesh.Normal[v * 3], mesh.Normal[v * 3 + 1], mesh.Normal[v * 3 + 2]);
	if (!mesh.Skin)
	{
		return rotate(pose.Transform[checkedBone(rigidBone, pose)], n0);
	}
	const Vector3 a = rotate(pose.Transform[checkedBone(mesh.Bone0[v], pose)], n0);
	const float w0 = mesh.Weight0[v];
	const float w1 = mesh.Weight1[v];
	if (w1 == 0.0f)
	{
		return Vector3(a.X * w0, a.Y * w0, a.Z * w0);
	}
	const Vector3 n1 = mesh.DualBone ? Vector3(mesh.Normal1[v * 3], mesh.Normal1[v * 3 + 1], mesh.Normal1[v * 3 + 2]) : n0;
	const Vector3 b = rotate(pose.Transform[checkedBone(mesh.Bone1[v], pose)], n1);
	return Vector3(a.X * w0 + b.X * w1, a.Y * w0 + b.Y * w1, a.Z * w0 + b.Z * w1);
}

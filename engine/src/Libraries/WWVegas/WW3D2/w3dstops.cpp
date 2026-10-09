// OpenBFME infrastructure: see w3dstops.h. GPL-3.0.

#include "Libraries/WWVegas/WW3D2/w3dstops.h"

#include "Common/AsciiString.h"
#include "GameEngineDevice/W3DDevice/GameClient/HouseColor.h"
#include "Libraries/WWVegas/WW3D2/mapper.h"

std::string W3D_Stop_Message(const std::string &id, const std::string &detail)
{
	return "[" + id + "] " + detail;
}

namespace
{
void add(std::vector<W3DStopHit> &out, const char *id, const std::string &detail, size_t count = 1)
{
	W3DStopHit h;
	h.Id = id;
	h.Message = W3D_Stop_Message(id, detail);
	h.Count = count;
	out.push_back(std::move(h));
}
} // namespace

void W3D_Collect_Mesh_Stops(const MeshRenderData &mesh, std::vector<W3DStopHit> &out)
{
	if (mesh.ZeroWeightVertices > 0)
	{
		add(out, "S-020",
			std::to_string(mesh.ZeroWeightVertices) + " vertices of " + mesh.Name + " have both skin weights 0; drawn with weight 100 on their first bone (OpenSAGE's reading, not a retail fact)",
			mesh.ZeroWeightVertices);
	}
	if (mesh.DualBone)
	{
		add(out, "S-021",
			mesh.Name + " blends two bones per vertex with w0*M0*p0 + w1*M1*p1 (spec reading of the file; the binary's blend code was not located)", mesh.NumVertices);
	}
	for (const std::string &w : mesh.Warnings)
	{
		// the only warning the builder raises is the texture-without-coordinates case
		add(out, "S-025", w);
	}
}

void W3D_Collect_Surface_Stops(const MeshRenderData &mesh, const MeshDrawSurface &surf, const MeshModelClass &source, const HouseColorTable *houseColors,
	std::vector<W3DStopHit> &out)
{
	std::vector<std::string> textures;
	if (surf.Kind == MATERIAL_CLASSIC)
	{
		for (int s = 0; s < 2; ++s)
			if (surf.Stage[s].HasTexture) textures.push_back(surf.Stage[s].TextureName);
	}
	else
	{
		const MeshShaderMaterialDef &fx = source.ShaderMaterials[(size_t)surf.ShaderMaterialId];
		// RENDER-1: every FX surface draws with normalmapped.fxo's lighting (decoded from Shaders.big); exact for normalmapped.fx except the
		// cloud / shadow / fog terms (S-390), an approximation for the other effects (simple, default, watershader, ...)
		if (AsciiStringUtil::lowered(fx.TypeName) == "normalmapped.fx")
			add(out, "S-023", mesh.Name + ": FX material " + fx.TypeName + " lit like normalmapped.fxo without its cloud, shadow and fog terms");
		else if (AsciiStringUtil::lowered(fx.TypeName) == "simple.fx") // RENDER-2: decoded (simple.fxo blobs 0 / 1)
			add(out, "S-023", mesh.Name + ": FX material " + fx.TypeName + " drawn as simple.fxo (unlit Texture_0 * ColorEmissive, the UV transform) without its fog; the pass "
				"states AlphaBlendEnable / ZWriteEnable are taken from the AlphaBlendingEnable / DepthWriteEnable parameters and Time is taken as seconds (inference)");
		else
			add(out, "S-023", mesh.Name + ": FX material " + fx.TypeName + " drawn with normalmapped.fxo's lighting (its own effect is not recovered)");
		for (const MeshShaderMaterialProperty &p : fx.Properties)
		{
			if (AsciiStringUtil::lowered(p.Name) == "diffusetexture" && !p.StringValue.empty()) textures.push_back(p.StringValue);
		}
	}

	if (houseColors)
	{
		for (const std::string &t : textures)
		{
			if (const std::string *house = houseColors->Find(t))
			{
				add(out, "S-022", mesh.Name + ": texture " + t + " has house colour texture " + *house + " in housecolor.ini; the team colour is NOT applied (combine unrecovered)");
			}
		}
	}

	if (surf.Kind == MATERIAL_CLASSIC)
	{
		const W3DRenderState &st = surf.State;
		if (st.Divergent) add(out, "S-026", mesh.Name + ": " + st.Divergence);
		if (st.PriGradient == W3D_GRADIENT_BUMPENVMAP || st.PriGradient == W3D_GRADIENT_BUMPENVMAPLUMINANCE)
			add(out, "S-026", mesh.Name + ": BUMPENVMAP gradient drawn with the diffuse colour only (ZH fallback without BUMPENV caps)");

		if (surf.HasVertexMaterial)
		{
			for (int s = 0; s < 2; ++s)
			{
				if (!surf.Stage[s].HasTexture) continue;
				std::string error;
				std::unique_ptr<TextureMapperClass> m = Create_Texture_Mapper(surf.Stage[s].MapperType, (unsigned)s, surf.Stage[s].MapperArgs, 0, MapperRandom(), &error, nullptr);
				if (!m) continue; // the material reports the construction error itself
				if (m->Is_Time_Variant())
				{
					add(out, "S-027", mesh.Name + ": stage " + std::to_string(s) + " mapper (type " + std::to_string(surf.Stage[s].MapperType) +
						") animates with a phase shared by every instance of the material; retail keeps it per render object");
				}
				if (surf.Stage[s].MapperType == W3D_MAPPING_RANDOM)
				{
					add(out, "S-027", mesh.Name + ": stage " + std::to_string(s) + " RANDOM mapper draws from a generator that is not retail's Random4Class sequence");
				}
			}
		}
	}
}

W3DStopHit W3D_Bare_Clip_Stop(const std::string &clipName, const std::string &hierarchy, const std::string &resolved)
{
	W3DStopHit h;
	h.Id = "S-024";
	h.Message = W3D_Stop_Message("S-024", "clip \"" + clipName + "\" has no hierarchy prefix; the model's hierarchy " + hierarchy + " was assumed, giving " + resolved +
		" (what retail's bfmePrepVNV and call site do to the name was not recovered)");
	return h;
}

W3DStopHit W3D_Pose_Cull_Stop()
{
	W3DStopHit h;
	h.Id = "S-1730";
	h.Message = W3D_Stop_Message("S-1730", "poses of instances outside the camera's frustum are left pending (pose culling, presentation only): the bound tested is "
		"1.5 x (the farthest bind-pose pivot + the largest mesh extent) + 30 units, not retail's render-object bounds; an animation that reaches beyond it at the "
		"frustum's edge would show its last pose there");
	return h;
}

float W3D_Pose_Cull_Radius(float farthestPivot, float largestMeshExtent)
{
	return (farthestPivot + largestMeshExtent) * 1.5f + 30.0f;
}

W3DStopHit W3D_Pose_Order_Stop(size_t instances)
{
	W3DStopHit h;
	h.Id = "S-028";
	h.Count = instances;
	h.Message = W3D_Stop_Message("S-028", std::to_string(instances) +
		" pose(s) evaluated by the raw / blend arm with the classic arm's float summation order (the arm's own order is not transcribed)");
	return h;
}

W3DStopHit W3D_Fade_Dither_Stop(size_t instances)
{
	W3DStopHit h;
	h.Id = "S-029";
	h.Count = instances;
	h.Message = W3D_Stop_Message("S-029", std::to_string(instances) +
		" instance(s) of opaque surfaces drawn with a screen-door dither for their pivot fade; retail's alpha-blended fade pass is not reproduced");
	return h;
}

W3DStopHit W3D_Batch_Order_Stop(size_t pairs)
{
	W3DStopHit h;
	h.Id = "S-029";
	h.Count = pairs;
	h.Message = W3D_Stop_Message("S-029", "blended draw batches with interleaved depths (equal render priority) are drawn batch by batch, not back to front across batches; "
		"Godot draws each MultiMesh as one instanced draw (instances inside one batch are sorted)");
	return h;
}

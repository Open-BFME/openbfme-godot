// OpenBFME unit tests: every mesh of the retail corpus through the render pipeline. GPL-3.0.
//
// Runs only when ROTWK_INSTALL and BFME2_INSTALL are set (prints SKIP otherwise); uses the shared retail mount.
// Expected values are the spec's survey numbers (w3d-and-draw.md 1.4, 2.3, 3.1, 3.2: texcheck.py, skinstats.py, the shader
// combination table), produced by scripts that share no code with the engine.

#include "doctest.h"
#include "RetailTestMount.h"

#include "Common/AsciiString.h"
#include "GameEngineDevice/W3DDevice/GameClient/HouseColor.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"
#include "Libraries/WWVegas/WW3D2/hanim.h"
#include "Libraries/WWVegas/WW3D2/mapper.h"
#include "Libraries/WWVegas/WW3D2/meshrender.h"
#include "Libraries/WWVegas/WW3D2/textureloader.h"
#include "Libraries/WWVegas/WW3D2/w3dstops.h"

#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

TEST_CASE("retail corpus: every mesh builds render data; zero-weight skins, divergent blend shaders and texture resolution match the spec's survey")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("render corpus sweep");
		return;
	}
	REQUIRE_MESSAGE(mount->fs, mount->error);

	FilenameList list;
	mount->fs->getFileListInDirectory("", "", "*.w3d", list, true);
	REQUIRE(list.size() == 14475); // spec 2: 14,475 effective .w3d files

	size_t files = 0, loadFailures = 0, meshes = 0, built = 0, surfaces = 0, fxSurfaces = 0, skins = 0, dual = 0;
	size_t zeroWeightVertices = 0, zeroWeightMeshes = 0, mapperStages = 0;
	std::map<int, size_t> mapperTypeStages;
	std::map<int, size_t> blendSurfaces;
	std::map<std::string, size_t> divergenceNotes;
	std::map<std::string, size_t> buildFailures;
	std::map<std::string, size_t> buildWarnings;
	size_t divergentShaderEntries = 0;
	std::set<std::string> textureNames; // lower-cased distinct names of classic texture chunks and FX texture properties

	// housecolor.ini: the stop S-022 reports which surfaces name a house texture
	HouseColorTable houseColors;
	{
		std::vector<std::uint8_t> hc;
		std::string hcError;
		REQUIRE_MESSAGE(mount->fs->readFile("data\\ini\\housecolor.ini", hc, &hcError), hcError);
		REQUIRE_MESSAGE(houseColors.Parse(std::string(hc.begin(), hc.end()), &hcError), hcError);
	}
	// Run-time stop reports over the whole corpus (w3dstops.h): hits per id, covered counts, and the meshes that raise each.
	std::map<std::string, size_t> stopHits, stopCovered;
	std::map<std::string, std::set<std::string>> stopMeshes;
	std::map<std::string, size_t> s026Kinds; // by message tail
	struct FadeClip { std::string Hierarchy, Clip; int Pivot, Frame; float Fade; };
	std::vector<FadeClip> fadeClips;                  // animations with a pivot fade between 0.05 and 0.9 (candidates for the S-029 smoke test)
	std::map<std::string, std::string> hlodByHierarchy; // lower-cased hierarchy -> first HLOD that uses it
	std::vector<std::string> sortedSingleMeshes; // bare meshes with a sorted blended surface: candidates for the viewer's sorting test
	size_t rawBumpSurfaces = 0, rawHouseSurfaces = 0, randomStages = 0, timeVariantStages = 0;

	std::vector<std::uint8_t> bytes;
	for (const std::string &path : list)
	{
		std::string error;
		if (!mount->fs->readFile(path, bytes, &error))
		{
			FAIL_CHECK("cannot read " << path << ": " << error);
			continue;
		}
		++files;
		W3DFileContents contents;
		if (!Load_W3D_File(bytes.data(), bytes.size(), contents, &error))
		{
			++loadFailures;
			continue;
		}
		for (const HLodDefClass &hl : contents.HLods) hlodByHierarchy.emplace(AsciiStringUtil::lowered(hl.HierarchyName), hl.Name);
		auto scanFade = [&](const HAnimClass &a) {
			if (fadeClips.size() >= 400) return;
			const int frames = a.Get_Num_Frames();
			for (int piv = 0; piv < a.Get_Num_Pivots(); ++piv)
			{
				for (int f = 0; f < frames; f += (frames > 8 ? frames / 8 : 1))
				{
					const float fade = a.Get_Fade(piv, (float)f);
					if (fade < 0.9f && fade > 0.05f)
					{
						fadeClips.push_back({ AsciiStringUtil::lowered(a.Get_HName()), a.Get_Name(), piv, f, fade });
						return;
					}
				}
			}
		};
		for (const HRawAnimClass &a : contents.RawAnims) scanFade(a);
		for (const HCompressedAnimClass &a : contents.CompressedAnims) scanFade(a);
		for (const MeshModelClass &mesh : contents.Meshes)
		{
			++meshes;
			for (const W3dShaderStruct &sh : mesh.Shaders)
			{
				if (Translate_W3D_Shader(sh).Blend == W3D_GODOT_BLEND_APPROX_ADD) ++divergentShaderEntries;
			}
			for (const MeshTextureDef &t : mesh.Textures) textureNames.insert(AsciiStringUtil::lowered(t.Name));
			for (const MeshShaderMaterialDef &fx : mesh.ShaderMaterials)
				for (const MeshShaderMaterialProperty &p : fx.Properties)
					if (p.Type == W3DSHADERMATERIAL_PROPERTY_TEXTURE && !p.StringValue.empty()) textureNames.insert(AsciiStringUtil::lowered(p.StringValue));

			MeshRenderData r;
			if (!Build_Mesh_Render_Data(mesh, r, &error))
			{
				size_t colon = error.find(": ");
				++buildFailures[colon == std::string::npos ? error : error.substr(colon + 2)];
				continue;
			}
			++built;
			{
				std::vector<W3DStopHit> hits;
				W3D_Collect_Mesh_Stops(r, hits);
				for (const MeshDrawSurface &surf : r.Surfaces)
				{
					W3D_Collect_Surface_Stops(r, surf, mesh, &houseColors, hits);
					// independent counts straight from the surface fields
					if (surf.Kind == MATERIAL_CLASSIC)
					{
						if (surf.Shader.PriGradient == 3 || surf.Shader.PriGradient == 4) ++rawBumpSurfaces;
						bool house = false;
						for (int st = 0; st < 2; ++st)
							if (surf.Stage[st].HasTexture && houseColors.Find(surf.Stage[st].TextureName)) house = true;
						if (house) ++rawHouseSurfaces;
					}
				}
				for (const W3DStopHit &h : hits)
				{
					++stopHits[h.Id];
					stopCovered[h.Id] += h.Count;
					stopMeshes[h.Id].insert(r.Name);
					if (h.Id == "S-026") ++s026Kinds[h.Message.substr(h.Message.find(": ") + 2)];
					if (h.Id == "S-027" && h.Message.find("Random4Class") != std::string::npos) ++randomStages;
					if (h.Id == "S-027" && h.Message.find("shared by every instance") != std::string::npos) ++timeVariantStages;
				}
			}
			for (const std::string &w : r.Warnings)
			{
				size_t at = w.find(" has texture ");
				++buildWarnings[at == std::string::npos ? w : w.substr(at)];
			}
			surfaces += r.Surfaces.size();
			if (r.Skin) ++skins;
			if (r.DualBone) ++dual;
			if (contents.Meshes.size() == 1 && contents.HLods.empty() && sortedSingleMeshes.size() < 12)
			{
				for (const MeshDrawSurface &ss : r.Surfaces)
					if (ss.Kind == MATERIAL_CLASSIC && ss.Sorted && ss.State.Blend != W3D_GODOT_BLEND_OPAQUE && ss.State.Blend != W3D_GODOT_BLEND_NONE && !r.Skin)
					{
						sortedSingleMeshes.push_back(mesh.Get_Name());
						break;
					}
			}
			if (r.ZeroWeightVertices)
			{
				zeroWeightVertices += r.ZeroWeightVertices;
				++zeroWeightMeshes;
			}
			for (const MeshDrawSurface &s : r.Surfaces)
			{
				if (s.Kind == MATERIAL_FX)
				{
					++fxSurfaces;
					continue;
				}
				++blendSurfaces[(int)s.State.Blend];
				if (s.State.Divergent) ++divergenceNotes[s.State.Divergence];
				for (int st = 0; st < 2; ++st)
				{
					if (s.HasVertexMaterial && s.Stage[st].MapperType != 0)
					{
						++mapperStages;
						++mapperTypeStages[s.Stage[st].MapperType];
					}
				}
			}
		}
	}

	// Texture names the way spec 1.4's texcheck.py counts them: spaces removed from the name first (ZH GameFileClass::Set_Name).
	auto exists = [&](const std::string &p) { return mount->fs->doesFileExist(p); };
	size_t stripDds = 0, stripTga = 0, stripMissing = 0;
	// ... and the way the resolver does (the BFME2 binary does not strip): the name as written. Names whose archive entry keeps the space only resolve this way.
	size_t dds = 0, tga = 0, missing = 0;
	std::vector<std::string> spacedHits;
	for (const std::string &name : textureNames)
	{
		std::string stripped;
		for (char c : name) if (c != ' ') stripped += c;
		TextureResolution a = Resolve_W3D_Texture(stripped, exists);
		if (!a.Found) ++stripMissing;
		else if (a.IsDDS) ++stripDds;
		else ++stripTga;

		TextureResolution res = Resolve_W3D_Texture(name, exists);
		if (!res.Found) ++missing;
		else if (res.IsDDS) ++dds;
		else ++tga;
		if (res.Found && !a.Found) spacedHits.push_back(name + " -> " + res.Path);
	}

	std::printf("[w3d render] corpus: %zu files, %zu fail to load, %zu meshes (%zu built, %zu skins, %zu dual-bone), %zu draw surfaces (%zu FX)\n", files,
		loadFailures, meshes, built, skins, dual, surfaces, fxSurfaces);
	std::printf("[w3d render] corpus: %zu zero-weight vertices in %zu meshes; %zu mapper stages (types", zeroWeightVertices, zeroWeightMeshes, mapperStages);
	for (const auto &kv : mapperTypeStages) std::printf(" %s=%zu", W3D_Mapper_Type_Name(kv.first), kv.second);
	std::printf(")\n[w3d render] corpus: classic surfaces by Godot blend:");
	for (const auto &kv : blendSurfaces) std::printf(" %s=%zu", W3D_Godot_Blend_Name((W3DGodotBlend)kv.first), kv.second);
	std::printf("\n[w3d render] corpus: %zu divergent blend shader entries; surfaces by divergence:\n", divergentShaderEntries);
	for (const auto &kv : divergenceNotes) std::printf("[w3d render]   %zu x %s\n", kv.second, kv.first.c_str());
	std::printf("[w3d render] corpus: %zu distinct texture names: spaces stripped first (texcheck.py): %zu dds, %zu tga, %zu missing; name as written: %zu dds, %zu tga, %zu missing\n",
		textureNames.size(), stripDds, stripTga, stripMissing, dds, tga, missing);
	for (const std::string &h : spacedHits) std::printf("[w3d render]   resolves only with the space kept: %s\n", h.c_str());
	for (const auto &kv : buildFailures) std::printf("[w3d render]   build failure x%zu: %s\n", kv.second, kv.first.c_str());
	for (const auto &kv : buildWarnings) std::printf("[w3d render]   warning x%zu: mesh%s\n", kv.second, kv.first.c_str());

	// The 8 corrupt files of spec 2.3 fail to load, nothing else does.
	CHECK(loadFailures == 8);
	CHECK(buildFailures.empty());
	// Spec 3.1: 1,653 vertices in 15 meshes carry weights 0/0.
	CHECK(zeroWeightVertices == 1653);
	CHECK(zeroWeightMeshes == 15);
	// Spec 3.2: 46 shaders need an approximation: (ONE,SRCALPHA) x26, (ONE,INVSRCCOLOR) x14, (ONE,SRCCOLOR) x6.
	CHECK(divergentShaderEntries == 46);
	// Spec 1.4 (texcheck): of 2,641 distinct names 2,114 resolve to a .dds, 285 to a .tga, 242 are missing.
	// That script strips spaces from the name first; the numbers below reproduce it with the resolver on the stripped names.
	CHECK(textureNames.size() == 2641);
	CHECK(stripDds == 2114);
	CHECK(stripTga == 285);
	CHECK(stripMissing == 242);
	// 30 archive entries keep a space in their file name ("exicemunitionsalpha .dds"); the names that point at them resolve
	// only when the space is kept, which is what the resolver does first. Each of these is a file the stripped lookup misses.
	for (const std::string &h : spacedHits) CHECK(h.find(' ') != std::string::npos);
	CHECK(spacedHits.size() == stripMissing - missing);

	// ---- run-time stop reports over the corpus (review finding: totals asserted, not just printed) ----
	std::printf("[w3d render] corpus stops:");
	for (const auto &kv : stopHits) std::printf(" %s=%zu hits/%zu covered/%zu meshes", kv.first.c_str(), kv.second, stopCovered[kv.first], stopMeshes[kv.first].size());
	std::printf("\n[w3d render] corpus S-026 by kind:\n");
	for (const auto &kv : s026Kinds) std::printf("[w3d render]   %zu x %s\n", kv.second, kv.first.c_str());
	std::printf("[w3d render] corpus fade clips: %zu;", fadeClips.size());
	for (size_t i = 0; i < fadeClips.size() && i < 400; ++i)
	{
		auto h = hlodByHierarchy.find(fadeClips[i].Hierarchy);
		if (h != hlodByHierarchy.end()) std::printf(" [%s | %s | pivot %d frame %d fade %.2f]", h->second.c_str(), fadeClips[i].Clip.c_str(), fadeClips[i].Pivot, fadeClips[i].Frame, fadeClips[i].Fade);
	}
	std::printf("\n[w3d render] corpus sorted single meshes:");
	for (const std::string &n : sortedSingleMeshes) std::printf(" %s", n.c_str());
	std::printf("\n[w3d render] corpus S-025 meshes:");
	for (const std::string &n : stopMeshes["S-025"]) std::printf(" %s", n.c_str());
	std::printf("\n[w3d render] corpus S-022: %zu hits, %zu classic surfaces with a house texture; S-027: %zu shared-phase stages, %zu RANDOM stages\n", stopHits["S-022"],
		rawHouseSurfaces, timeVariantStages, randomStages);

	// S-020: spec 3.1, 1,653 zero-weight vertices in 15 meshes.
	CHECK(stopMeshes["S-020"].size() == 15);
	CHECK(stopCovered["S-020"] == 1653);
	// S-021: spec 2.3 / 3.1, 1,697 meshes carry the second-bone streams.
	CHECK(stopHits["S-021"] == 1697);
	// S-023: spec 2.3, 8,609 BFME2 FX shader materials, each drawn as one approximated surface.
	CHECK(stopHits["S-023"] == 8609);
	// S-026: spec 3.2, (ONE,SRCALPHA) x26, (ONE,INVSRCCOLOR) x14, (ONE,SRCCOLOR) x6 and depth function LESS x37.
	CHECK(s026Kinds["no Godot blend mode for (ONE, SRCALPHA); drawn as blend_add"] == 26);
	CHECK(s026Kinds["no Godot blend mode for (ONE, INVSRCCOLOR); drawn as blend_add"] == 14);
	CHECK(s026Kinds["no Godot blend mode for (ONE, SRCCOLOR); drawn as blend_add"] == 6);
	CHECK(s026Kinds["depth function LESS drawn as LESSEQUAL (differs on coplanar surfaces only)"] == 37);
	// S-027: spec 3.4, RANDOM mappers 6 (stage 0) + 8 (stage 1) = 14.
	CHECK(randomStages == 14);
	// S-026 BUMPENVMAP gradients: counted twice, by the stop collector and straight from the surfaces' raw shader field.
	CHECK(s026Kinds["BUMPENVMAP gradient drawn with the diffuse colour only (ZH fallback without BUMPENV caps)"] == rawBumpSurfaces);
	CHECK(rawBumpSurfaces > 0);
	// S-025: no external survey counts these meshes (spec silent). This is a regression pin of what the build pipeline reports for the
	// retail corpus: 9 stage warnings in 8 meshes (GBCastWall_U / _UD1, GUSiegTreb_dmg / _dskn, IULurtz_Arow, IUScaff04, LM_BrdrPupil, shell_buttons).
	CHECK(stopHits["S-025"] == 9);
	CHECK(stopMeshes["S-025"].size() == 8);
	// S-022: every surface with a texture named in housecolor.ini reports it; surfaces without one do not.
	CHECK(stopHits["S-022"] >= rawHouseSurfaces);
	CHECK(rawHouseSurfaces > 0);
}

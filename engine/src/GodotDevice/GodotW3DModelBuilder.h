// OpenBFME. GPL-3.0.
//
// Device layer: turns a W3D model (HLod + HTree + meshes, loaded by the WW3D2 ports) into
// Godot nodes in bind pose. One ArrayMesh per HLod sub-object, placed like ZH HLodClass
// places sub-objects: rigid meshes ride their bone's transform, skins are deformed per vertex
// by their influence bone (ZH MeshGeometryClass::get_deformed_vertices).
//
// Space: vertex data stays in W3D space (right-handed, Z up). A single "W3DSpace" node
// rotates it into Godot's Y-up frame: godot = (x, z, -y).

#pragma once

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include <map>
#include <string>

namespace godot
{

class RetailFileSystem;

class W3DModelBuilder : public RefCounted
{
	GDCLASS(W3DModelBuilder, RefCounted)

public:
	// model_name as written in INI (e.g. "GUMAArms_SKN"). Returns null on failure; see
	// get_report() either way.
	Node3D *build_model(const Ref<RetailFileSystem> &fs, const String &model_name);

	// { model, file, hierarchy, lod_index, meshes: [..], skipped: [..], errors: [..] }
	Dictionary get_report() const { return m_report; }

protected:
	static void _bind_methods();

private:
	Ref<Texture2D> loadTexture(RetailFileSystem &fs, const std::string &name, Dictionary &meshInfo);

	Dictionary m_report;
	std::map<std::string, Ref<Texture2D>> m_textureCache;
};

} // namespace godot

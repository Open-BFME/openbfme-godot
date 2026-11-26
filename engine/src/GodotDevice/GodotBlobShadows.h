// OpenBFME. GPL-3.0.
#pragma once

// Lane BLOBSHADOW-1: the device side of retail's SHADOW_DECAL blob shadows (GameClient/BlobShadow.h). One SubViewport (2D only, cleared to white)
// covers the ground the camera sees; every visible blob is a quad multiplied into it (godot/shaders/blob_shadow_quad.gdshader, one MultiMesh per
// blob texture), and the terrain shaders multiply their gamma colour with the result (godot/shaders/blob_shadows.gdshaderinc: the globals
// blob_shadow_tex and blob_shadow_rect). Presentation only: nothing here reaches the simulation.

#include "GameClient/BlobShadow.h"

#include <godot_cpp/classes/color_rect.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/multi_mesh_instance2d.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/quad_mesh.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace godot
{
class BlobShadowLayer
{
public:
	// the accumulation texture's edge in texels
	static constexpr int kSize = 2048;
	// the largest ground side it covers (SAGE units): a farther view keeps this size around the camera's ground point
	static constexpr float kMaxSpan = 3000.0f;

	using TextureLoader = std::function<Ref<Texture2D>(const std::string &file)>;

	BlobShadowLayer() = default;
	~BlobShadowLayer();
	BlobShadowLayer(const BlobShadowLayer &) = delete;
	BlobShadowLayer &operator=(const BlobShadowLayer &) = delete;

	// creates the viewport under `parent` (once); `load` resolves a blob texture file (asset packs first, as the model textures)
	void attach(Node *parent, TextureLoader load);
	void detach();
	bool attached() const { return alive() != nullptr; }

	// a frame's blobs: begin with the covered ground rectangle (SAGE min x, min y, width, height), add the quads, end uploads them and sets the
	// globals. `enabled` false (ShadowLOD without decals and no override): the layer is cleared and the globals say "none"
	void begin(float x0, float y0, float width, float height);
	void add(const std::string &textureFile, const BlobShadow::Quad &quad);
	void end();
	void disable();

	int lastCount() const { return m_lastCount; }
	const std::vector<std::string> &missingTextures() const { return m_missing; }

private:
	struct Batch
	{
		MultiMeshInstance2D *node = nullptr;
		Ref<MultiMesh> mm;
		std::vector<float> buffer;
		int count = 0;
		bool valid = false; // the texture loaded
	};
	Batch *batch(const std::string &file);
	// the viewport while it exists: the parent frees its children (NOTIFICATION_PREDELETE) before a C++ owner's destructor runs
	SubViewport *alive() const;
	uint64_t m_viewportId = 0;

	SubViewport *m_viewport = nullptr;
	ColorRect *m_clear = nullptr;
	Ref<QuadMesh> m_quad;
	Ref<ShaderMaterial> m_material;
	TextureLoader m_load;
	std::map<std::string, Batch> m_batches;
	std::vector<std::string> m_missing;
	float m_x0 = 0.0f, m_y0 = 0.0f, m_sx = 0.0f, m_sy = 0.0f;
	bool m_active = false;
	int m_lastCount = 0;
};
} // namespace godot

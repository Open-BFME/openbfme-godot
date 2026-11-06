// OpenBFME. GPL-3.0.
//
// The Godot varying budget, counted from a shader's source text (no Godot dependency): the commit gate's
// tools/check_shader_varyings.py and the runtime guard of the shaders built as strings share this rule.
//
// Godot 4.7.2 (render-sdk/godot) counts a shader's used varyings as
//   the shader type's base index + one location per user varying.
// The base index is the number of varyings the rendering pipeline consumes before the shader's own:
//   scene_shader_forward_clustered.cpp / scene_shader_forward_mobile.cpp `spatial` 15, renderer_canvas_render_rd.cpp
//   `canvas_item` 9, sky.cpp `sky` 10, particles_storage.cpp `particles` 10 (and `fog` 0).
// servers/rendering/shader_language.cpp (~line 11113, "Too many varyings used in shader") adds, per user varying,
// 1 location whatever its width; an array its length; mat2/mat3/mat4 2/3/4 per element, then compares with
// min(maxVertexOutputComponents, maxFragmentInputComponents) / 4 (rendering_device_driver_vulkan.cpp).
// Intel's GPUs report 116 fragment input components, so 29 locations. NVIDIA and AMD report 128 (32): a shader
// at 30..32 compiles there and fails on Intel, where the material draws with nothing (the terrain at 30 drew as
// flat brown ground on an Arc A770).
//
// The count is conservative: every `varying` in the shader and in every file it #includes counts, including
// the ones inside #if branches.

#pragma once

#include <functional>
#include <string>
#include <vector>

namespace ShaderVaryings
{

// Intel's limit: 116 fragment input components / 4. NVIDIA and AMD allow 32.
constexpr int kMaxVaryings = 29;

// The pipeline's own varying locations before a user varying (Godot 4.7.2 base_varying_index values).
constexpr int kSpatialBase = 15;
constexpr int kCanvasItemBase = 9;
constexpr int kSkyBase = 10;
constexpr int kParticlesBase = 10;
constexpr int kFogBase = 0;

// One `varying` declaration: `name` in `file` takes `slots` locations (array length x matrix width).
struct Varying
{
	std::string file;
	std::string name;
	int slots = 1;
};

struct Result
{
	bool ok = false;
	std::string error;      // why the count failed, when !ok
	std::string shaderType; // spatial, canvas_item, sky, particles, fog
	int base = 0;
	int used = 0;
	std::vector<Varying> varyings;
};

// Reads a project-relative posix path (e.g. "shaders/object_shadow.gdshaderinc"); false + error text when it
// cannot be read. `res://` includes are passed without the prefix; relative includes are resolved against the
// including file's folder.
using Loader = std::function<bool(const std::string &path, std::string &out)>;

// Counts `code` (the root shader, displayed as `path`) and every file it includes, each file once.
Result Count(const std::string &code, const std::string &path, const Loader &load);

// The problem text for a failed or over-budget result, naming `name`, the count and the limit.
std::string Problem(const Result &result, const std::string &name);

// The runtime guard: counts first, then reports one problem through `report`. Returns true when the shader fits.
// It never changes `code`.
using Reporter = std::function<void(const std::string &message)>;
bool Guard(const std::string &name, const std::string &code, const std::string &path, const Loader &load, const Reporter &report);

// The count `code` uses, or -1 when it cannot be counted (Problem(error)).
int Used(const std::string &code, const std::string &path, const Loader &load, std::string &error);

} // namespace ShaderVaryings

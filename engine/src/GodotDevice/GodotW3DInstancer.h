// OpenBFME. GPL-3.0.
//
// Device layer: draws retail W3D models, many instances of each, with GPU skinning and no Skeleton3D / scene node per unit
// (spec w3d-and-draw.md 5.2, 5.3).
//
//  * the C++ side owns the asset registry, the pose evaluation (HTreeClass::Anim_Pose, one HTreePose per instance) and the
//    clip state; Godot only receives a palette texture (3 RGBA32F texels per pivot) and, per HLOD sub object, one MultiMesh
//    whose instance buffer holds {transform, palette offset, opacity, rigid bone};
//  * hidden sub objects (pivot visibility, HIDDEN meshes, fade 0) are simply not written to their MultiMesh;
//  * animation is driven by clip name: "HIERARCHY.CLIP" or a bare clip name resolved through the model's hierarchy name
//    (BFME2 resolution, assetmgr.h).
//
// Scope limits (reported by get_model_report): no model condition states of its own (the draw module runtime,
// GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DScriptedModelDraw.h, chooses model and clips and hands a clip pair to
// set_instance_pose), no turrets / captured bones, no house colour, no shadows, no emitters. See GodotW3DMaterial.h for the shading divergences.

#pragma once

#include "GodotDevice/GodotW3DMaterial.h"

#include "Libraries/WWVegas/WW3D2/assetmgr.h"
#include "Libraries/WWVegas/WW3D2/htree.h"
#include "Libraries/WWVegas/WW3D2/meshrender.h"
#include "GameEngineDevice/W3DDevice/GameClient/HouseColor.h"
#include "Libraries/WWVegas/WW3D2/w3dstops.h"
#include "Libraries/WWVegas/WW3D2/w3dsort.h"

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/multi_mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/transform3d.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <set>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace godot
{

class RetailFileSystem;

class W3DInstancer : public Node3D
{
	GDCLASS(W3DInstancer, Node3D)

public:
	W3DInstancer();
	~W3DInstancer() override;

	// Mounts nothing itself: takes the mounted archives of a RetailFileSystem. { ok, errors }
	Dictionary setup(const Ref<RetailFileSystem> &fs);

	// Builds the model (meshes, materials, MultiMeshes). Returns the model id, or -1; the report says why either way.
	int64_t add_model(const String &model_name);
	Dictionary get_model_report(int64_t model) const;

	// clip: "HIERARCHY.CLIP" or a clip name of the model's hierarchy; empty = bind pose. Returns the instance id or -1.
	int64_t add_instance(int64_t model, const Transform3D &transform, const String &clip, double start_time, double speed);
	void set_instance_transform(int64_t instance, const Transform3D &transform);
	// Takes an instance out of the picture (it is no longer posed or written to any MultiMesh; its id is not reused). Idempotent; false for an
	// unknown id. (Added by lane LOGIC-1: live objects die.)
	bool remove_instance(int64_t instance);
	bool set_instance_clip(int64_t instance, const String &clip, double start_time, double speed);
	// Poses an instance exactly, from the draw module runtime (W3DScriptedModelDraw::frame): clip0 at frame0, blended with clip1 at
	// frame1 by `percentage` (0 = clip0, 1 = clip1; HTreeClass::Blend_Pose); an empty clip1 poses clip0 alone, an empty clip0 gives the
	// bind pose. The pose then stays until set_instance_clip or this is called again. Clip names are resolved as in set_instance_clip.
	// (Added by lane DRAW-1; the GDExtension is not built in the lane's environment, so this is unbuilt, unrun code.)
	bool set_instance_pose(int64_t instance, const String &clip0, double frame0, const String &clip1, double frame1, double percentage);
	// lane SMOOTH-1 (C++ only, not bound): set_instance_pose without the Godot String round trip, for GameWorld's per-frame posing; clip names are
	// resolved once per model (cached) and an unchanged pose is not evaluated again
	bool set_instance_pose_native(int64_t instance, const std::string &clip0, double frame0, const std::string &clip1, double frame1, double percentage);
	// Hides sub objects of ONE instance by name (case-insensitive; the names the draw module runtime reports in W3DDrawFrame::hiddenSubObjects,
	// i.e. what the retail BeginScript bodies hide): a hidden sub object is not written to its MultiMesh for that instance. An empty list shows
	// everything again. Unknown names are an error (returns false, recorded). (Added by lane MAPOBJ-1.)
	bool set_instance_hidden_subobjects(int64_t instance, const PackedStringArray &names);
	int64_t get_instance_count() const { return (int64_t)Instances.size(); }
	// Stop S-119 (lane MAPOBJ-1): team colours. After setup() and BEFORE the first add_model, enable them and every classic surface whose stage
	// 0 texture has a housecolor.ini texture gets the HouseColor shader permutation (W3DShaderKey::HouseColor: base = mix(base, base * team,
	// house texture alpha), a HYPOTHESIS: the combine is unrecovered, S-022); the per-surface S-022 reports are replaced by one S-119 report
	// per mesh. set_instance_house_color then tints one instance (alpha < 0.5: none).
	void set_house_colors_enabled(bool enabled);
	bool get_house_colors_enabled() const { return HouseColorEnabled; }
	void set_instance_house_color(int64_t instance, const Color &color);
	// RENDER-1 (stop S-390): the instance is lit by the map's infantry light set (KINDOF_INFANTRY drawables) instead of the objects set;
	// the buffer stores the palette base as -(base + 1) for it.
	void set_instance_infantry_light(int64_t instance, bool infantry);
	// lane PROJ-2: the drawable's opacity (RW Drawable +0xB0, the fade of DrawableFade): multiplies every pivot's fade of the instance; 0 draws nothing
	void set_instance_opacity(int64_t instance, double opacity);
	// A scene that needs no per-frame work (every instance static) turns the automatic update off and calls update_now() itself after
	// changes. Default on. (Added by lane MAPOBJ-1.)
	void set_auto_update(bool enabled);
	bool get_auto_update() const { return AutoUpdate; }

	// { ok, name, frames, frame_rate, seconds, pivots } for a clip of a model
	Dictionary get_clip_info(int64_t model, const String &clip);

	// Animation clock. set_global_time pins it (deterministic screenshots); the clock runs only while playing.
	void set_global_time(double seconds) { GlobalTime = seconds; PoseDirty = true; }
	double get_global_time() const { return GlobalTime; }
	void set_playing(bool playing) { Playing = playing; }
	bool is_playing() const { return Playing; }
	void set_time_scale(double scale) { TimeScale = scale; }
	void set_worker_threads(int count);
	int get_worker_threads() const { return Workers; }

	// Evaluates everything now (also done by _process).
	void update_now();
	// Only the texture mappers: recomputes every time-variant mapper's matrix at the animation clock and sets it on its material. The
	// instance poses and buffers are not touched, so a scene whose poses are static (batched once) still has moving textures. (Added
	// by lane MAPOBJ-1.)
	void update_mappers();
	// One entry per time-variant mapper binding: { source (mesh name), stage, r0, r1 } with the matrix rows read back from the material.
	Array get_mapper_state() const;

	// World position of a pivot of an instance in W3D model space (current pose), for tests.
	Vector3 get_bone_position(int64_t instance, int bone) const;
	// lane UI-2 (C++ only): the posed transform of a pivot in W3D model space as of the last update (call update_now() after posing); the camera bone
	// of an Apt View3D viewer. False for an unknown instance or pivot.
	bool get_bone_transform_native(int64_t instance, int bone, ::Matrix3D &out) const;
	// lane UI-2 (C++ only): a pivot of a model's hierarchy by name (case-insensitive); -1 when absent
	int get_bone_index_native(int64_t model, const std::string &name) const;
	// { instances, models, draw_items, multimeshes, palette_texels, shaders, textures, pose_ms, upload_ms, mapper_ms, visible_instances }
	Dictionary get_stats() const;
	PackedStringArray get_errors() const;
	// Every stop the render path took at run time, one line each, in first-use order ("[S-0xx] ..."). A stop is reported when a model
	// or clip that triggers it is created, never silently; docs/STOPS.md registers the ids.
	PackedStringArray get_warnings() const;

	// Camera for depth sorting of blended draw items. Without it the viewport's current Camera3D is used; with
	// neither, sorted draw items keep their insertion order and a warning says so. `transform` is the camera's global transform.
	void set_sort_camera(const Transform3D &transform);
	// Instance ids of draw item `draw` of `model` in the order the last update wrote them (for tests): back to front for sorted items.
	PackedInt32Array get_draw_order(int64_t model, int64_t draw) const;

	// Shader coverage probe: scans every .w3d of the mounted archives, derives the shader permutation of every draw surface and
	// draws one probe mesh per distinct permutation (placeholder textures) starting at `origin`, 40 units apart, so a windowed run
	// compiles every generated shader. { files, meshes, surfaces, permutations, build_failures }
	Dictionary probe_shaders(const Vector3 &origin);

	void _ready() override;
	void _process(double delta) override;
	void _notification(int what); // lane RENDER-3: installs the gamma-space transparent pass (S-831)

protected:
	static void _bind_methods();

private:
	struct MeshGpu
	{
		MeshRenderData Data;
		Ref<ArrayMesh> Mesh;
		std::vector<Ref<ShaderMaterial>> Materials;
		std::vector<W3DMapperBinding> Mappers;
		std::vector<std::string> Notes;
		std::vector<std::string> Errors;
		std::vector<W3DStopHit> Stops;   // S-020..S-027 raised by this mesh and its surfaces (w3dstops.h)
		bool UsesFx = false;
		std::set<int> SortPriorities;    // render priorities of the sorted blended surfaces
		bool HasSorted = false;          // a surface with MeshDrawSurface::Sorted that blends: instances need back to front order
		bool HasOpaque = false;          // an opaque surface: the pivot fade is drawn with a dither (S-029)
	};
	struct DrawItem
	{
		const RenderSubObject *Sub = nullptr;
		int SubIndex = 0;                // index of Sub in the prototype's SubObjects
		std::shared_ptr<MeshGpu> Mesh;
		MultiMeshInstance3D *Node = nullptr;
		Ref<MultiMesh> MM;
		PackedFloat32Array Buffer;
		int Allocated = 0;
		std::vector<int> Order;          // instance ids as written by the last write_buffers
		W3DSortBatch Batch;              // SMOOTH-1: the depths of the last write (sorted items), kept for the S-029 check of frames that skip this model
		bool HasBatch = false;
	};
	struct Model
	{
		std::string Name;
		const RenderObjPrototype *Proto = nullptr;
		std::vector<DrawItem> Draws;
		std::vector<int> InstanceIds;    // the LIVE instances in creation order (SMOOTH-1: a removed instance leaves the list)
		Dictionary Report;
		// lane SMOOTH-1: per-frame work only where something changed
		std::unordered_map<std::string, const HAnimClass *> Clips; // clip name as asked -> resolved clip (resolve_clip once per name)
		bool BuffersDirty = true;        // an instance moved / was posed / added / removed / re-tinted: its MultiMesh buffers are rewritten
		bool HasSorted = false;          // a draw item sorts back to front: rewritten whenever the camera moves
		int Dithered = 0;                // S-029 counts of the last write of this model
		int SortedItems = 0;
	};
	struct Instance
	{
		int Model = 0;
		Transform3D Xf;
		const HAnimClass *Anim = nullptr;
		bool Explicit = false;                // posed by set_instance_pose: Anim at Frame0, blended with Anim1 at Frame1 by Percentage
		const HAnimClass *Anim1 = nullptr;
		float Frame0 = 0.0f;
		float Frame1 = 0.0f;
		float Percentage = 0.0f;
		double Start = 0.0;
		double Speed = 1.0;
		int PaletteOffset = 0; // first texel
		HTreePose Pose;
		float HcPacked = 0.0f;                // W3D_Pack_House_Color or 0 (INSTANCE_CUSTOM.z)
		bool InfantryLight = false;           // set_instance_infantry_light (INSTANCE_CUSTOM.x < 0)
		std::vector<std::uint8_t> HiddenSubs; // by sub object index; empty = none hidden (set_instance_hidden_subobjects)
		bool Removed = false;                 // remove_instance: skipped by the pose and buffer passes
		float Opacity = 1.0f;                 // set_instance_opacity (lane PROJ-2)
		bool PoseStale = true;                // SMOOTH-1: the pose inputs changed since the last evaluation (clip-driven instances follow GlobalTime)
		bool Inexact = false;                 // SMOOTH-1: the last evaluation used an arm whose summation order is not retail's (S-028)
		int PaletteSize = 0;                  // SMOOTH-1: texels of the palette block (pivots * W3D_PALETTE_TEXELS_PER_PIVOT); 0 = none yet
	};

	void warn(const std::string &message);
	const HAnimClass *resolve_clip(const Model &m, const std::string &name); // nullptr (and an error recorded) when the clip is not registered
	std::shared_ptr<MeshGpu> build_mesh(const MeshModelClass &mesh, std::vector<std::string> &errors);
	void rebuild_layout();
	void evaluate_pose(Instance &inst) const;
	void write_palette(const Instance &inst);
	void write_buffers();
	const HAnimClass *resolve_clip_cached(Model &m, const std::string &name);
	void allocate_palette(Instance &inst);   // SMOOTH-1: a block of the palette for one instance (a free block of the size, else the end)
	void grow_palette(int texels);           // SMOOTH-1: a larger palette image / texture (capacity grows by half), the data kept
	void mark_model(int64_t instance);       // SMOOTH-1: the instance's model rewrites its buffers
	void run_parallel(size_t count, const std::function<void(size_t, size_t)> &fn);

	Ref<RetailFileSystem> FsRef;
	std::unique_ptr<W3DFileSource> Source;
	std::unique_ptr<WW3DAssetManager> Assets;
	std::unique_ptr<W3DMaterialFactory> Materials;
	std::map<std::string, std::shared_ptr<MeshGpu>> MeshCache;
	std::vector<std::unique_ptr<Model>> Models;
	std::vector<Instance> Instances;
	std::vector<Ref<ShaderMaterial>> AllMaterials;
	std::vector<W3DMapperBinding> Mappers;

	// palette
	std::vector<float> PaletteData;
	int PaletteTexels = 0;
	int PaletteHeight = 0;
	Ref<ImageTexture> PaletteTexture;
	Ref<Image> PaletteImage;
	bool LayoutDirty = true;
	bool PoseDirty = true;
	// lane SMOOTH-1: the palette is allocated per instance (removed instances' blocks are reused by size) instead of relaid out on every add
	int PaletteUsed = 0;                         // texels up to the high-water mark
	std::map<int, std::vector<int>> PaletteFree; // block size -> free block offsets
	bool PaletteGrown = false;                   // the texture was recreated: set on every material, full upload
	bool PaletteChanged = true;                  // some instance's palette block changed: upload
	double PosedTime = -1.0;                     // GlobalTime of the last evaluation of clip-driven instances
	bool HaveLastCamera = false;
	Transform3D LastCamera;                      // the camera of the last write (sorted draw items are rewritten when it moves)
	Transform3D LastNodeXf;
	std::vector<size_t> StaleScratch;            // per-frame scratch, kept to avoid allocations
	std::vector<int> IdScratch;
	std::vector<float> DepthScratch;
	std::vector<W3DSortBatch> SortBatchScratch;
	std::vector<std::pair<StringName, StringName>> MapperNames; // SMOOTH-1: per stage, the w3d_m<stage>r0 / r1 uniform names
	std::uint32_t MapperSyncMs = 0xFFFFFFFFu;     // the mapper clock of the last update_mappers
	size_t MapperCount = 0;
	int PoseEvaluations = 0, BufferWrites = 0;   // stats of the last update: instances posed, models rewritten

	double GlobalTime = 0.0;
	double TimeScale = 1.0;
	bool Playing = true;
	bool AutoUpdate = true;
	bool HouseColorEnabled = false;

	// stats
	double PoseMs = 0.0, UploadMs = 0.0, MapperMs = 0.0;
	int VisibleInstances = 0;
	std::vector<std::string> Errors;
	std::vector<std::string> Warnings;          // see get_warnings
	std::set<std::string> WarnedMessages;
	HouseColorTable HouseColors;                // data\ini\housecolor.ini, read in setup (S-022 reports which textures name a house texture)
	bool HouseColorsLoaded = false;
	mutable std::atomic<int> InexactPoseInstances{ 0 }; // poses of the last update evaluated with an arm whose summation order is not retail's (S-028)
	int InexactPoseTotal = 0;
	int DitheredFadeInstances = 0;              // instances of opaque draw items drawn with the fade dither (S-029) in the last update
	int SortedDrawItems = 0;
	int InterleavedBatchPairs = 0;               // blended batch pairs whose depths interleave (S-029) in the last update
	bool SortCameraSet = false;
	Transform3D SortCamera;
	std::chrono::steady_clock::time_point Epoch;

	// worker threads (pose evaluation and buffer writing are independent per instance / per draw item); lane PERF-2: the slices run on the client job pool
	int Workers = 1;
};

} // namespace godot

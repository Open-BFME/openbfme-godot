# Object shadow maps (S-3304)

The Godot renderer uses directional shadow maps for volume-shadow objects. This is a presentation substitute, not a reproduction of SAGE's CPU-extruded stencil volumes or RotWK's shadow-map shaders. It changes no simulation state or random stream.

## Implemented

- Object-template `Shadow` bits select `SHADOW_VOLUME`, `SHADOW_VOLUME_NEW` and their `OR_DECAL` forms. A model state's shadow description overrides the template; `ShadowForceDisable` wins. `SHADOW_DECAL` objects (infantry, optimized trees) are a projected blob texture in retail, not a silhouette, so they cast no map. Floors, particles, UI models, decal objects and projection-only objects do not become mesh casters.
- Each instancer creates shadow-only MultiMeshes lazily. They share the visible mesh, materials and bone palette, but contain only eligible instances. Hidden subobjects, invisible pivots, zero opacity and removed instances are omitted by the same visibility pass. Skinning, alpha cutouts and vegetation sway run through the same vertex/fragment shaders.
- While a shadow map is drawn, casters continue posing outside the presentation camera's frustum: an off-screen object can cast an on-screen shadow. This deliberately trades some pose-culling savings for correct animation; with ShadowLOD Off or Low they are pose-culled like any instance. No new per-object scene nodes are created.
- Opaque W3D surfaces, terrain and roads receive shadows. The custom light function uses only directional-light shadow coverage; it does not replace the existing retail lighting calculations with Godot lighting. UI/standalone W3D instancers do not receive shadows unless explicitly enabled. Terrain selection and radius overlays remain above shadows.
- The existing main object light supplies the direction. `ShadowLOD` now controls Off/Low (no volume maps), Medium and High (2048) and UltraHigh (4096) directional atlases. Each of the four cascades gets a quarter of the atlas, i.e. RotWK's own shadow map size: the `ShadowMap` block's `MapSize` (retail 1024, RW 0x47B69E), raised to at least 2048 at UltraHigh (RW 0x47B2B5); retail draws no shadow map at Medium, which keeps High's size. Turning the maps off restores Godot's default 4096 atlas. Low's retail decal shadows remain unimplemented. All enabled levels use four cascades, a 4000-world-unit range and 32-bit depth. Changing the setting applies during a game.

## Evidence and limits

The original GPL [ZH W3DShadowManager::setTimeOfDay](https://github.com/electronicarts/CnC_Generals_Zero_Hour/blob/main/GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/Shadow/W3DShadow.cpp) uses the first object light. [W3DVolumetricShadowManager::renderStencilShadows](https://github.com/electronicarts/CnC_Generals_Zero_Hour/blob/main/GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/Shadow/W3DVolumetricShadow.cpp) multiplies framebuffer RGB by the shadow colour (`DESTCOLOR`, `ZERO`); its alpha is not the blend weight. This implementation applies that multiplier in gamma space, before the final output conversion. Applying this donor composite to RotWK shadow-map coverage is an **inference**, not target-binary verification. This does not reproduce BFME2 High's main-light-only shadow attenuation; all enabled presets currently use the same donor composite. Fully lit pixels retain the pre-shadow output; overlapping casters do not repeatedly darken it.

Godot's [spatial shader documentation](https://docs.godotengine.org/en/stable/tutorials/shaders/shader_reference/spatial_shader.html) distinguishes the light's shadow-pass view from `MAIN_CAM_INV_VIEW_MATRIX`. Camera-facing W3D geometry uses the latter to keep its visible orientation in the shadow pass. Opaque and transparent shader permutations explicitly share instance-uniform indices, so adding reception cannot move the tint slot. The fragment-to-light colour replaces an unused varying, preserving the existing interpolation budget.

Still S-3304: texture/decal shadows (the SHADOW_DECAL blobs of infantry and trees), dynamic/directional projected shadows, non-self volume variants, per-object sun-angle/height restrictions, shadow-opacity curves, translucent placement ghosts, water reception and transparent W3D reception. The Godot maps use alpha-cutout silhouettes rather than retail stencil geometry. Resolution, bias, cascade filtering and distance fade are Godot presentation choices; they are not retail parity claims. Rendered on Vulkan Forward+ only; Metal, mobile and Compatibility need separate validation.

## Validation

Native tests cover template/state eligibility, force-disable, shader generation/interpolator budget, existing lighting math and the enabled options row. GPU tests:

- `godot/tests/object_shadow_render_test.gd`: actual terrain shader; numeric gamma-space shadow colour, unchanged unshadowed output, moving caster, runtime off.
- `godot/tests/object_shadow_instances_test.gd`: shared-model eligibility, transforms, off-camera animation, hidden subobjects, opacity and removal. Requires retail assets and a real rendering backend for buffer readback.
- `godot/tests/object_shadow_building_test.gd`: a retail barracks on synthetic terrain casts/receives a visible shadow, never adds light, and restores the exact baseline when disabled. Optional `OPENBFME_SHADOW_CAPTURE_DIR` saves local evidence; no game-derived images belong in the repository.
- Existing `shader_test.gd` and `building_render_test.gd`: terrain compositing, real-map rendering and normal-mapped/team-colour building permutations.

Run GPU scripts without `--headless`, with `--path godot --script res://tests/<name>.gd`. Retail tests require `ROTWK_INSTALL` and `BFME2_INSTALL`.

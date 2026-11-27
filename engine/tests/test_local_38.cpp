// OpenBFME. GPL-3.0.
//
// Narrow tests for lane LOCAL-38 (fork issue 294, stop S-687): object lights as retail. Every render object gets its own LightEnvironmentClass
// (RTS3DScene::renderOneObject RW 0x46F48E): the global lights and the dynamic lights whose sphere meets the object's, ranked by
// Contribution (RW 0x5403ED), at most four (RW 0x54044B); the effects draw the directional slots and the FIRST point slot (PointLight[1]
// in normalmapped / defaultw3d / vegetation .fxo; SAS split RW 0x53F140 .. 0x53F1D0, RW 0x54D135 / 0x54D1E8 / 0x54D26F).
//
// Representative synthetic light values (campfire, torch):
//   Campfire: Ambient R:210 G:101 B:40, Diffuse R:255 G:120 B:80, Radius 200, Intensity 3
//   TownsmanTorch: Ambient 0, Diffuse R:255 G:192 B:64, Radius 65, Intensity 7
//   GenericTorch: Ambient 0, Diffuse R:240 G:240 B:100, Radius 40, Intensity 1.2

#include "doctest.h"
#include "ObjectTestUtil.h"

#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/Thing.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/ClientEffects.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DLightDraw.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"
#include "Libraries/WWVegas/WW3D2/lightenvironment.h"
#include "Libraries/WWVegas/WW3D2/w3dshadergen.h"

#include <cmath>
#include <string>
#include <vector>

namespace
{
// RGBColor as INI::parseRGBColor gives it (0..255 -> 0..1)
Vector3 rgb(int r, int g, int b)
{
	return Vector3((float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f);
}

// a W3DLightDraw's light as LightPulseToEnvSource hands it over (ambient / diffuse times the intensity, the raw diffuse for the filter)
LightEnvSourceLight drawLight(const Vector3 &pos, const Vector3 &ambient, const Vector3 &diffuse, float radius, float intensity)
{
	LightEnvSourceLight l;
	l.type = LightEnvSourceLight::POINT;
	l.position = pos;
	l.ambient = Vector3(ambient.X * intensity, ambient.Y * intensity, ambient.Z * intensity);
	l.diffuse = Vector3(diffuse.X * intensity, diffuse.Y * intensity, diffuse.Z * intensity);
	l.filterDiffuse = diffuse;
	l.useFilterDiffuse = true;
	l.farAttenuation = true; // RW 0x4CF227
	l.farAttenStart = 1.0f;  // RW 0x4CF21C
	l.farAttenEnd = radius;  // RW 0x4CF216
	return l;
}

LightEnvSourceLight campfire(const Vector3 &pos)
{
	return drawLight(pos, rgb(210, 101, 40), rgb(255, 120, 80), 200.0f, 3.0f);
}

LightEnvSourceLight townsmanTorch(const Vector3 &pos)
{
	return drawLight(pos, Vector3(), rgb(255, 192, 64), 65.0f, 7.0f);
}

LightEnvSourceLight genericTorch(const Vector3 &pos)
{
	return drawLight(pos, Vector3(), rgb(240, 240, 100), 40.0f, 1.2f);
}

// a night map's three objects lights (directional, diffuse at or above 0.05, ambient 0: W3DObjectLighting::fillSet)
std::vector<LightEnvSourceLight> nightSuns()
{
	std::vector<LightEnvSourceLight> s(3);
	s[0].diffuse = Vector3(0.30f, 0.30f, 0.45f);
	s[1].diffuse = Vector3(0.10f, 0.10f, 0.15f);
	s[2].diffuse = Vector3(0.06f, 0.06f, 0.08f);
	for (int i = 0; i < 3; ++i)
	{
		s[(size_t)i].type = LightEnvSourceLight::DIRECTIONAL;
		s[(size_t)i].direction = Vector3(0.0f, 0.0f, 1.0f);
	}
	return s;
}

float linearAtten(float dist, float start, float end)
{
	float a = 1.0f - (dist - start) / (end - start);
	return a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
}
} // namespace

TEST_CASE("LOCAL-38: six lights around two objects far apart: each object is lit by its own lights, not the scene's top four")
{
	// two soldiers 2000 apart, three torch bearers around each; retail ranks per render object (RW 0x46F48E), the old Godot path took four
	// for the whole scene
	const Vector3 a(0.0f, 0.0f, 10.0f), b(2000.0f, 0.0f, 10.0f);
	const float radius = 12.0f;
	std::vector<LightEnvSourceLight> dyn = {
		townsmanTorch(Vector3(20.0f, 0.0f, 10.0f)),   // near a, closest
		townsmanTorch(Vector3(0.0f, 40.0f, 10.0f)),   // near a
		genericTorch(Vector3(-30.0f, 0.0f, 10.0f)),   // near a, weaker
		townsmanTorch(Vector3(2000.0f, 30.0f, 10.0f)), // near b
		townsmanTorch(Vector3(2010.0f, 0.0f, 10.0f)),  // near b, closest
		genericTorch(Vector3(1980.0f, 20.0f, 10.0f)),  // near b
	};
	const std::vector<LightEnvSourceLight> suns = nightSuns();
	const W3DObjectLightSet la = W3D_Collect_Object_Lights(a, radius, Vector3(0.1f, 0.1f, 0.15f), suns.data(), 3, dyn.data(), (int)dyn.size());
	const W3DObjectLightSet lb = W3D_Collect_Object_Lights(b, radius, Vector3(0.1f, 0.1f, 0.15f), suns.data(), 3, dyn.data(), (int)dyn.size());
	// the sphere test (RW 0x46E93D) leaves each object only its own three
	CHECK(la.dynamicAdded == 3);
	CHECK(lb.dynamicAdded == 3);
	// PointLight[0]: the strongest of its own (the closest townsman torch)
	REQUIRE(la.hasPoint);
	REQUIRE(lb.hasPoint);
	CHECK(la.pointId == 0);
	CHECK(lb.pointId == 4);
	CHECK(la.pointPosition.X == 20.0f);
	CHECK(lb.pointPosition.X == 2010.0f);
	// the colour is the unattenuated diffuse * intensity (+0x48, RW 0x54D135), the range the outer radius (RW 0x54D26F)
	CHECK(la.pointColor.X == (255.0f / 255.0f) * 7.0f);
	CHECK(la.pointColor.Y == (192.0f / 255.0f) * 7.0f);
	CHECK(la.pointRange == 65.0f);
	// three point lights outrank the night suns 1 and 2 (four slots: three points + the strongest sun); sun 0 stays
	CHECK(la.pointSlots == 3);
	CHECK(la.droppedSuns == 6u);
	CHECK(lb.droppedSuns == 6u);
}

TEST_CASE("LOCAL-38: Add_Light ranks by Diffuse.Length2, keeps four, drops black lights by the raw diffuse")
{
	LightEnvironmentClass env;
	env.Reset(Vector3(0.0f, 0.0f, 0.0f), Vector3());
	const std::vector<LightEnvSourceLight> suns = nightSuns();
	for (int i = 0; i < 3; ++i)
	{
		LightEnvSourceLight s = suns[(size_t)i];
		s.id = i;
		env.Add_Light(s);
	}
	REQUIRE(env.Get_Light_Count() == 3);
	CHECK(env.Get_Input_Light(0).Id == 0);
	CHECK(env.Get_Input_Light(2).Id == 2);

	// RW 0x540261: raw diffuse below 0.05 in every channel is dropped although the intensity would make it bright
	LightEnvSourceLight black = drawLight(Vector3(10.0f, 0.0f, 0.0f), Vector3(1.0f, 1.0f, 1.0f), Vector3(0.04f, 0.049f, 0.0f), 100.0f, 50.0f);
	black.id = 10;
	env.Add_Light(black);
	CHECK(env.Get_Light_Count() == 3);
	CHECK(env.Get_Equivalent_Ambient().X == 0.0f); // its ambient is not added either (the filter comes first)

	// a dim point light (contribution below sun 1, above sun 2) goes between them: strictly greater moves the weaker back (RW 0x54043B)
	LightEnvSourceLight dim = drawLight(Vector3(30.0f, 0.0f, 0.0f), Vector3(), Vector3(0.3f, 0.3f, 0.3f), 40.0f, 1.0f);
	dim.id = 11;
	env.Add_Light(dim);
	REQUIRE(env.Get_Light_Count() == 4);
	CHECK(env.Get_Input_Light(0).Id == 0);
	CHECK(env.Get_Input_Light(1).Id == 1);
	CHECK(env.Get_Input_Light(2).Id == 11);
	CHECK(env.Get_Input_Light(3).Id == 2);
	// its diffuse is attenuated for the ranking (1 - (30 - 1) / (40 - 1), RW 0x53F757)
	const float at = linearAtten(30.0f, 1.0f, 40.0f);
	CHECK(env.Get_Input_Light(2).Diffuse.X == 0.3f * at);
	CHECK(env.Get_Input_Light(2).PointDiffuse.X == 0.3f);

	// a fifth, strongest light: first slot; the weakest (sun 2) falls off the end
	LightEnvSourceLight strong = townsmanTorch(Vector3(5.0f, 0.0f, 0.0f));
	strong.id = 12;
	env.Add_Light(strong);
	REQUIRE(env.Get_Light_Count() == 4);
	CHECK(env.Get_Input_Light(0).Id == 12);
	CHECK(env.Get_Input_Light(3).Id == 11);
	// the SAS split: DirectionalLight[] = the non-point slots in order, PointLight[0] = the first point slot
	CHECK(env.Directional_Count() == 2);
	CHECK(env.Directional_Index(0) == 1);
	CHECK(env.Directional_Index(1) == 2);
	CHECK(env.Point_Count() == 2);
	CHECK(env.Point_Index(0) == 0);
	CHECK(env.Point_Index(1) == 3);
	CHECK(env.Point_Index(2) == -1);

	// an equal contribution does not move a kept light (only strictly greater does)
	LightEnvironmentClass tie;
	tie.Reset(Vector3(), Vector3());
	LightEnvSourceLight t0 = suns[0], t1 = suns[0];
	t0.id = 0;
	t1.id = 1;
	tie.Add_Light(t0);
	tie.Add_Light(t1);
	CHECK(tie.Get_Input_Light(0).Id == 0);
	CHECK(tie.Get_Input_Light(1).Id == 1);
}

TEST_CASE("LOCAL-38: W3DLightDraw Ambient reaches the object's ambient, attenuated from the object, clamped to one")
{
	// CampfireLight's Ambient 210 / 101 / 40 * Intensity 3, linear over 1 .. 200 from the object's centre (RW 0x53F757, RW 0x540394)
	const Vector3 fire(0.0f, 0.0f, 0.0f);
	std::vector<LightEnvSourceLight> dyn = { campfire(fire) };
	const Vector3 scene(0.2f, 0.2f, 0.25f);
	const W3DObjectLightSet near = W3D_Collect_Object_Lights(Vector3(10.0f, 0.0f, 0.0f), 5.0f, scene, nullptr, 0, dyn.data(), 1);
	const W3DObjectLightSet far = W3D_Collect_Object_Lights(Vector3(150.0f, 0.0f, 0.0f), 5.0f, scene, nullptr, 0, dyn.data(), 1);
	const W3DObjectLightSet out = W3D_Collect_Object_Lights(Vector3(206.0f, 0.0f, 0.0f), 5.0f, scene, nullptr, 0, dyn.data(), 1);
	// near: 0.2 + 0.82 * 3 * 0.95 is above one: Pre_Render_Update clamps (RW 0x54063B)
	CHECK(near.ambient.X == 1.0f);
	// far: attenuated from the object's centre (150 away), not from the camera
	const float at = linearAtten(150.0f, 1.0f, 200.0f);
	CHECK(far.ambient.Z == doctest::Approx(0.25f + (40.0f / 255.0f) * 3.0f * at).epsilon(1e-6)); // tolerance: float order of the sum
	CHECK(far.ambient.Z < 1.0f);
	// outside the light's sphere (200 + 5 < 206): not added at all (RW 0x46FCF9)
	CHECK(out.dynamicAdded == 0);
	CHECK(out.ambient.X == 0.2f);
	CHECK_FALSE(out.hasPoint);
}

TEST_CASE("LOCAL-38: pool lights as Add_Light reads them: W3DLightDraw sets FAR_ATTENUATION, a light pulse does not and keeps the slot's flag")
{
	LightPulsePool pool;
	// createLightPulse (RW 0x443B5D): ambient = diffuse = the colour, no FAR_ATTENUATION (RW 0x443C33 sets only +0x145 / +0x146)
	REQUIRE(pool.add(Coord3D{ 0.0f, 0.0f, 0.0f }, RGBColor{ 1.0f, 0.5f, 0.25f }, 60.0f, 0, 10));
	LightEnvSourceLight s;
	LightPulseToEnvSource(pool.lights()[0], s);
	CHECK_FALSE(s.farAttenuation);
	CHECK(s.ambient.X == 1.0f);
	CHECK(s.ambient.Z == 0.25f);
	CHECK(s.farAttenEnd == 61.0f);
	// without the flag the attenuation is 1 anywhere inside the sphere (RW 0x53F71C): the full ambient and diffuse rank and light
	LightEnvironmentClass env;
	env.Reset(Vector3(50.0f, 0.0f, 0.0f), Vector3());
	env.Add_Light(s);
	CHECK(env.Get_Equivalent_Ambient().X == 1.0f);
	CHECK(env.Get_Input_Light(0).Diffuse.Y == 0.5f);

	// a W3DLightDraw light sets the flag (RW 0x4CF227); when it is removed and a pulse reuses the light, the flag stays (RW 0x46F226)
	const int h = pool.addPersistent(Coord3D{ 0.0f, 0.0f, 0.0f }, RGBColor{ 1.0f, 1.0f, 1.0f }, 40.0f);
	REQUIRE(h == 1);
	CHECK(pool.lights()[1].farAttenuation);
	pool.removePersistent(h);
	pool.step(); // the first pulse decays; slot 0 stays lit
	REQUIRE(pool.add(Coord3D{ 0.0f, 0.0f, 0.0f }, RGBColor{ 1.0f, 1.0f, 1.0f }, 60.0f, 0, 10));
	CHECK(pool.lights()[1].enabled);
	CHECK(pool.lights()[1].farAttenuation);
}

TEST_CASE("LOCAL-38: W3DLightDraw hands Ambient * Intensity and the raw Diffuse to the pool (RW 0x4CF18E, RW 0x53F90C)")
{
	LightPulsePool pool;
	objtest::World w;
	W3DLightDraw::registerClass(w.modules);
	// Synthetic campfire light parameters
	REQUIRE(w.load("Object L38Campfire\n"
				   "  KindOf = SELECTABLE\n"
				   "  Draw = W3DLightDraw ModuleTag_L\n"
				   "    Ambient = R:210 G:101 B:40\n"
				   "    Diffuse = R:255 G:120 B:80\n"
				   "    Radius = 200\n"
				   "    Intensity = 3\n"
				   "  End\n"
				   "End\n")
				.empty());
	const ThingTemplate *tt = w.get("L38Campfire");
	REQUIRE(tt != nullptr);
	const auto *data = dynamic_cast<const W3DLightDrawModuleData *>(tt->drawModules().nuggets()[0].data.get());
	REQUIRE(data != nullptr);
	Thing thing(tt);
	Coord3D pos{ 100.0f, 50.0f, 0.0f };
	thing.setPosition(&pos);
	W3DLightDraw light(&thing, data);
	light.setLightPool(&pool);
	REQUIRE(light.lightHandle() == 0);
	const LightPulseFx &l = pool.lights()[0];
	// the parsed colours (INI::parseRGBColor's 0..1), times the Intensity 3
	CHECK(data->m_ambient.red == doctest::Approx(210.0f / 255.0f)); // tolerance: the parser's own 0..255 -> 0..1 rounding
	CHECK(l.ambientNow().red == data->m_ambient.red * 3.0f);
	CHECK(l.ambientNow().blue == data->m_ambient.blue * 3.0f);
	CHECK(l.filterNow().green == data->m_diffuse.green);
	CHECK(l.colorNow().green == data->m_diffuse.green * 3.0f);
	CHECK(l.farAttenuation);
	LightEnvSourceLight s;
	LightPulseToEnvSource(l, s);
	CHECK(s.useFilterDiffuse);
	CHECK(s.farAttenStart == 1.0f);
	CHECK(s.farAttenEnd == 200.0f);
}

TEST_CASE("LOCAL-38: the object shaders take one point light and the light set from the instance's palette block, no viewport-global pulses")
{
	// defaultw3d.fxo blob 151 (classic) and normalmapped.fxo blob 22 (FX): Color * max(1 - d / Range, 0)^2 * max(N.L, 0) per vertex
	W3DShaderKey k;
	k.Texturing = true;
	k.Tex[0] = true;
	k.PriGradient = W3D_GRADIENT_MODULATE;
	const std::string classic = Generate_W3D_Shader_Code(k);
	CHECK(classic.find("w3d_pulse") == std::string::npos);
	CHECK(classic.find("float att = max(1.0 - dl / pl.w, 0.0);") != std::string::npos);
	CHECK(classic.find("return col * (att * att) * max(dot(wn, d / max(dl, 1e-4)), 0.0);") != std::string::npos);
	CHECK(classic.find("int idx = base - " + std::to_string(W3D_PALETTE_LIGHT_TEXELS) + " + k;") != std::string::npos);
	CHECK(classic.find("dsum += w3d_pt;") != std::string::npos);
	CHECK(classic.find("amb += w3d_ol2.rgb;") != std::string::npos);
	CHECK(classic.find("if ((w3d_drop & 2) != 0) lc[1] = vec3(0.0);") != std::string::npos);

	W3DShaderKey f;
	f.Fx = true;
	f.Texturing = true;
	f.Tex[0] = true;
	f.NormalMap = true;
	f.Tangents = true;
	f.PriGradient = W3D_GRADIENT_MODULATE;
	const std::string fx = Generate_W3D_Shader_Code(f);
	CHECK(fx.find("w3d_pulse") == std::string::npos);
	// the point term and the dynamic ambient saturate together with the scene ambient (oD0)
	CHECK(fx.find("col += t0.rgb * (clamp((amb * w3d_ambient_mat + w3d_pex) * 0.5, 0.0, 1.0) - clamp(amb * w3d_ambient_mat * 0.5, 0.0, 1.0)) * 2.0;") !=
		std::string::npos);
	CHECK(fx.find("v_vnrm4.w = w3d_pex.x; v_wnrm4.w = w3d_pex.y; v_wview4.w = w3d_pex.z;") != std::string::npos);
	CHECK(fx.find("int w3d_drop = int(v_instance.y * 0.5);") != std::string::npos);
	// no extra varying: the three normals were vec3 and are vec4 now
	CHECK(fx.find("varying vec3 v_vnrm;") == std::string::npos);
	CHECK(fx.find("varying vec4 v_vnrm4;") != std::string::npos);
}

TEST_CASE("LOCAL-38: the render object's sphere is its BOUNDINGBOX moved by its bone (DONOR BFME2 0x0019DB50), else the meshes' spheres merged")
{
	W3dBoxStruct box = {};
	box.Attributes = 1;
	box.Center = { 0.0f, 0.0f, 9.0f };
	box.Extent = { 3.0f, 4.0f, 12.0f };
	RenderObjPrototype proto;
	proto.Type = RenderObjPrototype::PROTO_HLOD;
	RenderSubObject b;
	b.Name = "GUSOLDIER_SKN.BOUNDINGBOX";
	b.Type = RenderSubObject::SUB_BOX;
	b.Box = &box;
	b.BoneIndex = 1;
	proto.SubObjects.push_back(b);
	std::vector<Matrix3D> pivots(2);
	pivots[1].Row[0][3] = 5.0f; // the box's bone is moved 5 along X
	::Vector3 c;
	float r = 0.0f;
	REQUIRE(W3D_Render_Object_Bounding_Sphere(proto, pivots, c, r));
	CHECK(c.X == 5.0f);
	CHECK(c.Z == 9.0f);
	CHECK(r == 13.0f); // |(3, 4, 12)|

	// another name is not the bounding box: without meshes there is no sphere
	proto.SubObjects[0].Name = "GUSOLDIER_SKN.BOX01";
	CHECK_FALSE(W3D_Render_Object_Bounding_Sphere(proto, pivots, c, r));
}

// OpenBFME. GPL-3.0. Test-only extension entry; compiled by w3d_shader_reuse_test.py.
// The actual factory translation unit is included with only its driver-policy
// query replaced, so both sampler domains can be exercised without switching GPUs.
#include "GodotDevice/GodotW3DMaterial.h"
static W3DSamplerPolicy testSamplerPolicy = W3DSamplerPolicy::Anisotropic;
static size_t testCacheEntryCount = 0;
#include "MaterialUnderTest.cpp"
#include "Common/ArchiveFile.h"
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/godot.hpp>

namespace godot
{
class ShaderTestFiles : public ArchiveFileSystem
{
	std::unique_ptr<ArchiveFile> openArchiveFile(const std::string &, std::string *error) override
	{
		if (error) *error = "shader ownership test does not mount archives";
		return nullptr;
	}
};

class W3DShaderReuseProbe : public RefCounted
{
	GDCLASS(W3DShaderReuseProbe, RefCounted)
protected:
	static void _bind_methods() { ClassDB::bind_static_method("W3DShaderReuseProbe", D_METHOD("run"), &W3DShaderReuseProbe::run); }
public:
	static Dictionary run()
	{
		Dictionary checks;
		ShaderTestFiles fs; // no archives or retail bytes required
		W3DShaderKey key;
		// Placement-ghost permutation: headless dummy rendering cannot preprocess
		// the opaque world's shadow include. This real path has no includes.
		key.AlphaOverride = true;
		key.Texturing = true;
		key.Tex[0] = true;
		auto unrelated = std::make_unique<W3DMaterialFactory>(fs);
		W3DShaderKey other = key;
		other.AlphaTest = true;
		unrelated->Get_Shader(other);
		Ref<ShaderMaterial> materialA, materialB;
		materialA.instantiate(); materialB.instantiate();
		ObjectID firstId;
		{
			auto a = std::make_unique<W3DMaterialFactory>(fs);
			auto b = std::make_unique<W3DMaterialFactory>(fs);
			Ref<Shader> first = a->Get_Shader(key);
			Ref<Shader> second = b->Get_Shader(key);
			firstId = first->get_instance_id();
			checks["same permutation across factories shares resource"] = first.ptr() == second.ptr();
			checks["different permutations never alias"] = unrelated->Get_Shader(other).ptr() != first.ptr() && unrelated->Get_Shader(other)->get_code() != first->get_code();
			checks["local repeated request is stable"] = a->Get_Shader(key).ptr() == first.ptr();
			checks["local reports count requested permutations"] = a->Shader_Count() == 1 && b->Shader_Count() == 1 && a->All_Shaders().size() == 1;
			materialA->set_shader(first); materialB->set_shader(second);
			materialA->set_shader_parameter("w3d_alpha_ref", 0.25);
			materialB->set_shader_parameter("w3d_alpha_ref", 0.75);
			checks["material parameters stay independent"] = double(materialA->get_shader_parameter("w3d_alpha_ref")) == 0.25 && double(materialB->get_shader_parameter("w3d_alpha_ref")) == 0.75;
			testSamplerPolicy = W3DSamplerPolicy::Metal16;
			Ref<Shader> metal = b->Get_Shader(key);
			checks["sampler policies never alias"] = metal.ptr() != second.ptr() && metal->get_code() != second->get_code();
			checks["Metal policy shares only its own domain"] = a->Get_Shader(key).ptr() == metal.ptr();
			testSamplerPolicy = W3DSamplerPolicy::Anisotropic;
			checks["returning to original policy retains its resource"] = a->Get_Shader(key).ptr() == first.ptr() && a->Shader_Count() == 2;
			a.reset();
			checks["destroying one factory preserves other owner"] = b->Get_Shader(key).ptr() == second.ptr();
		}
		checks["materials survive requesting factories"] = ObjectDB::get_instance(firstId) != nullptr && ObjectID(materialA->get_shader()->get_instance_id()) == firstId;
		{
			W3DMaterialFactory fresh(fs);
			Ref<Shader> recreated = fresh.Get_Shader(key);
			// Material retention is independent of cache retention. The unrelated
			// factory must not pin the destroyed factories' cache entry.
			checks["unused shader entry expires despite unrelated factory"] = ObjectID(recreated->get_instance_id()) != firstId;
			checks["recreated shader preserves exact generated text"] = recreated->get_code() == materialA->get_shader()->get_code();
		}
		materialA.unref(); materialB.unref();
		checks["last material releases shader with unrelated factory alive"] = ObjectDB::get_instance(firstId) == nullptr;
		ObjectID finalId;
		{
			W3DMaterialFactory finalFactory(fs);
			finalId = finalFactory.Get_Shader(key)->get_instance_id();
		}
		checks["factory-only shader released immediately"] = ObjectDB::get_instance(finalId) == nullptr;
		unrelated.reset();
		// Vary independent generator inputs in both driver domains. Compare the
		// returned resource's complete source with the unchanged core generator,
		// so accidental cache-key aliasing cannot pass an identity-only check.
		bool shared = true, exact = true, released = true;
		std::vector<ObjectID> matrixIds;
		{
			W3DMaterialFactory a(fs), b(fs);
			for (const auto policy : { W3DSamplerPolicy::Anisotropic, W3DSamplerPolicy::Metal16 })
			{
				testSamplerPolicy = policy;
				for (int bits = 0; bits < 64; ++bits)
				{
					W3DShaderKey variant = key;
					variant.Skin = (bits & 1) != 0;
					variant.AlphaTest = (bits & 2) != 0;
					variant.Unshaded = (bits & 4) != 0;
					variant.ClampU[0] = (bits & 8) != 0;
					variant.NoLod[0] = (bits & 16) != 0;
					variant.CullDisabled = (bits & 32) != 0;
					const Ref<Shader> first = a.Get_Shader(variant), second = b.Get_Shader(variant);
					shared = shared && first.ptr() == second.ptr();
					exact = exact && first->get_code() == toGodot(Generate_W3D_Shader_Code(variant, policy));
					matrixIds.emplace_back(first->get_instance_id());
				}
			}
			checks["matrix requests preserve all 128 local entries"] = a.Shader_Count() == 128 && b.Shader_Count() == 128;
		}
		for (const auto id : matrixIds) released = released && ObjectDB::get_instance(id) == nullptr;
		checks["128 permutation and sampler requests share across factories"] = shared;
		checks["128 requests preserve complete generated shader text"] = exact;
		checks["all matrix shaders release after final owner"] = released;
		testSamplerPolicy = W3DSamplerPolicy::Anisotropic;
#ifndef SHADER_REUSE_BASELINE
		{
			W3DMaterialFactory fresh(fs);
			fresh.Get_Shader(key);
			checks["cache miss prunes all expired matrix metadata"] = testCacheEntryCount == 1;
		}
#endif
		return checks;
	}
};

static void initializeProbe(ModuleInitializationLevel level)
{
	if (level == MODULE_INITIALIZATION_LEVEL_SCENE) ClassDB::register_class<W3DShaderReuseProbe>();
}
} // namespace godot

extern "C" GDExtensionBool GDE_EXPORT shader_reuse_test_init(GDExtensionInterfaceGetProcAddress getProc,
	GDExtensionClassLibraryPtr library, GDExtensionInitialization *initialization)
{
	godot::GDExtensionBinding::InitObject init(getProc, library, initialization);
	init.register_initializer(godot::initializeProbe);
	init.set_minimum_library_initialization_level(godot::MODULE_INITIALIZATION_LEVEL_SCENE);
	return init.init();
}

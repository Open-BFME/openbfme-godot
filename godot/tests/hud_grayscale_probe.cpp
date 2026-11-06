// OpenBFME. GPL-3.0. Compiled only by hud_grayscale_test.py.
// Execute the production grayTextureFor body with real Godot CPU Images.
// The texture boundary records uploads/readbacks without requiring a renderer.
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>
#include <cmath>
#include <map>
#include <string>

namespace godot
{
class RecordedTexture : public RefCounted
{
	GDCLASS(RecordedTexture, RefCounted)
protected:
	static void _bind_methods() {}
public:
	Ref<Image> image;
	static inline int uploads = 0, readbacks = 0;
	static Ref<RecordedTexture> create_from_image(const Ref<Image> &source)
	{
		++uploads;
		Ref<RecordedTexture> texture;
		texture.instantiate();
		texture->image = source;
		return texture;
	}
	Ref<Image> get_image() { ++readbacks; return image; }
};
} // namespace godot

namespace fixture
{
using namespace godot;
using ImageTexture = RecordedTexture;
struct HudDevice
{
	std::map<std::string, Ref<Image>> images;
	std::map<std::string, Ref<ImageTexture>> grayTextures, colours;
	int imageRequests = 0, colourRequests = 0;
	Ref<Image> imageFor(const std::string &name) { ++imageRequests; return images[name]; }
	Ref<ImageTexture> textureFor(const std::string &name)
	{
		++colourRequests;
		auto &texture = colours[name];
		if (texture.is_null() && images[name].is_valid()) texture = ImageTexture::create_from_image(images[name]);
		return texture;
	}
	Ref<ImageTexture> grayTextureFor(const std::string &file);
};
#include "GrayUnderTest.inc"
} // namespace fixture

namespace godot
{
class HudGrayscaleProbe : public RefCounted
{
	GDCLASS(HudGrayscaleProbe, RefCounted)
protected:
	static void _bind_methods() { ClassDB::bind_static_method("HudGrayscaleProbe", D_METHOD("run"), &HudGrayscaleProbe::run); }
public:
	static Dictionary run()
	{
		Dictionary checks;
		fixture::HudDevice hud;
		const uint8_t input[] = {255,0,0,0, 0,255,0,17, 0,0,255,128, 255,255,255,255,
			0,0,0,1, 12,34,56,99, 123,45,67,200, 128,128,128,254};
		const uint8_t expected[] = {76,76,76,0, 150,150,150,17, 29,29,29,128, 255,255,255,255,
			0,0,0,1, 30,30,30,99, 71,71,71,200, 128,128,128,254};
		PackedByteArray pixels, want;
		pixels.resize(sizeof(input)); want.resize(sizeof(expected));
		for (int i = 0; i < int(sizeof(input)); ++i) { pixels[i] = input[i]; want[i] = expected[i]; }
		hud.images["icon"] = Image::create_from_data(4, 2, false, Image::FORMAT_RGBA8, pixels);
		RecordedTexture::uploads = RecordedTexture::readbacks = 0;
		auto gray = hud.grayTextureFor("icon");
		checks["one gray upload without colour upload or readback"] = RecordedTexture::uploads == 1 && RecordedTexture::readbacks == 0 && hud.colourRequests == 0;
		checks["exact luminance bytes and alpha"] = gray.is_valid() && gray->image->get_data() == want;
		checks["source image unchanged"] = hud.images["icon"]->get_data() == pixels;
		checks["cached gray identity without another request"] = hud.grayTextureFor("icon").ptr() == gray.ptr() && hud.imageRequests == 1 && RecordedTexture::uploads == 1;
		auto colour = hud.textureFor("icon");
		checks["later colour texture keeps original pixels"] = colour.is_valid() && colour->image->get_data() == pixels;
		hud.images["colour_first"] = hud.images["icon"];
		auto colourFirst = hud.textureFor("colour_first");
		const int beforeGray = RecordedTexture::uploads;
		auto graySecond = hud.grayTextureFor("colour_first");
		checks["colour first shares CPU source without readback"] = graySecond.is_valid() && graySecond->image->get_data() == want && colourFirst->image->get_data() == pixels && RecordedTexture::readbacks == 0 && RecordedTexture::uploads == beforeGray + 1;
		hud.images["mips"] = hud.images["icon"]->duplicate();
		hud.images["mips"]->generate_mipmaps();
		const auto before = hud.images["mips"]->get_data();
		auto mips = hud.grayTextureFor("mips");
		checks["mipmap dimensions and byte count preserved"] = mips.is_valid() && mips->image->get_width() == 4 && mips->image->get_height() == 2 && mips->image->has_mipmaps() && mips->image->get_data().size() == before.size();
		bool alphaKept = mips.is_valid();
		if (alphaKept)
		{
			const auto after = mips->image->get_data();
			for (int64_t i = 0; i < before.size(); i += 4)
				alphaKept = alphaKept && after[i] == after[i+1] && after[i] == after[i+2] && after[i+3] == before[i+3];
		}
		checks["every mip converted with alpha kept and source intact"] = alphaKept && hud.images["mips"]->get_data() == before;
		// Three synthetic BC3/DXT5 blocks: constant red/green/blue at mip
		// sizes 4x4/2x2/1x1. Index zero selects the explicit alpha and RGB565
		// endpoints; no compressor, DDS file, retail bytes or GPU is needed.
		PackedByteArray blocks, compressedWant;
		blocks.resize(3 * 16); blocks.fill(0);
		const uint16_t rgb565[] = {0xf800, 0x07e0, 0x001f};
		const uint8_t alphas[] = {17, 128, 254}, grays[] = {76, 150, 29};
		for (int level = 0; level < 3; ++level)
		{
			blocks[level*16] = blocks[level*16+1] = alphas[level];
			blocks[level*16+8] = blocks[level*16+10] = uint8_t(rgb565[level]);
			blocks[level*16+9] = blocks[level*16+11] = uint8_t(rgb565[level] >> 8);
			const int side = 4 >> level;
			for (int pixel = 0; pixel < side * side; ++pixel)
			{
				for (int channel = 0; channel < 3; ++channel) compressedWant.append(grays[level]);
				compressedWant.append(alphas[level]);
			}
		}
		hud.images["compressed"] = Image::create_from_data(4, 4, true, Image::FORMAT_DXT5, blocks);
		auto compressed = hud.grayTextureFor("compressed");
		checks["compressed mip exact grayscale and alpha"] = compressed.is_valid() && compressed->image->get_data() == compressedWant;
		checks["compressed mip metadata preserved"] = compressed.is_valid() && compressed->image->get_format() == Image::FORMAT_RGBA8 && compressed->image->has_mipmaps() && compressed->image->get_width() == 4 && compressed->image->get_height() == 4;
		checks["compressed source format and blocks unchanged"] = hud.images["compressed"]->is_compressed() && hud.images["compressed"]->get_format() == Image::FORMAT_DXT5 && hud.images["compressed"]->has_mipmaps() && hud.images["compressed"]->get_data() == blocks;
		const int uploads = RecordedTexture::uploads;
		checks["missing image stays null"] = hud.grayTextureFor("missing").is_null();
		const int requests = hud.imageRequests;
		checks["missing result is cached"] = hud.grayTextureFor("missing").is_null() && hud.imageRequests == requests;
		hud.images["empty"].instantiate();
		checks["empty image creates no texture"] = hud.grayTextureFor("empty").is_null() && RecordedTexture::uploads == uploads;
		return checks;
	}
};
static void initializeProbe(ModuleInitializationLevel level)
{
	if (level == MODULE_INITIALIZATION_LEVEL_SCENE)
	{
		ClassDB::register_class<RecordedTexture>();
		ClassDB::register_class<HudGrayscaleProbe>();
	}
}
} // namespace godot

extern "C" GDExtensionBool GDE_EXPORT hud_grayscale_init(GDExtensionInterfaceGetProcAddress proc,
	GDExtensionClassLibraryPtr library, GDExtensionInitialization *initialization)
{
	godot::GDExtensionBinding::InitObject init(proc, library, initialization);
	init.register_initializer(godot::initializeProbe);
	init.set_minimum_library_initialization_level(godot::MODULE_INITIALIZATION_LEVEL_SCENE);
	return init.init();
}

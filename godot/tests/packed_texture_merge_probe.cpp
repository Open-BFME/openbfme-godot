// OpenBFME. GPL-3.0. Test-only extension, compiled by packed_texture_merge_test.py.
// Both editions' presentation: execute the actual loader and pre-change loader.
#include "GodotDevice/GodotPackedTexture.h"
#define loadPackedTexture baselineLoadPackedTexture
#define mergePackedRgba8 baselineMergeInternal
#include "Baseline.cpp"
#undef mergePackedRgba8
#undef loadPackedTexture
#include "Candidate.cpp"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>

#include <chrono>
#include <map>
#include <set>

using namespace godot;

class MemoryFiles : public AptFileSource
{
public:
	std::map<std::string, std::vector<uint8_t>> files;
	std::map<std::string, int> reads;
	std::set<std::string> unreadable;

	void put(const std::string &name, const PackedByteArray &data)
	{
		files[name] = {data.ptr(), data.ptr() + data.size()};
	}
	bool readFile(const std::string &name, std::vector<uint8_t> &out, std::string *) override
	{
		++reads[name];
		const auto it = files.find(name);
		if (it == files.end() || unreadable.count(name)) return false;
		out = it->second;
		return true;
	}
	bool fileExists(const std::string &name) override { return files.count(name) != 0; }
	std::vector<std::string> listMovies() override { return {}; }
};

static Ref<Image> fixture(int size, bool mask)
{
	PackedByteArray data;
	data.resize(size * size * 4);
	auto *p = data.ptrw();
	for (int i = 0; i < size * size; ++i)
	{
		p[4 * i] = (i * 13 + 7) % 256;
		p[4 * i + 1] = (i * 71 + 31) % 256;
		p[4 * i + 2] = (i * 137 + 179) % 256;
		p[4 * i + 3] = mask ? (i * 19 + 3) % 256 : 211;
	}
	return Image::create_from_data(size, size, false, Image::FORMAT_RGBA8, data);
}

static bool equalImages(const Ref<Image> &a, const Ref<Image> &b)
{
	return a.is_valid() && b.is_valid() && a->get_size() == b->get_size()
		&& a->get_format() == b->get_format() && a->has_mipmaps() == b->has_mipmaps()
		&& a->get_data() == b->get_data();
}

class PackedMergeProbe : public RefCounted
{
	GDCLASS(PackedMergeProbe, RefCounted)
protected:
	static void _bind_methods()
	{
		ClassDB::bind_static_method("PackedMergeProbe", D_METHOD("run"), &PackedMergeProbe::run);
	}
public:
	static Dictionary run()
	{
		Dictionary result, checks;
		// The loader normally resizes companions first. Exercise the internal
		// boundary directly so a future caller cannot read a short mask or
		// silently ignore trailing mask bytes; neither input may be changed.
		for (const int maskSize : {1, 4})
		{
			auto image = fixture(2, false);
			auto wrongMask = fixture(maskSize, true);
			const auto imageBefore = image->get_data(), maskBefore = wrongMask->get_data();
			const bool merged = mergePackedRgba8(image, wrongMask);
			checks[String("mismatched_mask_rejected_") + String::num_int64(maskSize)] = !merged;
			checks[String("mismatched_inputs_unchanged_") + String::num_int64(maskSize)] =
				image->get_data() == imageBefore && wrongMask->get_data() == maskBefore;
		}

		PackedByteArray a, b;
		a.resize(256 * 256 * 4);
		b.resize(a.size());
		auto *pa = a.ptrw();
		auto *pb = b.ptrw();
		for (int x = 0; x < 256; ++x)
			for (int y = 0; y < 256; ++y)
			{
				const int i = 4 * (y * 256 + x);
				for (int c = 0; c < 3; ++c) { pa[i + c] = x; pb[i + c] = y; }
				pa[i + 3] = 255 - y;
				pb[i + 3] = y;
			}
		auto input = Image::create_from_data(256, 256, false, Image::FORMAT_RGBA8, a);
		auto mask = Image::create_from_data(256, 256, false, Image::FORMAT_RGBA8, b);
		Ref<Image> expected = input->duplicate();
		// Independent numerical oracle: the original C++ Godot API operations.
		for (int y = 0; y < 256; ++y)
			for (int x = 0; x < 256; ++x)
			{
				const Color c = expected->get_pixel(x, y), m = mask->get_pixel(x, y);
				expected->set_pixel(x, y, Color(c.r * m.r, c.g * m.g, c.b * m.b, m.a));
			}
		mergePackedRgba8(input, mask);
		checks["all65536_cpp_channel_pairs_and_alpha"] = equalImages(input, expected);
		checks["source_and_mask_arrays_unchanged"] = input->get_data() != a && mask->get_data() == b;
		const auto pixels = expected->get_data();
		int roundDiff = 0, alphaDiff = 0;
		for (int x = 0; x < 256; ++x)
			for (int y = 0; y < 256; ++y)
			{
				const int i = 4 * (y * 256 + x);
				roundDiff += pixels[i] != (x * y + 127) / 255;
				alphaDiff += pixels[i + 3] != (255 - y) * y / 255;
			}
		checks["rounded_negative_control"] = roundDiff == 31770;
		checks["alpha_multiply_negative_control"] = alphaDiff == 65280;
		result["rounded_differences"] = roundDiff;
		result["alpha_differences"] = alphaDiff;

		Array measurements;
		for (const int size : {256, 512, 1024})
		{
			MemoryFiles files;
			const auto jpg = fixture(size, false)->save_jpg_to_buffer(0.95);
			files.put("probe.jpg", jpg);
			files.put("probe.png", fixture(size, true)->save_png_to_buffer());
			bool beforePaired = false, paired = false;
			auto before = baselineLoadPackedTexture(files, "probe", &beforePaired, nullptr);
			auto after = loadPackedTexture(files, "probe", &paired);
			checks[String("full_loader_rgba_") + String::num_int64(size)] = equalImages(before, after) && beforePaired && paired;
			files.reads.clear();
			after = loadPackedTexture(files, "probe", &paired, &jpg);
			checks[String("preloaded_paired_no_reread_") + String::num_int64(size)] = equalImages(before, after)
				&& paired && files.reads["probe.jpg"] == 0 && files.reads["probe.png"] == 1 && files.reads["probe.dds"] == 1;
			checks[String("preloaded_bytes_unchanged_") + String::num_int64(size)] = jpg == fixture(size, false)->save_jpg_to_buffer(0.95);
			Array baselineTimes, candidateTimes;
			// Diagnostic CPU timings only, no noisy performance threshold.
			for (int n = 0; n < 3; ++n)
				for (int which = 0; which < 2; ++which)
				{
					const auto start = std::chrono::steady_clock::now();
					auto image = which ? loadPackedTexture(files, "probe", &paired, &jpg)
						: baselineLoadPackedTexture(files, "probe", &paired, nullptr);
					const auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
					(which ? candidateTimes : baselineTimes).push_back((int64_t)us);
				}
			Dictionary row;
			row["size"] = size; row["baseline_us"] = baselineTimes; row["candidate_us"] = candidateTimes;
			measurements.push_back(row);
			files.put("probe.png", fixture(size / 2, true)->save_png_to_buffer());
			before = baselineLoadPackedTexture(files, "probe", &beforePaired, nullptr);
			after = loadPackedTexture(files, "probe", &paired, &jpg);
			checks[String("nearest_resize_rgba_") + String::num_int64(size)] = equalImages(before, after) && paired && beforePaired;
			auto rgbMask = fixture(size, true);
			rgbMask->convert(Image::FORMAT_RGB8);
			files.put("probe.png", rgbMask->save_png_to_buffer());
			before = baselineLoadPackedTexture(files, "probe", &beforePaired, nullptr);
			after = loadPackedTexture(files, "probe", &paired, &jpg);
			checks[String("rgb_mask_rgba_") + String::num_int64(size)] = equalImages(before, after) && paired && beforePaired;
			if (size != 256) continue;

			files.files.erase("probe.png");
			before = baselineLoadPackedTexture(files, "probe", &beforePaired, nullptr);
			after = loadPackedTexture(files, "probe", &paired, &jpg);
			checks["no_companion"] = !paired && !beforePaired && equalImages(before, after);
			files.files["probe.png"] = {1, 2, 3};
			checks["corrupt_companion"] = loadPackedTexture(files, "probe", &paired, &jpg).is_null();
			files.unreadable.insert("probe.png");
			checks["unreadable_companion"] = loadPackedTexture(files, "probe", &paired, &jpg).is_null();
			files.unreadable.clear();
			files.files.erase("probe.png");
			files.files["probe.jpg"] = {1, 2, 3};
			checks["corrupt_jpg"] = loadPackedTexture(files, "probe", &paired).is_null();
			files.reads.clear();
			after = loadPackedTexture(files, "probe", &paired, &jpg);
			checks["preloaded_jpg_no_reread"] = after.is_valid() && files.reads["probe.jpg"] == 0;
			files.reads.clear();
			checks["old_loader_requires_jpg_read"] = baselineLoadPackedTexture(files, "probe", &paired, &jpg).is_null()
				&& files.reads["probe.jpg"] == 1;
			files.put("probe.jpg", jpg);
			const PackedByteArray bad = PackedByteArray();
			checks["invalid_preloaded_no_fallback"] = loadPackedTexture(files, "probe", &paired, &bad).is_null();
			files.files.erase("probe.jpg");
			checks["missing_jpg"] = loadPackedTexture(files, "probe", &paired).is_null();
			files.files["probe.dds"] = {1, 2, 3};
			checks["corrupt_dds_precedence"] = loadPackedTexture(files, "probe", &paired, &jpg).is_null();
			auto dds = fixture(4, false);
			files.put("probe.dds", dds->save_dds_to_buffer());
			files.files["probe.png"] = {1, 2, 3};
			files.reads.clear();
			after = loadPackedTexture(files, "probe", &paired, &bad);
			checks["valid_dds_precedence"] = equalImages(after, dds) && !paired && files.reads["probe.jpg"] == 0 && files.reads["probe.png"] == 0;
		}
		result["checks"] = checks;
		result["loader_timings"] = measurements;
		return result;
	}
};

static void init(ModuleInitializationLevel level)
{
	if (level == MODULE_INITIALIZATION_LEVEL_SCENE) ClassDB::register_class<PackedMergeProbe>();
}
extern "C" GDExtensionBool GDE_EXPORT packed_merge_init(GDExtensionInterfaceGetProcAddress p, GDExtensionClassLibraryPtr l, GDExtensionInitialization *i)
{
	GDExtensionBinding::InitObject extension(p, l, i);
	extension.register_initializer(init);
	extension.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
	return extension.init();
}

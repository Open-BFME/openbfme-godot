// OpenBFME. GPL-3.0.
//
// Lane CAMP-2: the Godot view of a movie stream (GameClient/VP6Decoder.h): TheVideoPlayer's stream of one movie file, its frames decoded on the render
// clock into a texture. scripts/movie_player.gd plays the campaign, intro and in-game movies with it; the movie's sound is its audio events
// (GameClient/VideoPlayer.h, RW 0x49112C), played by the script.
//   VP6MovieStream.open(path)        the file's container: { ok, error, codec, width, height, frames, fps, duration_ms, alpha }
//   advance_to(frame)                decodes up to frame `frame` (0-based; the frames between are decoded, not shown: VP6 inter frames need them);
//                                    false + get_error() for a damaged frame or a read error
//   get_texture()                    the last decoded frame (RGBA8, display orientation, the header's width x height; the "_with_alpha" movies'
//                                    alpha from their AVhd stream)
//   get_frame()                      the frames decoded so far - 1 (-1 before the first)
//   get_stats()                      { frames_decoded, decode_ms, luma_sum (the last frame's luma sum: tests see the picture change) }
#pragma once

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

class VP6Decoder;
struct EAVP6Movie;

namespace godot
{

class VP6MovieStream : public RefCounted
{
	GDCLASS(VP6MovieStream, RefCounted)

public:
	VP6MovieStream();
	~VP6MovieStream() override;

	Dictionary open(const String &path);
	bool advance_to(int64_t frame);
	Ref<ImageTexture> get_texture() const { return m_texture; }
	int64_t get_frame() const { return m_frame; }
	int64_t get_frame_count() const;
	String get_error() const { return m_error; }
	Dictionary get_stats() const;
	void restart();               // the stream from its first frame again (a looping movie), the same texture
	void set_opaque(bool opaque); // the alpha stream is not applied (a BinkMovie whose _UseAlpha is false)

protected:
	static void _bind_methods();

private:
	bool decodeOne(int64_t index, bool upload);
	bool readChunk(std::uint32_t offset, std::uint32_t size);

	std::unique_ptr<EAVP6Movie> m_movie;
	std::unique_ptr<VP6Decoder> m_video, m_alpha;
	std::ifstream m_file;
	std::vector<std::uint8_t> m_chunk, m_rgba;
	Ref<Image> m_image;
	Ref<ImageTexture> m_texture;
	int64_t m_frame = -1;
	int64_t m_decoded = 0;
	double m_decodeMs = 0.0;
	String m_error;
	bool m_opaque = false;
};

} // namespace godot

// OpenBFME. GPL-3.0.
// See GodotDevice/GodotVideoStream.h.

#include "GodotDevice/GodotVideoStream.h"

#include "GameClient/VP6Decoder.h"

#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

#include <algorithm>
#include <cstring>

namespace godot
{

VP6MovieStream::VP6MovieStream() = default;
VP6MovieStream::~VP6MovieStream() = default;

void VP6MovieStream::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("open", "path"), &VP6MovieStream::open);
	ClassDB::bind_method(D_METHOD("advance_to", "frame"), &VP6MovieStream::advance_to);
	ClassDB::bind_method(D_METHOD("get_texture"), &VP6MovieStream::get_texture);
	ClassDB::bind_method(D_METHOD("get_frame"), &VP6MovieStream::get_frame);
	ClassDB::bind_method(D_METHOD("get_frame_count"), &VP6MovieStream::get_frame_count);
	ClassDB::bind_method(D_METHOD("get_error"), &VP6MovieStream::get_error);
	ClassDB::bind_method(D_METHOD("get_stats"), &VP6MovieStream::get_stats);
	ClassDB::bind_method(D_METHOD("restart"), &VP6MovieStream::restart);
	ClassDB::bind_method(D_METHOD("set_opaque", "opaque"), &VP6MovieStream::set_opaque);
}

Dictionary VP6MovieStream::open(const String &path)
{
	Dictionary d;
	d["ok"] = false;
	const std::string p = path.utf8().get_data();
	m_movie = std::make_unique<EAVP6Movie>();
	std::string error;
	if (!EAVP6Movie::parseFile(p, *m_movie, &error))
	{
		m_error = String::utf8((p + ": " + error).c_str());
		d["error"] = m_error;
		m_movie.reset();
		return d;
	}
	m_file.close();
	m_file.clear();
	m_file.open(p, std::ios::binary);
	if (!m_file)
	{
		m_error = String::utf8(("cannot open " + p).c_str());
		d["error"] = m_error;
		m_movie.reset();
		return d;
	}
	m_video = std::make_unique<VP6Decoder>();
	m_alpha = m_movie->alpha.empty() ? nullptr : std::make_unique<VP6Decoder>();
	m_frame = -1;
	m_decoded = 0;
	m_decodeMs = 0.0;
	m_error = String();
	if (m_movie->width == 0 || m_movie->height == 0 || m_movie->width > EAVP6Movie::kMaxDimension || m_movie->height > EAVP6Movie::kMaxDimension)
	{
		// EAVP6Movie::parseFile refuses these already; never let such a size reach Godot's Image (review r1)
		m_error = String::utf8((p + ": the picture size is outside the codec's").c_str());
		d["error"] = m_error;
		m_movie.reset();
		return d;
	}
	const int w = (int)m_movie->width, h = (int)m_movie->height;
	m_rgba.assign((size_t)w * (size_t)h * 4, 0);
	for (size_t i = 3; i < m_rgba.size(); i += 4)
	{
		m_rgba[i] = 255; // black until the first frame
	}
	PackedByteArray px;
	px.resize((int64_t)m_rgba.size());
	std::memcpy(px.ptrw(), m_rgba.data(), m_rgba.size());
	m_image = Image::create_from_data(w, h, false, Image::FORMAT_RGBA8, px);
	m_texture = ImageTexture::create_from_image(m_image);
	d["ok"] = true;
	d["codec"] = String(m_movie->codec.c_str());
	d["width"] = (int64_t)w;
	d["height"] = (int64_t)h;
	d["frames"] = (int64_t)m_movie->video.size();
	d["header_frames"] = (int64_t)m_movie->frames;
	d["fps"] = (double)m_movie->rateNumerator / (double)m_movie->rateDenominator;
	d["duration_ms"] = m_movie->durationMs();
	d["alpha"] = m_alpha != nullptr;
	return d;
}

int64_t VP6MovieStream::get_frame_count() const { return m_movie ? (int64_t)m_movie->video.size() : 0; }

bool VP6MovieStream::readChunk(std::uint32_t offset, std::uint32_t size)
{
	m_chunk.resize(size);
	m_file.clear();
	m_file.seekg((std::streamoff)offset);
	m_file.read((char *)m_chunk.data(), (std::streamsize)size);
	return (std::uint32_t)m_file.gcount() == size;
}

bool VP6MovieStream::decodeOne(int64_t index, bool upload)
{
	const EAVP6Chunk &c = m_movie->video[(size_t)index];
	std::string error;
	if (!readChunk(c.offset, c.size))
	{
		m_error = String::utf8(("read error at frame " + std::to_string(index)).c_str());
		return false;
	}
	if (!m_video->decodeFrame(m_chunk.data(), m_chunk.size(), &error))
	{
		m_error = String::utf8(("frame " + std::to_string(index) + ": " + error).c_str());
		return false;
	}
	if (m_alpha && (size_t)index < m_movie->alpha.size())
	{
		const EAVP6Chunk &a = m_movie->alpha[(size_t)index];
		if (!readChunk(a.offset, a.size) || !m_alpha->decodeFrame(m_chunk.data(), m_chunk.size(), &error))
		{
			m_error = String::utf8(("alpha frame " + std::to_string(index) + ": " + error).c_str());
			return false;
		}
	}
	m_decoded += 1;
	if (upload)
	{
		const int w = (int)m_movie->width, h = (int)m_movie->height;
		m_video->toRGBA(w, h, m_rgba.data(), m_opaque ? nullptr : m_alpha.get());
		PackedByteArray px;
		px.resize((int64_t)m_rgba.size());
		std::memcpy(px.ptrw(), m_rgba.data(), m_rgba.size());
		m_image->set_data(w, h, false, Image::FORMAT_RGBA8, px);
		m_texture->update(m_image);
	}
	return true;
}

bool VP6MovieStream::advance_to(int64_t frame)
{
	if (!m_movie)
	{
		m_error = "no movie is open";
		return false;
	}
	const int64_t last = std::min<int64_t>(frame, (int64_t)m_movie->video.size() - 1);
	const uint64_t start = Time::get_singleton()->get_ticks_usec();
	while (m_frame < last)
	{
		if (!decodeOne(m_frame + 1, m_frame + 1 == last))
		{
			return false;
		}
		m_frame += 1;
	}
	m_decodeMs += (double)(Time::get_singleton()->get_ticks_usec() - start) / 1000.0;
	return true;
}

void VP6MovieStream::restart()
{
	if (!m_movie)
	{
		return;
	}
	m_video = std::make_unique<VP6Decoder>();
	m_alpha = m_movie->alpha.empty() ? nullptr : std::make_unique<VP6Decoder>();
	m_frame = -1;
}

void VP6MovieStream::set_opaque(bool opaque) { m_opaque = opaque; }

Dictionary VP6MovieStream::get_stats() const
{
	Dictionary d;
	d["frames_decoded"] = m_decoded;
	d["decode_ms"] = m_decodeMs;
	int64_t sum = 0;
	if (m_video && m_video->hasFrame())
	{
		for (int y = 0; y < m_video->codedHeight(); y += 4)
		{
			for (int x = 0; x < m_video->codedWidth(); x += 4)
			{
				sum += m_video->lumaAt(x, y);
			}
		}
	}
	d["luma_sum"] = sum;
	return d;
}

} // namespace godot

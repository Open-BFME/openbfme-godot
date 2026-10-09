// OpenBFME AUDIO-1 dev tool. GPL-3.0.
//
// audio_decode_cli [--ima-multiply] [--info] [--stream N] [--time] <file>
// Decodes a WAV / MP3 file with AudioDecode and writes the raw interleaved signed 16-bit little-endian
// samples to stdout (nothing with --info). One line "format=... rate=... channels=... frames=..." goes to
// stderr, then one "warning: ..." line per reported defect. Exit status: 0 ok, 1 decode error (message on
// stderr), 2 usage / I/O error. tools/audio/oracle_check.py drives it against ffmpeg / mpg123.
//   --ima-multiply  IMA ADPCM with ffmpeg's ((2m+1)*step)>>3 arithmetic instead of the reference series
//   --stream N      MP3 only: read through Mp3Stream in chunks of N sample frames instead of the whole-file decode
//   --time          print the decode time in milliseconds to stderr
// audio_decode_cli --compare a.raw b.raw
//   compares two raw s16le files over their common length and prints "n=.. lenA=.. lenB=.. max=.. rms=.." on stdout.

#include "Common/Audio/AudioDecode.h"
#include "Common/Audio/Mp3Decoder.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

static int compareRaw(const char *pa, const char *pb)
{
	std::vector<int16_t> v[2];
	const char *paths[2] = {pa, pb};
	for (int k = 0; k < 2; ++k)
	{
		FILE *f = std::fopen(paths[k], "rb");
		if (!f)
		{
			std::fprintf(stderr, "cannot open %s\n", paths[k]);
			return 2;
		}
		int16_t buf[4096];
		size_t n;
		while ((n = std::fread(buf, sizeof(int16_t), 4096, f)) > 0) v[k].insert(v[k].end(), buf, buf + n);
		std::fclose(f);
	}
	size_t n = v[0].size() < v[1].size() ? v[0].size() : v[1].size();
	long maxDiff = 0;
	double sum = 0;
	for (size_t i = 0; i < n; ++i)
	{
		long d = long(v[0][i]) - long(v[1][i]);
		long a = d < 0 ? -d : d;
		if (a > maxDiff) maxDiff = a;
		sum += double(d) * double(d);
	}
	std::printf("n=%zu lenA=%zu lenB=%zu max=%ld rms=%.5f\n", n, v[0].size(), v[1].size(), maxDiff, n ? std::sqrt(sum / double(n)) : 0.0);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc == 4 && !std::strcmp(argv[1], "--compare")) return compareRaw(argv[2], argv[3]);
	bool info = false, multiply = false, timing = false;
	size_t streamChunk = 0;
	const char *path = nullptr;
	for (int i = 1; i < argc; ++i)
	{
		if (!std::strcmp(argv[i], "--info")) info = true;
		else if (!std::strcmp(argv[i], "--ima-multiply")) multiply = true;
		else if (!std::strcmp(argv[i], "--time")) timing = true;
		else if (!std::strcmp(argv[i], "--stream") && i + 1 < argc) streamChunk = size_t(std::strtoul(argv[++i], nullptr, 10));
		else if (argv[i][0] != '-') path = argv[i];
		else
		{
			std::fprintf(stderr, "usage: audio_decode_cli [--ima-multiply] [--info] [--stream N] [--time] <file>\n");
			return 2;
		}
	}
	if (!path)
	{
		std::fprintf(stderr, "usage: audio_decode_cli [--ima-multiply] [--info] [--stream N] [--time] <file>\n");
		return 2;
	}
	FILE *f = std::fopen(path, "rb");
	if (!f)
	{
		std::fprintf(stderr, "cannot open %s\n", path);
		return 2;
	}
	std::vector<uint8_t> bytes;
	{
		uint8_t buf[65536];
		size_t n;
		while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) bytes.insert(bytes.end(), buf, buf + n);
	}
	std::fclose(f);

	std::vector<std::string> warnings;
	AudioDecode::DecodeOptions options;
	if (multiply) options.ima = AudioDecode::ImaRounding::FfmpegMultiply;
	try
	{
		auto t0 = std::chrono::steady_clock::now();
		AudioDecode::DecodedAudio audio;
		if (info)
		{
			audio.info = AudioDecode::probe(bytes.data(), bytes.size(), &warnings);
		}
		else if (streamChunk && AudioDecode::sniff(bytes.data(), bytes.size()) == AudioDecode::Container::Mp3)
		{
			AudioDecode::Mp3Stream stream(bytes.data(), bytes.size(), &warnings);
			audio.info = stream.info();
			std::vector<int16_t> chunk(streamChunk * stream.info().channels);
			size_t n;
			while ((n = stream.read(chunk.data(), streamChunk)) > 0)
			{
				audio.pcm.insert(audio.pcm.end(), chunk.begin(), chunk.begin() + n * stream.info().channels);
			}
		}
		else
		{
			audio = AudioDecode::decode(bytes.data(), bytes.size(), &warnings, &options);
		}
		auto t1 = std::chrono::steady_clock::now();
		std::fprintf(stderr, "format=%s rate=%u channels=%u frames=%llu bits=%u blockAlign=%u\n", AudioDecode::formatName(audio.info.format),
			audio.info.sampleRate, audio.info.channels, (unsigned long long)audio.info.frames, audio.info.bitsPerSample, audio.info.blockAlign);
		for (const std::string &w : warnings) std::fprintf(stderr, "warning: %s\n", w.c_str());
		if (timing) std::fprintf(stderr, "time_ms=%.3f\n", std::chrono::duration<double, std::milli>(t1 - t0).count());
		if (!info)
		{
#ifdef _WIN32
			_setmode(_fileno(stdout), _O_BINARY);
#endif
			if (!audio.pcm.empty()) std::fwrite(audio.pcm.data(), sizeof(int16_t), audio.pcm.size(), stdout);
		}
	}
	catch (const std::exception &e)
	{
		for (const std::string &w : warnings) std::fprintf(stderr, "warning: %s\n", w.c_str());
		std::fprintf(stderr, "error: %s\n", e.what());
		return 1;
	}
	return 0;
}

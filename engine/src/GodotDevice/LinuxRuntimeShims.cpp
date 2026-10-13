// OpenBFME. GPL-3.0. Lane DEP-2: two C library functions the Linux GDExtension provides itself, so it needs no glibc newer than 2.18.
//
// The extension is linked with -Wl,--wrap=powf,--wrap=getentropy (engine/CMakeLists.txt, Linux only): every reference to powf / getentropy in
// the extension's objects and in the static libstdc++ / godot-cpp code it links resolves to the __wrap_ function below instead of glibc's.
//   powf        GLIBC_2.27 (glibc's new single-precision pow): our code (Mp3Decoder, w3dshadergen) and godot-cpp's Color call it. Client code only:
//               the simulation never calls powf. Computed as the deterministic double pow rounded to float: the correctly rounded float except
//               near ties (glibc's powf is within 0.82 float ulp, so the two differ in the last bit now and then; no game state depends on it).
//   getentropy  GLIBC_2.25: libstdc++'s std::random_device (CreateAHeroHeroList's GUID, RW 0x80A3A8 CoCreateGuid) may read it. The same
//               contract as glibc's sysdeps/unix/sysv/linux/getentropy.c: at most 256 bytes (else EIO), the getrandom system call until the
//               buffer is full (EINTR retried), -1 with errno on any other error (ENOSYS on a kernel older than 3.17).
// The one import above GLIBC 2.17 that stays is __cxa_thread_atexit_impl (GLIBC_2.18): libstdc++ registers the destructors of thread_local
// objects through it (docs/LINUX_DEPENDENCIES.md).

#include "Common/DeterministicLibm.h"

#include <cerrno>
#include <cstddef>
#include <sys/syscall.h>
#include <unistd.h>

extern "C" {

float __wrap_powf(float x, float y)
{
	return (float)DetLibm::pow((double)x, (double)y);
}

int __wrap_getentropy(void *buffer, std::size_t length)
{
	if (length > 256)
	{
		errno = EIO;
		return -1;
	}
	unsigned char *p = static_cast<unsigned char *>(buffer);
	unsigned char *const end = p + length;
	while (p < end)
	{
		const long got = syscall(SYS_getrandom, p, (std::size_t)(end - p), 0);
		if (got < 0)
		{
			if (errno == EINTR)
			{
				continue;
			}
			return -1;
		}
		p += got;
	}
	return 0;
}
}

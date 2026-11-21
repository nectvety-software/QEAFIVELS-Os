/* SPDX-License-Identifier: MIT */
/* Stub for pd-mapper's optional liblzma helper. Vendor firmware *.jsn.xz
 * files are skipped (raw *.jsn files are what the chopin dump carries), and
 * a missing blob must never be invented. */
#include <errno.h>
#include <fcntl.h>

int lzma_decomp(const char *file)
{
	(void)file;
	errno = ENOSYS;
	return -1;
}

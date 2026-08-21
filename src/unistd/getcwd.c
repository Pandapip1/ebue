/* getcwd returns the DOS form, C:\dir, with backslashes turned into
 * forward slashes so that programs that split paths on '/' (which is
 * most of them) keep working.  A trailing slash is removed except at a
 * drive root. */
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "libc.h"

char *getcwd(char *buf, size_t size)
{
	WCHAR w[4096];
	char tmp[4096 * 3];
	ULONG n;
	size_t i, len;
	int r;

	n = RtlGetCurrentDirectory_U(sizeof w, w);
	if (!n || n > sizeof w) { errno = ERANGE; return 0; }
	n /= sizeof(WCHAR);
	for (i = 0; i < n; i++) if (w[i] == '\\') w[i] = '/';
	if (n > 3 && w[n-1] == '/') n--;
	r = __utf16_to_utf8_buf(w, n, tmp, sizeof tmp);
	if (r < 0) return 0;
	len = (size_t)r;
	if (!buf) {
		if (!size) size = len + 1;
		if (len + 1 > size) { errno = ERANGE; return 0; }
		buf = malloc(size);
		if (!buf) return 0;
	} else if (!size) {
		errno = EINVAL; return 0;
	} else if (len + 1 > size) {
		errno = ERANGE; return 0;
	}
	memcpy(buf, tmp, len + 1);
	return buf;
}

char *get_current_dir_name(void)
{
	return getcwd(0, 0);
}

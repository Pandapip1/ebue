#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "libc.h"

int chdir(const char *path)
{
	WCHAR *w;
	size_t n, i;
	UNICODE_STRING us;
	NTSTATUS st;

	if (!path || !*path) { errno = ENOENT; return -1; }
	w = __utf8_to_utf16(path, &n);
	if (!w) return -1;
	for (i = 0; i < n; i++) if (w[i] == '/') w[i] = '\\';
	us.Buffer = w;
	us.Length = (USHORT)(n * sizeof(WCHAR));
	us.MaximumLength = us.Length + sizeof(WCHAR);
	st = RtlSetCurrentDirectory_U(&us);
	__free(w);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}

int fchdir(int fd)
{
	char *p = __handle_path(__fd_handle(fd));
	int r;
	if (!p) return -1;
	r = chdir(p);
	__free(p);
	return r;
}

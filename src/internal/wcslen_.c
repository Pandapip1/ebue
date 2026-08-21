#include "libc.h"
size_t wcslen_(const WCHAR *s)
{
	size_t n = 0;
	while (s[n]) n++;
	return n;
}

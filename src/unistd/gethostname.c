#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

int gethostname(char *name, size_t len)
{
	char *h = getenv("COMPUTERNAME");
	size_t n;
	if (!h) h = "localhost";
	n = strlen(h);
	if (n >= len) { memcpy(name, h, len); errno = ENAMETOOLONG; return -1; }
	memcpy(name, h, n + 1);
	return 0;
}

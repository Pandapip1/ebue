#include <unistd.h>
#include <time.h>
#include <errno.h>
#include "libc.h"

int nanosleep(const struct timespec *req, struct timespec *rem)
{
	LARGE_INTEGER t;
	if (!req || req->tv_nsec < 0 || req->tv_nsec >= 1000000000L || req->tv_sec < 0) { errno = EINVAL; return -1; }
	t = -(req->tv_sec * 10000000LL + (req->tv_nsec + 99) / 100);
	if (!t) t = -1;
	NtDelayExecution(0, &t);
	if (rem) rem->tv_sec = rem->tv_nsec = 0;
	return 0;
}

unsigned sleep(unsigned s)
{
	struct timespec ts = { s, 0 };
	nanosleep(&ts, 0);
	return 0;
}

int usleep(unsigned us)
{
	struct timespec ts = { us / 1000000, (us % 1000000) * 1000 };
	return nanosleep(&ts, 0);
}

int pause(void)
{
	LARGE_INTEGER never = 0x7fffffffffffffffLL;
	NtDelayExecution(1, &never);
	errno = EINTR;
	return -1;
}

unsigned alarm(unsigned s) { (void)s; return 0; }

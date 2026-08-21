#include <stdlib.h>
#include <signal.h>
#include "libc.h"

_Noreturn void abort(void)
{
	__raise_internal(SIGABRT);
	/* If a handler returned, or SIGABRT was ignored, die anyway -- with
	 * the status a Unix process killed by SIGABRT would have. */
	__nt_exit(128 + SIGABRT);
}

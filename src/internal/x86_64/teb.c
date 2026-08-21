#include "libc.h"
PTEB __teb(void)
{
	PTEB t;
	__asm__ __volatile__("movq %%gs:0x30, %0" : "=r"(t));
	return t;
}

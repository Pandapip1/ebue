#include "libc.h"
PTEB __teb(void)
{
	PTEB t;
	__asm__ __volatile__("movl %%fs:0x18, %0" : "=r"(t));
	return t;
}

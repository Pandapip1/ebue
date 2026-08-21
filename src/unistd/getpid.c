#include <unistd.h>
#include "libc.h"

pid_t getpid(void)
{
	return (pid_t)(ULONG_PTR)__teb()->ClientId.UniqueProcess;
}

pid_t getppid(void)
{
	PROCESS_BASIC_INFORMATION pbi;
	if (!NT_SUCCESS(NtQueryInformationProcess(NtCurrentProcess(), ProcessBasicInformation, &pbi, sizeof pbi, 0)))
		return 1;
	return (pid_t)pbi.InheritedFromUniqueProcessId;
}

pid_t gettid(void)
{
	return (pid_t)(ULONG_PTR)__teb()->ClientId.UniqueThread;
}

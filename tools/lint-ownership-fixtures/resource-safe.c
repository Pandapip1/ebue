/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

typedef __SIZE_TYPE__ size_t;
typedef struct file FILE;
int open(const char *, int, ...);
int close(int);
long write(int, const void *, size_t);
FILE *fopen(const char *, const char *);
int fclose(FILE *);

void descriptor(void)
{
	int fd = open("name", 0);
	if (fd < 0)
		return;
	write(fd, "x", 1);
	close(fd);
}

void stream(void)
{
	FILE *file = fopen("name", "r");
	if (file)
		fclose(file);
}

/* NT's own syscalls, unlike open()/socket()/..., never return the handle
 * they acquire -- they return an NTSTATUS and write the handle through
 * an out-pointer instead. Without ResourceLifecycleChecker knowing that
 * shape, this exact function would have reported "resource is not
 * proven live" on NtClose(h) below: acquiredFamily()'s old
 * Call.getReturnValue()-only tracking could never see a handle that
 * never comes back as a return value, so this codebase's entire NT
 * backend -- every NtCreateFile/NtOpenFile/NtCreateEvent/... acquisition
 * followed by NtClose -- was structurally unprovable. Pinned here so a
 * regression in handleOutParamArgument() is caught locally rather than
 * silently reappearing as ~100 findings tree-wide. */
typedef long NTSTATUS;
typedef void *HANDLE;
NTSTATUS NtCreateFile(HANDLE *out, int access, void *oa, void *io,
                      long alloc, unsigned attrs, unsigned share,
                      unsigned disp, unsigned options, void *ea,
                      unsigned ealength);
NTSTATUS NtClose(HANDLE h);

void handle_lifecycle(void)
{
	HANDLE h;
	NtCreateFile(&h, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	NtClose(h);
}

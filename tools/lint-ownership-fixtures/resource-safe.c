/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

typedef __SIZE_TYPE__ size_t;
typedef struct file FILE;
int open(const char *, int, ...);
int close(int);
long write(int, const void *, size_t);
FILE *fopen(const char *, const char *);
int fclose(FILE *);
int fflush(FILE *);

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

/* A descriptor received as a plain parameter -- posix_close(int fd)'s
 * own shape (src/unistd/posix_close.c: `return close(fd);`), and
 * closedir()'s `dp->fd` read through a borrowed struct pointer. Just
 * like Ownership's release_borrow (see safe.c's own comment for the
 * full reasoning), ResourceMap can only ever gain a live entry for a
 * symbol by watching THIS analysis's own open()/socket()/opendir()/...
 * acquire it; a parameter's value exists before any code in this
 * function has run, so no code on the callee side can ever satisfy that
 * check. A literal, made-up descriptor is real evidence of a bug and is
 * still reported (see resource-unsafe.c's bogus_literal). */
void descriptor_borrow(int fd)
{
	write(fd, "x", 1);
}

/* fflush(NULL) is ISO C's own "flush every open stream" spelling (7.21.5.2p2),
 * not a use of any one, specific FILE* this checker could ever have proof
 * for -- see __assert_fail's real fflush(0) in src/exit/assert.c, matching
 * musl's own convention here. */
void flush_all(void)
{
	fflush(0);
}

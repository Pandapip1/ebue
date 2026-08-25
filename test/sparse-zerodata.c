/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * FSCTL_SET_ZERO_DATA: what NT actually does to a 65536-byte file when
 * asked to zero 8192..40960, for a file previously marked sparse with
 * FSCTL_SET_SPARSE and for one that was never marked.
 *
 * WHY THIS FILE EXISTS
 *
 * Wine implements FSCTL_SET_SPARSE -- it persists the mark in a
 * `user.WINESPARSE` extended attribute -- but not FSCTL_SET_ZERO_DATA,
 * the call that actually punches the hole.  So under Wine a program can
 * mark a file sparse and then never create a hole in it, and every
 * question about the resulting file's *allocation* is unanswerable
 * there.  This binary is therefore a probe whose only real oracle is the
 * `windows-test` CI leg (real Windows Server 2025); under Wine it exits
 * 77 naming the missing mechanism rather than passing vacuously.
 *
 * THE UNMARKED CASE IS THE DISCRIMINATING ONE, AND ONE NUMBER IS NOT
 * ENOUGH TO SETTLE IT
 *
 * "The range read back as zero" is true both when NT zero-filled the
 * bytes in place and when NT deallocated the clusters underneath them --
 * a hole reads as zero.  The two are distinguished only by
 * AllocationSize: zeroing in place leaves it unchanged, deallocating
 * drops it.  So both EndOfFile and AllocationSize are captured before
 * and after via NtQueryInformationFile(FileStandardInformation), and the
 * verdict below is printed explicitly rather than left to be inferred
 * from the numbers by whoever reads the log.
 *
 * WHAT IS ASSERTED AND WHAT IS ONLY MEASURED
 *
 * Asserted (these can turn the leg red): if the ioctl reports success,
 * then the requested range must read back as zero, the bytes on either
 * side of it must be untouched, and EndOfFile must be unchanged --
 * zeroing a range is not a truncation.  Those are the documented
 * contract, and a violation is a real finding.
 *
 * Only measured (never asserted): AllocationSize.  Whether NT
 * deallocates for a non-sparse file is exactly the open question this
 * probe exists to answer; asserting an expected value here would be
 * deriving the answer instead of observing it.
 *
 * If the ioctl fails, the contract assertions have no input to run
 * against.  A not-supported-family status makes the whole binary
 * unverified (rc=77) naming the status; any other failure status is
 * printed just as prominently and also leaves the case unverified,
 * because a status is a measurement and this file's job is to report
 * measurements.
 *
 * Constants and structure layouts are copied by hand from mingw-w64's
 * winioctl.h (FSCTL_SET_SPARSE / FSCTL_SET_ZERO_DATA at :1509-1510,
 * FILE_SET_SPARSE_BUFFER at :1905, FILE_ZERO_DATA_INFORMATION at :1909)
 * and from src/internal/nt.h (FILE_STANDARD_INFORMATION,
 * FileStandardInformation = 5), because test/*.c is not built with
 * src/internal/ on the include path -- the same hand-declaration
 * convention test/posix-errno.c and test/posix-signal.c already use.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

static int fails;
static int unverified;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* ---- hand-declared NT surface ---------------------------------- */
typedef int NTSTATUS;
typedef void *HANDLE;
typedef void *PVOID;
typedef unsigned long ULONG;
typedef unsigned char BOOLEAN;
typedef long long LONGLONG;
#ifdef __i386__
#define NTAPI __attribute__((stdcall))
typedef unsigned long ULONG_PTR;
#else
#define NTAPI
typedef unsigned long long ULONG_PTR;
#endif
#define NT_SUCCESS(s) ((NTSTATUS)(s) >= 0)

typedef struct _IO_STATUS_BLOCK {
	union { NTSTATUS Status; PVOID Pointer; };
	ULONG_PTR Information;
} IO_STATUS_BLOCK;

typedef struct _FILE_STANDARD_INFORMATION {
	LONGLONG AllocationSize;
	LONGLONG EndOfFile;
	ULONG NumberOfLinks;
	BOOLEAN DeletePending;
	BOOLEAN Directory;
} FILE_STANDARD_INFORMATION;
#define FileStandardInformation 5

typedef struct _FILE_SET_SPARSE_BUFFER { BOOLEAN SetSparse; } FILE_SET_SPARSE_BUFFER;
typedef struct _FILE_ZERO_DATA_INFORMATION {
	LONGLONG FileOffset;
	LONGLONG BeyondFinalZero;
} FILE_ZERO_DATA_INFORMATION;

/* CTL_CODE(FILE_DEVICE_FILE_SYSTEM=9, fn, METHOD_BUFFERED=0, access)
 * == (9 << 16) | (access << 14) | (fn << 2) | 0.
 * SET_SPARSE:    fn 49, FILE_SPECIAL_ACCESS (== FILE_ANY_ACCESS, 0).
 * SET_ZERO_DATA: fn 50, FILE_WRITE_DATA (2). */
#define FSCTL_SET_SPARSE     0x000900C4UL
#define FSCTL_SET_ZERO_DATA  0x000980C8UL

#define STATUS_NOT_IMPLEMENTED        ((NTSTATUS)0xC0000002L)
#define STATUS_INVALID_DEVICE_REQUEST ((NTSTATUS)0xC0000010L)
#define STATUS_NOT_SUPPORTED          ((NTSTATUS)0xC00000BBL)

NTSTATUS NTAPI NtFsControlFile(HANDLE, HANDLE, PVOID, PVOID, IO_STATUS_BLOCK *, ULONG, PVOID, ULONG, PVOID, ULONG);
NTSTATUS NTAPI NtQueryInformationFile(HANDLE, IO_STATUS_BLOCK *, PVOID, ULONG, int);

/* src/internal/libc.h: "HANDLE __fd_handle(int fd)  -- NULL with errno=EBADF". */
HANDLE __fd_handle(int fd);

/* ---- probe parameters ------------------------------------------- */
#define FILE_SIZE  65536
#define ZERO_FROM   8192
#define ZERO_TO    40960
#define FILL_BYTE  0xAB

static int not_supported(NTSTATUS st)
{
	return st == STATUS_NOT_SUPPORTED || st == STATUS_INVALID_DEVICE_REQUEST ||
	       st == STATUS_NOT_IMPLEMENTED;
}

static int query_std(HANDLE h, FILE_STANDARD_INFORMATION *si, const char *when, const char *tag)
{
	IO_STATUS_BLOCK io;
	NTSTATUS st;
	memset(si, 0, sizeof *si);
	st = NtQueryInformationFile(h, &io, si, (ULONG)sizeof *si, FileStandardInformation);
	if (!NT_SUCCESS(st)) {
		printf("zerodata[%s]: NtQueryInformationFile(FileStandardInformation) %s "
		       "-> 0x%08lX; nothing further can be measured for this case\n",
		       tag, when, (unsigned long)(unsigned)st);
		return 0;
	}
	return 1;
}

/* Runs one case.  `mark_sparse` selects (a) vs (b). */
static void run_case(const char *tag, int mark_sparse)
{
	char path[64];
	unsigned char *buf;
	FILE_STANDARD_INFORMATION before, after;
	IO_STATUS_BLOCK io;
	FILE_ZERO_DATA_INFORMATION zd;
	NTSTATUS st_sparse = 0, st_zero;
	HANDLE h;
	int fd, i, range_zero = 1, outside_intact = 1;
	long long delta;

	snprintf(path, sizeof path, "zd-%s.bin", tag);
	buf = malloc(FILE_SIZE);
	if (!buf) { printf("FAIL %s: malloc\n", tag); fails++; return; }

	fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0600);
	if (fd < 0) { printf("FAIL %s: open: %s\n", tag, strerror(errno)); fails++; free(buf); return; }

	h = __fd_handle(fd);
	if (!h) { printf("FAIL %s: __fd_handle: %s\n", tag, strerror(errno)); fails++; close(fd); free(buf); return; }

	/* (a) mark sparse before the data is written -- the canonical order,
	 * and the one a caller that wants a sparse file would use. */
	if (mark_sparse) {
		FILE_SET_SPARSE_BUFFER sb;
		sb.SetSparse = 1;
		st_sparse = NtFsControlFile(h, 0, 0, 0, &io, FSCTL_SET_SPARSE, &sb, (ULONG)sizeof sb, 0, 0);
		printf("zerodata[%s]: FSCTL_SET_SPARSE -> 0x%08lX\n", tag, (unsigned long)(unsigned)st_sparse);
		if (!NT_SUCCESS(st_sparse)) {
			printf("SKIP zerodata[%s]: the file could not be marked sparse "
			       "(FSCTL_SET_SPARSE -> 0x%08lX), so the sparse case was not "
			       "measured\n", tag, (unsigned long)(unsigned)st_sparse);
			unverified++;
			close(fd); free(buf);
			return;
		}
	}

	memset(buf, FILL_BYTE, FILE_SIZE);
	if (write(fd, buf, FILE_SIZE) != FILE_SIZE) {
		printf("FAIL %s: short write: %s\n", tag, strerror(errno)); fails++;
		close(fd); free(buf); return;
	}
	if (fsync(fd) != 0) { printf("FAIL %s: fsync: %s\n", tag, strerror(errno)); fails++; }

	if (!query_std(h, &before, "before", tag)) { unverified++; close(fd); free(buf); return; }

	zd.FileOffset = ZERO_FROM;
	zd.BeyondFinalZero = ZERO_TO;
	st_zero = NtFsControlFile(h, 0, 0, 0, &io, FSCTL_SET_ZERO_DATA, &zd, (ULONG)sizeof zd, 0, 0);

	printf("zerodata[%s]: FSCTL_SET_ZERO_DATA [%d,%d) -> 0x%08lX\n",
	       tag, ZERO_FROM, ZERO_TO, (unsigned long)(unsigned)st_zero);

	if (!query_std(h, &after, "after", tag)) { unverified++; close(fd); free(buf); return; }

	printf("zerodata[%s]: EndOfFile      before=%lld after=%lld\n",
	       tag, (long long)before.EndOfFile, (long long)after.EndOfFile);
	printf("zerodata[%s]: AllocationSize before=%lld after=%lld\n",
	       tag, (long long)before.AllocationSize, (long long)after.AllocationSize);

	if (!NT_SUCCESS(st_zero)) {
		if (not_supported(st_zero))
			printf("SKIP zerodata[%s]: FSCTL_SET_ZERO_DATA is unimplemented in this "
			       "environment (status 0x%08lX) -- this is what stock Wine does; "
			       "it implements FSCTL_SET_SPARSE (persisted as a user.WINESPARSE "
			       "xattr) but never punches the hole.  Real NT is required to "
			       "answer this probe.\n", tag, (unsigned long)(unsigned)st_zero);
		else
			printf("SKIP zerodata[%s]: FSCTL_SET_ZERO_DATA failed with 0x%08lX -- "
			       "that status IS the measurement for this case; the success "
			       "contract below could not be evaluated.\n",
			       tag, (unsigned long)(unsigned)st_zero);
		unverified++;
		close(fd); free(buf);
		return;
	}

	/* ---- contract assertions (success path only) ---- */

	/* Zeroing a range is not a truncation. */
	CHECK(after.EndOfFile == before.EndOfFile);
	CHECK(after.EndOfFile == FILE_SIZE);

	memset(buf, 0x5A, FILE_SIZE);   /* poison, so a short read cannot look like zeros */
	if (lseek(fd, 0, SEEK_SET) != 0) { printf("FAIL %s: lseek\n", tag); fails++; }
	{
		size_t got = 0;
		while (got < FILE_SIZE) {
			ssize_t n = read(fd, buf + got, FILE_SIZE - got);
			if (n <= 0) break;
			got += (size_t)n;
		}
		CHECK(got == FILE_SIZE);
		if (got != FILE_SIZE) { close(fd); free(buf); return; }
	}
	for (i = ZERO_FROM; i < ZERO_TO; i++) if (buf[i] != 0) { range_zero = 0; break; }
	for (i = 0; i < ZERO_FROM; i++) if (buf[i] != FILL_BYTE) { outside_intact = 0; break; }
	for (i = ZERO_TO; i < FILE_SIZE; i++) if (buf[i] != FILL_BYTE) { outside_intact = 0; break; }

	printf("zerodata[%s]: range [%d,%d) reads back as zero: %s\n",
	       tag, ZERO_FROM, ZERO_TO, range_zero ? "yes" : "NO");
	printf("zerodata[%s]: bytes outside the range still 0x%02X: %s\n",
	       tag, FILL_BYTE, outside_intact ? "yes" : "NO");

	CHECK(range_zero);
	CHECK(outside_intact);

	/* ---- the discriminating verdict, stated rather than implied ---- */
	delta = (long long)before.AllocationSize - (long long)after.AllocationSize;
	if (delta > 0)
		printf("zerodata[%s]: VERDICT=DEALLOCATED -- AllocationSize dropped by %lld "
		       "bytes, so NT punched a hole rather than writing zeros.\n", tag, delta);
	else if (delta == 0)
		printf("zerodata[%s]: VERDICT=ZEROED-IN-PLACE -- AllocationSize is unchanged, "
		       "so the bytes were zeroed without deallocating any clusters.\n", tag);
	else
		printf("zerodata[%s]: VERDICT=GREW -- AllocationSize rose by %lld bytes, which "
		       "neither plain zeroing nor deallocation predicts.\n", tag, -delta);

	close(fd);
	free(buf);
}

int main(void)
{
	char dir[] = "zerodatXXXXXX";

	if (!mkdtemp(dir)) { printf("FAIL mkdtemp: %s\n", strerror(errno)); return 1; }
	if (chdir(dir) != 0) { printf("FAIL chdir: %s\n", strerror(errno)); return 1; }

	printf("zerodata: file size %d, zero range [%d,%d), fill byte 0x%02X\n",
	       FILE_SIZE, ZERO_FROM, ZERO_TO, FILL_BYTE);

	/* (a) previously marked with FSCTL_SET_SPARSE */
	run_case("sparse", 1);
	/* (b) never marked -- the discriminating case */
	run_case("unmarked", 0);

	if (fails) { printf("FAILED %d check(s)\n", fails); return 1; }
	if (unverified) {
		printf("UNVERIFIED zerodata: %d of 2 case(s) could not be measured in this "
		       "environment (see SKIP lines above); no failures in what did run\n",
		       unverified);
		return 77;
	}
	printf("PASS zerodata\n");
	return 0;
}

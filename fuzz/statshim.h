/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A layout-neutral struct stat, and the one seam that translates between
 * ntlibc's `struct stat` and the host's.
 *
 * Why it has to exist.  libFuzzer is part of the compiler runtime: it was
 * compiled long ago against the *host* headers, and it calls stat() to
 * decide whether the corpus path it was given is a directory
 * (ValidateDirectoryExists -> IsDirectory -> S_ISDIR).  The stat() it
 * reaches is ntlibc's, because ntlibc's objects are linked into the same
 * executable.  The two `struct stat`s do not agree:
 *
 *     field       ntlibc offset   glibc/x86_64 offset
 *     st_mode          16                 24
 *     st_nlink         24                 16
 *     st_uid           32                 28
 *     st_gid           36                 32
 *
 * (measured, not assumed: a host-headers probe reading ntlibc's answer
 * for a directory saw st_mode = 01, which is st_nlink.)  So S_ISDIR is
 * false for every directory, and libFuzzer rejects any corpus directory
 * with `ERROR: The required directory "..." does not exist` -- even one
 * that is really there.  ntlibc uses the generic POSIX field order that
 * musl uses on architectures with no Linux ABI to match; glibc and musl
 * on x86_64 both use the kernel's order.  Neither is wrong; they are just
 * different, and a native build puts both in one address space.
 *
 * The harnesses are therefore linked with -Wl,--wrap=stat: libFuzzer's
 * stat() calls land in __wrap_stat (ntstubs.c), which asks ntlibc's real
 * stat() and hands the answer to __ntfuzz_pack_stat (host_oracle.c, the
 * one file here built against the host headers) to write out in the
 * host's layout.  Neither file has to hand-transcribe the other's struct;
 * this one, which uses no libc types at all, is what they share.
 *
 * Confined to fuzz/ on purpose.  Reordering ntlibc's own struct stat to
 * match glibc's would fix this and several latent siblings, but it is a
 * public header used by every build and every test, and this problem is
 * only ever visible in a native sanitizer build.  It is recorded as a
 * finding rather than fixed here.
 */
#ifndef NTFUZZ_STATSHIM_H
#define NTFUZZ_STATSHIM_H

struct ntfuzz_stat {
	unsigned long long dev, ino, rdev, nlink;
	unsigned int mode, uid, gid;
	long long size, blksize, blocks;
	long long atim_sec, atim_nsec;
	long long mtim_sec, mtim_nsec;
	long long ctim_sec, ctim_nsec;
};

/* Writes *s into `hostbuf`, which must be a host `struct stat`. */
void __ntfuzz_pack_stat(void *hostbuf, const struct ntfuzz_stat *s);
/* sizeof(struct stat) as the host sees it, so the wrapper can zero it. */
unsigned long __ntfuzz_host_stat_size(void);

#endif

<!--
SPDX-FileCopyrightText: (C) 2026 Gavin John
SPDX-License-Identifier: GPL-3.0-or-later
-->

# unistd.h / fcntl.h / sys/stat.h coverage fragment

Owned by the `unistd.h` agent of the parallel POSIX-COVERAGE.md pass (see
that file's priority order, item 6).  Scope: `test/posix-unistd.c` (new),
`src/unistd/`, `src/fcntl/`, `src/stat/`.  Pre-existing coverage lives in
`test/unistd.c` (~330 assertions, sanity-style) and `test/posix-io.c`
(errno-focused). This fragment records only what `test/posix-unistd.c`
newly checked, plus every clause verified against the pre-existing files
while scoping this pass. It is **not** merged into `test/POSIX-COVERAGE.md`
(out of scope for this agent) -- a future pass should fold it in.

Status vocabulary matches `test/POSIX-COVERAGE.md`: covered / N/A
(reason) / BUG (fenced) / not yet reached.

## open / openat / creat (open.html, creat.html)

| clause | status | test |
|---|---|---|
| O_CREAT+O_EXCL -> EEXIST | covered | test/unistd.c |
| O_RDONLY on a directory (EISDIR retry-as-dir path) | covered | test/unistd.c |
| O_WRONLY/O_RDWR on a directory -> EISDIR | covered (Wine-detect pattern) | test/unistd.c |
| O_DIRECTORY on a non-directory -> ENOTDIR | covered | test/unistd.c |
| O_TRUNC truncates on open | covered | test/unistd.c |
| O_APPEND: writes land at EOF regardless of seek | covered | test/unistd.c |
| O_CLOEXEC sets FD_CLOEXEC | covered | test/unistd.c |
| mode bits ANDed with ~umask | **BUG (fenced)** | test/posix-unistd.c `test_open_umask_bug` |
| O_TRUNC without O_WRONLY/O_RDWR is undefined | N/A (POSIX leaves this undefined; not asserted either way) | -- |
| creat() truncates an existing file to 0 | covered | test/posix-unistd.c `test_creat_truncates_existing` |
| openat relative to dirfd, ENOENT | covered | test/unistd.c |

## close / read / write / pread / pwrite / lseek

| clause | status | test |
|---|---|---|
| read/write wrong-direction fd -> EBADF | covered | test/posix-io.c, test/unistd.c |
| read at EOF -> 0, not an error | covered | test/posix-io.c |
| zero-length read/write are no-ops | covered | test/unistd.c |
| lseek SEEK_SET/CUR/END arithmetic | covered | test/unistd.c |
| lseek does not itself extend the file; later write leaves a hole that reads as zero | covered | test/unistd.c |
| lseek ESPIPE on a pipe | covered | test/posix-io.c, test/unistd.c |
| lseek EINVAL: negative resulting offset via SEEK_SET | covered | test/unistd.c |
| lseek EINVAL: negative resulting offset via SEEK_CUR, offset unchanged after the failed call | covered | test/posix-unistd.c `test_lseek_seek_cur_negative` |
| pread/pwrite do not move the file offset (this session fixed a real bug here, see git log e70010c) | covered | test/unistd.c |
| pwrite beyond EOF extends the file, offset unmoved | covered | test/unistd.c |
| pread/pwrite ESPIPE on a pipe | covered | test/unistd.c |
| close(-1)/close(already-closed) -> EBADF | covered | test/posix-io.c, test/unistd.c |
| unlinked-but-open file stays fully usable (POSIX delete semantics) | covered | test/posix-unistd.c `test_unlink_open_file_stays_usable` |

## dup / dup2 / dup3 / fcntl

| clause | status | test |
|---|---|---|
| dup2(fd,fd) returns fd without closing | covered | test/unistd.c |
| dup2(fd,fd): FD_CLOEXEC on fd2 left unchanged | covered | test/posix-unistd.c `test_dup2_self_preserves_cloexec` |
| dup2 onto an open fd closes it first | covered | test/unistd.c |
| dup3(fd,fd,_) -> EINVAL | covered | test/unistd.c |
| F_DUPFD/F_DUPFD_CLOEXEC: lowest available fd >= arg | covered (>= arg only) + covered (lowest-of-two, freed slot reused) | test/unistd.c, test/posix-unistd.c `test_fcntl_dupfd_lowest` |
| F_DUPFD arg negative or >= OPEN_MAX -> EINVAL (this session fixed a real out-of-range bug here per the task brief) | covered | test/unistd.c, test/posix-io.c |
| F_GETFD/F_SETFD FD_CLOEXEC round-trip | covered | test/unistd.c |
| F_GETFL reports access mode + status flags | covered | test/unistd.c |
| F_SETFL ignores access-mode/creation bits in arg | covered | test/posix-unistd.c `test_fcntl_setfl_ignores_accmode` |
| F_GETLK/F_SETLK/F_SETLKW: locking unimplemented, F_GETLK reports F_UNLCK, SETLK(W) report success | covered | test/posix-unistd.c `test_fcntl_locks_are_noops` |
| fcntl invalid cmd -> EINVAL, bad fd -> EBADF | covered | test/unistd.c, test/posix-io.c |

## pipe / pipe2

| clause | status | test |
|---|---|---|
| two fds, [0] read-only [1] write-only | covered | test/unistd.c |
| data flows through, EOF after writer closes | covered | test/unistd.c, test/posix-io.c |
| ESPIPE for lseek/pread/pwrite on a pipe fd | covered | test/unistd.c |
| S_ISFIFO via fstat | covered | test/unistd.c |
| pipe2(O_CLOEXEC) | covered | test/unistd.c |
| closing one end doesn't invalidate the other | covered | test/posix-unistd.c `test_pipe_ends_independent` |

## stat / fstat / lstat / fstatat / chmod / fchmod

| clause | status | test |
|---|---|---|
| st_mode S_IS*, st_size, st_nlink, st_mtime range | covered | test/unistd.c |
| st_ino/st_dev identical for two paths to the same file | covered | test/unistd.c |
| st_ino/st_dev distinct for two different files | covered | test/posix-unistd.c `test_stat_ino_distinct` |
| st_mtime advances after a write | covered | test/posix-unistd.c `test_stat_mtime_after_write` |
| stat/lstat ENOENT, fstat(bad fd) EBADF | covered | test/unistd.c, test/posix-io.c |
| chmod clears/sets the only bit NTFS can express (owner write -> READONLY attribute); Wine's read-only-chmod quirk vs real NT documented and detected | covered | test/unistd.c |
| st_size for a directory (POSIX imposes no specific value; ntlibc hardcodes 0, see src/stat/stat.c) | N/A (implementation-defined) | -- |
| directory mode bits from `mkdir(path, mode)` (mode argument is accepted but NTFS directories don't carry Unix permission bits; src/stat/mkdir.c discards `mode` entirely and src/stat/stat.c always reports a hardcoded 0755) | N/A (documented hardware limitation, matches the file's own design comment) | -- |

## mkdir / rmdir / unlink / unlinkat

| clause | status | test |
|---|---|---|
| mkdir EEXIST (existing dir, existing file), ENOENT (missing parent) | covered | test/unistd.c, test/posix-io.c |
| rmdir ENOTEMPTY, ENOTDIR, ENOENT | covered | test/unistd.c, test/posix-io.c |
| unlink EISDIR (must use rmdir), ENOENT | covered | test/unistd.c, test/posix-io.c |
| unlinked-but-open file: name gone, fd still fully usable (postponed reclaim) | covered | test/posix-unistd.c `test_unlink_open_file_stays_usable` |

## rename / renameat

| clause | status | test |
|---|---|---|
| rename over an existing file replaces it | covered | test/unistd.c, test/posix-io.c |
| rename ENOENT (source missing) | covered | test/unistd.c, test/posix-io.c |
| rename of a directory (success case) | covered | test/unistd.c |
| rename(old,old) / same-file rename: succeeds, no-op | covered | test/posix-unistd.c `test_rename_same_file_noop` |
| rename EISDIR: new is a directory, old is a non-directory | **BUG (fenced)** -- NT reports STATUS_ACCESS_DENIED here, which `__set_errno_status()` (used by src/stdio/misc.c's `renameat`, outside this agent's src/unistd+src/fcntl+src/stat scope) maps to EACCES, not EISDIR | test/posix-unistd.c `test_rename_new_dir_old_file_eisdir` |
| rename ENOTEMPTY/EEXIST: new is a non-empty directory | **BUG (fenced)** -- same root cause, EACCES instead of ENOTEMPTY/EEXIST | test/posix-unistd.c `test_rename_onto_nonempty_dir` |

## link / symlink / readlink

| clause | status | test |
|---|---|---|
| hard link shares data/inode, nlink increments/decrements | covered | test/unistd.c |
| link EEXIST, ENOENT | covered | test/unistd.c |
| symlink/readlink round trip, ENAMETOOLONG guard on oversize targets (USHORT-length REPARSE_DATA_BUFFER fields; this session's audit flagged and fixed exactly this class of bug elsewhere) | covered | test/unistd.c |

## access / faccessat

| clause | status | test |
|---|---|---|
| F_OK/R_OK/W_OK/X_OK on a file | covered | test/unistd.c |
| ENOENT | covered | test/unistd.c |
| EACCES (X_OK on a file with no execute bit; W_OK after chmod 0444) | covered | test/unistd.c |
| "checks using the real, not effective, uid/gid" | N/A (ntlibc has no separate real/effective identity -- `getuid()==geteuid()` always per src/unistd/ids.c; nothing to distinguish) | -- |
| ENOTDIR: path ends in trailing slash(es) but the final component is not a directory | **BUG (fenced)** -- `src/internal/path.c`'s `__ntpath()` unconditionally strips a trailing slash without checking the resolved object is a directory, so `access("file/", F_OK)` succeeds; path.c is outside this agent's scope | test/posix-unistd.c `test_access_trailing_slash_enotdir` |

## chdir / fchdir / getcwd

| clause | status | test |
|---|---|---|
| round trip, relative/absolute, `.`/`..` | covered | test/unistd.c |
| chdir ENOENT/ENOTDIR, cwd unchanged on failure | covered | test/unistd.c, test/posix-io.c |
| getcwd ERANGE for an undersized fixed buffer | covered | test/unistd.c |
| getcwd ERANGE at the exact off-by-one boundary (size == pathlen, one short of pathlen+1) vs. exact fit at pathlen+1 | covered | test/posix-unistd.c `test_getcwd_off_by_one` |
| getcwd(NULL, 0) mallocs exactly the right size | covered | test/unistd.c |
| getcwd EINVAL for size==0 with a non-NULL buffer | covered | test/unistd.c |

## ftruncate / truncate

| clause | status | test |
|---|---|---|
| grow (zero-fills) and shrink | covered | test/unistd.c |
| EINVAL negative length, EBADF bad fd, ENOENT missing path | covered | test/unistd.c |

## fsync / fdatasync

| clause | status | test |
|---|---|---|
| succeeds on a regular file and a pipe | covered | test/unistd.c |
| EBADF | covered | test/posix-io.c |

## isatty / ttyname / ttyname_r

| clause | status | test |
|---|---|---|
| ENOTTY on a non-tty fd | covered | test/unistd.c |
| ttyname_r ERANGE for an undersized buffer | **not reached** -- only reachable from an actual console fd, which this test runner (Wine headless, `make asan` native) may not attach; test detects and skips rather than asserting either shape | test/posix-unistd.c `test_ttyname_r_erange` |

## getpid / getppid / sysconf / pathconf / fpathconf / umask

| clause | status | test |
|---|---|---|
| getpid stable and positive | covered | test/unistd.c |
| getppid: 0 under Wine (parent is a Unix process), documented divergence | covered | test/unistd.c |
| sysconf(_SC_PAGESIZE/_SC_OPEN_MAX/_SC_NPROCESSORS_ONLN/_SC_CLK_TCK/_SC_VERSION), EINVAL for an unknown name | covered | test/unistd.c |
| sysconf(_SC_CHILD_MAX): a real, large, positive ceiling (this session's `448da1a` fixed this to come from `sysconf`, not a small hardcoded `CHILD_MAX_`) | covered | test/posix-unistd.c `test_sysconf_child_max` |
| pathconf/fpathconf NAME_MAX/PATH_MAX, EINVAL | covered | test/unistd.c |
| umask returns the old mask and installs the new one | covered | test/unistd.c |
| umask actually affecting file-creation mode | see the open.html BUG above -- umask_value is stored but never consulted by open() or mkdir() | test/posix-unistd.c `test_open_umask_bug` |

## utimensat / futimens / utime / utimes / futimes / lutimes / futimesat

| clause | status | test |
|---|---|---|
| explicit timestamps, UTIME_NOW, UTIME_OMIT, NULL -> now | covered | test/unistd.c |
| ENOENT | covered | test/unistd.c |
| timeval (usec) resolution via futimesat | covered | test/unistd.c |

## Not reached / out of this agent's authority to fix

- `src/internal/path.c` (trailing-slash ENOTDIR) and `src/stdio/misc.c`
  (rename's EISDIR/ENOTEMPTY mapping) both need fixes for the two BUGs
  above; neither file is in this agent's `src/unistd`, `src/fcntl`,
  `src/stat` scope.
- `src/stat/mkdir.c`'s mode argument and `sys/wait.h` are explicitly
  someone else's territory per the task brief.
- No extraction of an internal pure function was needed this session --
  every clause here was directly observable through the public API
  (unlike, say, `__errno_from_status`, none of these decisions were
  buried past the syscall boundary).

### Bugs found this session (fenced in test/posix-unistd.c)

1. **open() ignores the file mode creation mask.** open.html
   DESCRIPTION: mode bits must be ANDed with `~umask`. `umask()`
   (src/stat/chmod.c) records the mask but nothing in src/fcntl/open.c
   or src/stat/mkdir.c ever reads it. `test_open_umask_bug`.
2. **access("file/", F_OK) does not report ENOTDIR** for a trailing
   slash on a non-directory. Root cause in `src/internal/path.c`
   (out of scope). `test_access_trailing_slash_enotdir`.
3. **rename() onto an existing directory reports EACCES, not EISDIR**,
   when the source is a non-directory. Root cause in
   `src/stdio/misc.c`'s `renameat()` (out of scope).
   `test_rename_new_dir_old_file_eisdir`.
4. **rename() of a directory onto a non-empty directory reports EACCES,
   not ENOTEMPTY/EEXIST.** Same root cause as #3.
   `test_rename_onto_nonempty_dir`.

All four were checked against Wine's actual STATUS code (measured, not
assumed) before being fenced; #2-#4 are plausible on real NT too since
the divergent behaviour is produced by ntlibc's own translation layer,
not by anything Wine-specific.

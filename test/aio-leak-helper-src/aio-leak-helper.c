/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Standalone helper process for test/posix-realtime-linux.c's AIO
 * worker-process-leak regression test (see that file's own header
 * comment for the bug and the fix). Deliberately a SEPARATE executable,
 * exec()'d fresh rather than driven by AIO calls made directly inside a
 * plain fork()'d child of the test harness: fork() followed by real AIO
 * use in the child was tried first and found to crash (SIGSEGV inside
 * aio_suspend()) completely independently of the leak fix under test --
 * a real, separate, pre-existing bug this regression test is not about
 * and must not depend on, so it stays undisclosed-but-avoided here
 * rather than papered over by coincidence. execve()ing this program
 * instead gives the exact same cold-start AIO codepath a genuinely
 * separate process invocation of test/posix-realtime.exe or this file's
 * own standalone repro already exercised cleanly, with no fork()-then-
 * AIO interaction anywhere in the picture.
 *
 * Submits exactly one real aio_write(), waits for it via aio_suspend(),
 * and exits -- the minimal shape that starts the background AIO worker
 * at all. argv[1] is the file to write to (the caller's own, so two
 * concurrent test runs cannot collide on a shared path).
 */
#include <aio.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	struct aiocb cb;
	const struct aiocb *list[1];
	static const char payload[] = "aio-leak-helper probe\n";
	int fd;

	if (argc < 2) return 1;
	fd = open(argv[1], O_CREAT | O_WRONLY | O_TRUNC, 0644);
	if (fd < 0) return 2;

	memset(&cb, 0, sizeof cb);
	cb.aio_fildes = fd;
	cb.aio_buf = (void *)payload;
	cb.aio_nbytes = sizeof payload - 1;
	cb.aio_offset = 0;

	if (aio_write(&cb) != 0) return 3;
	list[0] = &cb;
	aio_suspend(list, 1, NULL);
	if (aio_return(&cb) != (ssize_t)(sizeof payload - 1)) return 4;

	close(fd);
	return 0;
}

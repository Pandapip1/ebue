/* SPDX-License-Identifier: GPL-3.0-or-later */

int *__errno_location(void);
#define errno (*__errno_location())

int __set_errno_status(int st);
int close(int fd);
int harmless(void);

/* The read matches the call the code just checked. */
int diagnosed_read(int fd) {
	if (close(fd) < 0) {
		if (errno == 9)
			return -1;
	}
	return 0;
}

/* Copying the diagnosed call's errno into a local is still a read of
 * that same call's errno, not a stale one. */
int copied_out(int fd) {
	if (close(fd) < 0) {
		int saved = errno;
		return saved;
	}
	return 0;
}

/* An unrelated, non-capable call between the diagnosed call and the
 * read does not invalidate the read. */
int survives_unrelated_call(int fd) {
	if (close(fd) < 0) {
		harmless();
		if (errno == 9)
			return -1;
	}
	return 0;
}

/* A direct assignment is its own trusted origin. */
int reset_then_read(void) {
	errno = 0;
	if (errno == 0)
		return 0;
	return -1;
}

/* A capable call happened but its result was never compared for
 * failure: neither of this checker's two proof obligations applies. */
int uncompared_call(int fd) {
	close(fd);
	if (errno == 9)
		return -1;
	return 0;
}

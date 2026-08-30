/* SPDX-License-Identifier: GPL-3.0-or-later */

int *__errno_location(void);
#define errno (*__errno_location())

int __set_errno_status(int st);
int close(int fd);

/* CERT ERR30-C: the cleanup call after the diagnosed failure clobbers
 * errno before it is read. */
int stale_after_cleanup(int fd) {
	if (close(fd) < 0) {
		__set_errno_status(5);
		if (errno == 9) /* errno-discipline-expect */
			return -1;
	}
	return 0;
}

/* No call or assignment on this path could have set errno; this trusts
 * leftover/uninitialized errno state from function entry. */
int unset_origin(void) {
	if (errno == 9) /* errno-discipline-expect */
		return -1;
	return 0;
}

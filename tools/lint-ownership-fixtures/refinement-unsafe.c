/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "refinement-fixture.h"

void use_without_check(void)
{
	int fd withtok(unchecked_fd) = acquire_fd();
	use_fd(fd); /* ownership-expect: unchecked-operation */
} /* ownership-expect: unchecked-not-dropped */

void wrong_equality_does_not_check(void)
{
	int fd withtok(unchecked_fd) = acquire_fd();
	if (fd == 0)
		return; /* ownership-expect: wrong-sentinel-return */
	use_fd(fd); /* ownership-expect: wrong-sentinel */
} /* ownership-expect: wrong-sentinel-not-dropped */

void switch_without_sentinel_case_does_not_check(void)
{
	int fd withtok(unchecked_fd) = acquire_fd();
	switch (fd) {
	case 0:
		return; /* ownership-expect: wrong-switch-return */
	default:
		break;
	}
	use_fd(fd); /* ownership-expect: wrong-switch */
} /* ownership-expect: wrong-switch-not-dropped */

int dereference_without_check(void)
{
	int *pointer withtok(unchecked_null) = maybe_pointer();
	return *pointer; /* ownership-expect: unchecked-dereference */
}

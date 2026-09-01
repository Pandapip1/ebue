/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "refinement-fixture.h"

void equality_refines_fd(void)
{
	int fd withtok(unchecked_fd) = acquire_fd();
	if (fd == -1)
		return;
	use_fd(fd);
}

void switch_refines_fd(void)
{
	int fd withtok(unchecked_fd) = acquire_fd();
	switch (fd) {
	case -1:
		return;
	default:
		break;
	}
	use_fd(fd);
}

int equality_allows_dereference(void)
{
	int *pointer withtok(unchecked_null) = maybe_pointer();
	if (pointer == 0)
		return 0;
	return *pointer;
}

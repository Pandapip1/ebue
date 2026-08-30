/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

typedef __SIZE_TYPE__ size_t;
int open(const char *, int, ...);
int close(int);
long write(int, const void *, size_t);

/* A literal, made-up descriptor -- concretely known to this analysis to
 * not be the result of any open()/socket()/... it could have tracked
 * (it is not even a symbol, see ResourceLifecycleChecker::checkResource's
 * own comment), and not one of the standard streams isStandardDescriptor()
 * recognises either. Real, checked evidence of a bug, unlike an opaque
 * parameter (see resource-safe.c's descriptor_borrow for why that shape
 * is trusted instead). */
void bogus_literal(void)
{
	write(99, "x", 1); /* ownership-expect: resource-unproved */
}

void release_twice(void)
{
	int fd = open("name", 0);
	if (fd < 0)
		return;
	close(fd);
	close(fd); /* ownership-expect: resource-released */
}

void use_after_release(void)
{
	int fd = open("name", 0);
	if (fd < 0)
		return;
	close(fd);
	write(fd, "x", 1); /* ownership-expect: resource-use-released */
}

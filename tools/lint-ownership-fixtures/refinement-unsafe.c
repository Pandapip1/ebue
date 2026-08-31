/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

[[ownership_holds_token(unchecked_fd),
  ownership_token_consumed_by_equal(unchecked_fd, -1),
  ownership_token_consumed_by_switch(unchecked_fd, -1)]]
int acquire_fd(void);
void use_fd(int fd [[ownership_requires_absent_token(unchecked_fd)]]);

[[ownership_holds_token(unchecked_null),
  ownership_token_consumed_by_equal(unchecked_null, 0),
  ownership_token_blocks_dereference(unchecked_null)]]
int *maybe_pointer(void);

void use_without_check(void)
{
	int fd [[ownership_holds_token(unchecked_fd)]] = acquire_fd();
	use_fd(fd); /* ownership-expect: unchecked-operation */
}

void wrong_equality_does_not_check(void)
{
	int fd [[ownership_holds_token(unchecked_fd),
	        ownership_token_consumed_by_equal(unchecked_fd, -1)]] = acquire_fd();
	if (fd == 0)
		return;
	use_fd(fd); /* ownership-expect: wrong-sentinel */
}

void switch_without_sentinel_case_does_not_check(void)
{
	int fd [[ownership_holds_token(unchecked_fd),
	        ownership_token_consumed_by_switch(unchecked_fd, -1)]] = acquire_fd();
	switch (fd) {
	case 0:
		return;
	default:
		break;
	}
	use_fd(fd); /* ownership-expect: wrong-switch */
}

int dereference_without_check(void)
{
	int *pointer [[ownership_holds_token(unchecked_null),
	              ownership_token_consumed_by_equal(unchecked_null, 0),
	              ownership_token_blocks_dereference(unchecked_null)]] = maybe_pointer();
	return *pointer; /* ownership-expect: unchecked-dereference */
}

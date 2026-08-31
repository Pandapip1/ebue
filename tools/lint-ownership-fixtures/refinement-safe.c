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

void equality_refines_fd(void)
{
	int fd [[ownership_holds_token(unchecked_fd),
	        ownership_token_consumed_by_equal(unchecked_fd, -1)]] = acquire_fd();
	if (fd == -1)
		return;
	use_fd(fd);
}

void switch_refines_fd(void)
{
	int fd [[ownership_holds_token(unchecked_fd),
	        ownership_token_consumed_by_switch(unchecked_fd, -1)]] = acquire_fd();
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
	int *pointer [[ownership_holds_token(unchecked_null),
	              ownership_token_consumed_by_equal(unchecked_null, 0),
	              ownership_token_blocks_dereference(unchecked_null)]] = maybe_pointer();
	if (pointer == 0)
		return 0;
	return *pointer;
}

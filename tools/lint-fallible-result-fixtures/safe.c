/* SPDX-FileCopyrightText: (C) 2026 Gavin John */
/* SPDX-License-Identifier: GPL-3.0-or-later */

int close(int);

int propagated_close(int fd) { return close(fd); }

int checked_close(int fd) {
  if (close(fd) < 0)
    return -1;
  return 0;
}

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

typedef __SIZE_TYPE__ size_t;
typedef struct file FILE;
int open(const char *, int, ...);
int close(int);
long write(int, const void *, size_t);
FILE *fopen(const char *, const char *);
int fclose(FILE *);

void descriptor(void)
{
	int fd = open("name", 0);
	if (fd < 0)
		return;
	write(fd, "x", 1);
	close(fd);
}

void stream(void)
{
	FILE *file = fopen("name", "r");
	if (file)
		fclose(file);
}

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* ecvt/fcvt/gcvt on top of snprintf. */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static char buf[64];

/* Format with the %e conversion, then strip the exponent and the point
 * out so that the digits remain. */
char *ecvt(double x, int n, int *dp, int *sign)
{
	char tmp[80], *e;
	int i, j = 0, exp;

	if (n < 1) n = 1;
	if (n > 17) n = 17;
	*sign = x < 0 || (x == 0 && 1.0 / x < 0);
	if (*sign) x = -x;
	if (x != x) { strcpy(buf, "nan"); *dp = 0; return buf; }
	if (x == 1.0 / 0.0) { strcpy(buf, "inf"); *dp = 0; return buf; }
	snprintf(tmp, sizeof tmp, "%.*e", n - 1, x);
	for (i = 0; tmp[i] && tmp[i] != 'e'; i++)
		if (isdigit((unsigned char)tmp[i])) buf[j++] = tmp[i];
	buf[j] = 0;
	e = tmp[i] ? tmp + i + 1 : 0;
	exp = e ? atoi(e) : 0; // NOLINT(cert-err34-c) -- e is our own snprintf("%.*e") output, always a well-formed exponent
	*dp = x == 0 ? 0 : exp + 1;
	return buf;
}

char *fcvt(double x, int n, int *dp, int *sign)
{
	char tmp[1600];
	int i, j = 0, lead;

	if (n < 0) n = 0;
	if (n > 40) n = 40;
	*sign = x < 0 || (x == 0 && 1.0 / x < 0);
	if (*sign) x = -x;
	if (x != x) { strcpy(buf, "nan"); *dp = 0; return buf; }
	if (x == 1.0 / 0.0) { strcpy(buf, "inf"); *dp = 0; return buf; }
	snprintf(tmp, sizeof tmp, "%.*f", n, x);
	/* digits before the point is *dp; a leading "0." before the point
	 * is not a significant digit. */
	for (lead = 0; tmp[lead] && tmp[lead] != '.'; lead++);
	if (lead == 1 && tmp[0] == '0') {
		*dp = 0;
		for (i = 2; tmp[i]; i++) {
			if (tmp[i] == '0' && !j) { (*dp)--; continue; }
			if (j < 60) buf[j++] = tmp[i];
		}
		if (!j) { *dp = 0; for (i = 2; tmp[i] && j < 60; i++) buf[j++] = tmp[i]; }
	} else {
		*dp = lead;
		for (i = 0; tmp[i]; i++)
			if (tmp[i] != '.' && j < 60) buf[j++] = tmp[i];
	}
	buf[j] = 0;
	return buf;
}

char *gcvt(double x, int n, char *out)
{
	sprintf(out, "%.*g", n, x);
	return out;
}

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include "libc.h"

char *optarg;
int optind = 1, opterr = 1, optopt, optreset;

/* The position within the current argv element, for clustered
 * short options (-abc). */
int __optpos;

extern char *__progname;

static void writestr(const char *s)
{
	write(2, s, strlen(s));
}

/* "prog: msg: -c\n", or "prog: msg: --name\n" for getopt_long. */
void __getopt_msg(const char *msg, const char *optname, size_t l)
{
	const char *p = __progname ? __progname : "";
	writestr(p);
	writestr(": ");
	writestr(msg);
	writestr(": ");
	write(2, optname, l);
	write(2, "\n", 1);
}

int getopt(int argc, char *const argv[], const char *optstring)
{
	int c, d;
	char optchar[3];
	const char *p;

	if (!optind || optreset) {
		__optpos = 0;
		optind = 1;
		optreset = 0;
	}

	if (optind >= argc || !argv[optind])
		return -1;

	if (argv[optind][0] != '-') {
		if (optstring[0] == '-') {
			optarg = argv[optind++];
			return 1;
		}
		return -1;
	}

	if (!argv[optind][1])
		return -1;

	if (argv[optind][1] == '-' && !argv[optind][2])
		return optind++, -1;

	if (!__optpos) __optpos++;
	c = (unsigned char)argv[optind][__optpos];
	__optpos++;

	if (!argv[optind][__optpos]) {
		optind++;
		__optpos = 0;
	}

	if (optstring[0] == '-' || optstring[0] == '+')
		optstring++;

	for (p = optstring; *p; p++) {
		d = (unsigned char)*p;
		if (d == c && c != ':') break;
	}

	if (!*p || c == ':') {
		optopt = c;
		if (optstring[0] != ':' && opterr) {
			optchar[0] = '-'; optchar[1] = (char)c; optchar[2] = 0;
			__getopt_msg("unrecognized option", optchar, 2);
		}
		return '?';
	}
	if (p[1] == ':') {
		optarg = 0;
		if (__optpos) {
			/* The argument is the rest of this element (-ofoo). */
			optarg = argv[optind++] + __optpos;
			__optpos = 0;
		} else if (p[2] != ':') {
			/* Required: the next element. */
			if (optind < argc && argv[optind]) {
				optarg = argv[optind++];
			} else {
				optopt = c;
				if (optstring[0] == ':') return ':';
				if (opterr) {
					optchar[0] = '-'; optchar[1] = (char)c; optchar[2] = 0;
					__getopt_msg("option requires an argument", optchar, 2);
				}
				return '?';
			}
		}
	}
	return c;
}

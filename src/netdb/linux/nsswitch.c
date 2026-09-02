/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __nsswitch_order(): a real /etc/nsswitch.conf(5) parser, Linux-only
 * (see src/internal/nsswitch.h's own banner for what it is shared by
 * and why it lives in this tree at all -- NSS as ntlibc's own
 * statically-linked "files"/"dns" backends, dispatched by a real
 * config file, rather than an attempt to dlopen() glibc's own
 * libnss_*.so.2 modules, which are built against glibc's private,
 * unstable internal ABI that ntlibc's own dlopen() (src/dlfcn/linux/
 * plat_dlfcn.c) has no reason to replicate. musl's real-world
 * precedent is the same call: musl supports only built-in
 * "files"/"dns", never dynamically loaded NSS modules, for the
 * identical ABI-instability reason.
 *
 * What is genuinely NOT implemented, on purpose: the `[STATUS=action]`
 * qualifier glibc's own nsswitch.conf(5) supports after a service name
 * (e.g. `files [NOTFOUND=return] dns`, meaning "give up rather than
 * falling through to dns on a clean not-found"). This parser tokenizes
 * and skips any `[...]` group as an opaque unit -- so a line
 * containing one is not a parse error -- but never changes behavior
 * based on it: every recognized service in the line is simply tried in
 * order until one produces a result, which is nsswitch.conf's own
 * documented default action set anyway (SUCCESS=return,
 * UNAVAIL=continue, NOTFOUND=continue, TRYAGAIN=continue) for every
 * status this file's two backends can even produce. An admin who
 * writes an explicit non-default action list to get different
 * fallback behavior will not get it; documented here rather than
 * silently almost-right.
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "nsswitch.h"
#include "nss_paths.h"

/* Longer than any real nsswitch.conf line has ever needed to be; a
 * longer line is truncated by fgets() (its own guaranteed NUL
 * termination), which can only cost this parser a trailing service
 * token on a pathological line, never a crash or a wrong match on the
 * db name/colon at the front. */
#define NSS_LINE_MAX 512

static int is_db_line(const char *line, const char *db, const char **restp)
{
	size_t n = strlen(db);
	const char *p = line;

	while (*p == ' ' || *p == '\t') p++;
	if (strncmp(p, db, n) != 0) return 0;
	p += n;
	while (*p == ' ' || *p == '\t') p++;
	if (*p != ':') return 0;
	p++;
	*restp = p;
	return 1;
}

/* Tokenizes *rest in place (whitespace-delimited), skipping any
 * `[...]` bracket group as one opaque unit (see this file's own
 * banner). Returns the next real service token, or NULL at end of
 * line. *rest is advanced past the returned token. */
static char *next_service_token(char **rest)
{
	char *p = *rest;
	char *tok;

	for (;;) {
		while (*p == ' ' || *p == '\t') p++;
		if (*p == '\0') { *rest = p; return NULL; }
		if (*p == '[') {
			while (*p != '\0' && *p != ']') p++;
			if (*p == ']') p++;
			continue;
		}
		break;
	}
	tok = p;
	while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '[') p++;
	if (*p != '\0') { *p = '\0'; p++; }
	*rest = p;
	return tok;
}

static int lc_eq(const char *a, const char *b)
{
	while (*a && *b) {
		if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
		a++; b++;
	}
	return *a == '\0' && *b == '\0';
}

static void default_order(const char *db, enum __nss_service *out, int max, int *n)
{
	*n = 0;
	if (lc_eq(db, "hosts")) {
		if (max > 0) out[(*n)++] = __NSS_SVC_FILES;
		if (max > 1) out[(*n)++] = __NSS_SVC_DNS;
	} else {
		/* passwd, group, and (defensively) anything else this
		 * library is ever asked about: "files" is the only
		 * backend that could possibly apply. */
		if (max > 0) out[(*n)++] = __NSS_SVC_FILES;
	}
}

int __nsswitch_order(const char *db, enum __nss_service *out, int max)
{
	FILE *f;
	char line[NSS_LINE_MAX];
	int n = 0;

	f = fopen(__NSS_NSSWITCH_PATH(), "r");
	if (!f) {
		default_order(db, out, max, &n);
		return n;
	}

	while (fgets(line, sizeof line, f) != NULL) {
		const char *rest;
		char *cursor, *tok;
		char *hash = strchr(line, '#');
		char *nl;

		if (hash) *hash = '\0';
		/* fgets() keeps the trailing newline (and, for a CRLF
		 * file, the \r before it); strip both so the LAST token
		 * on the line is not silently corrupted into "dns\n" (or
		 * "files\r\n") and dropped by next_service_token()'s own
		 * plain strcmp-shaped lc_eq() match below -- caught by
		 * this file's own scratch probe against a real fixture,
		 * not by inspection: "hosts: files dns" was parsing as
		 * files-only every time. */
		nl = strchr(line, '\n');
		if (nl) *nl = '\0';
		nl = strchr(line, '\r');
		if (nl) *nl = '\0';
		if (!is_db_line(line, db, &rest)) continue;

		/* Found the one stanza for this database (real
		 * nsswitch.conf never repeats a db: line; if it did,
		 * only the first is honored here, the same "first
		 * match wins" simplicity src/misc/linux/pwd.c's own
		 * file scan uses for /etc/passwd itself). */
		cursor = (char *)rest;
		while ((tok = next_service_token(&cursor)) != NULL) {
			enum __nss_service svc;

			if (lc_eq(tok, "files")) svc = __NSS_SVC_FILES;
			else if (lc_eq(tok, "dns")) svc = __NSS_SVC_DNS;
			else continue; /* unimplemented service: see banner */

			if (n < max) out[n] = svc;
			n++;
		}
		fclose(f);
		return n < max ? n : max;
	}

	fclose(f);
	/* File exists but never mentions this database: same documented
	 * default as a missing file. */
	default_order(db, out, max, &n);
	return n;
}

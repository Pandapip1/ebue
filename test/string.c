/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include <string.h>
#include <strings.h>
#include <wchar.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

static int fails;
static void wr(const char *s) { const char *e = s; while (*e) e++; write(2, s, e - s); }
static void wrnum(int n) { char b[16]; int i = 15; b[i] = 0; if (n < 0) { wr("-"); n = -n; } do b[--i] = '0' + n % 10; while (n /= 10); wr(b + i); }
#define CHECK(x) do { if (!(x)) { fails++; wr(__FILE__ ":"); wrnum(__LINE__); wr(": FAIL: " #x "\n"); } } while (0)

static int streq(const char *a, const char *b) { return a && b && !strcmp(a, b); }

int main(void)
{
	char buf[64], buf2[64], *p, *sp;
	wchar_t wb[16];
	static const wchar_t w1[] = { 'a', 'b', 'c', 0 };
	static const wchar_t w2[] = { 'a', 'b', 'd', 0 };
	static const wchar_t w3[] = { 0xffff, 0 };
	static const wchar_t w4[] = { 'a', 0 };
	size_t i;

	/* mem* */
	for (i = 0; i < sizeof buf; i++) buf[i] = (char)i;
	memcpy(buf2, buf, sizeof buf);
	CHECK(!memcmp(buf, buf2, sizeof buf));
	CHECK(memcmp("abc", "abd", 3) < 0 && memcmp("abd", "abc", 3) > 0);
	CHECK(memcmp("\xff", "\x01", 1) > 0);
	memmove(buf + 1, buf, 10); CHECK(buf[1] == 0 && buf[10] == 9 && buf[11] == 11);
	memmove(buf, buf + 1, 10); CHECK(buf[0] == 0 && buf[9] == 9);
	memset(buf, 'x', 33); CHECK(buf[0] == 'x' && buf[32] == 'x' && buf[33] == 33);
	memset(buf + 1, 0xab, 3); CHECK((unsigned char)buf[3] == 0xab && buf[0] == 'x');
	CHECK(memchr("hello", 'l', 5) == (void *)("hello" + 2) || *(char *)memchr("hello", 'l', 5) == 'l');
	CHECK(!memchr("hello", 'z', 5) && !memchr("hello", 'o', 4));
	strcpy(buf, "hello"); CHECK(memrchr(buf, 'l', 5) == buf + 3);
	CHECK(memmem("abcabcd", 7, "abcd", 4) == (void *)0 || streq(memmem("abcabcd", 7, "abcd", 4), "abcd"));
	strcpy(buf, "abcabcd"); CHECK(memmem(buf, 7, "abcd", 4) == buf + 3 && !memmem(buf, 7, "abce", 4) && memmem(buf, 7, "", 0) == buf);
	CHECK(memmem(buf, 7, "d", 1) == buf + 6 && memmem(buf, 7, "cd", 2) == buf + 5 && !memmem(buf, 3, "abcd", 4));
	CHECK(mempcpy(buf2, "xy", 2) == buf2 + 2);
	CHECK(memccpy(buf2, "hello", 'l', 5) == buf2 + 3 && !memccpy(buf2, "hello", 'z', 5));

	/* str basics */
	CHECK(strlen("") == 0 && strlen("abc") == 3 && strlen("0123456789abcdefghij") == 20);
	CHECK(strlen("0123456789abcdefghij" + 3) == 17);
	CHECK(strnlen("abc", 2) == 2 && strnlen("abc", 10) == 3);
	CHECK(strcpy(buf, "abc") == buf && streq(buf, "abc"));
	CHECK(stpcpy(buf, "abcd") == buf + 4);
	memset(buf, 'z', 10); strncpy(buf, "ab", 5); CHECK(buf[1] == 'b' && buf[2] == 0 && buf[4] == 0 && buf[5] == 'z');
	memset(buf, 'z', 10); strncpy(buf, "abcdef", 3); CHECK(buf[2] == 'c' && buf[3] == 'z');
	CHECK(stpncpy(buf, "ab", 5) == buf + 2);
	strcpy(buf, "ab"); CHECK(strcat(buf, "cd") == buf && streq(buf, "abcd"));
	strcpy(buf, "ab"); strncat(buf, "cdef", 2); CHECK(streq(buf, "abcd"));
	CHECK(strcmp("a", "a") == 0 && strcmp("a", "b") < 0 && strcmp("b", "a") > 0 && strcmp("ab", "a") > 0 && strcmp("", "a") < 0);
	CHECK(strcmp("\xff", "a") > 0);
	CHECK(strncmp("abc", "abd", 2) == 0 && strncmp("abc", "abd", 3) < 0 && strncmp("a", "b", 0) == 0 && strncmp("ab", "a", 5) > 0);
	CHECK(strcoll("a", "b") < 0);
	CHECK(strxfrm(buf, "hello", sizeof buf) == 5 && streq(buf, "hello"));
	CHECK(strxfrm(buf, "hello", 3) == 5 && streq(buf, "he"));
	CHECK(strxfrm(0, "hello", 0) == 5);
	strcpy(buf, "hello"); CHECK(strchr(buf, 'l') == buf + 2 && strchr(buf, 0) == buf + 5 && !strchr(buf, 'z'));
	CHECK(strrchr(buf, 'l') == buf + 3 && strrchr(buf, 0) == buf + 5 && !strrchr(buf, 'z') && strrchr(buf, 'h') == buf);
	CHECK(strchrnul(buf, 'z') == buf + 5 && strchrnul(buf, 'e') == buf + 1);
	CHECK(strcspn("hello", "lo") == 2 && strcspn("hello", "z") == 5 && strcspn("hello", "") == 5 && strcspn("hello", "h") == 0);
	CHECK(strspn("hello", "he") == 2 && strspn("hello", "") == 0 && strspn("aaa", "a") == 3 && strspn("hello", "hel") == 4);
	CHECK(strpbrk(buf, "ol") == buf + 2 && !strpbrk(buf, "xyz"));
	CHECK(strstr(buf, "ll") == buf + 2 && strstr(buf, "") == buf && !strstr(buf, "llx") && strstr(buf, "o") == buf + 4 && !strstr("ab", "abc"));
	CHECK(strcasestr("HeLLo", "ll") != 0 && !strcasestr("hello", "lx"));

	/* tokenising */
	strcpy(buf, " a  b,c ");
	p = strtok(buf, " ,"); CHECK(streq(p, "a"));
	p = strtok(0, " ,"); CHECK(streq(p, "b"));
	p = strtok(0, " ,"); CHECK(streq(p, "c"));
	CHECK(!strtok(0, " ,"));
	strcpy(buf, "x:y::z");
	p = strtok_r(buf, ":", &sp); CHECK(streq(p, "x"));
	p = strtok_r(0, ":", &sp); CHECK(streq(p, "y"));
	p = strtok_r(0, ":", &sp); CHECK(streq(p, "z"));
	CHECK(!strtok_r(0, ":", &sp));
	strcpy(buf, "x:y::z"); sp = buf;
	CHECK(streq(strsep(&sp, ":"), "x"));
	CHECK(streq(strsep(&sp, ":"), "y"));
	CHECK(streq(strsep(&sp, ":"), ""));
	CHECK(streq(strsep(&sp, ":"), "z") && sp == 0);
	CHECK(strsep(&sp, ":") == 0);

	/* dup / lcpy / lcat */
	p = strdup("dup"); CHECK(streq(p, "dup")); free(p);
	p = strndup("duplicate", 3); CHECK(streq(p, "dup")); free(p);
	p = strndup("du", 5); CHECK(streq(p, "du")); free(p);
	CHECK(strlcpy(buf, "hello", 3) == 5 && streq(buf, "he"));
	CHECK(strlcpy(buf, "hello", sizeof buf) == 5 && streq(buf, "hello"));
	CHECK(strlcpy(buf, "x", 0) == 1 && streq(buf, "hello"));
	CHECK(strlcat(buf, " world", sizeof buf) == 11 && streq(buf, "hello world"));
	CHECK(strlcat(buf, "!", 8) == 9 && streq(buf, "hello world"));
	strcpy(buf, "ab"); CHECK(strlcat(buf, "cdef", 5) == 6 && streq(buf, "abcd"));

	/* case */
	CHECK(strcasecmp("Hello", "hELLO") == 0 && strcasecmp("a", "B") < 0 && strcasecmp("abc", "ab") > 0);
	CHECK(strncasecmp("HeLx", "helY", 3) == 0 && strncasecmp("HeLx", "helY", 4) < 0 && strncasecmp("a", "b", 0) == 0);
	CHECK(strcasecmp_l("A", "a", 0) == 0 && strncasecmp_l("Ab", "aC", 1, 0) == 0);

	/* verscmp */
	CHECK(strverscmp("a1", "a10") < 0 && strverscmp("a2", "a10") < 0 && strverscmp("a10", "a2") > 0);
	CHECK(strverscmp("1.0", "1.0") == 0 && strverscmp("a", "b") < 0);
	CHECK(strverscmp("foo009", "foo1") < 0 && strverscmp("foo010", "foo09") < 0);

	/* bsd */
	CHECK(bcmp("ab", "ab", 2) == 0 && bcmp("ab", "ac", 2) != 0);
	strcpy(buf, "hello"); bcopy(buf, buf + 1, 5); CHECK(!memcmp(buf, "hhello", 6));
	bzero(buf, 3); CHECK(!buf[0] && !buf[2] && buf[3] == 'l');
	strcpy(buf, "secret"); explicit_bzero(buf, 6); CHECK(!buf[0] && !buf[5]);
	strcpy(buf, "hello"); CHECK(index(buf, 'l') == buf + 2 && rindex(buf, 'l') == buf + 3);
	CHECK(ffs(0) == 0 && ffs(1) == 1 && ffs(0x80) == 8 && ffs(-1) == 1 && ffs(0x80000000) == 32);
	CHECK(ffsl(0) == 0 && ffsl(0x100) == 9 && ffsl(-1L) == 1);
	CHECK(ffsll(0) == 0 && ffsll(1LL << 40) == 41 && ffsll(-1LL) == 1 && ffsll((long long)(1ULL << 63)) == 64);

	/* errors and signals */
	CHECK(streq(strerror(0), "No error information"));
	CHECK(streq(strerror(ENOENT), "No such file or directory"));
	CHECK(streq(strerror(EINVAL), "Invalid argument"));
	CHECK(streq(strerror(ENOTRECOVERABLE), "State not recoverable"));
	CHECK(streq(strerror(ENOTSUP), "Not supported"));
	CHECK(streq(strerror(EWOULDBLOCK), strerror(EAGAIN)));
	CHECK(streq(strerror(41), "No error information"));
	CHECK(streq(strerror(-1), "No error information") && streq(strerror(9999), "No error information"));
	CHECK(streq(strerror_l(EPIPE, 0), "Broken pipe"));
	CHECK(strerror_r(EDOM, buf, sizeof buf) == 0 && streq(buf, "Domain error"));
	CHECK(strerror_r(EDOM, buf, 4) == ERANGE && streq(buf, "Dom"));
	CHECK(strerror_r(EDOM, buf, 0) == ERANGE);
	CHECK(streq(strsignal(SIGINT), "Interrupt") && streq(strsignal(SIGSEGV), "Segmentation fault"));
	CHECK(streq(strsignal(SIGSYS), "Bad system call") && streq(strsignal(0), "Unknown signal") && streq(strsignal(99), "Unknown signal"));

	/* wide */
	CHECK(wcslen(w1) == 3 && wcslen(w1 + 3) == 0);
	CHECK(wcscpy(wb, w1) == wb && !wcscmp(wb, w1));
	CHECK(wcscmp(w1, w2) < 0 && wcscmp(w2, w1) > 0 && wcsncmp(w1, w2, 2) == 0 && wcsncmp(w1, w2, 3) < 0);
	wmemset(wb, 'z', 8); wcsncpy(wb, w1, 5); CHECK(wb[2] == 'c' && wb[3] == 0 && wb[4] == 0 && wb[5] == 'z');
	wcscpy(wb, w1); CHECK(wcscat(wb, w2) == wb && wcslen(wb) == 6 && wb[5] == 'd');
	wcscpy(wb, w1); wcsncat(wb, w2, 1); CHECK(wcslen(wb) == 4 && wb[3] == 'a');
	CHECK(wcschr(w1, 'b') == w1 + 1 && !wcschr(w1, 'z') && wcschr(w1, 0) == w1 + 3);
	wcscpy(wb, w1); wb[3] = 'a'; wb[4] = 0;
	CHECK(wcsrchr(wb, 'a') == wb + 3 && wcsrchr(wb, 'b') == wb + 1 && !wcsrchr(wb, 'z') && wcsrchr(wb, 0) == wb + 4);
	CHECK(wmemcpy(wb, w2, 4) == wb && !wmemcmp(wb, w2, 4) && wmemcmp(w1, w2, 3) < 0 && wmemcmp(w1, w2, 2) == 0);
	wmemmove(wb + 1, wb, 3); CHECK(wb[1] == 'a' && wb[3] == 'd');
	CHECK(wmemchr(w1, 'c', 3) == w1 + 2 && !wmemchr(w1, 'c', 2));
	CHECK(wmemcmp(w3, w4, 1) > 0);

	if (fails) { wr("string: failures: "); wrnum(fails); wr("\n"); return 1; }
	wr("string: all ok\n");
	return 0;
}

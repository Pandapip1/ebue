/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <ctype.h>
#include <unistd.h>

static int fails;
static void wr(const char *s) { const char *e = s; while (*e) e++; write(2, s, e - s); }
static void wrnum(int n) { char b[16]; int i = 15; b[i] = 0; if (n < 0) { wr("-"); n = -n; } do b[--i] = '0' + n % 10; while (n /= 10); wr(b + i); }
#define CHECK(x) do { if (!(x)) { fails++; wr(__FILE__ ":"); wrnum(__LINE__); wr(": FAIL: " #x "\n"); } } while (0)

int main(void)
{
	int c;
	for (c = -1; c < 256; c++) {
		int up = c >= 'A' && c <= 'Z', lo = c >= 'a' && c <= 'z', dg = c >= '0' && c <= '9';
		int sp = c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
		int pr = c >= 0x20 && c < 0x7f;
		CHECK(!!isupper(c) == up);
		CHECK(!!islower(c) == lo);
		CHECK(!!isalpha(c) == (up || lo));
		CHECK(!!isdigit(c) == dg);
		CHECK(!!isalnum(c) == (up || lo || dg));
		CHECK(!!isxdigit(c) == (dg || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')));
		CHECK(!!isspace(c) == sp);
		CHECK(!!isblank(c) == (c == ' ' || c == '\t'));
		CHECK(!!iscntrl(c) == ((c >= 0 && c < 0x20) || c == 0x7f));
		CHECK(!!isprint(c) == pr);
		CHECK(!!isgraph(c) == (pr && c != ' '));
		CHECK(!!ispunct(c) == (pr && c != ' ' && !up && !lo && !dg));
		CHECK(toupper(c) == (lo ? c - 32 : c));
		CHECK(tolower(c) == (up ? c + 32 : c));
		CHECK(!!isascii(c) == (c >= 0 && c < 128));
		CHECK(toascii(c) == (c & 0x7f));
	}
	if (fails) { wr("ctype: failures: "); wrnum(fails); wr("\n"); return 1; }
	wr("ctype: all ok\n");
	return 0;
}

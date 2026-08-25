/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * hcreate/hsearch/hdestroy: one process-wide open-addressing hash
 * table, exactly as hcreate.html specifies -- "Only one hash table may
 * be active at a time." hdestroy() frees the table's own bookkeeping
 * only; ENTRY.key/.data are the caller's, never touched here (same
 * rule tdelete()/tsearch.c's free_subtree() follows for tree nodes).
 */
#include <search.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

struct slot {
	char *key;	/* NULL: empty; (char *)-1 unused here, no tombstones needed (no delete op) */
	void *data;
	unsigned long h;
	int used;
};

static struct slot *table;
static size_t table_size;

/* FNV-1a: fine for short C-string keys, deliberately wraps mod 2^32
 * (unsigned arithmetic, not UB) -- __wraps documents that instead of
 * letting -fsanitize=unsigned-integer-overflow flag it as accidental. */
__wraps static unsigned long hash_str(const char *s)
{
	unsigned long h = 2166136261UL;
	while (*s) {
		h ^= (unsigned char)*s++;
		h *= 16777619UL;
	}
	return h;
}

int hcreate(size_t nel)
{
	size_t cap;

	if (table) hdestroy();

	/* hcreate.html RETURN VALUE: "shall return 0 if it cannot allocate
	 * sufficient space for the table".
	 *
	 * The padding below is `nel + nel/2 + 8` in size_t, and that
	 * arithmetic WRAPS for a large enough nel -- silently producing a
	 * tiny capacity that calloc() then satisfies easily, so hcreate()
	 * reported success for a table that could not come close to holding
	 * nel entries.  Measured before this check: hcreate((SIZE_MAX/3)*2 +
	 * 2) returned 1 and the 11th ENTER returned NULL -- ten slots
	 * reported as sufficient for 1.2e19 entries.  That is exactly the
	 * case the RETURN VALUE clause exists to report, so the wrap turned
	 * a required failure into a false success.
	 *
	 * Refusing up front is the fix rather than checking after the fact,
	 * because after the wrap there is nothing left to detect: the sum is
	 * a perfectly ordinary small number.  calloc() cannot cover this
	 * either -- it guards its OWN multiply (m > SIZE_MAX/n) and is handed
	 * an already-wrapped cap.
	 *
	 * The bound is stated so it cannot itself overflow: dividing first,
	 * never adding first.  [ENOMEM] is hcreate.html's one listed error
	 * ("may fail"), and is the honest description of a request whose
	 * table could never be allocated. */
	if (nel > (((size_t)-1 - 8) / 3) * 2) { errno = ENOMEM; return 0; }
	/* "may be adjusted upward" -- pad for load factor, keep it a
	 * comfortable margin above nel so linear probing stays cheap. */
	cap = nel + nel / 2 + 8;
	table = calloc(cap, sizeof *table);
	if (!table) return 0;
	table_size = cap;
	return 1;
}

void hdestroy(void)
{
	free(table);
	table = NULL;
	table_size = 0;
}

ENTRY *hsearch(ENTRY item, ACTION action)
{
	unsigned long h;
	size_t i, start;

	if (!table) return NULL;

	h = hash_str(item.key);
	start = h % table_size;
	i = start;
	do {
		if (!table[i].used) {
			if (action != ENTER) return NULL;
			table[i].key = item.key;
			table[i].data = item.data;
			table[i].h = h;
			table[i].used = 1;
			return (ENTRY *)&table[i];
		}
		if (table[i].h == h && strcmp(table[i].key, item.key) == 0)
			return (ENTRY *)&table[i];	/* ENTER on a hit: leave existing data alone */
		i = (i + 1) % table_size;
	} while (i != start);

	return NULL;	/* table full */
}

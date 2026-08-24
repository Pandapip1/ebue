/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * src/search -- hsearch (an open-addressed hash table), tsearch (an
 * unbalanced binary search tree), lsearch/lfind (linear search over a
 * caller's array) and insque/remque (intrusive doubly-linked lists).
 * Four small data structures with no OS dependency, whose bugs are
 * exactly the kind a fixed test misses: they appear only after a
 * particular *sequence* of operations, which is what a fuzzer generates
 * and a hand-written test does not.
 *
 * The input is read as a program: a leading table size, then one
 * operation per record, each naming a key drawn from the remaining
 * bytes.  The same key stream drives all four structures, so an insert
 * order that breaks the tree is also tried against the hash table.
 *
 * hcreate's nel IS BOUNDED, deliberately.  test/posix-glob.c's
 * test_search_hcreate_overflow fence records that `cap = nel + nel/2 + 8`
 * has no overflow check, so nel near SIZE_MAX wraps to a capacity of 10
 * and hcreate reports success; the wrapped table then divides by a
 * table_size that a further wrap can make 0.  That defect is known,
 * fenced, and reproduced deterministically by a two-line test -- a
 * fuzzer adds nothing to it and would only spend its budget dying on
 * it.  nel is therefore taken modulo 4096, which also keeps calloc off
 * multi-gigabyte allocations that would make every input slow.
 *
 * WHAT IS ASSERTED.  The structural invariants, not a reimplementation:
 *
 *   - hsearch(FIND) after hsearch(ENTER) of the same key finds the same
 *     slot, and FIND on a key never entered returns NULL;
 *   - ENTER on an existing key returns the existing entry and leaves
 *     its data alone (hsearch.html: "shall not be changed");
 *   - every pointer hsearch returns is inside the table it allocated,
 *     which is what catches a probe sequence that walks off the end;
 *   - tsearch returns a pointer to a slot holding the key, tfind finds
 *     exactly what tsearch inserted, tdelete removes exactly one, and
 *     the tree empties completely when every inserted key is deleted --
 *     the last of which is also what lets LeakSanitizer account for the
 *     nodes, since POSIX gives no tdestroy;
 *   - twalk visits each node once as a leaf or three times as an
 *     internal node, and the count matches the tree's size;
 *   - lsearch appends at most one element per miss and lfind never
 *     appends;
 *   - insque/remque keep the list's forward and backward chains each
 *     other's inverse.
 *
 * Keys live in one static arena and are never freed, because hsearch
 * stores the caller's `char *key` pointer rather than a copy
 * (src/search/hsearch.c) and POSIX says the caller owns it.  Freeing
 * them between inputs would make every subsequent FIND a use-after-free
 * in the harness, not in the library.
 */
#include <search.h>
#include <string.h>
#include <stdlib.h>

extern void oracle_mismatch_i(const char *, const char *, long long, long long);

#define NKEY   32
#define KEYLEN 16
#define ARENA  (NKEY * (KEYLEN + 1))

static char arena[ARENA];
static char *keys[NKEY];
static int nkeys;

static int cmp(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

static size_t walked;
static void visit(const void *node, VISIT order, int depth)
{
	(void)node; (void)depth;
	if (order == leaf || order == postorder) walked++;
}

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
	size_t nel, i, ops;
	void *troot = 0;
	char *inserted[NKEY];
	int ninserted = 0;
	char lbase[NKEY][KEYLEN + 1];
	size_t lnel = 0;

	if (size < 3) return 0;

	/* ---- carve the key stream ------------------------------------- */
	nkeys = 0;
	{
		size_t off = 0;
		const unsigned char *p = data + 2;
		size_t n = size - 2;
		while (nkeys < NKEY && n) {
			size_t klen = (size_t)p[0] % KEYLEN + 1;
			size_t j;
			p++; n--;
			if (klen > n) klen = n;
			if (!klen) break;
			for (j = 0; j < klen; j++) {
				unsigned char c = p[j];
				/* No embedded NUL: these are C strings, and a NUL would
				 * silently shorten every key to the same empty one. */
				arena[off + j] = c ? (char)c : '_';
			}
			arena[off + klen] = 0;
			keys[nkeys++] = arena + off;
			off += klen + 1;
			p += klen; n -= klen;
		}
	}
	if (!nkeys) return 0;

	/* ---- hsearch --------------------------------------------------- */
	nel = (size_t)data[0] * 16 + data[1];   /* 0 .. 4095; see the banner */
	if (hcreate(nel)) {
		int entered[NKEY];
		memset(entered, 0, sizeof entered);
		for (i = 0; i < (size_t)nkeys; i++) {
			ENTRY item, *e;
			item.key = keys[i];
			item.data = keys[i];

			e = hsearch(item, FIND);
			if (!entered[i] && e && strcmp(e->key, keys[i]) != 0)
				oracle_mismatch_i("hsearch(FIND) returned a different key", keys[i], 0, 1);

			e = hsearch(item, ENTER);
			if (!e) continue;               /* table full is a legal answer */
			entered[i] = 1;
			if (strcmp(e->key, keys[i]) != 0)
				oracle_mismatch_i("hsearch(ENTER) returned a different key", keys[i], 0, 1);
			{
				ENTRY probe, *f;
				probe.key = keys[i];
				probe.data = 0;
				f = hsearch(probe, FIND);
				if (f != e)
					oracle_mismatch_i("hsearch(FIND) did not find what ENTER returned",
					                  keys[i], f != 0, 1);
				/* "shall not be changed" -- a second ENTER on a present
				 * key must leave the stored data alone. */
				probe.data = (void *)arena;
				f = hsearch(probe, ENTER);
				if (f && f->data != e->data)
					oracle_mismatch_i("hsearch(ENTER) overwrote an existing entry's data",
					                  keys[i], 0, 1);
			}
		}
		hdestroy();
	}

	/* ---- tsearch / tfind / tdelete / twalk -------------------------- */
	for (i = 0; i < (size_t)nkeys; i++) {
		void **slot = tsearch(keys[i], &troot, cmp);
		if (!slot) continue;                    /* out of memory is legal */
		if (strcmp(*(char **)slot, keys[i]) != 0)
			oracle_mismatch_i("tsearch slot does not hold the key", keys[i], 0, 1);
		if (tfind(keys[i], &troot, cmp) == 0)
			oracle_mismatch_i("tfind cannot find what tsearch just inserted",
			                  keys[i], 0, 1);
		if (*(char **)slot == keys[i]) {
			int dup = 0, j;
			for (j = 0; j < ninserted; j++)
				if (strcmp(inserted[j], keys[i]) == 0) dup = 1;
			if (!dup) inserted[ninserted++] = keys[i];
		}
	}
	walked = 0;
	twalk(troot, visit);
	if (troot && walked != (size_t)ninserted)
		oracle_mismatch_i("twalk visited a different number of nodes than were inserted",
		                  keys[0], (long long)walked, (long long)ninserted);
	for (i = 0; i < (size_t)ninserted; i++) {
		if (tdelete(inserted[i], &troot, cmp) == 0)
			oracle_mismatch_i("tdelete could not remove an inserted key",
			                  inserted[i], 0, 1);
		if (tfind(inserted[i], &troot, cmp) != 0)
			oracle_mismatch_i("tfind still finds a deleted key", inserted[i], 0, 1);
	}
	if (troot != 0)
		oracle_mismatch_i("the tree is not empty after deleting every key",
		                  keys[0], 1, 0);

	/* ---- lsearch / lfind -------------------------------------------- */
	for (i = 0; i < (size_t)nkeys; i++) {
		size_t before = lnel;
		void *r;

		r = lfind(keys[i], lbase, &lnel, KEYLEN + 1, cmp);
		if (lnel != before)
			oracle_mismatch_i("lfind changed the element count", keys[i],
			                  (long long)lnel, (long long)before);
		if (r && strcmp((char *)r, keys[i]) != 0)
			oracle_mismatch_i("lfind returned a different element", keys[i], 0, 1);

		if (lnel >= NKEY) break;                /* the array is the caller's */
		{
			char pad[KEYLEN + 1];
			memset(pad, 0, sizeof pad);
			strcpy(pad, keys[i]);
			r = lsearch(pad, lbase, &lnel, KEYLEN + 1, cmp);
			if (!r)
				oracle_mismatch_i("lsearch returned NULL with room to spare",
				                  keys[i], 0, 1);
			else if (strcmp((char *)r, keys[i]) != 0)
				oracle_mismatch_i("lsearch returned a different element", keys[i], 0, 1);
			if (lnel > before + 1)
				oracle_mismatch_i("lsearch appended more than one element", keys[i],
				                  (long long)lnel, (long long)(before + 1));
			if (lnel == before && !lfind(keys[i], lbase, &lnel, KEYLEN + 1, cmp))
				oracle_mismatch_i("lsearch appended nothing and the key is absent",
				                  keys[i], 0, 1);
		}
	}

	/* ---- insque / remque --------------------------------------------
	 * The elements are the harness's own; the library only rewrites the
	 * two pointers at the front of each.  What is checked is that the
	 * forward and backward chains stay each other's inverse through an
	 * arbitrary insert/remove order -- a linear list here, since the
	 * circular case has no terminator and a fuzzer-driven walk of it
	 * could not tell a correct ring from a corrupted one. */
	{
		struct qel { struct qel *fwd, *bwd; int id; } el[NKEY];
		int n = nkeys < NKEY ? nkeys : NKEY;
		int j;

		for (j = 0; j < n; j++) { el[j].fwd = el[j].bwd = 0; el[j].id = j; }
		insque(&el[0], 0);
		for (j = 1; j < n; j++) {
			/* Insert after an element chosen by the key stream, so the
			 * order is the fuzzer's, not a fixed append. */
			int after = (unsigned char)keys[j][0] % j;
			insque(&el[j], &el[after]);
		}
		for (j = 0; j < n; j++) {
			if (el[j].fwd && el[j].fwd->bwd != &el[j])
				oracle_mismatch_i("insque: fwd->bwd is not the element", keys[0], j, 0);
			if (el[j].bwd && el[j].bwd->fwd != &el[j])
				oracle_mismatch_i("insque: bwd->fwd is not the element", keys[0], j, 0);
		}
		for (j = n - 1; j >= 0; j--) {
			remque(&el[j]);
			{
				int k;
				for (k = 0; k < j; k++) {
					if (el[k].fwd && el[k].fwd->bwd != &el[k])
						oracle_mismatch_i("remque: fwd->bwd is not the element",
						                  keys[0], k, 0);
					if (el[k].bwd && el[k].bwd->fwd != &el[k])
						oracle_mismatch_i("remque: bwd->fwd is not the element",
						                  keys[0], k, 0);
				}
			}
		}
	}
	return 0;
}

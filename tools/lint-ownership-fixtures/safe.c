/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

typedef __SIZE_TYPE__ size_t;
[[clang::ownership_returns(malloc)]]
void *malloc(size_t);
[[clang::ownership_returns(malloc)]]
void *calloc(size_t, size_t);
[[ownership_reallocates(1), clang::ownership_returns(malloc)]]
void *realloc(void *, size_t);
[[clang::ownership_takes(malloc, 1)]]
void free(void *);

void direct_owner(void)
{
	void *p = malloc(8);
	free(p);
}

void alias_borrow(void)
{
	void *owner = calloc(2, 8);
	void *borrow = owner;
	free(borrow);
}

void nullable_owner(int choose)
{
	void *p = choose ? malloc(8) : 0;
	free(p);
}

void null_release(void)
{
	free(0);
}

void conditional_transfer(void)
{
	void *owner = malloc(8);
	void *replacement = realloc(owner, 16);
	if (replacement)
		free(replacement);
	else
		free(owner);
}

/* A parameter that arrives already carrying ownership -- the exact shape
 * of every destructor-style wrapper in this tree: closedir(DIR *dp)
 * frees dp itself (malloc'd by opendir(), a DIFFERENT function this
 * per-function analysis never sees), posix_close(int fd) closes an fd
 * opened by whoever called it, and so on. OwnershipMap can only ever
 * gain an Owned entry for a symbol by watching THIS analysis's own
 * malloc()/realloc() return it (checkPostCall above); a parameter's
 * value exists before any code in this function has run, so no code on
 * the callee side can *ever* satisfy that check -- unlike the nonnull
 * proof (checkPointerExpression), where an explicit `if (!p) return`
 * really does establish the fact for the checker to see, there is no
 * corresponding pattern here that would ever flip this from unproved to
 * proved. Demanding it anyway does not distinguish a real ownership bug
 * from an ordinary release wrapper; it fires identically, 100% of the
 * time, for both -- which is exactly the "unconditional noise, not a
 * real proof obligation" situation that justified trusting a borrow's
 * liveness in ValidPointerChecker (see its own comment). Freeing a
 * pointer this analysis can concretely see is NOT a heap allocation --
 * the address of a stack variable, a string literal -- is real, checked
 * evidence of a bug and is still reported (unsafe.c's release_stack/
 * release_literal remain flagged); only the "provenance is simply
 * unknown to this function" case is trusted here. */
void release_borrow(void *borrow)
{
	free(borrow);
}

void resize_borrow(void *borrow)
{
	(void)realloc(borrow, 16);
}

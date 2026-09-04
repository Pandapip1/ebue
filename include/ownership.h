/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _OWNERSHIP_H
#define _OWNERSHIP_H

/* An opt-in ownership dialect expressed entirely in ordinary C syntax.
 * Token policy belongs to the nominal token typedef; values and operations
 * refer to that policy by name. */
#define __token_type typedef struct { char _tok; }
#define tokdef __token_type
/* Linear tokens are strict by default.  A permissive token remains linear,
 * but may stay behind while other, unlimited tokens on its carrier copy. */
#define l_strict __attribute__((annotate("qual:l_strict")))
#define l_permissive __attribute__((annotate("qual:l_permissive")))
#define l_unlimited __attribute__((annotate("qual:l_unlimited")))
#define implicit_drop __attribute__((annotate("qual:implicit_drop")))
#define dynamic_storage __attribute__((annotate("qual:dynamic_storage")))
#define implemented_by(token_name) \
	__attribute__((annotate("qual:implemented_by=" #token_name)))
#define string_literal __attribute__((annotate("qual:string_literal")))
#define extent_at_least __attribute__((annotate("qual:extent_at_least")))
#define element_extent __attribute__((annotate("qual:element_extent")))
#define disjoint_extent __attribute__((annotate("qual:disjoint_extent")))
#define zero_vacuous __attribute__((annotate("qual:zero_vacuous")))
#define sentinel_exclude(value) \
	__attribute__((annotate("qual:sentinel_exclude=" #value)))
#define blocks_dereference \
	__attribute__((annotate("qual:blocks_dereference")))
#define withtok(token_name) \
	__attribute__((annotate("withtok:" #token_name)))
#ifdef NTLIBC_OWNERSHIP_ANALYSIS
#define elements_withtok(token_name, extent_name) \
	__attribute__((annotate("elements_withtok:" #token_name ":" #extent_name)))
#else
#define elements_withtok(token_name, extent_name)
#endif
#define withhandle(handle_name) \
	__attribute__((annotate("withhandle:" #handle_name)))
#define withouttok(token_name) \
	__attribute__((annotate("withouttok:" #token_name)))
#define consume(token_name) \
	__attribute__((annotate("consume:" #token_name)))
#define consume_any(token_name) \
	__attribute__((annotate("consume_any:" #token_name)))
#define grant(token_name) \
	__attribute__((annotate("grant:" #token_name)))
#define drop(token_name) \
	__attribute__((annotate("drop:" #token_name)))
#define consume_if_nonnull_return(token_name) \
	__attribute__((annotate("consume_if_nonnull_return:" #token_name)))
#define construct(handle_name) \
	__attribute__((annotate("construct:" #handle_name)))
#define destroy(handle_name) \
	__attribute__((annotate("destroy:" #handle_name)))
#define handle(handle_name) \
	__attribute__((annotate("handle:" #handle_name)))
#define static_handle(handle_name) \
	__attribute__((annotate("static_handle:" #handle_name)))
/* A pointer-to-struct parameter whose pointee type carries its own
 * withtok(readable_elements(...))/withtok(writable_elements(...)) field
 * contracts (see include/memory_tokens.h). fields_established is a real,
 * two-sided obligation, not a blind trust of whatever the fields happen
 * to hold: MemoryContractChecker's checkPreCall independently verifies,
 * from the CALLER's own current knowledge, that every one of those field
 * contracts already holds for the argument BEFORE allowing the call --
 * exactly the same "prove it at the call site" discipline withtok(...)
 * Require parameters already get. Only once that is satisfied does
 * checkBeginFunction seed the callee's own reasoning (so a function
 * analyzed on its own, with no visible caller, is not forced to treat an
 * incoming struct's fields as permanently unprovable). Omitting this on
 * a parameter that is actually mutated is always safe -- the checker
 * just treats the incoming fields as unconstrained, as it always did. */
#define fields_established \
	__attribute__((annotate("fields_established")))

#endif

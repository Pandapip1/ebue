/* C library internals and platform ABI fields intentionally use the
 * implementation-reserved namespace so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Internal types and functions shared among awk's own four translation
 * units (src/util/awk_lex.c, src/util/awk_parse.c, src/util/awk_run.c,
 * src/util/awk.c) -- the same "one piece of logic, several callers, a
 * private header nobody outside awk includes" shape as src/util/
 * tablist.h and src/util/modeparse.h, except here all four "callers"
 * are pieces of the same utility rather than independent utilities
 * sharing one grammar.  See src/util/awk.c's own header comment for
 * the XCU awk(1p) citations, the full list of what is and is not
 * implemented, and every deliberate scope narrowing; this header is
 * just the shared plumbing those decisions are built out of.
 */
#ifndef _NTLIBC_UTIL_AWK_PRIV_H
#define _NTLIBC_UTIL_AWK_PRIV_H

#include <stddef.h>
#include <stdio.h>
#include <regex.h>

/* ==== string-keyed hash table ===========================================
 *
 * One generic implementation, reused for: the global variable table
 * (name -> struct awk_cell *), every array's own element table
 * (subscript string, already SUBSEP-joined for a multi-dimensional
 * reference -> struct awk_cell *), the open-stream table print/printf/
 * getline redirection and close() share (target string -> struct
 * awk_stream *), and the dynamic-regex compile cache (ERE source
 * string -> regex_t *).  Chained (separately-linked buckets), not
 * open-addressed: awk programs delete array elements and close streams
 * routinely, and a chained table never needs the tombstone bookkeeping
 * open addressing does under deletion.
 */
struct awk_hnode {
	struct awk_hnode *next;
	char *key;   /* owned */
	void *val;   /* opaque; ownership is the specific table's caller's */
};

struct awk_htab {
	struct awk_hnode **buckets;
	size_t nbuckets;
	size_t count;
};

void awk_htab_init(struct awk_htab *t) __attribute__((nonnull(1)));
/* NULL if key is absent. */
void *awk_htab_get(struct awk_htab *t, const char *key) __attribute__((nonnull(1, 2)));
/* Returns the address of the val slot for key, creating an entry with
 * val==NULL if it was absent.  NULL only on allocation failure. */
void **awk_htab_getp(struct awk_htab *t, const char *key) __attribute__((nonnull(1, 2)));
/* Removes key if present, calling free_val (if non-NULL) on its val
 * first.  No-op if key was absent. */
void awk_htab_del(struct awk_htab *t, const char *key, void (*free_val)(void *)) __attribute__((nonnull(1, 2)));
void awk_htab_free(struct awk_htab *t, void (*free_val)(void *)) __attribute__((nonnull(1)));

struct awk_hiter {
	struct awk_htab *t;
	size_t bi;
	struct awk_hnode *n;
};
void awk_hiter_init(struct awk_hiter *it, struct awk_htab *t) __attribute__((nonnull(1, 2)));
/* Returns the next node, or NULL when exhausted.  Iteration order is
 * bucket order then chain order -- an arbitrary but stable-for-one-
 * table order, which is all XCU awk(1p) itself promises `for (k in
 * arr)` ("The order ... is unspecified"); see src/util/awk.c's header
 * for this being recorded there as the deliberate reading of that
 * unspecified case, not an oversight. */
struct awk_hnode *awk_hiter_next(struct awk_hiter *it) __attribute__((nonnull(1)));

/* ==== scalar/array cell (the one storage cell every variable, array
 * element, and function parameter is) ====================================
 *
 * A cell starts out untyped (is_array==0, flags==0): the "uninitialized
 * value" XCU awk(1p) defines as simultaneously numeric 0 and string ""
 * and which compares numerically against a numeric operand. It commits
 * to being a scalar the first time a number or string is stored into
 * it (flags gains AWK_HAS_NUM and/or AWK_HAS_STR), or to being an array
 * the first time it is subscripted, iterated with `for (k in x)`,
 * handed to split()'s array argument, or named in `delete x[...]` --
 * see awk_run.c's promote-to-array helper. AWK_STRNUM additionally
 * marks a scalar as a "numeric string" per XCU's own defined term
 * (field variables, split() results, getline-assigned variables,
 * ARGV/ENVIRON elements, and command-line var=value operands, and only
 * those, per that section's own enumeration) -- awk_run.c's comparison
 * code is the one reader of this bit that actually changes behavior
 * (src/util/awk.c's header quotes the exact comparison rule this bit
 * implements). */
#define AWK_HAS_NUM 0x1
#define AWK_HAS_STR 0x2
#define AWK_STRNUM  0x4

struct awk_cell {
	unsigned char is_array;
	unsigned char flags;
	double num;
	char *str;          /* owned, NUL-terminated; valid iff flags&AWK_HAS_STR */
	struct awk_htab *arr; /* non-NULL iff is_array */
};

/* ==== lexer ==============================================================
 *
 * One token of lookahead is all the grammar below ever needs (the
 * parser peeks tok, calls awk_lex_next() to advance) -- XCU awk(1p)'s
 * own grammar is LALR(1) and this recursive-descent parser mirrors it
 * one production at a time, so it inherits the same lookahead bound.
 */
enum awk_toktype {
	T_EOF, T_NEWLINE,
	T_NUMBER, T_STRING, T_ERE, T_NAME, T_FUNC_NAME, T_BUILTIN_NAME,
	/* keywords */
	T_BEGIN, T_END, T_FUNCTION, T_IF, T_ELSE, T_WHILE, T_FOR, T_DO,
	T_BREAK, T_CONTINUE, T_NEXT, T_EXIT, T_RETURN, T_DELETE, T_IN,
	T_GETLINE, T_PRINT, T_PRINTF,
	/* punctuation/operators */
	T_LBRACE, T_RBRACE, T_LPAREN, T_RPAREN, T_LBRACKET, T_RBRACKET,
	T_SEMI, T_COMMA,
	T_PLUS, T_MINUS, T_STAR, T_SLASH, T_PERCENT, T_CARET,
	T_ASSIGN, T_ADD_ASSIGN, T_SUB_ASSIGN, T_MUL_ASSIGN, T_DIV_ASSIGN,
	T_MOD_ASSIGN, T_POW_ASSIGN,
	T_EQ, T_NE, T_LT, T_LE, T_GT, T_GE,
	T_MATCH, T_NOMATCH,
	T_NOT, T_AND, T_OR,
	T_INCR, T_DECR,
	T_DOLLAR, T_QUESTION, T_COLON, T_PIPE, T_APPEND
};

struct awk_token {
	enum awk_toktype type;
	char *text;      /* owned: identifier/string/ERE literal text, else NULL */
	double num;      /* T_NUMBER */
	int adj_lparen;  /* T_NAME/T_FUNC_NAME: '(' immediately followed, no space --
	                  * XCU awk(1p)'s own rule for telling a function call
	                  * apart from concatenation-of-a-parenthesized-expr. */
};

/* The lexer emits a real T_NEWLINE token for every physical newline
 * (other than a backslash-newline pair, which is a line continuation
 * and produces no token at all) -- it does not itself decide which
 * newlines terminate a statement. XCU awk(1p)'s own grammar handles
 * that with an `opt_nls` production admitting zero or more newlines at
 * specific points (right after `{` `,` `&&` `||` `do` `else`, and right
 * after the `)` that closes an if/while/for header, before its body);
 * awk_parse.c's skip_newlines() calls at exactly those points are this
 * parser's implementation of the same `opt_nls`, so the suppression
 * logic lives once, in the parser, rather than being guessed at by the
 * lexer from token-kind heuristics. */
struct awk_lexer {
	const char *src;
	size_t pos, len;
	int err;
	char errmsg[256];
};

void awk_lex_init(struct awk_lexer *lx, const char *src) __attribute__((nonnull(1, 2)));
/* Fills *out with the next token (caller owns out->text if non-NULL:
 * free it, or hand it off, before the next call reuses nothing -- each
 * call allocates its own). Returns 0 on success, -1 on a lexical error
 * (message in lx->errmsg). */
int awk_lex_next(struct awk_lexer *lx, struct awk_token *out) __attribute__((nonnull(1, 2)));

/* ==== AST ================================================================ */

enum awk_ntype {
	N_NUM, N_STR, N_REGEX, N_VAR, N_FIELD, N_ARRIDX,
	N_ASSIGN, N_TERNARY, N_OR, N_AND, N_IN, N_MATCH, N_RELOP, N_CONCAT,
	N_BINOP, N_UMINUS, N_UPLUS, N_NOT,
	N_PREINCR, N_PREDECR, N_POSTINCR, N_POSTDECR,
	N_CALL, N_GETLINE, N_GROUP,
	N_PRINT, N_PRINTF, N_IF, N_WHILE, N_DOWHILE, N_FOR, N_FORIN,
	N_BREAK, N_CONTINUE, N_NEXT, N_EXIT, N_RETURN, N_DELETE, N_DELETE_ALL,
	N_BLOCK, N_EXPRSTMT
};

/* getline forms, XCU awk(1p) table (also src/util/awk.c's header):
 * plain, plain-with-var, <file, var<file, cmd|getline, cmd|getline var --
 * distinguished by (src_kind, target!=NULL). */
enum awk_getline_src { GL_MAIN, GL_FILE, GL_CMD };

/* print/printf redirection target, XCU: none, >file, >>file, |cmd. */
enum awk_redir { RD_NONE, RD_FILE, RD_APPEND, RD_PIPE };

struct awk_node {
	enum awk_ntype type;
	int op;                 /* operator/assign-op/relop kind, node-specific */
	double num;              /* N_NUM literal */
	char *str;                /* N_STR/N_VAR/N_CALL(name)/N_REGEX(source) literal text */
	regex_t *re;               /* N_REGEX: compiled once at parse time */
	struct awk_node *a, *b, *c, *d; /* generic children, meaning is per-type */
	struct awk_node **list;   /* generic child list (args, subscripts, stmts) */
	int nlist;
	enum awk_getline_src gl_src; /* N_GETLINE */
	enum awk_redir redir;     /* N_PRINT/N_PRINTF */
};

struct awk_func {
	char *name;
	char **params;
	int nparams;
	struct awk_node *body;
};

enum awk_rule_kind { RULE_BEGIN, RULE_END, RULE_ALWAYS, RULE_EXPR, RULE_REGEX, RULE_RANGE };

struct awk_rule {
	enum awk_rule_kind kind;
	struct awk_node *pat1, *pat2; /* pat2 only for RULE_RANGE */
	struct awk_node *action;      /* NULL means the default "{ print }" */
	int range_active;             /* RULE_RANGE runtime state */
};

struct awk_program {
	struct awk_rule *rules;
	int nrules;
	struct awk_func *funcs;
	int nfuncs;
};

struct awk_parser {
	struct awk_lexer lx;
	struct awk_token tok;   /* one token of lookahead */
	int err;
	char errmsg[256];
	struct awk_program *prog; /* being built; funcs/rules grown as parsed */
};

/* Parses the whole program text in src into a fresh struct awk_program.
 * Returns it on success; on a syntax error returns NULL and writes a
 * "awk: <message>" diagnostic to stderr itself (matching this project's
 * other multi-stage parsers, e.g. src/util/modeparse.c, which also
 * diagnose in place rather than pushing the message back through a
 * second channel). */
struct awk_program *awk_parse_program(const char *src) __attribute__((nonnull(1)));

/* ==== interpreter ========================================================= */

struct awk_stream {
	FILE *f;
	int is_pipe;
	int is_input;
};

struct awk_frame {
	char **names;             /* nparams formal parameter names, borrowed from the awk_func */
	struct awk_cell **cells;  /* nparams bound cells */
	unsigned char *is_alias;  /* nparams: cells[i] is shared with an outer scope's cell */
	int nparams;
};

struct awk_interp {
	struct awk_program *prog;
	struct awk_htab globals;   /* name -> struct awk_cell * */
	struct awk_frame *frame;   /* current call frame, NULL at top level */
	int depth;                 /* recursion depth guard */

	/* current record's fields; $0 is rec, $1.. are flds[0]..flds[nf-1] */
	char *rec;
	char **flds;
	int nf, fcap;

	struct awk_htab streams;   /* target string -> struct awk_stream * */
	struct awk_htab recmp;     /* dynamic ERE source -> regex_t * (compile cache) */

	FILE *curfile;             /* main input: currently open ARGV file, or stdin */
	int curfile_is_stdin;
	int argi;                  /* next ARGV index main-input advance should try */
	int any_input_used;        /* saw at least one real file operand consumed */

	unsigned long rand_state;
	double rand_prev_seed;

	int exit_status;
	int exiting;                /* exit statement reached: run END (once) then stop */
	int range_reentrancy_guard;

	const char *diag_prefix;    /* "awk" -- argv[0] is not necessarily that */
};

/* Statement execution's control-flow signal, threaded back up through
 * exec_stmt() instead of setjmp/longjmp -- see awk_run.c's header for
 * why: it is a small, finite set of "unwind to the nearest handler"
 * targets (loop body -> break/continue, whole rule -> next, whole
 * program -> exit, function call -> return), and every one of those
 * handlers already sits on the C call stack at exactly the point that
 * needs to catch it, so an ordinary return value does the whole job. */
enum awk_sig { SIG_NONE, SIG_BREAK, SIG_CONTINUE, SIG_NEXT, SIG_EXIT, SIG_RETURN };

int __util_awk_main(int argc, char **argv) __attribute__((nonnull(2)));

#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

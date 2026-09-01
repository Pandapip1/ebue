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
 * `kind` is the value's TRUE, immutable type tag, set once at
 * construction and never touched afterward -- deliberately kept
 * separate from num/str's own lazy cross-conversion caching
 * (numcached/strcached below) so that caching one representation from
 * the other (awk_run.c's v_num()/v_str()) can never itself change
 * comparison behavior. An earlier version of this design used a single
 * "flags" bitset doing both jobs (AWK_HAS_NUM/AWK_HAS_STR marking
 * which representation was valid) and that is exactly the bug it had:
 * v_str() caching a display string for a pure number would have set
 * the same bit real string values set, silently turning a numeric
 * comparison into a string one the moment anything printed the value
 * first. Splitting "what kind of value is this" from "what have we
 * computed and cached so far" removes that trap entirely.
 *
 * VK_UNINIT: the "uninitialized value" XCU awk(1p) defines as
 * simultaneously numeric 0 and string "" -- and which counts as
 * numeric for the comparison rule below.
 * VK_NUM: constructed from a numeric literal or an arithmetic result.
 * Always numeric for comparison.
 * VK_STR: constructed from a string literal, concatenation, or a
 * string-returning built-in/sub/gsub/etc. result -- i.e. every
 * *construction* the grammar has that is not one of the few origins
 * VK_STRNUM lists below. Never numeric for comparison, even if the
 * text happens to look like a number ("10.0" == 10 is a STRING
 * comparison and therefore false -- a well-known, deliberate awk
 * surprise, not a bug).
 * VK_STRNUM: XCU awk(1p)'s own "numeric string" -- a value that both
 * originates from field splitting, split()'s own result array,
 * getline's assigned variable, an ARGV/ENVIRON element, or a
 * command-line var=value operand, AND whose text passes
 * awk_run.c's looks_numeric() (an input string that does not look
 * like a number classifies as plain VK_STR instead, per that same
 * section). Numeric for comparison.
 *
 * A cell starts out is_array==0, kind==VK_UNINIT. It commits to being
 * an array the first time it is subscripted, iterated with
 * `for (k in x)`, handed to split()'s array argument, or named in
 * `delete x[...]` -- see awk_run.c's promote-to-array helper. */
enum awk_valkind { VK_UNINIT, VK_NUM, VK_STR, VK_STRNUM };

struct awk_cell {
	unsigned char is_array;
	unsigned char kind;       /* enum awk_valkind */
	unsigned char numcached, strcached; /* lazy cross-conversion validity */
	double num;
	char *str;                 /* owned, NUL-terminated; valid iff strcached */
	struct awk_htab *arr;       /* non-NULL iff is_array */
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
	enum awk_toktype prevtype; /* disambiguates '/' (division vs. an ERE
	                            * literal's opening delimiter): division
	                            * only right after a token that can end a
	                            * value (NUMBER/STRING/NAME/BUILTIN_NAME/
	                            * ')'/']'/++/--/ERE); an ERE literal
	                            * everywhere else, including at the very
	                            * start of the program -- the same rule
	                            * every real awk lexer applies. */
	int err;
	char errmsg[256];
};

void awk_lex_init(struct awk_lexer *lx, const char *src) __attribute__((nonnull(1, 2)));
/* Fills *out with the next token (caller owns out->text if non-NULL:
 * free it, or hand it off, before the next call reuses nothing -- each
 * call allocates its own). Returns 0 on success, -1 on a lexical error
 * (message in lx->errmsg). */
int awk_lex_next(struct awk_lexer *lx, struct awk_token *out) __attribute__((nonnull(1, 2)));
/* Is `s` one of XCU awk(1p)'s BUILTIN_FUNC_NAME identifiers (the ones
 * awk_parse.c dispatches to a fixed set of interpreter primitives
 * rather than a user function lookup)? Shared with awk_parse.c so a
 * program cannot `function length(...)` shadow one of these -- XCU
 * awk(1p) reserves the whole BUILTIN_FUNC_NAME lexical class, not just
 * the call syntax. */
int awk_is_builtin_name(const char *s) __attribute__((nonnull(1)));

/* ==== AST ================================================================ */

enum awk_ntype {
	N_NUM, N_STR, N_REGEX, N_VAR, N_FIELD, N_ARRIDX,
	N_ASSIGN, N_TERNARY, N_OR, N_AND, N_IN, N_MATCH, N_RELOP, N_CONCAT,
	N_BINOP, N_UMINUS, N_UPLUS, N_NOT,
	N_PREINCR, N_PREDECR, N_POSTINCR, N_POSTDECR,
	N_CALL, N_GETLINE,
	/* A parenthesized, comma-separated list, `(e1, e2, ...)` -- only
	 * meaningful in the two grammar positions XCU awk(1p) actually
	 * defines for it: immediately before `in` (a multi-dimensional
	 * array membership test, awk_parse.c's parse_in()) and as print/
	 * printf's whole argument list (awk_parse.c's parse_print_args()
	 * unwraps a lone one). A single-element `(e)` is never wrapped in
	 * this -- parse_primary() just returns e itself, since parenthesized
	 * grouping has no separate runtime meaning once precedence is
	 * already baked into the tree shape. */
	N_ELIST,
	N_PRINT, N_PRINTF, N_IF, N_WHILE, N_DOWHILE, N_FOR, N_FORIN,
	N_BREAK, N_CONTINUE, N_NEXT, N_EXIT, N_RETURN, N_DELETE,
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
	char *str;                /* N_STR/N_VAR/N_CALL(name)/N_REGEX(source)/N_ARRIDX,N_DELETE(array name)/N_FORIN(loop var) literal text */
	char *str2;                /* N_FORIN: array name (str is the loop variable) */
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
	struct awk_token tok;   /* current token */
	struct awk_token tok2;  /* one further token of lookahead, lazily filled */
	int has_tok2;
	int err;
	char errmsg[256];
	struct awk_program *prog; /* being built; funcs/rules grown as parsed */
	int suppress_gt;        /* inside a print/printf argument list, outside
	                         * any nested parentheses: a bare '>' is output
	                         * redirection, not the relational operator --
	                         * see awk_parse.c's parse_print_stmt(). */
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

/* Statement execution's control-flow signal, threaded back up through
 * exec_stmt() instead of setjmp/longjmp -- see awk_run.c's header for
 * why: it is a small, finite set of "unwind to the nearest handler"
 * targets (loop body -> break/continue, whole rule -> next, whole
 * program -> exit, function call -> return), and every one of those
 * handlers already sits on the C call stack at exactly the point that
 * needs to catch it, so an ordinary return value does the whole job. */
enum awk_sig { SIG_NONE, SIG_BREAK, SIG_CONTINUE, SIG_NEXT, SIG_EXIT, SIG_RETURN };

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
	enum awk_sig unwind;        /* set by next/exit so a signal can cross an
	                             * eval()->call_user_func() boundary, which
	                             * has no room in its own return type for
	                             * one -- see awk_run.c's header. */

	const char *diag_prefix;    /* "awk" -- argv[0] is not necessarily that */
};

/* ==== awk_run.c's entry points, used by awk.c's __util_awk_main() ======= */

void awk_interp_init(struct awk_interp *ip, struct awk_program *prog) __attribute__((nonnull(1, 2)));
/* Sets a global variable from program-startup text (a -v assignment or
 * a command-line var=value operand) -- ere_input selects VK_STRNUM vs.
 * VK_STR the same way a field or split() result would (XCU awk(1p)'s
 * own "numeric string" origin list includes these operands). */
void awk_interp_set_str(struct awk_interp *ip, const char *name, const char *val) __attribute__((nonnull(1, 2, 3)));
/* Builds the ARGV array (ARGV[0]==prog_name, ARGV[1..]==operands) and
 * ARGC. Every element is a numeric string subject to the same
 * classification, per XCU's own ARGV/ENVIRON callout. */
void awk_interp_setup_argv(struct awk_interp *ip, const char *prog_name, int nargs, char **args) __attribute__((nonnull(1, 2)));
void awk_interp_setup_environ(struct awk_interp *ip, char **envp) __attribute__((nonnull(1)));
/* Runs BEGIN, the main input loop (consuming ARGV, applying var=value
 * operands as they are reached -- see src/util/awk.c's header), and
 * END; returns the process exit status XCU awk(1p) defines. */
int awk_interp_run(struct awk_interp *ip) __attribute__((nonnull(1)));
void awk_interp_free(struct awk_interp *ip) __attribute__((nonnull(1)));

int __util_awk_main(int argc, char **argv) __attribute__((nonnull(2)));

#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

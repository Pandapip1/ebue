/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Stage 1: lexer + recursive-descent parser for the Shell Command
 * Language subset test/sh-design.md scopes in (XCU chapter 2). No
 * execution here -- this file only turns source text into the AST
 * declared in sh.h. See sh.h's header comment for the two deliberate
 * lexical simplifications this makes relative to strict POSIX:
 *
 *  - '|' '&' ';' '<' '>' '(' ')' '{' '}' are always lexed as operator
 *    tokens, never as ordinary word characters, matching
 *    src/wordexp/wordexp.c's existing WRDE_BADCHAR treatment of exactly
 *    this set. Only '!' is recognised as a reserved word by comparing
 *    a WORD token's text, and only in pipeline-start position.
 *  - Word text is kept raw (quotes and backslashes intact) rather than
 *    quote-removed here, so each sh_word.text is exactly the form
 *    wordexp()'s existing scanner already knows how to expand -- the
 *    expansion stage reuses that scanner per word instead of
 *    duplicating quote handling here.
 *
 * Grammar (informal, this project's subset of XCU 2.10.2):
 *   list       := [ NEWLINE... ] andor ( sep andor )* [ sep ]
 *   sep        := ';' | '&' | NEWLINE
 *   andor      := pipeline ( ('&&'|'||') NEWLINE... pipeline )*
 *   pipeline   := ['!'] command ( '|' NEWLINE... command )*
 *   command    := '(' list ')' redir*
 *              |  '{' list '}' redir*
 *              |  simple_command
 *   simple_command := ( assignment | redir )* WORD? ( WORD | redir )*
 *   redir      := [IONUMBER] ('<'|'>'|'>>'|'<&'|'>&'|'<>'|'>|'|'<<'|'<<-') WORD
 *
 * Here-documents (XCU 2.7.4): the body of a '<<'/'<<-' operand is not
 * available where the operator is parsed -- it is the text between the
 * next unescaped newline and a following line consisting solely of the
 * delimiter. The lexer therefore queues a "pending heredoc" per such
 * redirection as it is parsed and drains the queue (reading body lines
 * straight out of the source buffer) the moment it produces the
 * NEWLINE/EOF token that ends the current line, before that token is
 * returned to the parser -- the standard technique for hand-written
 * shell lexers.
 */
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include "libc.h"
#include "sh.h"

/* __sh_free_words()/__sh_free_redirs() live in free.c alongside
 * __sh_list_free() (which needs the same logic to free a whole parsed
 * list); declared in sh.h and reused here for cleanup on parse error. */

/* ---- growable buffers --------------------------------------------------- */
struct gbuf { char *d; size_t n, cap; };

static int gbuf_push(struct gbuf *b, char c)
{
	if (b->n == b->cap) {
		size_t nc = b->cap ? b->cap * 2 : 32;
		char *nd = __malloc(nc);
		if (!nd) return -1;
		if (b->d) memcpy(nd, b->d, b->n);
		__free(b->d);
		b->d = nd;
		b->cap = nc;
	}
	b->d[b->n++] = c;
	return 0;
}

static int gbuf_push_n(struct gbuf *b, const char *s, size_t n)
{
	size_t i;
	for (i = 0; i < n; i++) if (gbuf_push(b, s[i])) return -1;
	return 0;
}

static char *xstrdup(const char *s)
{
	size_t n = strlen(s) + 1;
	char *p = __malloc(n);
	if (p) memcpy(p, s, n);
	return p;
}

/* ---- word-boundary handling for $(...), ${...} and `...` -------------
 *
 * '(' '{' '`' are otherwise always break/operator characters (see
 * scan_word below), which is wrong the moment they follow an unquoted
 * '$' (command substitution / parameter expansion) or stand alone
 * (old-form command substitution): 2.6.3/2.6.2 make the whole
 * delimited region part of the *word*, not a place token scanning
 * stops. These helpers copy such a region verbatim (still raw,
 * unexpanded -- actually running a substituted command is stage 5's
 * job) into the word buffer, balancing nested delimiters of the same
 * kind and skipping over quoted text inside so an embedded quote
 * containing '(' '{' '`' etc. can't desync the count. Known, accepted
 * gap: a *double*-quoted region inside one of these (e.g. the classic
 * "$(echo "hi")") is not specially handled, since scan_word's Q_DOUBLE
 * state has no matching awareness of command substitution either --
 * out of scope for this bounded fix, same as the double-quote branch's
 * other limits. */
static int copy_squoted(const char **pp, struct gbuf *b)
{
	const char *p = *pp;
	if (gbuf_push(b, *p)) return -1;
	p++;
	while (*p && *p != '\'') { if (gbuf_push(b, *p)) return -1; p++; }
	if (!*p) return -1;
	if (gbuf_push(b, *p)) return -1;
	p++;
	*pp = p;
	return 0;
}

static int copy_dquoted(const char **pp, struct gbuf *b)
{
	const char *p = *pp;
	if (gbuf_push(b, *p)) return -1;
	p++;
	while (*p && *p != '"') {
		if (*p == '\\' && p[1]) { if (gbuf_push(b, *p) || gbuf_push(b, p[1])) return -1; p += 2; continue; }
		if (gbuf_push(b, *p)) return -1;
		p++;
	}
	if (!*p) return -1;
	if (gbuf_push(b, *p)) return -1;
	p++;
	*pp = p;
	return 0;
}

/* *pp points at `open` ('(' or '{'). Copies through the matching
 * `close`, tracking nesting depth and skipping quoted regions, into b
 * (both delimiters included). */
static int copy_balanced(const char **pp, struct gbuf *b, char open, char close)
{
	const char *p = *pp;
	int depth = 1;
	if (gbuf_push(b, *p)) return -1;
	p++;
	while (depth > 0) {
		char c = *p;
		if (!c) return -1;
		if (c == '\\' && p[1]) { if (gbuf_push(b, c) || gbuf_push(b, p[1])) return -1; p += 2; continue; }
		if (c == '\'') { if (copy_squoted(&p, b)) return -1; continue; }
		if (c == '"') { if (copy_dquoted(&p, b)) return -1; continue; }
		if (c == open) depth++;
		else if (c == close) depth--;
		if (gbuf_push(b, c)) return -1;
		p++;
	}
	*pp = p;
	return 0;
}

/* *pp points at the opening '`'. Copies through the matching closing
 * '`' (old-form command substitution does not nest), into b. */
static int copy_backquoted(const char **pp, struct gbuf *b)
{
	const char *p = *pp;
	if (gbuf_push(b, *p)) return -1;
	p++;
	while (*p && *p != '`') {
		if (*p == '\\' && p[1]) { if (gbuf_push(b, *p) || gbuf_push(b, p[1])) return -1; p += 2; continue; }
		if (gbuf_push(b, *p)) return -1;
		p++;
	}
	if (!*p) return -1;
	if (gbuf_push(b, *p)) return -1;
	p++;
	*pp = p;
	return 0;
}

/* ---- tokens --------------------------------------------------------------*/
enum tok_type {
	T_WORD, T_IONUM,
	T_PIPE, T_AND, T_OR, T_SEMI, T_AMP,
	T_LPAREN, T_RPAREN, T_LBRACE, T_RBRACE,
	T_LESS, T_GREAT, T_DGREAT, T_LESSAND, T_GREATAND,
	T_LESSGREAT, T_CLOBBER, T_DLESS, T_DLESSDASH,
	T_NEWLINE, T_EOF, T_ERROR
};

struct token {
	enum tok_type type;
	char *text;   /* T_WORD: raw word text, __malloc'd, owned by caller */
	int ionum;    /* T_IONUM: the parsed value */
};

static int is_redir_op(enum tok_type t)
{
	switch (t) {
	case T_LESS: case T_GREAT: case T_DGREAT: case T_LESSAND:
	case T_GREATAND: case T_LESSGREAT: case T_CLOBBER:
	case T_DLESS: case T_DLESSDASH:
		return 1;
	default:
		return 0;
	}
}

/* ---- lexer ---------------------------------------------------------------*/
struct pending_hd {
	struct sh_redir *redir;
	int dash;
	struct pending_hd *next;
};

struct lexer {
	const char *p;
	struct pending_hd *pending_head, *pending_tail;
	int err;
	char errbuf[256];
	size_t errbuflen;
};

static void lex_errf(struct lexer *lx, const char *fmt, ...)
{
	va_list ap;
	if (!lx->err && lx->errbuflen) {
		va_start(ap, fmt);
		vsnprintf(lx->errbuf, lx->errbuflen, fmt, ap);
		va_end(ap);
	}
	lx->err = 1;
}

/* Quote-removes `raw` (a WORD token's raw text) for use as a heredoc
 * delimiter: strips the ' and " characters and backslash-escapes,
 * exactly the processing XCU 2.7.4 requires before comparing candidate
 * terminator lines. *quoted is set if any quoting/escaping was present
 * at all, which per 2.7.4 disables expansions within the body. */
static char *strip_delim(const char *raw, int *quoted)
{
	struct gbuf b = {0, 0, 0};
	enum { Q_NONE, Q_SINGLE, Q_DOUBLE } q = Q_NONE;
	const char *p = raw;
	*quoted = 0;
	while (*p) {
		char c = *p;
		if (q == Q_NONE) {
			if (c == '\'') { q = Q_SINGLE; *quoted = 1; p++; continue; }
			if (c == '"') { q = Q_DOUBLE; *quoted = 1; p++; continue; }
			if (c == '\\' && p[1]) { *quoted = 1; if (gbuf_push(&b, p[1])) goto oom; p += 2; continue; }
			if (gbuf_push(&b, c)) goto oom;
			p++;
		} else if (q == Q_SINGLE) {
			if (c == '\'') { q = Q_NONE; p++; continue; }
			if (gbuf_push(&b, c)) goto oom;
			p++;
		} else {
			if (c == '"') { q = Q_NONE; p++; continue; }
			if (c == '\\' && p[1] && strchr("\"\\$`", p[1])) { if (gbuf_push(&b, p[1])) goto oom; p += 2; continue; }
			if (gbuf_push(&b, c)) goto oom;
			p++;
		}
	}
	if (gbuf_push(&b, 0)) goto oom;
	return b.d;
oom:
	__free(b.d);
	return 0;
}

static int drain_heredocs(struct lexer *lx)
{
	struct pending_hd *h = lx->pending_head;
	lx->pending_head = lx->pending_tail = 0;
	while (h) {
		struct pending_hd *next = h->next;
		char *lit = strip_delim(h->redir->word, &h->redir->heredoc_quoted);
		size_t litlen;
		struct gbuf body = {0, 0, 0};
		if (!lit) { lex_errf(lx, "out of memory"); __free(h); h = next; continue; }
		litlen = strlen(lit);
		for (;;) {
			const char *line = lx->p;
			const char *eol = line;
			const char *cmp;
			size_t linelen, cmplen;
			while (*eol && *eol != '\n') eol++;
			linelen = (size_t)(eol - line);
			cmp = line; cmplen = linelen;
			if (h->dash) while (cmplen && *cmp == '\t') { cmp++; cmplen--; }
			if (cmplen == litlen && memcmp(cmp, lit, litlen) == 0) {
				lx->p = (*eol == '\n') ? eol + 1 : eol;
				break;
			}
			if (!*eol) {
				lex_errf(lx, "unexpected EOF while looking for matching `%s'", lit);
				__free(lit);
				__free(body.d);
				__free(h);
				while (next) { struct pending_hd *n2 = next->next; __free(next); next = n2; }
				return -1;
			}
			if (gbuf_push_n(&body, h->dash ? cmp : line, h->dash ? cmplen : linelen) ||
			    gbuf_push(&body, '\n')) {
				lex_errf(lx, "out of memory");
				__free(lit); __free(body.d); __free(h);
				return -1;
			}
			lx->p = eol + 1;
		}
		if (gbuf_push(&body, 0)) { lex_errf(lx, "out of memory"); __free(lit); __free(body.d); __free(h); return -1; }
		h->redir->heredoc = body.d;
		__free(lit);
		__free(h);
		h = next;
	}
	return 0;
}

/* Scans one WORD starting at lx->p (which must not currently be blank,
 * newline, EOF, comment-start or an operator character). Advances
 * lx->p past it. Returns the raw text (quotes/backslashes intact),
 * NUL-terminated, or NULL on OOM/unterminated-quote (lx->err is set
 * either way it fails). */
static char *scan_word(struct lexer *lx)
{
	struct gbuf b = {0, 0, 0};
	enum { Q_NONE, Q_SINGLE, Q_DOUBLE } q = Q_NONE;
	const char *p = lx->p;
	for (;;) {
		char c = *p;
		if (q == Q_NONE) {
			if (c == 0 || c == '\n' || c == ' ' || c == '\t' || strchr("|&;()<>{}", c)) break;
			if (c == '\\' && p[1] == '\n') { p += 2; continue; }
			if (c == '\\') {
				if (!p[1]) { lex_errf(lx, "backslash at end of input"); goto fail; }
				if (gbuf_push(&b, '\\') || gbuf_push(&b, p[1])) goto oom;
				p += 2;
				continue;
			}
			if (c == '\'') { if (gbuf_push(&b, c)) goto oom; q = Q_SINGLE; p++; continue; }
			if (c == '"') { if (gbuf_push(&b, c)) goto oom; q = Q_DOUBLE; p++; continue; }
			if (c == '$' && (p[1] == '(' || p[1] == '{')) {
				char open = p[1], close = (open == '(') ? ')' : '}';
				if (gbuf_push(&b, '$')) goto oom;
				p++;
				if (copy_balanced(&p, &b, open, close)) {
					lex_errf(lx, open == '(' ? "unterminated $(...)" : "unterminated ${...}");
					goto fail;
				}
				continue;
			}
			if (c == '`') {
				if (copy_backquoted(&p, &b)) { lex_errf(lx, "unterminated `...`"); goto fail; }
				continue;
			}
			if (gbuf_push(&b, c)) goto oom;
			p++;
		} else if (q == Q_SINGLE) {
			if (c == 0) { lex_errf(lx, "unterminated single-quoted string"); goto fail; }
			if (gbuf_push(&b, c)) goto oom;
			p++;
			if (c == '\'') q = Q_NONE;
		} else {
			if (c == 0) { lex_errf(lx, "unterminated double-quoted string"); goto fail; }
			if (c == '\\' && p[1] && strchr("\"\\$`\n", p[1])) {
				if (gbuf_push(&b, '\\') || gbuf_push(&b, p[1])) goto oom;
				p += 2;
				continue;
			}
			if (gbuf_push(&b, c)) goto oom;
			p++;
			if (c == '"') q = Q_NONE;
		}
	}
	lx->p = p;
	if (gbuf_push(&b, 0)) goto oom;
	return b.d;
oom:
	lex_errf(lx, "out of memory");
fail:
	__free(b.d);
	return 0;
}

static struct token mktok(enum tok_type t) { struct token tok; tok.type = t; tok.text = 0; tok.ionum = 0; return tok; }

static struct token next_raw_token(struct lexer *lx)
{
	for (;;) {
		char c = lx->p[0];
		if (c == ' ' || c == '\t') { lx->p++; continue; }
		if (c == '\\' && lx->p[1] == '\n') { lx->p += 2; continue; }
		if (c == '#') { while (*lx->p && *lx->p != '\n') lx->p++; continue; }
		if (c == 0) {
			if (lx->pending_head && drain_heredocs(lx)) return mktok(T_ERROR);
			return mktok(T_EOF);
		}
		if (c == '\n') {
			lx->p++;
			if (lx->pending_head && drain_heredocs(lx)) return mktok(T_ERROR);
			return mktok(T_NEWLINE);
		}
#define OP2(a, b, tt) if (c == (a) && lx->p[1] == (b)) { lx->p += 2; return mktok(tt); }
#define OP3(a, b, cc, tt) if (c == (a) && lx->p[1] == (b) && lx->p[2] == (cc)) { lx->p += 3; return mktok(tt); }
		OP3('<', '<', '-', T_DLESSDASH)
		OP2('<', '<', T_DLESS)
		OP2('<', '&', T_LESSAND)
		OP2('<', '>', T_LESSGREAT)
		OP2('>', '>', T_DGREAT)
		OP2('>', '&', T_GREATAND)
		OP2('>', '|', T_CLOBBER)
		OP2('&', '&', T_AND)
		OP2('|', '|', T_OR)
#undef OP2
#undef OP3
		if (c == '<') { lx->p++; return mktok(T_LESS); }
		if (c == '>') { lx->p++; return mktok(T_GREAT); }
		if (c == '|') { lx->p++; return mktok(T_PIPE); }
		if (c == '&') { lx->p++; return mktok(T_AMP); }
		if (c == ';') { lx->p++; return mktok(T_SEMI); }
		if (c == '(') { lx->p++; return mktok(T_LPAREN); }
		if (c == ')') { lx->p++; return mktok(T_RPAREN); }
		if (c == '{') { lx->p++; return mktok(T_LBRACE); }
		if (c == '}') { lx->p++; return mktok(T_RBRACE); }
		{
			char *w = scan_word(lx);
			struct token tok;
			size_t i, len;
			int alldig;
			if (!w) return mktok(T_ERROR);
			len = strlen(w);
			alldig = len > 0;
			for (i = 0; i < len; i++) if (!isdigit((unsigned char)w[i])) { alldig = 0; break; }
			if (alldig && (*lx->p == '<' || *lx->p == '>')) {
				int v = 0;
				for (i = 0; i < len; i++) v = v * 10 + (w[i] - '0');
				__free(w);
				tok = mktok(T_IONUM);
				tok.ionum = v;
				return tok;
			}
			tok = mktok(T_WORD);
			tok.text = w;
			return tok;
		}
	}
}

/* ---- parser ----------------------------------------------------------- */
struct parser {
	struct lexer lx;
	struct token cur;
	int had_error;
};

static void advance(struct parser *p)
{
	if (p->cur.type == T_WORD) { __free(p->cur.text); p->cur.text = 0; }
	if (p->had_error) return;
	p->cur = next_raw_token(&p->lx);
	if (p->cur.type == T_ERROR) p->had_error = 1;
}

static void perr(struct parser *p, const char *fmt, ...)
{
	va_list ap;
	if (!p->had_error) {
		va_start(ap, fmt);
		vsnprintf(p->lx.errbuf, p->lx.errbuflen, fmt, ap);
		va_end(ap);
	}
	p->had_error = 1;
}

static void skip_newlines(struct parser *p)
{
	while (!p->had_error && p->cur.type == T_NEWLINE) advance(p);
}

static int is_assignment_word(const char *s)
{
	const char *p = s;
	if (!(isalpha((unsigned char)*p) || *p == '_')) return 0;
	while (isalnum((unsigned char)*p) || *p == '_') p++;
	return p != s && *p == '=';
}

static struct sh_redir *parse_redir(struct parser *p)
{
	struct sh_redir *r;
	enum sh_redir_op op;
	int fd = -1;

	if (p->cur.type == T_IONUM) { fd = p->cur.ionum; advance(p); }
	switch (p->cur.type) {
	case T_LESS: op = SH_R_LESS; break;
	case T_GREAT: op = SH_R_GREAT; break;
	case T_DGREAT: op = SH_R_DGREAT; break;
	case T_LESSAND: op = SH_R_LESSAND; break;
	case T_GREATAND: op = SH_R_GREATAND; break;
	case T_LESSGREAT: op = SH_R_LESSGREAT; break;
	case T_CLOBBER: op = SH_R_CLOBBER; break;
	case T_DLESS: op = SH_R_DLESS; break;
	case T_DLESSDASH: op = SH_R_DLESSDASH; break;
	default: perr(p, "expected a redirection operator"); return 0;
	}
	advance(p);
	if (p->had_error) return 0;
	if (p->cur.type != T_WORD) { perr(p, "expected a word after redirection operator"); return 0; }

	r = __malloc(sizeof *r);
	if (!r) { perr(p, "out of memory"); return 0; }
	r->op = op;
	r->fd = fd;
	r->word = xstrdup(p->cur.text);
	r->heredoc = 0;
	r->heredoc_quoted = 0;
	r->next = 0;
	if (!r->word) { __free(r); perr(p, "out of memory"); return 0; }

	/* Registered *before* the advance() below: that call is what
	 * fetches the token after the delimiter word, and if that token
	 * is the newline ending this line, it is what triggers
	 * drain_heredocs() -- the queue has to already hold this entry
	 * when that happens, not be populated after the fact. */
	if (op == SH_R_DLESS || op == SH_R_DLESSDASH) {
		struct pending_hd *h = __malloc(sizeof *h);
		if (!h) { perr(p, "out of memory"); __free(r->word); __free(r); return 0; }
		h->redir = r;
		h->dash = (op == SH_R_DLESSDASH);
		h->next = 0;
		if (p->lx.pending_tail) p->lx.pending_tail->next = h; else p->lx.pending_head = h;
		p->lx.pending_tail = h;
	}
	advance(p);
	return r;
}

static struct sh_list *parse_list(struct parser *p, int stop_rparen, int stop_rbrace);

static int at_group_stop(struct parser *p, int stop_rparen, int stop_rbrace)
{
	if (p->cur.type == T_EOF) return 1;
	if (stop_rparen && p->cur.type == T_RPAREN) return 1;
	if (stop_rbrace && p->cur.type == T_RBRACE) return 1;
	return 0;
}

static struct sh_command *parse_command(struct parser *p)
{
	struct sh_command *cmd = __malloc(sizeof *cmd);
	struct sh_redir *rtail = 0;
	if (!cmd) { perr(p, "out of memory"); return 0; }
	cmd->assigns = 0; cmd->words = 0; cmd->body = 0; cmd->redirs = 0;

	if (p->cur.type == T_LPAREN) {
		advance(p);
		cmd->kind = SH_CMD_SUBSHELL;
		cmd->body = parse_list(p, 1, 0);
		if (p->had_error) { __sh_list_free(cmd->body); __free(cmd); return 0; }
		if (p->cur.type != T_RPAREN) { perr(p, "expected ')'"); __sh_list_free(cmd->body); __free(cmd); return 0; }
		advance(p);
	} else if (p->cur.type == T_LBRACE) {
		advance(p);
		cmd->kind = SH_CMD_BRACE;
		cmd->body = parse_list(p, 0, 1);
		if (p->had_error) { __sh_list_free(cmd->body); __free(cmd); return 0; }
		if (p->cur.type != T_RBRACE) { perr(p, "expected '}'"); __sh_list_free(cmd->body); __free(cmd); return 0; }
		advance(p);
	} else {
		struct sh_word *atail = 0, *wtail = 0;
		int seen_word = 0;
		cmd->kind = SH_CMD_SIMPLE;
		for (;;) {
			if (p->cur.type == T_IONUM || is_redir_op(p->cur.type)) {
				struct sh_redir *r = parse_redir(p);
				if (!r) goto simple_fail;
				if (rtail) rtail->next = r; else cmd->redirs = r;
				rtail = r;
				continue;
			}
			if (p->cur.type == T_WORD) {
				struct sh_word *w;
				if (!seen_word && is_assignment_word(p->cur.text)) {
					w = __malloc(sizeof *w);
					if (!w) { perr(p, "out of memory"); goto simple_fail; }
					w->text = xstrdup(p->cur.text);
					w->next = 0;
					if (atail) atail->next = w; else cmd->assigns = w;
					atail = w;
					advance(p);
					continue;
				}
				seen_word = 1;
				w = __malloc(sizeof *w);
				if (!w) { perr(p, "out of memory"); goto simple_fail; }
				w->text = xstrdup(p->cur.text);
				w->next = 0;
				if (wtail) wtail->next = w; else cmd->words = w;
				wtail = w;
				advance(p);
				continue;
			}
			break;
		}
		if (!seen_word && !cmd->assigns && !cmd->redirs) {
			perr(p, "expected a command");
			goto simple_fail;
		}
		return cmd;
simple_fail:
		__sh_free_words(cmd->assigns);
		__sh_free_words(cmd->words);
		__sh_free_redirs(cmd->redirs);
		__free(cmd);
		return 0;
	}

	/* redirections may also trail a subshell/brace group */
	while (p->cur.type == T_IONUM || is_redir_op(p->cur.type)) {
		struct sh_redir *r = parse_redir(p);
		if (!r) { __sh_list_free(cmd->body); __free(cmd); return 0; }
		if (rtail) rtail->next = r; else cmd->redirs = r;
		rtail = r;
	}
	return cmd;
}

static int parse_pipeline(struct parser *p, struct sh_pipeline *out)
{
	struct sh_command *arr = 0;
	size_t n = 0, cap = 0;
	out->bang = 0;
	if (p->cur.type == T_WORD && strcmp(p->cur.text, "!") == 0) { out->bang = 1; advance(p); }
	for (;;) {
		struct sh_command *cmd = parse_command(p);
		if (!cmd) goto fail;
		if (n == cap) {
			size_t nc = cap ? cap * 2 : 4;
			struct sh_command *na = __malloc(nc * sizeof *na);
			if (!na) {
				perr(p, "out of memory");
				__sh_free_command_contents(cmd);
				__free(cmd);
				goto fail;
			}
			if (arr) memcpy(na, arr, n * sizeof *na);
			__free(arr);
			arr = na; cap = nc;
		}
		arr[n++] = *cmd;
		__free(cmd);
		if (p->cur.type != T_PIPE) break;
		advance(p);
		skip_newlines(p);
	}
	out->commands = arr;
	out->ncommands = n;
	return 0;
fail:
	{
		size_t i;
		for (i = 0; i < n; i++) __sh_free_command_contents(&arr[i]);
	}
	__free(arr);
	return -1;
}

static struct sh_andor *parse_andor(struct parser *p)
{
	struct sh_andor *head, *tail;
	head = __malloc(sizeof *head);
	if (!head) { perr(p, "out of memory"); return 0; }
	head->op = SH_AO_NONE;
	head->next = 0;
	if (parse_pipeline(p, &head->pipeline)) { __free(head); return 0; }
	tail = head;
	for (;;) {
		enum sh_andor_op op;
		struct sh_andor *node;
		if (p->cur.type == T_AND) op = SH_AO_AND;
		else if (p->cur.type == T_OR) op = SH_AO_OR;
		else break;
		advance(p);
		skip_newlines(p);
		node = __malloc(sizeof *node);
		if (!node) { perr(p, "out of memory"); return head; }
		node->op = op;
		node->next = 0;
		if (parse_pipeline(p, &node->pipeline)) { __free(node); return head; }
		tail->next = node;
		tail = node;
	}
	return head;
}

static struct sh_list *parse_list(struct parser *p, int stop_rparen, int stop_rbrace)
{
	struct sh_list *list = __malloc(sizeof *list);
	struct sh_list_item *tail = 0;
	if (!list) { perr(p, "out of memory"); return 0; }
	list->items = 0;
	skip_newlines(p);
	while (!p->had_error && !at_group_stop(p, stop_rparen, stop_rbrace)) {
		struct sh_list_item *item = __malloc(sizeof *item);
		if (!item) { perr(p, "out of memory"); return list; }
		item->andor = parse_andor(p);
		item->next = 0;
		if (!item->andor) { __free(item); return list; }
		if (p->cur.type == T_SEMI) { item->sep = SH_SEP_SEQ; advance(p); }
		else if (p->cur.type == T_AMP) { item->sep = SH_SEP_AMP; advance(p); }
		else if (p->cur.type == T_NEWLINE) { item->sep = SH_SEP_SEQ; advance(p); }
		else item->sep = SH_SEP_END;
		if (tail) tail->next = item; else list->items = item;
		tail = item;
		if (item->sep == SH_SEP_END) break;
		skip_newlines(p);
	}
	return list;
}

struct sh_list *__sh_parse(const char *src, char *errbuf, size_t errbuflen)
{
	struct parser p;
	struct sh_list *list;

	p.lx.p = src;
	p.lx.pending_head = p.lx.pending_tail = 0;
	p.lx.err = 0;
	p.lx.errbuf[0] = 0;
	p.lx.errbuflen = sizeof p.lx.errbuf;
	p.had_error = 0;
	p.cur = mktok(T_EOF);
	advance(&p);

	list = parse_list(&p, 0, 0);

	if (!p.had_error && p.cur.type != T_EOF) {
		perr(&p, "unexpected token near '%s'", p.cur.type == T_WORD ? p.cur.text : "?");
	}
	if (p.cur.type == T_WORD) __free(p.cur.text);

	if (p.had_error) {
		if (errbuf && errbuflen) {
			size_t n = strlen(p.lx.errbuf);
			if (n >= errbuflen) n = errbuflen - 1;
			memcpy(errbuf, p.lx.errbuf, n);
			errbuf[n] = 0;
		}
		__sh_list_free(list);
		return 0;
	}
	return list;
}

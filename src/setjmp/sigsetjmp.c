/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <setjmp.h>
#include <signal.h>

/* Both dereference env unconditionally, first statement
 * (`env[0].__fl`); called only from the architecture entry points'
 * assembly, always the real jmp_buf being captured/restored, never a
 * value that could legitimately be null. */
void __sigsetjmp_save(sigjmp_buf, int) __attribute__((nonnull(1))); // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) -- libc-internal name is intentionally reserved against application collision
void __siglongjmp_restore(sigjmp_buf) __attribute__((nonnull(1))); // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) -- libc-internal name is intentionally reserved against application collision

/* Called by the architecture entry points before they capture or restore
 * registers.  Keeping sigprocmask() out of the assembly also keeps jmp_buf's
 * public mask storage layout in one C-visible place. */
void __sigsetjmp_save(sigjmp_buf env, int savemask)
{
	env[0].__fl = savemask != 0;
	if (savemask)
		sigprocmask(SIG_BLOCK, 0, (sigset_t *)env[0].__ss);
}

void __siglongjmp_restore(sigjmp_buf env)
{
	if (env[0].__fl)
		sigprocmask(SIG_SETMASK, (const sigset_t *)env[0].__ss, 0);
}

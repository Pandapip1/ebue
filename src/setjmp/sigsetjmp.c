/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <setjmp.h>
#include <signal.h>

void __sigsetjmp_save(sigjmp_buf, int);
void __siglongjmp_restore(sigjmp_buf);

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

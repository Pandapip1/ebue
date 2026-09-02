/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Real hardware-fault delivery on Linux: closes the gap
 * src/signal/linux/sigdelivery.c's own banner (and src/signal/signal.c's
 * __signal_init()) used to disclose outright -- a real SIGSEGV/SIGBUS/
 * SIGILL/SIGFPE never reached this library's dispatch at all, only the
 * kernel's own default action, bypassing __raise_internal_info()/user
 * handlers/altstack entirely. src/signal/linux/plat_signal.c's
 * __plat_sig_install_fault_handlers() is what now installs a real
 * rt_sigaction(2) handler (arch/aarch64/src/sigreturn_trampoline.S is
 * its restorer) for SIGSEGV/SIGBUS/SIGILL/SIGFPE/SIGTRAP; this file is
 * the real-hardware-fault coverage for it, genuinely crashing a spawned
 * CHILD process (never this test process itself) three different ways
 * and inspecting how it died.
 *
 * Follows test/posix-signal-crossproc.c's own __spawn()/waitpid() black-
 * box idiom (that file's own header comment explains why: fork() needs
 * RtlCloneUserProcess, unavailable under stock Wine -- moot for this
 * file specifically, since it only means anything on real Linux at all
 * (see the Makefile's own PLATFORM=linux gating for this "-linux"-
 * suffixed file, matching test/posix-realtime-linux.c's and test/posix-
 * dl-linux.c's identical convention), but __spawn() is what every other
 * file in this directory already uses for "a genuinely separate crashed
 * process", so there is no reason for this one to invent a second way).
 *
 * Two shapes per fault, both required by this change's own plan:
 *
 *   - DEFAULT disposition (no handler installed): the child must
 *     actually die from the real signal, and the PARENT's waitpid()
 *     must report WIFSIGNALED/WTERMSIG with the REAL, correct signal
 *     number -- proving the fault reached the kernel's own real
 *     disposition machinery, not a value this library invented.
 *   - CAUGHT disposition (sigaction() installed before the fault): the
 *     handler must actually run, see the CORRECT si_code for the fault
 *     (confirming __raise_internal_info() really was reached from a
 *     genuine async fault, with the kernel's own real siginfo_t, not a
 *     self-raised signal), and the child chooses to survive by exiting
 *     cleanly from inside the handler -- handler_*() below encode
 *     "ran, and si_code was right" as exit(42) and any other outcome
 *     (wrong signal, wrong si_code, or falling through to the line
 *     after the fault because nothing ever ran) as a different code, so
 *     the parent can tell all of "didn't run", "ran but wrong", and
 *     "isn't the fault that should have happened at all, the child
 *     lived to return normally" apart from the one true positive.
 *
 * SIGFPE deliberately does NOT use integer division by zero, unlike the
 * common x86 idiom (and unlike this project's own plan document, which
 * named it as the expected mechanism before this file was written and
 * this was checked against the real hardware): AArch64's SDIV/UDIV
 * instructions do not trap on division by zero at all -- the ISA defines
 * the result as a silent 0, full stop, confirmed empirically on this
 * exact host (`volatile int c = 1/0;` under real clang -O0 here prints
 * "no trap, c=0" and returns normally, no signal of any kind). What DOES
 * produce a real, kernel-delivered SIGFPE here -- also confirmed
 * empirically on this host before being relied on below -- is enabling
 * the AArch64 FPCR's DZE (Divide-by-Zero trap Enable, bit 9) trap-enable
 * bit via `msr fpcr` and then performing a real floating-point divide by
 * zero: this host's FPU actually implements trapped FP exceptions
 * (architecturally optional on AArch64; some cores wire the FPCR xxE
 * bits RES0 and never trap at all -- confirmed NOT the case here), and
 * the resulting real SIGFPE carries si_code FPE_FLTDIV, matching
 * signal.h's own FPE_FLTDIV value. This is exactly the "or an
 * feenableexcept-style FP trap" alternative this change's own plan
 * document names for precisely this reason.
 */
#define _GNU_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/time.h>

extern char **environ;
extern int __spawn(const char *, char *const *, char *const *);

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* ------------------------------------------------------------------ *
 * Fault generators and their handlers.  Each handler encodes what it
 * saw as an exit code rather than writing anywhere: _exit() from
 * signal-handler context is async-signal-safe, and a pipe or file
 * would not be. 42 always means "ran, and si_code/si_addr matched the
 * real fault"; 77 means "ran, but something did not match"; a fault
 * generator's own trailing `return 9x` means "the fault did not
 * actually happen" (the real bug this whole file exists to catch).
 * ------------------------------------------------------------------ */

static void handler_segv(int sig, siginfo_t *si, void *uc)
{
	(void)uc;
	if (sig == SIGSEGV && si->si_code == SEGV_MAPERR && si->si_addr == 0)
		_exit(42);
	_exit(77);
}

static int child_segv_default(void)
{
	volatile int *p = 0;
	*p = 1;
	return 90;   /* unreachable if the fault is real */
}

static int child_segv_handler(void)
{
	struct sigaction sa;
	volatile int *p;

	memset(&sa, 0, sizeof sa);
	sa.sa_sigaction = handler_segv;
	sa.sa_flags = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGSEGV, &sa, NULL) != 0) return 90;
	p = 0;
	*p = 1;
	return 91;   /* handler_segv() should already have _exit()'d */
}

/* .word 0xffffffff: AArch64 has no single canonical "undefined
 * instruction" mnemonic the way x86 has ud2, but 0xffffffff is not a
 * valid encoding of any real AArch64 instruction (every real encoding
 * reserves bit patterns this one does not match), confirmed empirically
 * on this host to raise a real SIGILL with si_code ILL_ILLOPC -- not
 * merely assumed from the ISA reference. */
static void trap_illegal_instruction(void)
{
	__asm__ volatile(".word 0xffffffff");
}

static void handler_ill(int sig, siginfo_t *si, void *uc)
{
	(void)uc;
	if (sig == SIGILL && si->si_code == ILL_ILLOPC)
		_exit(42);
	_exit(77);
}

static int child_ill_default(void)
{
	trap_illegal_instruction();
	return 90;
}

static int child_ill_handler(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof sa);
	sa.sa_sigaction = handler_ill;
	sa.sa_flags = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGILL, &sa, NULL) != 0) return 90;
	trap_illegal_instruction();
	return 91;
}

/* See this file's own header comment for why this, not integer division,
 * is what actually traps on AArch64. */
static void enable_fpe_divide_by_zero_trap(void)
{
	unsigned long fpcr;
	__asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
	fpcr |= 1UL << 9;   /* DZE */
	__asm__ volatile("msr fpcr, %0" :: "r"(fpcr));
}

static void trigger_float_divide_by_zero(void)
{
	volatile double a = 1.0, b = 0.0;
	volatile double c = a / b;
	(void)c;
}

static void handler_fpe(int sig, siginfo_t *si, void *uc)
{
	(void)uc;
	if (sig == SIGFPE && si->si_code == FPE_FLTDIV)
		_exit(42);
	_exit(77);
}

static int child_fpe_default(void)
{
	enable_fpe_divide_by_zero_trap();
	trigger_float_divide_by_zero();
	return 90;
}

static int child_fpe_handler(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof sa);
	sa.sa_sigaction = handler_fpe;
	sa.sa_flags = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGFPE, &sa, NULL) != 0) return 90;
	enable_fpe_divide_by_zero_trap();
	trigger_float_divide_by_zero();
	return 91;
}

/* ------------------------------------------------------------------ *
 * Parent side.
 * ------------------------------------------------------------------ */

static int spawn_child(const char *self, const char *mode, pid_t *pid)
{
	char *argv[3];
	argv[0] = (char *)self; argv[1] = (char *)mode; argv[2] = NULL;
	*pid = __spawn(self, argv, environ);
	return *pid > 0 ? 0 : -1;
}

static void describe(const char *what, int status)
{
	if (WIFEXITED(status))
		printf("    %s: exited %d\n", what, WEXITSTATUS(status));
	else if (WIFSIGNALED(status))
		printf("    %s: killed by signal %d\n", what, WTERMSIG(status));
	else
		printf("    %s: raw status 0x%x\n", what, (unsigned)status);
}

static void test_fault_default(const char *self, const char *mode,
				int expect_sig, const char *desc)
{
	pid_t pid;
	int status;

	if (spawn_child(self, mode, &pid) < 0) { CHECK(0 && "spawn failed"); return; }
	CHECK(waitpid(pid, &status, 0) == pid);
	describe(desc, status);
	CHECK(WIFSIGNALED(status) && WTERMSIG(status) == expect_sig);
}

static void test_fault_handler(const char *self, const char *mode, const char *desc)
{
	pid_t pid;
	int status;

	if (spawn_child(self, mode, &pid) < 0) { CHECK(0 && "spawn failed"); return; }
	CHECK(waitpid(pid, &status, 0) == pid);
	describe(desc, status);
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 42);
}

int main(int argc, char **argv)
{
	if (argc > 1) {
		if (!strcmp(argv[1], "--child-segv-default")) return child_segv_default();
		if (!strcmp(argv[1], "--child-segv-handler")) return child_segv_handler();
		if (!strcmp(argv[1], "--child-ill-default")) return child_ill_default();
		if (!strcmp(argv[1], "--child-ill-handler")) return child_ill_handler();
		if (!strcmp(argv[1], "--child-fpe-default")) return child_fpe_default();
		if (!strcmp(argv[1], "--child-fpe-handler")) return child_fpe_handler();
		return 89;
	}

	test_fault_default(argv[0], "--child-segv-default", SIGSEGV, "SIGSEGV default (NULL deref)");
	test_fault_handler(argv[0], "--child-segv-handler", "SIGSEGV caught (NULL deref)");
	test_fault_default(argv[0], "--child-ill-default", SIGILL, "SIGILL default (bad opcode)");
	test_fault_handler(argv[0], "--child-ill-handler", "SIGILL caught (bad opcode)");
	test_fault_default(argv[0], "--child-fpe-default", SIGFPE, "SIGFPE default (FP div/0)");
	test_fault_handler(argv[0], "--child-fpe-handler", "SIGFPE caught (FP div/0)");

	if (fails) printf("posix-signal-fault-linux: %d failure(s)\n", fails);
	else printf("posix-signal-fault-linux: ok\n");
	return fails != 0;
}

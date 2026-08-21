/* There is one user as far as this library is concerned. */
#include <unistd.h>
#include <errno.h>

uid_t getuid(void) { return 1000; }
uid_t geteuid(void) { return 1000; }
gid_t getgid(void) { return 1000; }
gid_t getegid(void) { return 1000; }
int setuid(uid_t u) { (void)u; return 0; }
int seteuid(uid_t u) { (void)u; return 0; }
int setgid(gid_t g) { (void)g; return 0; }
int setegid(gid_t g) { (void)g; return 0; }
int setreuid(uid_t r, uid_t e) { (void)r; (void)e; return 0; }
int setregid(gid_t r, gid_t e) { (void)r; (void)e; return 0; }
int getgroups(int n, gid_t *g) { if (n > 0) g[0] = 1000; return 1; }
int setgroups(size_t n, const gid_t *g) { (void)n; (void)g; return 0; }
pid_t getpgrp(void) { return 1; }
pid_t getpgid(pid_t p) { (void)p; return 1; }
int setpgid(pid_t a, pid_t b) { (void)a; (void)b; return 0; }
pid_t setpgrp(void) { return 1; }
pid_t setsid(void) { return 1; }
pid_t getsid(pid_t p) { (void)p; return 1; }
int chown(const char *p, uid_t u, gid_t g) { (void)p; (void)u; (void)g; return 0; }
int fchown(int f, uid_t u, gid_t g) { (void)f; (void)u; (void)g; return 0; }
int lchown(const char *p, uid_t u, gid_t g) { (void)p; (void)u; (void)g; return 0; }
int fchownat(int d, const char *p, uid_t u, gid_t g, int f) { (void)d; (void)p; (void)u; (void)g; (void)f; return 0; }
int nice(int n) { (void)n; return 0; }
int chroot(const char *p) { (void)p; errno = EPERM; return -1; }
int issetugid(void) { return 0; }
char *getlogin(void)
{
	extern char *getenv(const char *);
	char *u = getenv("USERNAME");
	return u ? u : getenv("USER");
}
int getlogin_r(char *buf, size_t n)
{
	char *l = getlogin();
	size_t i;
	if (!l) return ENXIO;
	for (i = 0; l[i] && i + 1 < n; i++) buf[i] = l[i];
	if (l[i]) return ERANGE;
	buf[i] = 0;
	return 0;
}

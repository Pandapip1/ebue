/* SPDX-FileCopyrightText: (C) 2026 Gavin John */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Every function below is genuinely pure but NOT yet annotated -- each
 * should be reported as a pure candidate, matching the sched.c precedent
 * (policy_valid()/priority_min() were exactly this shape before being
 * marked). */

int policy_valid(int policy) { /* purity-expect */
	return policy == 0 || policy == 1;
}

static int priority_min(int policy) { /* purity-expect */
	return policy == 0 ? 0 : 1;
}

struct pair { int value; int bad; };

/* Calls two other not-yet-marked candidates in this same file -- proves
 * the checker's own transitive, whole-call-graph purity derivation, not
 * just trust of an existing attribute. */
struct pair priority_for(int policy) { /* purity-expect */
	struct pair r = { 0, 0 };
	if (!policy_valid(policy)) { r.bad = 1; return r; }
	r.value = priority_min(policy);
	return r;
}

/* Direct self-recursion, not yet marked -- the checker must not treat the
 * recursive edge itself as a disqualifier (see safe.c's count_down() for
 * the already-marked mirror of this same shape). */
int factorial(int n) { /* purity-expect */
	return n <= 1 ? 1 : n * factorial(n - 1);
}

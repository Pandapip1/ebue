/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */


tokdef broken_allocated
	dynamic_storage
	implemented_by(heap_allocated);
tokdef inherited_allocated
	dynamic_storage
	implemented_by(heap_allocated);
tokdef unknown_implementation_allocated
	dynamic_storage
	implemented_by(no_such_allocation_family);
tokdef malformed_implementation_allocated
	dynamic_storage
	__attribute__((annotate("qual:implemented_by=not-a-family")));
tokdef conflicting_implementation_allocated
	dynamic_storage
	implemented_by(heap_allocated)
	implemented_by(inherited_allocated);

void inherited_destroy(void *consume(inherited_allocated));
withtok(inherited_allocated)
void *make_inherited(void);

void broken_destroy(void *consume(broken_allocated));
withtok(broken_allocated)
void *make_broken(void);

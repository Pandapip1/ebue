/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Source-level test disposition marker.  The normal compiler sees every
 * fenced case as disabled.  tools/test-policy.py enables one named case at
 * a time in a generated translation unit, so its disposition can be
 * validated without another known failure masking it.
 */
#ifndef NTLIBC_TEST_POLICY_H
#define NTLIBC_TEST_POLICY_H

#define NTLIBC_TEST(disposition, case_name) 0

#endif

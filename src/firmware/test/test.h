// Minimal test framework.
//
// Each test runs in its own forked process. The firmware keeps state in
// file-level and function-level statics that nothing resets, so a fresh
// process per test is the only way to keep tests independent. A crash
// fails just that test.

#ifndef _TEST_H_
#define _TEST_H_

#include <stdio.h>

typedef int (*test_fn)(void);

void test_run(const char* name, test_fn fn);

#define RUN_TEST(fn) test_run(#fn, fn)

#define ASSERT_TRUE(cond) \
	do { \
		if (!(cond)) { \
			printf("  %s:%d: expected %s\n", __FILE__, __LINE__, #cond); \
			return 0; \
		} \
	} while (0)

#define ASSERT_EQ(expected, actual) \
	do { \
		long e_ = (long)(expected); \
		long a_ = (long)(actual); \
		if (e_ != a_) { \
			printf("  %s:%d: %s == %s: expected %ld, got %ld\n", \
				__FILE__, __LINE__, #expected, #actual, e_, a_); \
			return 0; \
		} \
	} while (0)

#endif

#ifndef _TEST_H_
#define _TEST_H_

#include <stdio.h>

extern int g_tests_run;
extern int g_tests_failed;

#define RUN_TEST(fn) \
	do { \
		g_tests_run++; \
		if (!(fn())) { \
			g_tests_failed++; \
			printf("FAIL %s\n", #fn); \
		} else { \
			printf("PASS %s\n", #fn); \
		} \
	} while (0)

#define ASSERT_TRUE(cond) \
	do { \
		if (!(cond)) { \
			printf("  assertion failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
			return 0; \
		} \
	} while (0)

#define ASSERT_EQ(expected, actual) \
	do { \
		long e_ = (long)(expected); \
		long a_ = (long)(actual); \
		if (e_ != a_) { \
			printf("  assertion failed: %s == %s, expected %ld but got %ld (%s:%d)\n", \
				#expected, #actual, e_, a_, __FILE__, __LINE__); \
			return 0; \
		} \
	} while (0)

#endif

#include "test.h"

int g_tests_run = 0;
int g_tests_failed = 0;

void test_util_run(void);
void test_throttle_run(void);
void test_battery_run(void);
void test_cfgstore_run(void);

int main(void)
{
	test_util_run();
	test_throttle_run();
	test_battery_run();
	test_cfgstore_run();

	printf("\n%d run, %d failed\n", g_tests_run, g_tests_failed);

	return g_tests_failed != 0;
}

#include "test.h"

#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

static int tests_run = 0;
static int tests_failed = 0;

void test_run(const char* name, test_fn fn)
{
	fflush(stdout);
	tests_run++;

	pid_t pid = fork();
	if (pid == 0)
	{
		int ok = fn();
		fflush(stdout);
		_exit(ok ? 0 : 1);
	}

	int status = 0;
	waitpid(pid, &status, 0);

	if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
	{
		printf("PASS %s\n", name);
	}
	else
	{
		tests_failed++;
		if (WIFSIGNALED(status))
		{
			printf("FAIL %s (crashed with signal %d)\n", name, WTERMSIG(status));
		}
		else
		{
			printf("FAIL %s\n", name);
		}
	}
}

void test_throttle_run(void);
void test_battery_run(void);
void test_cfgstore_run(void);
void test_app_run(void);

int main(void)
{
	test_throttle_run();
	test_battery_run();
	test_cfgstore_run();
	test_app_run();

	printf("\n%d run, %d failed\n", tests_run, tests_failed);
	return tests_failed != 0;
}

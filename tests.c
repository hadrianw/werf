#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "tests.h"
#include "util.h"

int
skip_test(const char *name, int argc, char *argv[])
{
	int i;
	if(argc == 1) {
		return 0;
	}
	for(i = 1; i < argc; i++) {
		if(!strcmp(argv[i], name)) {
			return 0;
		}
	}
	return 1;
}

static size_t run_tests_count = LEN(TESTS);

static size_t
next_from_all(int argc, char *argv[]) {
	(void)argc; (void)argv;
	static size_t i = 0;
	return i++;
}

static size_t
next_from_args(int argc, char *argv[])
{
	static int i = 1;
	static size_t run = 0;

	size_t j;

	for(; i < argc; i++) {
		for(j = 0; j < LEN(TESTS); j++) {
			if(!strcmp(argv[i], TESTS[j].name)) {
				i++;
				run++;
				return j;
			}
		}
	}
	run_tests_count = run;
	return LEN(TESTS);
}

int __wrap_main(int argc, char *argv[])
{
	size_t i;
	size_t success = 0;
	pid_t pid;
	int status;

	size_t (*next)(int, char*[]);

	next = (argc > 1) ? next_from_args : next_from_all;

	while((i = next(argc, argv)) < LEN(TESTS)) {
		printf("Running: %s\n", TESTS[i].name);
		pid = fork();
		if(pid < 0) {
			perror("test suite failure, fork");
			return -1;
		}
		if(pid == 0) {
			return TESTS[i].func();
		} else {
			if(waitpid(pid, &status, 0) == -1) {
				perror("test suite failure, waitpid");
				return -1;
			}
			if(status) {
				printf("failure: exit code %d signal %d\n\n",
						WIFEXITED(status) ? WEXITSTATUS(status) : 0,
						WIFSIGNALED(status) ? WTERMSIG(status) : -1);
			} else {
				puts("success");
				success++;
			}
		}
	}
	printf("%s, tests ok: %zu / %zu\n",
			success == run_tests_count ? "success" : "failure",
			success, run_tests_count);

	return success == run_tests_count ? 0 : 1;
}

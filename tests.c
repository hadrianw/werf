#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "tests.h"

int __wrap_main(void)
{
	size_t i;
	size_t success = 0;
	const size_t len = sizeof(TESTS)/sizeof(TESTS[0]);
	pid_t pid;
	int status;

	for(i = 0; i < len; i++) {
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
			success == len ? "success" : "failure",
			success, len);

	return success == len ? 0 : 1;
}

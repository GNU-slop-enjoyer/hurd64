/* lkl-boot: start the LKL kernel (Linux with amdgpu) in this process, let
 * the drivers probe, print what Linux printed and stop.
 *
 *   lkl-boot [MiB] [lkl_pci=BUS:DEV.FN] [more kernel arguments...]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <lkl.h>
#include <lkl_host.h>

int main(int argc, char **argv)
{
	char cmd[1024];

	/* keep the log when the test is killed */
	setvbuf(stdout, NULL, _IONBF, 0);
	int mib = argc > 1 ? atoi(argv[1]) : 512;

	snprintf(cmd, sizeof cmd, "mem=%dM loglevel=8", mib > 0 ? mib : 512);
	for (int i = 2; i < argc; i++) {
		strncat(cmd, " ", sizeof cmd - strlen(cmd) - 1);
		strncat(cmd, argv[i], sizeof cmd - strlen(cmd) - 1);
	}
	if (lkl_init(&lkl_host_ops) < 0 || lkl_start_kernel(cmd) < 0) {
		fprintf(stderr, "lkl-boot: kernel did not start\n");
		return 1;
	}
	/* amdgpu probes from a workqueue; give it time */
	sleep(argc > 2 ? 20 : 1);
	lkl_sys_halt();
	lkl_cleanup();
	return 0;
}

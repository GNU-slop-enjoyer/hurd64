/* cpuset: run one task on a single CPU of its own, everything else as before.
 *
 *   cpuset PID [CPU]     move processor CPU (default: the last one) into a new
 *                        processor set and assign task PID and its threads to it
 *
 * For telling kernel SMP bugs from races inside a multithreaded server: if
 * a failure goes away once the server cannot run on two CPUs at once, the
 * race is in the server. Needs the privileged host port (run as root).
 */
#include <hurd.h>
#include <mach.h>
#include <mach/mach_host.h>
#include <mach_error.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: cpuset PID [CPU]\n");
        return 2;
    }
    pid_t pid = atoi(argv[1]);
    mach_port_t host_priv, device;
    kern_return_t kr = get_privileged_ports(&host_priv, &device);
    if (kr) {
        fprintf(stderr, "cpuset: no privileged host port: %s\n", mach_error_string(kr));
        return 1;
    }
    processor_array_t procs;
    mach_msg_type_number_t n;
    if ((kr = host_processors(host_priv, &procs, &n)) || n < 2) {
        fprintf(stderr, "cpuset: need at least 2 processors (%s)\n", kr ? mach_error_string(kr) : "only 1");
        return 1;
    }
    unsigned cpu = argc > 2 ? (unsigned)atoi(argv[2]) : n - 1;
    if (cpu == 0 || cpu >= n) {
        fprintf(stderr, "cpuset: CPU must be 1..%u (0 is the master)\n", n - 1);
        return 1;
    }

    processor_set_t pset;
    processor_set_name_t name;
    if ((kr = processor_set_create(host_priv, &pset, &name))) {
        fprintf(stderr, "cpuset: processor_set_create: %s\n", mach_error_string(kr));
        return 1;
    }
    if ((kr = processor_assign(procs[cpu], pset, TRUE))) {
        fprintf(stderr, "cpuset: processor_assign cpu %u: %s\n", cpu, mach_error_string(kr));
        return 1;
    }
    task_t task = pid2task(pid);
    if (task == MACH_PORT_NULL) {
        fprintf(stderr, "cpuset: no task for pid %d\n", pid);
        return 1;
    }
    if ((kr = task_assign(task, pset, TRUE))) {
        fprintf(stderr, "cpuset: task_assign: %s\n", mach_error_string(kr));
        return 1;
    }
    printf("pid %d now runs only on cpu %u\n", pid, cpu);
    return 0;
}

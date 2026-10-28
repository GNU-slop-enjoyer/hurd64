/* smpctl: put every processor into the default processor set, so ordinary
 * tasks run on all CPUs. GNU Mach starts the APs in a separate set on
 * purpose (commit aadb4339) and leaves enabling them to userspace.
 *
 *   smpctl          enable all processors, print how many are in use
 *   smpctl -s       only print the status
 *
 * Needs the privileged host port (run as root).
 */
#include <hurd.h>
#include <mach.h>
#include <mach/mach_host.h>
#include <mach_error.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    int status_only = argc > 1 && strcmp(argv[1], "-s") == 0;
    mach_port_t host_priv, device;
    kern_return_t kr = get_privileged_ports(&host_priv, &device);
    if (kr) {
        fprintf(stderr, "smpctl: no privileged host port (run as root): %s\n", mach_error_string(kr));
        return 1;
    }
    processor_set_name_t name;
    processor_set_t pset;
    if ((kr = processor_set_default(mach_host_self(), &name)) ||
        (kr = host_processor_set_priv(host_priv, name, &pset))) {
        fprintf(stderr, "smpctl: default processor set: %s\n", mach_error_string(kr));
        return 1;
    }
    processor_array_t procs;
    mach_msg_type_number_t n;
    if ((kr = host_processors(host_priv, &procs, &n))) {
        fprintf(stderr, "smpctl: host_processors: %s\n", mach_error_string(kr));
        return 1;
    }
    int moved = 0, failed = 0;
    for (mach_msg_type_number_t i = 0; i < n && !status_only; i++) {
        kr = processor_assign(procs[i], pset, FALSE);
        if (kr == KERN_SUCCESS)
            moved++;
        else {
            failed++;
            fprintf(stderr, "smpctl: cpu %u: %s\n", i, mach_error_string(kr));
        }
    }
    struct processor_set_basic_info info;
    mach_msg_type_number_t cnt = PROCESSOR_SET_BASIC_INFO_COUNT;
    host_t h;
    if (processor_set_info(name, PROCESSOR_SET_BASIC_INFO, &h, (processor_set_info_t)&info, &cnt) == KERN_SUCCESS)
        printf("%u processors, %d in the default set\n", n, info.processor_count);
    if (!status_only)
        printf("assigned %d, failed %d\n", moved, failed);
    return failed ? 1 : 0;
}

#ifndef AZ_ALLOCID_H
#define AZ_ALLOCID_H 1

#include <aznix/az_types.h>

#if (AZNIX_API_VERSION >= 200)
/* .ko ioctl interface */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Allocationid to pid mapping table.  Process start time is used to
 * disambiguate pid recycle case.
 */
typedef struct {
    az_allocid_t allocid;

    pid_t pid;
    int64_t start_time;
} az_allocid_to_pid_t;

extern int az_allocid_purge();

/*
 * Bind an allocid to a pid.
 */
extern int az_allocid_add(az_allocid_t allocid, pid_t pid);

/*
 * Get the list of the allocids and associated pids in the
 * buffer pointed by pm for the given count cnt. If the parameter pm
 * is NULL, then parameter cnt will have the number of entries
 * available in the list on successful return from the call.
 * The user could make a call first with NULL pm argument to get the
 * actual list size and make another call to get the list. If the process
 * list size is more than the given count cnt, then it will return EINVAL.
 * Return Value:
 *   0 in case of success
 *   appropriate error code in case of failure.
 */
extern int az_allocid_get_list(az_allocid_to_pid_t *pm, uint64_t *cnt);

extern pid_t az_find_pid_from_allocid(az_allocid_t allocid);

extern az_allocid_t az_find_allocid_from_pid(pid_t pid);

#ifdef __cplusplus
}
#endif

#endif

#endif

// Copyright 2010 Azul Systems, Inc. All rights reserved.
// AZUL Systems PROPRIETARY/CONFIDENTIAL. Use is subject to license terms
//
// az_pgroup.h - pgroup functionality support

#ifndef _AZNIX_AZ_PGROUP_H_
#define _AZNIX_AZ_PGROUP_H_ 1

#include "az_pgroup_ioctl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * =======================
 * Process group interface
 * =======================
 */
#define AZPGROUP_MAX_DATA_SIZE (16 * 1024)
#define AZPGROUP_MAX_BLOB_SIZE (16 * 1024)
#define AZ_MAX_PROCESSES 2048
#define AZ_MAX_PGROUPS 2048

/*
 * Create a process group with the identifier az_gid and associate the specified
 * data (gdata) with this group. The size of the data blob is specified in
 * gdata_size and it should be less than AZPGROUP_MAX_BLOB_SIZE.
 * (gdata can be NULL)
 * Return Value:
 *   0 in case of success
 *   appropriate error code in case of failure.
 */
extern int az_pgroup_create(az_groupid_t az_gid, const void *gdata, size_t gdata_size);

/*
 * Destroy the process group with az_gid identifier tag. A group can not be
 * destroyed if any process under the specified group is still running.
 * The CPU resources will be released back to the system if the group is
 * removed successfully.
 * Return Value:
 *   0 in case of success
 *   appropriate error code in case of failure.
 */
extern int az_pgroup_destroy(az_groupid_t az_gid);

/*
 * Set the group specific data gdata of size gdata_size to the process group
 * with id az_gid. The size should be less than AZPGROUP_MAX_DATA_SIZE.
 * (gdata can be NULL)
 * Return Value:
 *   0 in case of success
 *   appropriate error code in case of failure.
 */
extern int az_pgroup_set_data(az_groupid_t az_gid, const void *gdata, size_t gdata_size);

/*
 * Copy the data blob associated with process group specified in az_gid
 * into the area pointed to by gdata.
 * gdata_size on input contains the size of the buffer pointed to by gdata
 * and on output contains the actual size of the data blocb associated with
 * process group az_gid
 * If the parameter gdata is NULL, then it just returns the data blob
 * size in gdata_size.
 * Return Value:
 *   0 in case of success
 *   appropriate error code in case of failure.
 */
extern int az_pgroup_get_data(az_groupid_t az_gid, void *gdata, size_t *gdata_size);

/*
 * Set/Get resource limits for the process group identified by az_gid.
 * The parameter rtype is a place holder to specify different resource
 * types like CPU, memory. It currently uses only value 1 for the
 * resource type CPU. Refer to the az_rlimit_t above for more information
 * on the resource limit data structure. The set operation will check for
 * the available committable CPU count before setting the resource limits
 * to the process group. If the available resources are less than the
 * requested count then it will return EAGAIN.
 * Return Value:
 *   0 in case of success
 *   appropriate error code in case of failure.
 * TODO: See if we need to set the available CPU count for this group in a
 *       limited operation mode.
 */
extern int az_pgroup_set_rlimit(az_groupid_t az_gid, process_resource_t rtype, const az_rlimit_t *rl);
extern int az_pgroup_get_rlimit(az_groupid_t az_gid, process_resource_t rtype, az_rlimit_t *rl);

/*
 * Get all the group ids that have been defined so far.
 * Input:
 *   - pointer to an array into which the az_gids will be returned
 *   - a count of the number of az_gids that will fit in the above array
 * Output:
 *   - all the group ids that are defined so far is returned in the az_gids array
 *   - numgids contains a count of the number of group ids returned.
 * Return Value:
 *   0 in case of success
 *   appropriate error code in case of failure.
 */
extern int az_pgroup_get_list(az_groupid_t *az_gids, size_t *numgids);

/*
 * Uncommit CPU resources for all the process groups with their id greater
 * than the id specified in az_gid. This will set the minimum and the
 * committed CPU resource limits for the matching process groups to  0.
 * Return Value:
 *   0 in case of success
 *   appropriate error code in case of failure.
 */
extern int az_pgroup_uncommit_all(az_groupid_t az_gid);

/*
 * Add the process with the given process id pid and the allocation id
 * allocid to the process group identified by az_gid. Hold a reference to the
 * linux process entry and the process group on successful return from the call.
 * Return Value:
 *   0 in case of success
 *   appropriate error code in case of failure.
 */
#if   (AZNIX_API_VERSION >= 200) /* .ko ioctl interface */
extern int az_process_add(az_allocid_t ignored, pid_t pid, az_groupid_t az_gid);
#elif (AZNIX_API_VERSION >= 100) /* syscalls interface */
extern int az_process_add(az_allocid_t allocid, pid_t pid, az_groupid_t az_gid);
#else
#error AZNIX_API_VERSION must be defined
#endif

/*
 * Send signal signum to the process identified by the allocation id passed in
 * Return Value:
 *   0 in case of success
 *   appropriate error code in case of failure.
 */
#if   (AZNIX_API_VERSION >= 200) /* .ko ioctl interface */
extern int az_process_kill(pid_t pid, int signum);
#elif (AZNIX_API_VERSION >= 100) /* syscalls interface */
extern int az_process_kill(az_allocid_t allocid, int signum);
#else
#error AZNIX_API_VERSION must be defined
#endif

/*
 * Move the process with the given allocation id allocid from process group
 * identified by az_srcgid to the process group identified by az_dstgid.
 * This essentially remaps the associations as per the new group information.
 * Return Value:
 *   0 in case of success
 *   appropriate error code in case of failure.
 */
#if   (AZNIX_API_VERSION >= 200) /* .ko ioctl interface */
extern int az_process_move(pid_t pid, int64_t az_srcgid, int64_t az_dstgid);
#elif (AZNIX_API_VERSION >= 100) /* syscalls interface */
extern int az_process_move(az_allocid_t allocid, int64_t az_srcgid, int64_t az_dstgid);
#else
#error AZNIX_API_VERSION must be defined
#endif

/*
 * Get the list of the processes and the associated group ids in the
 * buffer pointed by pm for the given count cnt. If the parameter pm
 * is NULL, then parameter cnt will have the number of processes
 * available in the list on successful return from the call.
 * The user could make a call first with NULL pm argument to get the
 * actual list size and make another call to get the list. If the process
 * list size is more than the given count cnt, then it will return EINVAL.
 * Return Value:
 *   0 in case of success
 *   appropriate error code in case of failure.
 */
#if   (AZNIX_API_VERSION >= 200) /* .ko ioctl interface */
/* Deprecated */
extern int private_az_process_get_list(az_pgroup_member_t *pm, uint64_t *cnt);
#elif (AZNIX_API_VERSION >= 100) /* syscalls interface */
extern int az_process_get_list(az_pgroup_member_t *pm, uint64_t *cnt);
#else
#error AZNIX_API_VERSION must be defined
#endif

/*
 * Get the process group identifier for the given allocation id.
 * Return Value:
 *   0 in case of success
 *   appropriate error code in case of failure.
 */
#if   (AZNIX_API_VERSION >= 200) /* .ko ioctl interface */
extern int64_t az_process_get_gid(pid_t pid);
#elif (AZNIX_API_VERSION >= 100) /* syscalls interface */
extern int64_t az_process_get_gid(az_allocid_t allocid);
#else
#error AZNIX_API_VERSION must be defined
#endif

// To maintain compatibility with older code
#define process_resource_limit_t         az_rlimit_t
#define prl_minimum                      rl_minimum
#define prl_committed                    rl_committed
#define prl_livepeak                     rl_livepeak
#define prl_maximum                      rl_maximum
#define pgroup_member_t                  az_pgroup_member_t
#define process_group_create             az_pgroup_create
#define process_group_destroy            az_pgroup_destroy
#define process_group_set_blob           az_pgroup_set_data
#define process_group_get_blob           az_pgroup_get_data
#define process_group_get_resource_limit az_pgroup_set_rlimit
#define process_group_set_resource_limit az_pgroup_get_rlimit
#define process_group_uncommit_all       az_pgroup_uncommit_all
#define process_group_move               az_process_move
#define process_group_get_list           az_process_get_list

#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus

#endif // _OS_PROCESS_H_

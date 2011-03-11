#define _POSIX_C_SOURCE 200112L

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <pthread.h>
#include <errno.h>

#include <aznix/az_pgroup.h>
#include <aznix/az_pgroup_ioctl.h>

#define AZUL_PGROUP_DEV "/dev/azul_sched"
#define RL_TYPE_CPU   1

#ifdef AZ_PROXIED
extern int __open(const char *pathname, int flags);
extern int __ioctl(int d, int request, ...);
#define MY_OPEN __open
#define MY_IOCTL __ioctl
#else
#define MY_OPEN open
#define MY_IOCTL ioctl
#endif

static int __azulfd = -1;
static int __azul_errno = 0;
static pthread_once_t __azul_pgroup_init_ctrl = PTHREAD_ONCE_INIT;

static inline void __azul_openup(void)
{
	if ((__azulfd = MY_OPEN(AZUL_PGROUP_DEV, O_RDWR)) < 0)
		__azul_errno = errno;
}

static inline int __azulinit(void)
{
	(void)pthread_once(&__azul_pgroup_init_ctrl, __azul_openup);
	return __azul_errno;
}

int az_pgroup_create(az_groupid_t gid, const void *data, size_t size)
{
	struct az_pgroup_create_args a;

	if (__azulinit()) {
		errno = __azul_errno;
		return -1;
	}

	a.gid = gid;
	a.data = (void *)data;
	a.data_size = size;

	return MY_IOCTL(__azulfd, IOC_AZ_PGROUP_CREATE, &a);
}

int az_pgroup_destroy(az_groupid_t gid)
{
	struct az_pgroup_destroy_args a;

	if (__azulinit()) {
		errno = __azul_errno;
		return -1;
	}

	a.gid = gid;

	return MY_IOCTL(__azulfd, IOC_AZ_PGROUP_DESTROY, &a);
}

int az_pgroup_set_data(az_groupid_t gid, const void *data, size_t size)
{
	struct az_pgroup_set_data_args a;

	if (__azulinit()) {
		errno = __azul_errno;
		return -1;
	}

	a.gid = gid;
	a.data = (void *)data;
	a.data_size = size;

	return MY_IOCTL(__azulfd, IOC_AZ_PGROUP_SET_DATA, &a);
}

int az_pgroup_get_data(az_groupid_t gid, void *data, size_t *size)
{
	struct az_pgroup_get_data_args a;

	if (__azulinit()) {
		errno = __azul_errno;
		return -1;
	}

	a.gid = gid;
	a.data = data;
	a.data_size = size;

	return MY_IOCTL(__azulfd, IOC_AZ_PGROUP_GET_DATA, &a);
}

int az_pgroup_set_rlimit(az_groupid_t gid, process_resource_t type,
			 const az_rlimit_t *rl)
{
	struct az_pgroup_set_rlimit_args a;

	if (__azulinit()) {
		errno = __azul_errno;
		return -1;
	}

	a.gid = gid;
	a.resource = type;
	a.rlimit = (az_rlimit_t *)rl;

	return MY_IOCTL(__azulfd, IOC_AZ_PGROUP_SET_RLIMIT, &a);
}

int az_pgroup_get_rlimit(az_groupid_t gid, process_resource_t type,
			 az_rlimit_t *rl)
{
	struct az_pgroup_get_rlimit_args a;

	if (__azulinit()) {
		errno = __azul_errno;
		return -1;
	}

	a.gid = gid;
	a.resource = type;
	a.rlimit = rl;

	return MY_IOCTL(__azulfd, IOC_AZ_PGROUP_GET_RLIMIT, &a);
}

int az_pgroup_get_list(az_groupid_t *gids, size_t *numgids)
{
	struct az_pgroup_get_list_args a;

	if (__azulinit()) {
		errno = __azul_errno;
		return -1;
	}

	a.gidp = (uint64_t *)gids; /* TODO FIXME */
	a.countp = numgids;

	return MY_IOCTL(__azulfd, IOC_AZ_PGROUP_GET_LIST, &a);
}

int az_pgroup_uncommit_all(az_groupid_t gid)
{
	struct az_pgroup_uncommit_all_args a;

	if (__azulinit()) {
		errno = __azul_errno;
		return -1;
	}

	a.gid = gid;

	return MY_IOCTL(__azulfd, IOC_AZ_PGROUP_UNCOMMIT_ALL, &a);
}

int az_process_add(az_allocid_t ignored, pid_t pid, az_groupid_t gid)
{
	struct az_process_add_args a;

	if (__azulinit()) {
		errno = __azul_errno;
		return -1;
	}

	a.label = pid;
	a.pid = pid;
	a.gid = gid;

	return MY_IOCTL(__azulfd, IOC_AZ_PROCESS_ADD, &a);
}

int az_process_kill(pid_t pid, int signum)
{
	struct az_process_kill_args a;

	if (__azulinit()) {
		errno = __azul_errno;
		return -1;
	}

	a.label = pid;
	a.signo = signum;

	return MY_IOCTL(__azulfd, IOC_AZ_PROCESS_KILL, &a);
}

int az_process_move(pid_t pid, int64_t sgid, int64_t dgid)
{
	struct az_process_move_args a;

	if (__azulinit()) {
		errno = __azul_errno;
		return -1;
	}

	a.label = pid;
	a.sgid = sgid;
	a.dgid = dgid;

	return MY_IOCTL(__azulfd, IOC_AZ_PROCESS_MOVE, &a);
}

int private_az_process_get_list(az_pgroup_member_t *pm, uint64_t *cnt)
{
	struct az_process_get_list_args a;

	if (__azulinit()) {
		errno = __azul_errno;
		return -1;
	}

	a.memberp = pm;
	a.countp = cnt;

	return MY_IOCTL(__azulfd, IOC_AZ_PROCESS_GET_LIST, &a);
}

int64_t az_process_get_gid(pid_t pid)
{
	struct az_process_get_gid_args a;

	if (__azulinit()) {
		errno = __azul_errno;
		return -1;
	}

	a.label = pid;

	return MY_IOCTL(__azulfd, IOC_AZ_PROCESS_GET_GID, &a);
}

/*****************************************************************************
 *
 * Azul token management interface for the CPM based launches. It maintains
 * the tokens in AZ_TOKEN_FILE and is accessed with appropriate (read/write)
 * based on the interface call.
 * NOTE: In case of failure to get a lock for the given operation, it sleeps for
 * AZ_TOKEN_FILE_LOCK_RETRY_TIME before it tries again.
 *
 *****************************************************************************/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <syslog.h>

#include <aznix/az_pgroup.h>

#define AZ_TOKEN_FILE 			"/var/run/az_token.dat"
#define AZ_TOKEN_FILE_MODE 		0666
#define AZ_TOKEN_FILE_LOCK_RETRY_TIME 	100 /* useconds */


static const char *token_file = AZ_TOKEN_FILE;

static int az_flock(int fd, int type)
{
	struct flock fl;
	int rc;

	memset(&fl, 0, sizeof(fl));
	fl.l_type = type;
	fl.l_whence = SEEK_SET;

	do {
		rc = fcntl(fd, F_SETLK, &fl);
		if (!rc || (rc && (errno != EINTR && errno != EAGAIN &&
					errno != EACCES)))
			break;
		if (rc && (errno == EAGAIN || errno == EACCES))
			usleep(AZ_TOKEN_FILE_LOCK_RETRY_TIME);
	} while (1);
	if (rc) {
		syslog(LOG_WARNING, "az_flock(%d) failed for fd %d: %s\n",
				type, fd, strerror(errno));
		return -errno;
	}
	return 0;
}

static void _put_token_list(FILE *fp, uint64_t *list, size_t *size)
{
	size_t count = 0;
	int rc;

	while (count < *size) {
		rc = fprintf(fp, "%ld\n", list[count]);
		if (rc < 0) {
			syslog(LOG_WARNING, "_put_token_list failed %lu: %s",
					list[count], strerror(errno));
			break;
		}
		count++;
	}
	*size = count;
#ifdef AZ_TOKEN_WITH_MAIN
	syslog(LOG_DEBUG, "_put_token_list: %lu\n", *size);
#endif /* AZ_TOKEN_WITH_MAIN */
}

static void _get_token_list(FILE *fp, uint64_t *list, size_t *size)
{
	char buf[256];
	size_t count = 0;
	char *pos;

	while (count < *size) {
		if (!fgets(buf, sizeof(buf), fp))
			break;
		pos = buf;
		while (*pos != '\0') {
			if (*pos == '\n') {
				*pos = '\0';
				break;
			}
			pos++;
		}
		if (buf[0] == '\0')
			continue;

		/* List could be NULL if the user just wants to get the size */
		if (list)
			list[count] = strtoul(buf, NULL, 0);
		count++;
	}
	*size = count;
#ifdef AZ_TOKEN_WITH_MAIN
	syslog(LOG_DEBUG, "_get_token_list: %lu\n", *size);
#endif /* AZ_TOKEN_WITH_MAIN */
}

/* Azul Token Management External Interface */
int az_token_get_list(uint64_t *list, size_t *size)
{
	FILE *fp;
	int fd;
	int rc;

	fd = open(token_file, O_RDONLY);
	if (fd < 0 && errno == ENOENT) {
		*size = 0;
		return 0;
	}

	if (fd < 0)
		return -errno;
	rc = az_flock(fd, F_RDLCK);
	if (rc)
		return rc;
	fp = fdopen(fd, "r");
	if (!fp)
		return -errno;

	_get_token_list(fp, list, size);
	az_flock(fd, F_UNLCK);
	fclose(fp);
	return 0;
}

int az_token_delete(uint64_t token)
{
	FILE *fp;
	int fd;
	int rc;
	size_t i;
	uint64_t list[AZ_MAX_PROCESSES];
	size_t size = sizeof(list)/sizeof(list[0]);

	fd = open(token_file, O_RDWR);
	if (fd < 0)
		return -errno;
	rc = az_flock(fd, F_WRLCK);
	if (rc)
		return rc;
	fp = fdopen(fd, "r+");
	if (!fp)
		return -errno;

	_get_token_list(fp, list, &size);
	for (i = 0; i < size; i++) {
		if (list[i] == token) {
			list[i] = list[size - 1];
			break;
		}
	}
	if (i >= size) {
		rc = ENOENT;
		goto out;
	}

	size--;
	rc = ftruncate(fd, 0);
	if (rc)
		goto out;
	rewind(fp);
	_put_token_list(fp, list, &size);
	rc = 0;
out:
	az_flock(fd, F_UNLCK);
	fclose(fp);
	return rc;
}

int az_token_delete_all(void)
{
	int fd;
	int rc;

	fd = open(token_file, O_WRONLY);
	if (fd < 0 && errno == ENOENT)
		return 0;
	if (fd < 0)
		return -errno;

	rc = az_flock(fd, F_WRLCK);
	if (rc)
		return rc;
	rc = ftruncate(fd, 0);
	rc = rc; /* XXX make compiler happy */
	az_flock(fd, F_UNLCK);
	close(fd);
	return 0;
}

int az_token_create(uint64_t token, const void* tdata, size_t tdata_size)
{
	FILE *fp;
	int fd;
	int rc;
	size_t i;
	uint64_t list[AZ_MAX_PROCESSES];
	size_t size = sizeof(list)/sizeof(list[0]);

	fd = open(token_file, O_RDWR | O_CREAT | O_SYNC, AZ_TOKEN_FILE_MODE);
	if (fd < 0)
		return -errno;
	rc = az_flock(fd, F_WRLCK);
	if (rc)
		return rc;
	fp = fdopen(fd, "r+");
	if (!fp)
		return -errno;

	_get_token_list(fp, list, &size);
	for (i = 0; i < size; i++) {
		if (list[i] == token)
			break;
	}
	if (i < size) {
		rc = EEXIST;
		goto out;
	}
	if (size >= AZ_MAX_PROCESSES) {
		rc = ENOMEM;
		goto out;
	}

	if (size) {
		rc = ftruncate(fd, 0);
		if (rc)
			goto out;
		rewind(fp);
	}

	list[size++] = token;
	_put_token_list(fp, list, &size);
	rc = 0;
out:
	az_flock(fd, F_UNLCK);
	fclose(fp);
	return rc;
}

int az_token_set_data(uint64_t token, const void *tdata, size_t tdata_size)
{
	return ENOSYS;
}

int az_token_get_data(uint64_t token, void *tdata, size_t *tdata_size)
{
	return ENOSYS;
}

//#define AZ_TOKEN_WITH_MAIN 1
#ifdef AZ_TOKEN_WITH_MAIN
static int az_put_token_list(uint64_t *list, size_t *size)
{
	FILE *fp;
	int fd;
	int rc;

	fd = open(token_file, O_WRONLY | O_CREAT | O_SYNC, AZ_TOKEN_FILE_MODE);
	if (fd < 0)
		return -errno;
	rc = az_flock(fd, F_WRLCK);
	if (rc)
		return rc;
	fp = fdopen(fd, "w");
	if (!fp)
		return -errno;

	_put_token_list(fp, list, size);
	az_flock(fd, F_UNLCK);
	fclose(fp);
	return 0;
}

extern char *optarg;
extern int optind, opterr, optopt;

int main(int argc, char *argv[])
{
	uint64_t token_list[AZ_MAX_PROCESSES];
	size_t nr_tokens;
	int rc;
	size_t i;
	int opt;

	srand48(time(NULL));
	while((opt = getopt(argc, argv, "c:d:tgp:")) != -1) {
		switch (opt) {
			case 'c':
				token_list[0] = strtoul(optarg, NULL, 0);
				if (!token_list[0])
					token_list[0] = lrand48();
				rc = az_token_create(token_list[0], NULL, 0);
				if (rc)
					printf("Create %lu failed: %s\n",
							token_list[0],
							strerror(rc));
				break;
			case 'd':
				token_list[0] = strtoul(optarg, NULL, 0);
				if (!token_list[0])
					token_list[0] = lrand48();
				rc = az_token_delete(token_list[0]);
				if (rc)
					printf("Delete %lu failed: %s\n",
							token_list[0],
							strerror(rc));
				break;
			case 't':
				rc = az_token_delete_all();
				if (rc)
					printf("Delete all failed: %s\n",
							strerror(rc));
				break;
			case 'g':
				nr_tokens = AZ_MAX_PROCESSES;
				rc = az_token_get_list(token_list, &nr_tokens);
				if (rc)
					printf("Delete %lu failed: %s\n",
							token_list[0],
							strerror(rc));
				else
					for (i = 0; i < nr_tokens; i++)
						printf("Get[%ld]: %ld\n", i,
								token_list[i]);
				break;
			case 'p':
				nr_tokens = strtoul(optarg, NULL, 0);
				for (i = 0; i < nr_tokens; i++) {
					token_list[i] = lrand48();
					printf("Put[%ld]: %ld\n", i,
							token_list[i]);
				}
				rc = az_put_token_list(token_list, &nr_tokens);
				if (rc)
					printf("Put list failed: %s\n",
							strerror(rc));
				else
					for (i = 0; i < nr_tokens; i++)
						printf("Put[%ld]: %ld\n", i,
								token_list[i]);
				break;
			default:
				printf("Usage: %s [ -a | -d <token>] -r -g "
						"-p <nr>\n", argv[0]);
				exit(1);
		}
	}

	if (optind < argc || argc <= 1) {
		printf("Usage: %s [ -a | -d <token>] -r -g "
				"-p <nr>\n", argv[0]);
		exit(1);
	}
	exit(0);
}
#endif /* AZ_TOKEN_WITH_MAIN */

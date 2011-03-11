#if (AZNIX_API_VERSION >= 200)
/* .ko ioctl interface */

#include <assert.h>

#include <aznix/az_allocid.h>
#include <aznix/az_pgroup.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <stdlib.h>
#include <errno.h>

#include <string>
#include <vector>

#define ALLOCID_DIR "/var/run/allocid"

static char *
skip_token(const char *p)
{
    while (isspace(*p)) p++;
    while (*p && !isspace(*p)) p++;
    return (char *)p;
}

static int64_t
get_process_start_time (pid_t pid)
{
    /* PBSIZE should be much more than enough to read lines from proc */
#define PBSIZE 8192
    char buf[PBSIZE];

    {
        int fd, len;
        sprintf (buf, "/proc/%d/stat", pid);
        fd = open(buf, O_RDONLY);
        if (fd < 0) {
            return -1;
        }
        len = read(fd, buf, sizeof(buf)-1);
        buf[len] = 0;
        close (fd);
    }

    {
        // 31067 (java_g) S 12259 31067 12259 34825 31067 0 846 31 1120 181 8 21 0 1 9 0 0 0 2913886874 32096256 1295 4294967295 134512640 134573728 3221215952 3221207044 1074303286 0 16391 0 20487 3222305931 0 0 17 0

        char *p = &(buf[0]);
        int64_t start_time;

#define EXTRACT(name) \
    name = strtoull (p, &p, 10);

#define SKIPTOK(name) \
    p = skip_token (p);

        SKIPTOK(pid);
        SKIPTOK(name);
        SKIPTOK(state);
        SKIPTOK(ppid);
        SKIPTOK(pgrp);
        SKIPTOK(session);
        SKIPTOK(tty);
        SKIPTOK(ttypgrp);
        SKIPTOK(flags);
        SKIPTOK(minflt);
        SKIPTOK(cminflt);
        SKIPTOK(majflt);
        SKIPTOK(cmajflt);
        SKIPTOK(utime);
        SKIPTOK(stime);
        SKIPTOK(cutime);
        SKIPTOK(cstime);
        SKIPTOK(priority);
        SKIPTOK(nice);
        SKIPTOK(timeout);
        SKIPTOK(it_real_val);
        EXTRACT(start_time);
        SKIPTOK(vsize);
        SKIPTOK(rss);

        return start_time;
    }

    return -1;
}

static pid_t
read_pid_for_allocid(az_allocid_t allocid)
{
    pid_t pid = -1;
    char buf[1024];
    snprintf (buf, sizeof(buf)-1, ALLOCID_DIR "/%ld/pid", allocid);
    FILE *f = fopen (buf, "r");
    if (! f) {
        perror ("libaznix read_pid_for_allocid fopen");
        return -1;
    }

    int matched = fscanf (f, "%d", &pid);
    if (matched != 1) {
        fprintf (stderr, "libaznix read_pid_for_allocid fscanf");
        fclose (f);
        return -1;
    }

    int rc = fclose (f);
    if (rc < 0) {
        perror ("libaznix read_pid_for_allocid fclose");
        return -1;
    }

    return pid;
}

static int64_t
read_start_time_for_allocid(az_allocid_t allocid)
{
    int64_t start_time = -1;
    char buf[1024];
    snprintf (buf, sizeof(buf)-1, ALLOCID_DIR "/%ld/start_time", allocid);
    FILE *f = fopen (buf, "r");
    if (! f) {
        perror ("libaznix read_start_time_for_allocid fopen");
        return -1;
    }

    int matched = fscanf (f, "%ld", &start_time);
    if (matched != 1) {
        fprintf (stderr, "libaznix read_start_time_for_allocid fscanf");
        return -1;
    }

    int rc = fclose (f);
    if (rc < 0) {
        perror ("libaznix read_start_time_for_allocid fclose");
        return -1;
    }

    return start_time;
}

int
az_allocid_purge()
{
    DIR *d = opendir (ALLOCID_DIR);
    if (!d) {
        perror ("libaznix az_allocid_purge opendir");
        return -1;
    }

    std::vector<std::string> v;

    int rc;
    struct dirent entry;
    struct dirent *result = 0;
    do {
        rc = readdir_r (d, &entry, &result);
        if (rc < 0) {
            perror ("libaznix az_allocid_purge readdir_r");
            break;
        }
        if (! result) {
            break;
        }

        std::string s (result->d_name);
        if (s == ".") {
            continue;
        }
        if (s == "..") {
            continue;
        }
        if (s.find (".tmp") != std::string::npos) {
            continue;
        }
        if (s.find (".purge") != std::string::npos) {
            continue;
        }

        az_allocid_t allocid = strtoull (s.c_str(), NULL, 10);

        int pid = 0;
        int64_t cached_start_time = 0;
        int64_t actual_start_time = 0;
        pid = read_pid_for_allocid (allocid);
        cached_start_time = read_start_time_for_allocid (allocid);
        actual_start_time = get_process_start_time (pid);

        if ((pid > 0) &&
            (cached_start_time > 0) &&
            (actual_start_time > 0) &&
            (cached_start_time == actual_start_time)) {
            // Process is a match.  Do not purge.
            continue;
        }

//        printf ("PURGE allocid,pid,cached_start_time,actual_start_time %ld,%d,%ld,%ld\n", allocid, pid, cached_start_time, actual_start_time);

        v.push_back (s);
    } while (result);

    rc = closedir (d);
    if (rc < 0) {
        perror ("libaznix az_allocid_purge closedir");
        assert (0);
    }

    for (unsigned i = 0; i < v.size(); i++) {
        char from[1024];
        char to[1024];
        snprintf (from, sizeof(from)-1, ALLOCID_DIR "/%s", v[i].c_str());
        snprintf (to, sizeof(to)-1, ALLOCID_DIR "/%s.purge", v[i].c_str());

        rc = rename (from, to);
        if (rc < 0) {
            perror ("libaznix az_allocid_purge rename");
            assert (0);
        }
    }

    if (v.size() > 0) {
        rc = system ("/bin/rm -fr " ALLOCID_DIR "/*.purge");
        if (rc < 0) {
            perror ("libaznix az_allocid_purge rm -fr");
            assert (0);
        }
    }

    return 0;
}

int
az_allocid_add(az_allocid_t allocid, pid_t pid)
{
    char tmp[1024];
    char final[1024];
    int rc;

#ifdef AZ_PROXIED
    assert (0);
#endif

    az_allocid_purge();

    int64_t start_time = get_process_start_time (pid);
    if (start_time < 0) {
        return -1;
    }

    rc = mkdir (ALLOCID_DIR, 0777);
    if (rc < 0) {
        if (errno != EEXIST) {
            perror ("libaznix az_allocid_add mkdir 1");
            return -1;
        }
    }

    snprintf (tmp, sizeof(tmp)-1, ALLOCID_DIR "/%ld.tmp", allocid);
    snprintf (final, sizeof(final)-1, ALLOCID_DIR "/%ld", allocid);
    rc = mkdir (tmp, 0777);
    if (rc < 0) {
        perror ("libaznix az_allocid_add mkdir 2");
        return -1;
    }

    {
        char buf[1024];
        snprintf (buf, sizeof(buf)-1, ALLOCID_DIR "/%ld.tmp/pid", allocid);
        FILE *f = fopen (buf, "w");
        if (! f) {
            perror ("libaznix az_allocid_add fopen");
            return -1;
        }

        char pidstring[10];
        snprintf (pidstring, sizeof(pidstring), "%d", pid);
        rc = fwrite (pidstring, strlen(pidstring), 1, f);
        if (rc != 1) {
            perror ("libaznix az_allocid_add fwrite");
            return -1;
        }

        rc = fclose (f);
        if (rc < 0) {
            perror ("libaznix az_allocid_add fclose");
            return -1;
        }
    }

    {
        char buf[1024];
        snprintf (buf, sizeof(buf)-1, ALLOCID_DIR "/%ld.tmp/start_time", allocid);
        FILE *f = fopen (buf, "w");
        if (! f) {
            perror ("libaznix az_allocid_add fopen");
            return -1;
        }

        char ststring[20];
        snprintf (ststring, sizeof(ststring), "%ld", start_time);
        rc = fwrite (ststring, strlen(ststring), 1, f);
        if (rc != 1) {
            perror ("libaznix az_allocid_add fwrite");
            return -1;
        }

        rc = fclose (f);
        if (rc < 0) {
            perror ("libaznix az_allocid_add fclose");
            return -1;
        }
    }

    rc = rename (tmp, final);
    if (rc < 0) {
        perror ("libaznix az_allocid_add rename");
        return -1;
    }

    return 0;
}

int
az_allocid_get_list(az_allocid_to_pid_t *pm, uint64_t *cnt)
{
#ifdef AZ_PROXIED
    assert (0);
#endif

    if (!pm) {
        *cnt = 0;
    }
    uint64_t localcnt = 0;

    DIR *d = opendir (ALLOCID_DIR);
    if (!d) {
        perror ("libaznix az_allocid_get_list opendir");
        return -1;
    }

    int rc;
    struct dirent entry;
    struct dirent *result = 0;
    do {
        rc = readdir_r (d, &entry, &result);
        if (rc < 0) {
            perror ("libaznix az_allocid_get_list readdir_r");
            return -1;
        }
        if (! result) {
            break;
        }

        std::string s (result->d_name);
        if (s == ".") {
            continue;
        }
        if (s == "..") {
            continue;
        }
        if (s.find (".tmp") != std::string::npos) {
            continue;
        }
        if (s.find (".purge") != std::string::npos) {
            continue;
        }
        az_allocid_t allocid = strtoull (s.c_str(), NULL, 10);

        int pid = 0;
        int64_t start_time = 0;
        pid = read_pid_for_allocid (allocid);
        start_time = read_start_time_for_allocid (allocid);

//        printf ("allocid,pid,start_time %ld,%d,%ld\n", allocid, pid, start_time);

        if (pid < 0) {
            continue;
        }

        if (start_time < 0) {
            continue;
        }

        if (pm) {
            if (localcnt >= *cnt) {
                closedir (d);
                errno = E2BIG;
                return -1;
            }

            pm[localcnt].allocid = allocid;
            pm[localcnt].pid = pid;
            pm[localcnt].start_time = start_time;
        }

        localcnt++;
    } while (result);

    rc = closedir (d);
    if (rc < 0) {
        perror ("libaznix az_allocid_get_list closedir");
        return -1;
    }

    if (!pm) {
        *cnt = localcnt;
    }

    return 0;
}

pid_t
az_find_pid_from_allocid(az_allocid_t allocid)
{
#ifdef AZ_PROXIED
    assert (0);
#endif

    az_allocid_to_pid_t pm[AZ_MAX_PROCESSES];
    uint64_t cnt = AZ_MAX_PROCESSES;
    int rc = az_allocid_get_list (pm, &cnt);
    if (rc < 0) {
        return -1;
    }

    for (unsigned i = 0; i < cnt; i++) {
        if (pm[i].allocid != allocid) {
            continue;
        }

        pid_t pid = pm[i].pid;
        int64_t real_start_time = get_process_start_time (pid);
        if (real_start_time < 0) {
            return -1;
        }

        if (real_start_time != pm[i].start_time) {
            return -1;
        }

        return pid;
    }

    return -1;
}

az_allocid_t
az_find_allocid_from_pid(pid_t pid)
{
#ifdef AZ_PROXIED
    assert (0);
#endif

    az_allocid_to_pid_t pm[AZ_MAX_PROCESSES];
    uint64_t cnt = AZ_MAX_PROCESSES;
    int rc = az_allocid_get_list (pm, &cnt);
    if (rc < 0) {
        return -1;
    }

    for (unsigned i = 0; i < cnt; i++) {
        if (pm[i].pid != pid) {
            continue;
        }

        int64_t real_start_time = get_process_start_time (pid);
        if (real_start_time < 0) {
            return -1;
        }

        if (real_start_time != pm[i].start_time) {
            return -1;
        }

        az_allocid_t allocid = pm[i].allocid;
        return allocid;
    }

    return -1;
}

#endif

// Copyright 2003 Azul Systems, Inc. All rights reserved.
// AZUL Systems PROPRIETARY/CONFIDENTIAL. Use is subject to license terms
//
// pi.c - Process Introspection subsystem for Aztek OS Services

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <os/pi.h>
#include <os/memory.h>
#include <aznix/az_pgroup.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#include <assert.h>

#define PI_INFO_DIR "/var/run/pi_info"

static void pi_info_purge()
{
    DIR *d = NULL;
    struct dirent *ent = NULL;
    int rv = 0;

    d = opendir (PI_INFO_DIR);
    if (! d) {
        perror ("opendir");
        assert (0);
    }

    for (ent = readdir(d); ent != NULL; ent = readdir(d)) {
        if (! (strcmp (ent->d_name, "."))) {
            continue;
        }
        if (! (strcmp (ent->d_name, ".."))) {
            continue;
        }
        if (strstr (ent->d_name, ".tmp")) {
            continue;
        }

        az_allocid_t allocid = strtol (ent->d_name, NULL, 10);
        if (allocid < 2) {
            continue;
        }

        az_groupid_t azgid = az_process_get_gid (allocid);
        if (azgid < 0) {
            char fname[256];
            snprintf (fname, sizeof(fname)-1, "%s/%ld", PI_INFO_DIR, allocid);

            // Process no longer exists.  Purge it.
            rv = unlink (fname);
            int myerrno = 0;
            if (rv < 0) {
                myerrno = errno;
                if (errno != ENOENT) {
                    perror ("unlink");
                    assert (0);
                }
            }
        }
    }

    rv = closedir(d);
    if (rv < 0) {
        perror ("closedir");
        assert (0);
    }
}

extern sys_return_t pi_info_set(az_allocid_t allocid,
                                const pi_info_t *info)
{
    char fname[256];
    char tmpfname[256];
    int rv = 0;
    pid_t pid = 0;
    FILE *f = NULL;
    size_t sz = 0;

    snprintf (fname, sizeof(fname)-1, "%s/%ld", PI_INFO_DIR, allocid);
    pid = getpid();
    snprintf (tmpfname, sizeof(tmpfname)-1, "%s/%d.%ld.tmp", PI_INFO_DIR, pid, allocid);

    rv = mkdir (PI_INFO_DIR, 0600);
    if (rv < 0) {
        if (errno != EEXIST) {
            perror ("mkdir");
            assert (0);
        }
    }

    pi_info_purge();

    f = fopen (tmpfname, "w");
    if (!f) {
        perror ("fopen");
        assert (0);
    }

    sz = fwrite (info, sizeof(*info), 1, f);
    if (sz != 1) {
        perror ("fwrite");
        assert (0);
    }

    rv = fclose (f);
    if (rv < 0) {
        perror ("fclose");
        assert (0);
    }

    rv = rename (tmpfname, fname);
    if (rv < 0) {
        perror ("rename");
        assert (0);
    }

    return SYSERR_NONE;
}

extern sys_return_t pi_info_get(az_allocid_t allocid,
                                pi_info_t *info)
{
    char fname[256];
    int rv = 0;
    FILE *f = NULL;
    size_t sz = 0;

    snprintf (fname, sizeof(fname)-1, "%s/%ld", PI_INFO_DIR, allocid);

    f = fopen (fname, "r");
    if (!f) {
        // Handle the normal case of an invalid allocid being requested.
        if (errno == ENOENT) {
            return SYSERR_NOT_FOUND;
        }

        // All other errors are catastrophic.
        perror ("fopen");
        assert (0);
    }

    sz = fread (info, sizeof(*info), 1, f);
    if (sz != 1) {
        perror ("fread");
        assert (0);
    }

    rv = fclose (f);
    if (rv < 0) {
        perror ("fclose");
        assert (0);
    }

    return SYSERR_NONE;
}

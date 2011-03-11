// Copyright 2003 Azul Systems, Inc. All rights reserved.
// AZUL Systems PROPRIETARY/CONFIDENTIAL. Use is subject to license terms
//

#ifdef AZ_PROXIED

#include <os/posix_redefines.h>

/* HACK!  Is there a better way to get these numbers? */
#define OPEN_CREATE_FLAGS 0x241
#define OPEN_RDONLY_FLAGS 0x0

#else

#define OPEN_CREATE_FLAGS (O_WRONLY|O_CREAT|O_TRUNC)
#define OPEN_RDONLY_FLAGS (O_RDONLY)

#endif // AZ_PROXIED



#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <os/shared_user_data.h>
#include <aznix/az_pgroup.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#include <assert.h>
#include <syslog.h>

#include <fcntl.h>
#include <pwd.h>
#include <grp.h>


#define SUD_DIR "/var/run/shared_user_data"

#define MY_OPEN open
#define MY_CLOSE close
#define MY_READ read
#define MY_WRITE write
#define MY_RENAME rename

int
shared_user_data_create (az_allocid_t allocid)
{
#define AVM_USER_NAME "avm"
#define AVM_GROUP_NAME "avm"

    char buf[32*1024];
    int rv;

    int uid;
    {
        struct passwd pwbuf;
        struct passwd *pwbufp = NULL;
        long buflen = sysconf (_SC_GETPW_R_SIZE_MAX);
        assert (buflen < (long)sizeof(buf));
        rv = getpwnam_r (AVM_USER_NAME, &pwbuf, &buf[0], buflen, &pwbufp);
        if (rv) {
            perror ("getpwnam_r");
            syslog (LOG_ERR, "ERROR: sud getpwnam_r");
            return errno;
        }
        uid = pwbufp->pw_uid;
    }

    int gid;
    {
        struct group gbuf;
        struct group *gbufp;
        long buflen = sysconf (_SC_GETGR_R_SIZE_MAX);
        assert (buflen < (long)sizeof(buf));
        rv = getgrnam_r (AVM_GROUP_NAME, &gbuf, &buf[0], buflen, &gbufp);
        if (rv) {
            perror ("getgrnam_r");
            syslog (LOG_ERR, "ERROR: sud getgrnam_r");
            return errno;
        }
        gid = gbufp->gr_gid;
    }

    rv = mkdir (SUD_DIR, 0700);
    if (rv < 0) {
        if (errno != EEXIST) {
            perror ("mkdir (shared_user_data_create 1)");
            syslog (LOG_ERR, "ERROR: sud mkdir");
            return errno;
        }
    }

    rv = chown (SUD_DIR, uid, gid);
    if (rv < 0) {
        perror ("chown");
        syslog (LOG_ERR, "ERROR: sud chown 1");
        return errno;
    }

    char dname[256];
    snprintf (dname, sizeof(dname)-1, "%s/%ld", SUD_DIR, allocid);

    rv = mkdir (dname, 0700);
    if (rv < 0) {
        if (errno != EEXIST) {
            perror ("mkdir (shared_user_data_create 2)");
            syslog (LOG_ERR, "ERROR: sud mkdir");
            return errno;
        }
    }

    rv = chown (dname, uid, gid);
    if (rv < 0) {
        perror ("chown");
        syslog (LOG_ERR, "ERROR: sud chown 2");
        return errno;
    }

    return 0;
}

void
shared_user_data_purge()
{
    DIR *d = NULL;
    struct dirent *ent = NULL;
    int rv = 0;

    d = opendir (SUD_DIR);
    if (! d) {
        return;
    }

    for (ent = readdir(d); ent != NULL; ent = readdir(d)) {
        if (! (strcmp (ent->d_name, "."))) {
            continue;
        }
        if (! (strcmp (ent->d_name, ".."))) {
            continue;
        }

        az_allocid_t allocid = strtol (ent->d_name, NULL, 10);
        if (allocid < 2) {
            continue;
        }

        az_groupid_t azgid = az_process_get_gid (allocid);
        if (azgid < 0) {
            char dname[256];
            snprintf (dname, sizeof(dname)-1, "%s/%ld", SUD_DIR, allocid);

            // Process no longer exists.  Purge it.
            char cmd[512];
            snprintf (cmd, sizeof(cmd)-1, "/bin/rm -rf %s", dname);
            int rc = system (cmd);
            if (rc) {
                perror ("system (rm -rf)");
                syslog (LOG_ERR, "ERROR: sud system (rm -rf)");
            }
        }
    }

    rv = closedir(d);
    if (rv < 0) {
        int e = errno;
        perror ("closedir");
        syslog (LOG_ERR, "ERROR: sud closedir (errno %d)", e);
    }
}

#ifdef AZ_PROXIED
static ssize_t
fullWrite (int fd, const char *buf, size_t count)
// Retry partial or EAGAIN writes until count is completely written.
{
    size_t written = 0;
    while (written < count) {
        ssize_t rv = MY_WRITE (fd, buf+written, count-written);
        if (rv < 0) {
            if ((errno == EAGAIN) || (errno == EINTR)) {
                continue;
            }

            return rv;
        }

        written += rv;
    }

    assert (written == count);
    return written;
}
#endif

static ssize_t
fullRead (int fd, char *buf, size_t count)
// Retry partial or EAGAIN writes until count is completely written.
{
    size_t bytesread = 0;
    while (bytesread < count) {
        ssize_t rv = MY_READ (fd, buf+bytesread, count-bytesread);
        if (rv < 0) {
            if ((errno == EAGAIN) || (errno == EINTR)) {
                continue;
            }

            return rv;
        }
        else if (rv == 0) {
            return bytesread;
        }

        bytesread += rv;
    }

    assert (bytesread == count);
    return bytesread;
}

#ifdef AZ_PROXIED
static sys_return_t
sud_set_common (az_allocid_t allocid, const char *name,
                const char *buf, size_t len)
{
    char fname[256];
    char tmpfname[256];
    int rv = 0;
    ssize_t sz = 0;
    int fd;

    snprintf (fname, sizeof(fname)-1, "%s/%ld/%s", SUD_DIR, allocid, name);
    snprintf (tmpfname, sizeof(tmpfname)-1, "%s/%ld/%s.tmp", SUD_DIR, allocid, name);

    int flags = OPEN_CREATE_FLAGS;
    fd = MY_OPEN (tmpfname, flags, 0644);
    if (fd < 0) {
        int e = errno;
        perror ("sud open");
        syslog (LOG_ERR, "ERROR: sud open, %d", e);
        return SYSERR_NO_PRIVILEGE;
    }

    sz = fullWrite (fd, buf, len);
    if ((size_t)sz != len) {
        perror ("sud write");
        syslog (LOG_ERR, "ERROR: sud write");
        rv = MY_CLOSE (fd);
        if (rv < 0) {
            perror ("close");
            syslog (LOG_ERR, "ERROR: sud close");
        }
        return SYSERR_NO_PRIVILEGE;
    }

    rv = MY_CLOSE (fd);
    if (rv < 0) {
        perror ("sud close");
        syslog (LOG_ERR, "ERROR: sud close");
        return SYSERR_NO_PRIVILEGE;
    }

    rv = MY_RENAME (tmpfname, fname);
    if (rv < 0) {
        perror ("sud rename");
        syslog (LOG_ERR, "ERROR: sud rename");
        return SYSERR_NO_PRIVILEGE;
    }

    return SYSERR_NONE;
}
#endif

static sys_return_t
sud_get_common (az_allocid_t allocid, const char *name,
                char *buf, size_t len, uint64_t expectedRevision)
{
    char fname[256];
    int rv = 0;
    ssize_t sz = 0;
    int fd;
    int flags;

    assert (len >= 8);

    snprintf (fname, sizeof(fname)-1, "%s/%ld/%s", SUD_DIR, allocid, name);

    flags = OPEN_RDONLY_FLAGS;
    fd = MY_OPEN (fname, flags, 0);
    if (fd < 0) {
        // Handle the normal case of an invalid allocid being requested.
        if (errno == ENOENT) {
            return SYSERR_NOT_FOUND;
        }

        // All other errors are catastrophic.
        perror ("open");
        syslog (LOG_ERR, "ERROR: sud open");
        assert (0);
    }

    // Read in the revision number first.
    sz = fullRead (fd, buf, 8);
    if (sz != 8) {
        perror ("read 1");
        syslog (LOG_ERR, "ERROR: sud read 1");
        assert (0);
    }

    uint64_t actualRevision = *((uint64_t *) buf);
    if (expectedRevision != actualRevision) {
        // This is a normal condition.  Return an error and allow the 
        // caller to try again with a different expectedRevision.

        rv = MY_CLOSE (fd);
        if (rv < 0) {
            perror ("close");
            syslog (LOG_ERR, "ERROR: sud close");
        }

        return SYSERR_INVALID_STATE;
    }

    // Read the remainder of the record after the revision number.
    sz = fullRead (fd, buf+8, len-8);
    if ((size_t)sz != (len-8)) {
        perror ("read 2");
        syslog (LOG_ERR, "ERROR: sud read 2");
        assert (0);
    }

    rv = MY_CLOSE (fd);
    if (rv < 0) {
        perror ("close");
        syslog (LOG_ERR, "ERROR: sud close");
    }

    return SYSERR_NONE;
}

sys_return_t
shared_user_data_get_jvm_conf_rev1 (az_allocid_t allocid, 
                                    sud_jvm_conf_rev1_t *buf)
{
    uint64_t expectedRevision = 1;
    return sud_get_common (allocid, "jvm_conf", (char *)buf, sizeof(*buf), expectedRevision);
}

sys_return_t
shared_user_data_set_jvm_conf_rev1 (az_allocid_t allocid,
                                    const sud_jvm_conf_rev1_t *buf)
{
#ifdef AZ_PROXIED
    return sud_set_common (allocid, "jvm_conf", (const char *)buf, sizeof(*buf));
#else
    return SYSERR_NONE;
#endif
}

sys_return_t
shared_user_data_get_jvm_heap_rev1 (az_allocid_t allocid,
                                    sud_jvm_heap_rev1_t *buf)
{
    uint64_t expectedRevision = 1;
    return sud_get_common (allocid, "jvm_heap", (char *)buf, sizeof(*buf), expectedRevision);
}

sys_return_t
shared_user_data_set_jvm_heap_rev1 (az_allocid_t allocid,
                                    const sud_jvm_heap_rev1_t *buf)
{
#ifdef AZ_PROXIED
    return sud_set_common (allocid, "jvm_heap", (const char *)buf, sizeof(*buf));
#else
    return SYSERR_NONE;
#endif
}

sys_return_t
shared_user_data_get_arta_rev1 (az_allocid_t allocid,
                                sud_arta_rev1_t *buf)
{
    uint64_t expectedRevision = 1;
    return sud_get_common (allocid, "arta", (char *)buf, sizeof(*buf), expectedRevision);
}

sys_return_t
shared_user_data_set_arta_rev1 (az_allocid_t allocid,
                                const sud_arta_rev1_t *buf)
{
#ifdef AZ_PROXIED
    return sud_set_common (allocid, "arta", (const char *)buf, sizeof(*buf));
#else
    return SYSERR_NONE;
#endif
}

sys_return_t
shared_user_data_get_io_rev1 (az_allocid_t allocid,
                              sud_io_rev1_t *buf)
{
    uint64_t expectedRevision = 1;
    return sud_get_common (allocid, "io", (char *)buf, sizeof(*buf), expectedRevision);
}

sys_return_t
shared_user_data_set_io_rev1 (az_allocid_t allocid,
                              const sud_io_rev1_t *buf)
{
#ifdef AZ_PROXIED
    return sud_set_common (allocid, "io", (const char *)buf, sizeof(*buf));
#else
    return SYSERR_NONE;
#endif
}

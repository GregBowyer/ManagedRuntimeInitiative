// Copyright 2010 Azul Systems, Inc.  All Rights Reserved.
// DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
//
// This code is free software; you can redistribute it and/or modify it under 
// the terms of the GNU General Public License version 2 only, as published by 
// the Free Software Foundation. 
//
// This code is distributed in the hope that it will be useful, but WITHOUT ANY 
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
// A PARTICULAR PURPOSE.  See the GNU General Public License version 2 for  more
// details (a copy is included in the LICENSE file that accompanied this code).
//
// You should have received a copy of the GNU General Public License version 2 
// along with this work; if not, write to the Free Software Foundation,Inc., 
// 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
// 
// Please contact Azul Systems, Inc., 1600 Plymouth Street, Mountain View, 
// CA 94043 USA, or visit www.azulsystems.com if you need additional information 
// or have any questions.
#include <os/log.h>
#include <os/utilities.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static char _ident[128] = "";
static uint64_t _facility = LOG_USER;
static uint64_t _option = 0UL;

void log_set_defaults(const char *ident, uint64_t option, uint64_t facility) {
    if ((ident != NULL) && (strcmp(ident, "") != 0)) {
        strncpy(_ident, ident, sizeof(_ident));
    }
    // These are not currently used.
    _facility = facility;
    _option = option;
}


void log_messagev(uint64_t facility_priority, log_flags_t flags, const char *format, va_list ap) {
    char buf[SYSLOG_MAX_MGSLEN];
    size_t len = strlen(_ident);
    if (len > 0 ) {
        strncpy(buf, _ident, SYSLOG_MAX_MGSLEN);
        strncat(buf, ": ", SYSLOG_MAX_MGSLEN);
        len = strlen(buf);
    } else {
        buf[0] = '\0';
    }

    if (format == NULL) {
        format = "(null)";
    }

    char *bufp = buf + len;
    vsnprintf(bufp, sizeof(buf) - len, format, ap);

    if ((facility_priority & LOG_FACILITY_MASK) == LOG_DEFAULT_FACILITY) facility_priority |= _facility;
    syslog(facility_priority, buf, strlen(buf));
}


void log_message(uint64_t facility_priority, log_flags_t flags, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    vsyslog(facility_priority, format, ap);
    va_end(ap);
}

// This is basically the same as log_message except the priority is always LOG_ERR and
// (so facility should NOT include a priority code) and error_code is supplied as well.
void log_error(uint64_t facility, sys_return_t error_code, const char *format, ...) {
    char buf[SYSLOG_MAX_MGSLEN];
    size_t len = strlen(_ident);
    if (len > 0 ) {
        strncpy(buf, _ident, SYSLOG_MAX_MGSLEN - len);
        strncat(buf, ": ", SYSLOG_MAX_MGSLEN - (len + 2));
        len = strlen(buf);
    } else {
        buf[0] = '\0';
    }

    strncat(buf, error_message(error_code), SYSLOG_MAX_MGSLEN - len);
    strncat(buf, ": ", SYSLOG_MAX_MGSLEN - (len + 2));
    len = strlen(buf);

    va_list ap;
    va_start(ap, format);
    if (format == NULL) {
        format = "(null)";
    }

    char *bufp = buf + len;
    vsnprintf(bufp, sizeof(buf) - len, format, ap);

    facility &= LOG_FACILITY_MASK;    // Mask off any priority supplied by mistake.
    if (facility == LOG_DEFAULT_FACILITY) facility |= _facility;
    syslog((facility | LOG_ERR), buf, strlen(buf));
}

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

#include <sys/types.h>
#include <aznix/az_memory.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
usage()
{
    printf ("\n");
    printf ("usage:  azpmem [--reserve --pages N]\n");
    printf ("        azpmem [--unreserve --pages N]\n");
    printf ("\n");
    printf ("        --reserve          reserve 2M pages for azmm.\n");
    printf ("\n");
    printf ("        --unreserve        unreserve 2M pages for azmm.\n");
    printf ("\n");
    exit (1);
}

void
parseArgs (int argc, char **argv,
	   int *reserve,
	   int *unreserve,
	   uint64_t *num2mPages)
{
    int i;

    *reserve = 0;
    *unreserve = 0;
    *num2mPages = 0;

    for (i = 1; i < argc; i++) {
        char *s = argv[i];
        if (!strcmp(s, "--reserve")) {
            *reserve = 1;
        }
        else if (!strcmp(s, "--unreserve")) {
  	    *unreserve = 1;
        }
        else if (!strcmp(s, "--pages")) {
            i++;
	    if (i >= argc)
	    	usage();
            *num2mPages = strtoul (argv[i], NULL, 10);
        }
        else {
            usage();
        }
    }
}

void
doReserve (uint64_t num2mPages)
{
    int rc = az_pmem_reserve_pages (num2mPages);
    if (rc < 0) {
        fprintf(stderr, "az_pmem_reserve_pages (num2mPages %lu)", num2mPages);
	exit (1);
    }

    printf ("az_pmem_reserve_pages (num2mPages %lu) succeeded", num2mPages);
}

void
doUnreserve (uint64_t num2mPages)
{
    int rc = az_pmem_unreserve_pages (num2mPages);
    if (rc < 0) {
        fprintf(stderr, "az_pmem_unreserve_pages (num2mPages %lu)", num2mPages);
	exit (1);
    }

    printf ("az_pmem_unreserve_pages (num2mPages %lu) succeeded", num2mPages);
}

int
main (int argc, char **argv)
{
    int reserve;
    int unreserve;
    uint64_t num2mPages;

    parseArgs (argc, argv, &reserve, &unreserve, &num2mPages);

    if (reserve) {
        doReserve (num2mPages);
    }
    else if (unreserve) {
        doUnreserve (num2mPages);
    }
    else {
        usage();
    }

    return 0;
}

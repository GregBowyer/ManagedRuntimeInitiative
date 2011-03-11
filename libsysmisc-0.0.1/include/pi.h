// Copyright 2003 Azul Systems, Inc. All rights reserved.
// AZUL Systems PROPRIETARY/CONFIDENTIAL. Use is subject to license terms
//
// pi.h - Process Introspection subsystem for Aztek OS Services

#ifndef _OS_PI_H_
#define _OS_PI_H_ 1

#include <os/types.h>
#include <os/memory.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *pi_image_t;

// Lookup of sybols in images. This can be done in a first-found manner
// (PI_IMAGE_FIRST), in the current executable/main program (PI_IMAGE_MAIN),
// only in the current image - where the caller executes (PI_IMAGE_THIS) or
// searching from the next image in the list of objects loaded (PI_IMAGE_NEXT).
// These are akin to the dlsym options RTLD_NEXT and RTLD_DEFAULT, with
// embellishments.

#define	PI_IMAGE_FIRST		((pi_image_t)0)
#define	PI_IMAGE_MAIN		((pi_image_t)1)
#define	PI_IMAGE_THIS		((pi_image_t)2)
#define	PI_IMAGE_NEXT		((pi_image_t)-1)

extern address_t pi_symbol_to_address_by_name(pi_image_t  image,
					      const char *symbol_name);

extern address_t pi_symbol_to_address_by_name_version(pi_image_t  image,
						      const char *symbol_name,
						      const char *version);

// Lookup symbol info given an address.
// Return value is closest address with symbol match (or null).
// image in is where to look (or null).
// image out is where found (or null).
// symbol_name is located symbol, if any.
// extent out is the size of the object represented by the symbol
extern address_t pi_symbol_info_for_address(address_t    address,
                                            pi_image_t  *image /* in as hint, out as result */,
                                            const char **symbol_name,
                                            size_t      *extent);



// The pi_info stuff has nothing to do with the pi_symbol stuff.
//
// The extra +1 is for additional XML element information above
// and beyond what is stored in the rmap field of launchd.

#define LAUNCHD_RMAP_KILOBYTES (13+11)
#define PI_RMAP_SIZE ((LAUNCHD_RMAP_KILOBYTES+1)*1024)
#define PI_ARGS_SIZE 1024

typedef struct {
        char path[2048];
        char args[PI_ARGS_SIZE];
        char proxyusername[64];
        char proxyhostname[128];
        char proxyhostipasascii[32];    // not currently used.
        char proxyReleaseVersion[32];    
        uint64_t systemtimenanosstart;
        uint64_t proxypid;
        uint64_t proxyuid;
        uint64_t backendpid;

        char proxypubkey[1024];		// proxy public key
        char rmap[PI_RMAP_SIZE];	// resourceMap
} pi_info_t;

extern sys_return_t pi_info_set(az_allocid_t allocid,
                                const pi_info_t *info);
// Purpose:  Called by init to set up the structure with as much information
//           as is known.
// Returns:  SYSERR_NONE if success.

extern sys_return_t pi_info_get(az_allocid_t allocid,
                                pi_info_t *info);
// Purpose:  Called by ps (or other inspection program).
// Returns:  SYSERR_NONE if success.


#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus

#endif // _OS_PI_H_

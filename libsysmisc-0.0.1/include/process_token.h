// Copyright 2003 Azul Systems, Inc. All rights reserved.
// AZUL Systems PROPRIETARY/CONFIDENTIAL. Use is subject to license terms
//
// process_info.h - Process subsystem for Aztek OS Services

#ifndef _OS_PROCESS_TOKEN_H_
#define _OS_PROCESS_TOKEN_H_ 1

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Create a token for the specified allocid and associate the data pointed 
 * by tdata of size tdata_size.
 * Return Value:
 *   0 in case of success
 *   appropriate error code in case of failure.
 */
extern int az_token_create(az_allocid_t allocid, const void *tdata, size_t tdata_size);

/*
 * Delete the token entry with the value specified in allocid.
 * Return Value:
 *   0 in case of success
 *   appropriate error code in case of failure.
 */
extern int az_token_delete(az_allocid_t allocid);

/*
 * Set/modify the token data associated with the specified allocid to the data
 * in tdata whose size is specified by tdata_size.
 * Return Value:
 *   0 in case of success
 *   appropriate error code in case of failure.
 */
extern int az_token_set_data(az_allocid_t allocid, const void *tdata, size_t tdata_size);

/*
 * Copy the token data associated with the allocid into the buffer pointed by tdata. The size
 * of the buffer pointed to by tdata is specified in tdata_sizep. 
 * On a successful return tdata_sizep will point to the actual size of the token data.
 * Return Value:
 *   0 in case of success
 *   appropriate error code in case of failure.
 */
extern int az_token_get_data(az_allocid_t allocid, void *tdata, size_t *tdata_sizep);

/*
 * Get all the tokens defined in the system.
 *    token_array is a pointer to an array of az_token_t types
 *    On input countp contains the cardinality of token_array
 *    On return the token_array contains all the tokens (allocid and size of
 *    any data associated with the allocid) and countp contains the actual
 *    number of allocids returned.
 *    if token_array is NULL then on return countp points to the actual number of tokens
 *    registered in the system.
 * Return Value:
 *   0 in case of success
 *   appropriate error code in case of failure.
 */
extern int az_token_get_list(az_allocid_t *token_array, unsigned long *countp);

/*
 * Delete all the tokens already registered in the system.
 * Return Value:
 *   0 in case of success
 *   appropriate error code in case of failure.
 */
extern int az_token_delete_all(void);

#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus

#endif // _OS_PROCESS_TOKEN_H_

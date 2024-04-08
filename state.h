/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file 		state.h
 *
 * @brief 		Header file to manage the CXL switch state 
 *
 * @copyright 	Copyright (C) 2024 Jackrabbit Founders LLC. All rights reserved.
 *
 * @date 		Jan 2024
 * @author 		Barrett Edwards <code@jrlabs.io>
 * 
 */
/* INCLUDES ==================================================================*/

#ifndef _STATE_H
#define _STATE_H

/* __u8
 * __u16
 */
#include <linux/types.h>

/* pthread_mutex_t
 */
#include <pthread.h>

#include <fmapi.h> 
#include <cxlstate.h>

/* MACROS ====================================================================*/

#define MAX_LD 				16
#define MAX_PORTS 			256
#define MAX_VCSS 			MAX_PORTS
#define MAX_VPPBS_PER_VCS  	256
#define MAX_VPPBS 			MAX_PORTS * MAX_LD

#define MAX_INDENT 			32
#define INDENT 				2
#define CFG_SPACE_SIZE      4096
#define MAX_FILE_NAME_LEN	256

#define INITIAL_NUM_DEVICES 32

/* ENUMERATIONS ==============================================================*/

/* STRUCTS ===================================================================*/

/* PROTOTYPES ================================================================*/

int state_load(struct cxl_switch *s, char *filename);

/* GLOBAL VARIABLES ==========================================================*/

extern struct cxl_switch *cxls;

#endif //ifndef _STATE_H

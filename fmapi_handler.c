/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file 		fmapi_handler.c
 *
 * @brief 		Code file for methods to respond to FM API commands
 *
 * @copyright 	Copyright (C) 2024 Jackrabbit Founders LLC. All rights reserved.
 *
 * @date 		Jan 2024
 * @author 		Barrett Edwards <code@jrlabs.io>
 * 
 */
/* INCLUDES ==================================================================*/

/* gettid()
 */
#define _GNU_SOURCE

#include <unistd.h>

/* printf()
 */
#include <stdio.h>

/* memset()
 */
#include <string.h>

/* struct timespec 
 * timespec_get()
 *
 */
#include <time.h>

/* autl_prnt_buf()
 */
#include <arrayutils.h>

/* mctp_init()
 * mctp_set_mh()
 * mctp_run()
 */
#include <mctp.h>
#include <ptrqueue.h>
#include "signals.h"

#include "options.h"

#include "state.h"

#include <fmapi.h>

#include "fmapi_handler.h"

/* MACROS ====================================================================*/

#ifdef CSE_VERBOSE
 #define INIT 			unsigned step = 0;
 #define ENTER 					if (opts[CLOP_VERBOSITY].u64 & CLVB_CALLSTACK) 	printf("%d:%s Enter\n", 			gettid(), __FUNCTION__);
 #define STEP 			step++; if (opts[CLOP_VERBOSITY].u64 & CLVB_STEPS) 		printf("%d:%s STEP: %u\n", 			gettid(), __FUNCTION__, step);
 #define HEX32(m, i)			if (opts[CLOP_VERBOSITY].u64 & CLVB_STEPS) 		printf("%d:%s STEP: %u %s: 0x%x\n",	gettid(), __FUNCTION__, step, m, i);
 #define INT32(m, i)			if (opts[CLOP_VERBOSITY].u64 & CLVB_STEPS) 		printf("%d:%s STEP: %u %s: %d\n",	gettid(), __FUNCTION__, step, m, i);
 #define EXIT(rc) 				if (opts[CLOP_VERBOSITY].u64 & CLVB_CALLSTACK) 	printf("%d:%s Exit: %d\n", 			gettid(), __FUNCTION__,rc);
#else
 #define ENTER
 #define EXIT(rc)
 #define STEP
 #define HEX32(m, i)
 #define INT32(m, i)
 #define INIT 
#endif // CSE_VERBOSE

#define IFV(u) 					if (opts[CLOP_VERBOSITY].u64 & u) 

/* ENUMERATIONS ==============================================================*/

/* STRUCTS ===================================================================*/

/* PROTOTYPES ================================================================*/

int fmop_isc_bos			(struct mctp *m, struct mctp_action *ma);
int fmop_isc_id				(struct mctp *m, struct mctp_action *ma);
int fmop_isc_msg_limit_get	(struct mctp *m, struct mctp_action *ma);
int fmop_isc_msg_limit_set	(struct mctp *m, struct mctp_action *ma);

int fmop_mpc_cfg			(struct mctp *m, struct mctp_action *ma);
int fmop_mpc_mem			(struct mctp *m, struct mctp_action *ma);
int fmop_mpc_tmc			(struct mctp *m, struct mctp_action *ma);

int fmop_psc_cfg 			(struct mctp *m, struct mctp_action *ma);
int fmop_psc_id 			(struct mctp *m, struct mctp_action *ma);
int fmop_psc_port 			(struct mctp *m, struct mctp_action *ma);
int fmop_psc_port_ctrl 		(struct mctp *m, struct mctp_action *ma);

int fmop_vsc_aer 			(struct mctp *m, struct mctp_action *ma);
int fmop_vsc_bind 			(struct mctp *m, struct mctp_action *ma);
int fmop_vsc_info 			(struct mctp *m, struct mctp_action *ma);
int fmop_vsc_unbind 		(struct mctp *m, struct mctp_action *ma);

/* GLOBAL VARIABLES ==========================================================*/

/* FUNCTIONS =================================================================*/

/**
 * Handler for all FM API Opcodes
 * 
 * @return 	0 upon success, 1 otherwise
 *			
 * STEPS 
 * 1: Deserialize Header
 * 2: Verify FM API Message Category
 * 3: Handle Opcode
 */
int fmapi_handler(struct mctp *m, struct mctp_action *ma)
{
	INIT
	struct fmapi_hdr hdr; 
	int rv;

	ENTER

	// Initialize variables 
	rv = 0;

	STEP // 1: Deserialize FM API Header
	rv = fmapi_deserialize(&hdr, ma->req->payload, FMOB_HDR, NULL);
	if (rv <= 0) 
		goto end;

	STEP // 2: Verify FM API Message Category
	if (hdr.category != FMMT_REQ) 
		goto end;

	STEP // 3: Handle Opcode
	HEX32("Opcode",  hdr.opcode);
	switch(hdr.opcode)
	{
		case FMOP_ISC_BOS: 				rv = fmop_isc_bos(m, ma); 			break;	
		case FMOP_ISC_ID: 				rv = fmop_isc_id(m, ma); 			break;	
		case FMOP_ISC_MSG_LIMIT_GET:	rv = fmop_isc_msg_limit_get(m, ma); break;	
		case FMOP_ISC_MSG_LIMIT_SET:	rv = fmop_isc_msg_limit_set(m, ma); break;	
		case FMOP_PSC_ID:      			rv = fmop_psc_id(m, ma); 			break;	
		case FMOP_PSC_PORT:         	rv = fmop_psc_port(m, ma); 			break;	
		case FMOP_PSC_PORT_CTRL:       	rv = fmop_psc_port_ctrl(m, ma); 	break;
		case FMOP_PSC_CFG:             	rv = fmop_psc_cfg(m, ma); 			break;
		case FMOP_VSC_INFO:      		rv = fmop_vsc_info(m, ma); 			break;	
		case FMOP_VSC_BIND:            	rv = fmop_vsc_bind(m, ma); 			break;	
		case FMOP_VSC_UNBIND:          	rv = fmop_vsc_unbind(m, ma); 		break;	
		case FMOP_VSC_AER:             	rv = fmop_vsc_aer(m, ma); 			break;	
		case FMOP_MPC_TMC:        	   	rv = fmop_mpc_tmc(m, ma); 			break;
		case FMOP_MPC_CFG:             	rv = fmop_mpc_cfg(m, ma); 			break;
		case FMOP_MPC_MEM:             	rv = fmop_mpc_mem(m, ma); 			break;
		default: 															break;
	}

end:				
	
	// If subhandler fails, check in mctp_action
	if (rv != 0)
	{
		ma->completion_code = 1;
		pq_push(m->acq, ma);
	}

	EXIT(rv)
	return rv;

}

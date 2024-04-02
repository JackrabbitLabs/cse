/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file 		fmapi_isc_handler.c
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
#include <timeutils.h>
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

#define ISO_TIME_BUF_LEN		32

/* ENUMERATIONS ==============================================================*/

/* STRUCTS ===================================================================*/

/* PROTOTYPES ================================================================*/

/* GLOBAL VARIABLES ==========================================================*/

/* FUNCTIONS =================================================================*/

/**
 * Handler for FM API ISC Background Operation Status Opcode (0002h)
 *
 * @param m 	struct mctp* 
 * @param mm 	struct mctp_msg* 
 * @return 		0 upon success, 1 otherwise
 *
 * STEPS
 *  1: Initialize variables
 *  2: Checkout Response mctp_msg buffer
 *  3: Fill Response MCTP Header
 *  4: Set buffer pointers 
 *  5: Deserialize Request Header
 *  6: Deserialize Request Object 
 *  7: Extract parameters
 *  8: Obtain lock on switch state 
 *  9: Validate Inputs 
 * 10: Perform Action 
 * 11: Prepare Response Object
 * 12: Serialize Response Object
 * 13: Set return code
 * 14: Release lock on switch state 
 * 15: Fill Response Header
 * 16: Serialize Header 
 * 17: Push Response mctp_msg onto Transmit Message Queue 
 * 18: Checkin mctp_msgs 
 */
int fmop_isc_bos(struct mctp *m, struct mctp_action *ma)
{
	INIT
	char now[ISO_TIME_BUF_LEN];
	struct fmapi_msg req, rsp;
	unsigned rc;
	int rv, len;

	ENTER

	STEP // 1: Initialize variables
	rv = 1; 
	len = 0;
	rc = FMRC_INVALID_INPUT;
	isotime(now, ISO_TIME_BUF_LEN);

	STEP // 2: Get response mctp_msg buffer
	ma->rsp = pq_pop(m->msgs, 1);
	if (ma->rsp == NULL)  
		goto end;

	STEP // 3: Fill Response MCTP Header: dst, src, owner, tag, and type 
	mctp_fill_msg_hdr(ma->rsp, ma->req->src, m->state.eid, 0, ma->req->tag);
	ma->rsp->type = ma->req->type;
	
	// 4: Set buffer pointers 
	req.buf = (struct fmapi_buf*) ma->req->payload;
	rsp.buf = (struct fmapi_buf*) ma->rsp->payload;

	STEP // 5: Deserialize Request Header
	if ( fmapi_deserialize(&req.hdr, req.buf->hdr, FMOB_HDR, NULL) <= 0 )
		goto end;

	STEP // 6: Deserialize Request Object 
	if ( fmapi_deserialize(&req.obj, req.buf->payload, fmapi_fmob_req(req.hdr.opcode), NULL) < 0 )
		goto end;

	STEP // 7: Extract parameters

	IFV(CLVB_COMMANDS) printf("%s CMD: FM API ISC Background Operation Status\n", now);

	STEP // 8: Obtain lock on switch state 
	pthread_mutex_lock(&cxl_state->mtx);

	STEP // 9: Validate Inputs 

	STEP // 10: Perform Action 

	STEP // 11: Prepare Response Object
	rsp.obj.isc_bos.running = cxl_state->bos_running;
	rsp.obj.isc_bos.pcnt 	= cxl_state->bos_pcnt;
	rsp.obj.isc_bos.opcode 	= cxl_state->bos_opcode;
	rsp.obj.isc_bos.rc 		= cxl_state->bos_rc;
	rsp.obj.isc_bos.ext 	= cxl_state->bos_ext;

	STEP // 12: Serialize Response Object
	len = fmapi_serialize(rsp.buf->payload, &rsp.obj, fmapi_fmob_rsp(req.hdr.opcode));

	STEP // 13: Set return code
	rc = FMRC_SUCCESS;

//send:

	STEP // 14: Release lock on switch state 
	pthread_mutex_unlock(&cxl_state->mtx);

	if (len < 0)
		goto end;

	STEP // 15: Fill Response Header
	ma->rsp->len = fmapi_fill_hdr(&rsp.hdr, FMMT_RESP, req.hdr.tag, req.hdr.opcode, 0, len, rc, 0);

	STEP // 16: Serialize Header 
	fmapi_serialize(rsp.buf->hdr, &rsp.hdr, FMOB_HDR);

	STEP // 17: Push mctp_action onto queue 
	pq_push(m->tmq, ma);

	rv = 0;

end:				

	EXIT(rc)

	return rv;
}

/**
 * Handler for FM API ISC Identify Opcode (0001h)
 *
 * @param m 	struct mctp* 
 * @param mm 	struct mctp_msg* 
 * @return 		0 upon success, 1 otherwise
 *
 * STEPS
 *  1: Initialize variables
 *  2: Checkout Response mctp_msg buffer
 *  3: Fill Response MCTP Header
 *  4: Set buffer pointers 
 *  5: Deserialize Request Header
 *  6: Deserialize Request Object 
 *  7: Extract parameters
 *  8: Obtain lock on switch state 
 *  9: Validate Inputs 
 * 10: Perform Action 
 * 11: Prepare Response Object
 * 12: Serialize Response Object
 * 13: Set return code
 * 14: Release lock on switch state 
 * 15: Fill Response Header
 * 16: Serialize Header 
 * 17: Push Response mctp_msg onto Transmit Message Queue 
 * 18: Checkin mctp_msgs 
 */
int fmop_isc_id(struct mctp *m, struct mctp_action *ma)
{
	INIT
	char now[ISO_TIME_BUF_LEN];
	struct fmapi_msg req, rsp;
	unsigned rc;
	int rv, len;

	ENTER

	STEP // 1: Initialize variables
	rv = 1; 
	len = 0;
	rc = FMRC_INVALID_INPUT;
	isotime(now, ISO_TIME_BUF_LEN);

	STEP // 2: Get response mctp_msg buffer
	ma->rsp = pq_pop(m->msgs, 1);
	if (ma->rsp == NULL)  
		goto end;

	STEP // 3: Fill Response MCTP Header: dst, src, owner, tag, and type 
	mctp_fill_msg_hdr(ma->rsp, ma->req->src, m->state.eid, 0, ma->req->tag);
	ma->rsp->type = ma->req->type;
	
	STEP // 4: Set buffer pointers 
	req.buf = (struct fmapi_buf*) ma->req->payload;
	rsp.buf = (struct fmapi_buf*) ma->rsp->payload;

	STEP // 5: Deserialize Request Header
	if ( fmapi_deserialize(&req.hdr, req.buf->hdr, FMOB_HDR, NULL) <= 0 )
		goto end;

	STEP // 6: Deserialize Request Object 
	if ( fmapi_deserialize(&req.obj, req.buf->payload, fmapi_fmob_req(req.hdr.opcode), NULL) < 0 )
		goto end;

	STEP // 7: Extract parameters

	IFV(CLVB_COMMANDS) printf("%s CMD: FM API ISC Identify\n", now);

	STEP // 8: Obtain lock on switch state 
	pthread_mutex_lock(&cxl_state->mtx);

	STEP // 9: Validate Inputs 

	STEP // 10: Perform Action 

	STEP // 11: Prepare Response Object
		rsp.obj.isc_id_rsp.vid 	= cxl_state->vid;
		rsp.obj.isc_id_rsp.did 	= cxl_state->did;
		rsp.obj.isc_id_rsp.svid = cxl_state->svid;
		rsp.obj.isc_id_rsp.ssid = cxl_state->ssid;
		rsp.obj.isc_id_rsp.sn 	= cxl_state->sn;
		rsp.obj.isc_id_rsp.size = cxl_state->max_msg_size_n;

	STEP // 12: Serialize Response Object
	len = fmapi_serialize(rsp.buf->payload, &rsp.obj, fmapi_fmob_rsp(req.hdr.opcode));

	STEP // 13: Set return code
	rc = FMRC_SUCCESS;

//send:

	STEP // 14: Release lock on switch state 
	pthread_mutex_unlock(&cxl_state->mtx);

	if (len < 0)
		goto end;

	STEP // 15: Fill Response Header
	ma->rsp->len = fmapi_fill_hdr(&rsp.hdr, FMMT_RESP, req.hdr.tag, req.hdr.opcode, 0, len, rc, 0);

	STEP // 16: Serialize Header 
	fmapi_serialize(rsp.buf->hdr, &rsp.hdr, FMOB_HDR);

	STEP // 17: Push mctp_action onto queue 
	pq_push(m->tmq, ma);

	rv = 0;

end:				

	EXIT(rc)

	return rv;
}

/**
 * Handler for FM API ISC Get Response Message Limit Opcode (0003h)
 *
 * @param m 	struct mctp* 
 * @param mm 	struct mctp_msg* 
 * @return 		0 upon success, 1 otherwise
 *
 * STEPS
 *  1: Initialize variables
 *  2: Checkout Response mctp_msg buffer
 *  3: Fill Response MCTP Header
 *  4: Set buffer pointers 
 *  5: Deserialize Request Header
 *  6: Deserialize Request Object 
 *  7: Extract parameters
 *  8: Obtain lock on switch state 
 *  9: Validate Inputs 
 * 10: Perform Action 
 * 11: Prepare Response Object
 * 12: Serialize Response Object
 * 13: Set return code
 * 14: Release lock on switch state 
 * 15: Fill Response Header
 * 16: Serialize Header 
 * 17: Push Response mctp_msg onto Transmit Message Queue 
 * 18: Checkin mctp_msgs 
 */
int fmop_isc_msg_limit_get(struct mctp *m, struct mctp_action *ma)
{
	INIT
	char now[ISO_TIME_BUF_LEN];
	struct fmapi_msg req, rsp;
	unsigned rc;
	int rv, len;

	ENTER

	STEP // 1: Initialize variables
	rv = 1; 
	len = 0;
	rc = FMRC_INVALID_INPUT;
	isotime(now, ISO_TIME_BUF_LEN);

	STEP // 2: Get response mctp_msg buffer
	ma->rsp = pq_pop(m->msgs, 1);
	if (ma->rsp == NULL)  
		goto end;

	STEP // 3: Fill Response MCTP Header: dst, src, owner, tag, and type 
	mctp_fill_msg_hdr(ma->rsp, ma->req->src, m->state.eid, 0, ma->req->tag);
	ma->rsp->type = ma->req->type;
	
	// 4: Set buffer pointers 
	req.buf = (struct fmapi_buf*) ma->req->payload;
	rsp.buf = (struct fmapi_buf*) ma->rsp->payload;

	STEP // 5: Deserialize Request Header
	if ( fmapi_deserialize(&req.hdr, req.buf->hdr, FMOB_HDR, NULL) <= 0 )
		goto end;

	STEP // 6: Deserialize Request Object 
	if ( fmapi_deserialize(&req.obj, req.buf->payload, fmapi_fmob_req(req.hdr.opcode), NULL) < 0 )
		goto end;

	STEP // 7: Extract parameters

	IFV(CLVB_COMMANDS) printf("%s CMD: FM API ISC Get Response Message Limit\n", now);

	STEP // 8: Obtain lock on switch state 
	pthread_mutex_lock(&cxl_state->mtx);

	STEP // 9: Validate Inputs 

	STEP // 10: Perform Action 

	STEP // 11: Prepare Response Object
	rsp.obj.isc_msg_limit.limit = cxl_state->msg_rsp_limit_n;

	STEP // 12: Serialize Response Object
	len = fmapi_serialize(rsp.buf->payload, &rsp.obj, fmapi_fmob_rsp(req.hdr.opcode));

	STEP // 13: Set return code
	rc = FMRC_SUCCESS;

//send:

	STEP // 14: Release lock on switch state 
	pthread_mutex_unlock(&cxl_state->mtx);

	if (len < 0)
		goto end;

	STEP // 15: Fill Response Header
	ma->rsp->len = fmapi_fill_hdr(&rsp.hdr, FMMT_RESP, req.hdr.tag, req.hdr.opcode, 0, len, rc, 0);

	STEP // 16: Serialize Header 
	fmapi_serialize(rsp.buf->hdr, &rsp.hdr, FMOB_HDR);

	STEP // 17: Push mctp_action onto queue 
	pq_push(m->tmq, ma);

	rv = 0;

end:				

	EXIT(rc)

	return rv;
}

/**
 * Handler for FM API ISC Set Response Message Limit Opcode (0004h)
 *
 * @param m 	struct mctp* 
 * @param mm 	struct mctp_msg* 
 * @return 		0 upon success, 1 otherwise
 *
 * STEPS
 *  1: Initialize variables
 *  2: Checkout Response mctp_msg buffer
 *  3: Fill Response MCTP Header
 *  4: Set buffer pointers 
 *  5: Deserialize Request Header
 *  6: Deserialize Request Object 
 *  7: Extract parameters
 *  8: Obtain lock on switch state 
 *  9: Validate Inputs 
 * 10: Perform Action 
 * 11: Prepare Response Object
 * 12: Serialize Response Object
 * 13: Set return code
 * 14: Release lock on switch state 
 * 15: Fill Response Header
 * 16: Serialize Header 
 * 17: Push Response mctp_msg onto Transmit Message Queue 
 * 18: Checkin mctp_msgs 
 */
int fmop_isc_msg_limit_set(struct mctp *m, struct mctp_action *ma)
{
	INIT
	char now[ISO_TIME_BUF_LEN];
	struct fmapi_msg req, rsp;
	unsigned rc;
	int rv, len;

	ENTER

	STEP // 1: Initialize variables
	rv = 1; 
	len = 0;
	rc = FMRC_INVALID_INPUT;
	isotime(now, ISO_TIME_BUF_LEN);

	STEP // 2: Get response mctp_msg buffer
	ma->rsp = pq_pop(m->msgs, 1);
	if (ma->rsp == NULL)  
		goto end;

	STEP // 3: Fill Response MCTP Header: dst, src, owner, tag, and type 
	mctp_fill_msg_hdr(ma->rsp, ma->req->src, m->state.eid, 0, ma->req->tag);
	ma->rsp->type = ma->req->type;
	
	// 4: Set buffer pointers 
	req.buf = (struct fmapi_buf*) ma->req->payload;
	rsp.buf = (struct fmapi_buf*) ma->rsp->payload;

	STEP // 5: Deserialize Request Header
	if ( fmapi_deserialize(&req.hdr, req.buf->hdr, FMOB_HDR, NULL) <= 0 )
		goto end;

	STEP // 6: Deserialize Request Object 
	if ( fmapi_deserialize(&req.obj, req.buf->payload, fmapi_fmob_req(req.hdr.opcode), NULL) < 0 )
		goto end;

	STEP // 7: Extract parameters

	IFV(CLVB_COMMANDS) printf("%s CMD: FM API ISC Set Response Message Limit\n", now);

	STEP // 8: Obtain lock on switch state 
	pthread_mutex_lock(&cxl_state->mtx);

	STEP // 9: Validate Inputs 
	if (req.obj.isc_msg_limit.limit < 8 || req.obj.isc_msg_limit.limit > 20)
	{
		IFV(CLVB_ERRORS) printf("%s ERR: Requested Message Response Limit outside allowed values. Requested: %d min: 8 max: 20\n", now, req.obj.isc_msg_limit.limit);
		goto send;
	}

	STEP // 10: Perform Action 
	cxl_state->msg_rsp_limit_n = req.obj.isc_msg_limit.limit;

	STEP // 11: Prepare Response Object
	rsp.obj.isc_msg_limit.limit = cxl_state->msg_rsp_limit_n;

	STEP // 12: Serialize Response Object
	len = fmapi_serialize(rsp.buf->payload, &rsp.obj, fmapi_fmob_rsp(req.hdr.opcode));
	if (len < 0)
		goto end;

	STEP // 13: Set return code
	rc = FMRC_SUCCESS;

send:

	STEP // 14: Release lock on switch state 
	pthread_mutex_unlock(&cxl_state->mtx);

	STEP // 15: Fill Response Header
	ma->rsp->len = fmapi_fill_hdr(&rsp.hdr, FMMT_RESP, req.hdr.tag, req.hdr.opcode, 0, len, rc, 0);

	STEP // 16: Serialize Header 
	fmapi_serialize(rsp.buf->hdr, &rsp.hdr, FMOB_HDR);

	STEP // 17: Push mctp_action onto queue 
	pq_push(m->tmq, ma);

	rv = 0;

end:				

	EXIT(rc)

	return rv;
}

